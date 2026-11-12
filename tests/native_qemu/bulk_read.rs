//! Bulk reads (the second capability word's DG_CAP2_GL_BULK_READ) through the
//! device, with exact pixels: a texture level's shape in one QUERY, whole
//! 1024x768 RGBA8, BGRA-uploaded rectangle, RGB565, depth and packed
//! depth-stencil levels in one QUERY each into a scattered page list, float
//! reads chunked at the 4 MiB limit, unit counts around page, chunk and level
//! boundaries, whole-window ReadPixels (colour, odd rectangles, depth, stencil
//! and float), a compressed image, both the register and the queued transport,
//! and malformed destinations rejected without writing anything. Every bulk
//! result is compared byte for byte with the chunked reads older frontends
//! make, which keep working unchanged, and the round trips of both are counted.
use super::textures::{call, data_call, query, query_capacity, words};
use super::*;
use std::fmt::Write as _;

const CAPS2: u32 = 0x1194;
const CAP2_BULK_READ: u32 = 4;
const RESULT_ADDR_LO: u32 = 0x115c;
const RESULT_ADDR_HI: u32 = 0x1160;
const RESULT_CAPACITY: u32 = 0x1164;
const RESULT_BYTES: u32 = 0x1168;
const RESULT_TYPE: u32 = 0x116c;
const PAGES: u32 = 0x8000_0000;
const MAX_BULK: u32 = 0x40_0000;
const SHAPE: u32 = 0x1000;
const UNITS: u32 = 0x2000;
const UNIT: u32 = 64;
const READ_FLOAT: u32 = 0x8000;
const READ_DEPTH: u32 = 0x4000;
const READ_PIXELS_DEPTH: u32 = 0x10000;
const READ_PIXELS_STENCIL: u32 = 0x20000;
const READ_PIXELS_FLOAT: u32 = 0x30000;
const RESULT_INT: u32 = 2;
const RESULT_FLOAT: u32 = 3;
const ERROR_BATCH: u32 = 1;
const ERROR_DMA: u32 = 2;
const ERROR_UNSUPPORTED: u32 = 3;

const TEXTURE_2D: u32 = 0x0de1;
const RECT: u32 = 0x84f5;
const RGBA8: u32 = 0x8058;
const RGBA: u32 = 0x1908;
const BGRA: u32 = 0x80e1;
const RGB: u32 = 0x1907;
const RGB565: u32 = 0x8d62;
const UNSIGNED_BYTE: u32 = 0x1401;
const PACKED_565: u32 = 0x8363;
const FLOAT: u32 = 0x1406;
const DEPTH_COMPONENT: u32 = 0x1902;
const DEPTH_COMPONENT24: u32 = 0x81a6;
const DEPTH_STENCIL: u32 = 0x84f9;
const DEPTH24_STENCIL8: u32 = 0x88f0;
const UNSIGNED_INT_24_8: u32 = 0x84fa;
const DXT1: u32 = 0x83f0;
const WIDTH: u32 = 0x1000;
const HEIGHT: u32 = 0x1001;
const DEPTH_SIZE: u32 = 0x884a;
const STENCIL_SIZE: u32 = 0x88f1;
const MIN_FILTER: u32 = 0x2801;
const MAG_FILTER: u32 = 0x2800;
const NEAREST: u32 = 0x2600;
const TEXTURE_ENV: u32 = 0x2300;
const TEXTURE_ENV_MODE: u32 = 0x2200;
const REPLACE: u32 = 0x1e01;
const DITHER: u32 = 0x0bd0;
const DEPTH_TEST: u32 = 0x0b71;
const DEPTH_BIT: u32 = 0x0100;
const STENCIL_BIT: u32 = 0x0400;

const BIND: u32 = 0x04e;
const PARAMETER: u32 = 0x8df;
const IMAGE_2D: u32 = 0x8d4;
const COMPRESSED_IMAGE_2D: u32 = 286;
const TEX_ENV_I: u32 = 0x8c6;
const ENABLE: u32 = 0x209;
const DISABLE: u32 = 0x1c5;
const CLEAR: u32 = 0x09a;
const CLEAR_DEPTH: u32 = 167;
const CLEAR_STENCIL: u32 = 181;
const BEGIN: u32 = 0x01a;
const END: u32 = 0x216;
const TEXCOORD2F: u32 = 0x883;
const VERTEX2F: u32 = 0x9dc;
const VERTEX3F: u32 = 0x9ea;
const GET_TEX_IMAGE: u32 = 0x414;
const GET_COMPRESSED_IMAGE: u32 = 743;
const LEVEL_PARAMETER: u32 = 0x416;
const GET_ERROR: u32 = 0x2fc;
const READ_PIXELS: u32 = 0x7a4;

/// The window drawable and the main level: a WineD3D 1024x768 back buffer.
const W: u32 = 1024;
const H: u32 = 768;
/// Driver-owned page list and its 1024 destination pages: every other page
/// of FRAMES, in descending order. The pages between them are canaries.
const LIST: u64 = 0x80_0000;
const QUEUE_LIST: u64 = 0x80_1000;
const FRAMES: u64 = 0x100_0000;
const FRAME_SPAN: u64 = 0x80_0000;
const QUEUE_PAGE: u64 = 0x90_0000;
const QUEUE_BUFFER: u64 = 0xa0_0000;
const FILL: u8 = 0xcc;

fn frame(i: usize) -> u64 {
    FRAMES + (1023 - i as u64) * 0x2000
}
fn f(v: f32) -> u32 {
    v.to_bits()
}
fn rgba(x: u32, y: u32) -> [u8; 4] {
    [
        x as u8,
        y as u8,
        ((x >> 8) | (y >> 8) << 4) as u8,
        (x ^ y.wrapping_mul(3)) as u8,
    ]
}
fn hex(bytes: &[u8]) -> String {
    let mut out = String::with_capacity(2 + bytes.len() * 2);
    out.push_str("0x");
    for byte in bytes {
        write!(out, "{byte:02x}").unwrap();
    }
    out
}
fn unhex(reply: &str) -> Vec<u8> {
    let hex = reply
        .split_whitespace()
        .nth(1)
        .unwrap()
        .trim_start_matches("0x")
        .as_bytes();
    hex.as_chunks::<2>()
        .0
        .iter()
        .map(|pair| u8::from_str_radix(std::str::from_utf8(pair).unwrap(), 16).unwrap())
        .collect()
}

struct Device {
    qemu: Qemu,
    sequence: u32,
    /// QUERY round trips, the cost a frontend pays per synchronous read.
    queries: u32,
    /// Queued transport, once enabled: generation and produced entries.
    queue: Option<(u32, u32)>,
}
impl Device {
    fn submit(&mut self, records: &[(u32, Vec<u32>)]) {
        assert!(self.queue.is_none());
        self.sequence += 1;
        self.qemu.batch(self.sequence, records);
        self.qemu.await_completion(self.sequence);
    }
    fn word(&mut self, function: u32, args: &[u32]) -> u32 {
        self.queries += 1;
        words(&query(&mut self.qemu, &mut self.sequence, function, args).1)[0]
    }
    fn error(&mut self) -> u32 {
        self.word(GET_ERROR, &[])
    }
    /// A chunked read into a contiguous 64 KiB destination, as older
    /// frontends make it.
    fn legacy(&mut self, function: u32, args: &[u32], capacity: u32) -> (u32, Vec<u8>) {
        self.queries += 1;
        query_capacity(&mut self.qemu, &mut self.sequence, function, args, capacity)
    }
    fn fill(&mut self) {
        self.qemu
            .command(&format!("memset {FRAMES:#x} {FRAME_SPAN} {FILL:#x}"));
    }
    /// The destination pages after a result of `bytes`: its bytes, with every
    /// canary and every byte past the result still untouched.
    fn gather(&mut self, bytes: usize) -> Vec<u8> {
        let region = unhex(&self.qemu.command(&format!("read {FRAMES:#x} {FRAME_SPAN}")));
        let used = bytes.div_ceil(4096);
        let mut out = Vec::with_capacity(bytes);
        for i in 0..1024 {
            let at = (frame(i) - FRAMES) as usize;
            let page = &region[at..at + 4096];
            let n = bytes.saturating_sub(i * 4096).min(4096);
            if i < used {
                out.extend_from_slice(&page[..n]);
            }
            assert!(page[n..].iter().all(|b| *b == FILL), "page {i} past {n}");
            let gap = &region[at + 4096..at + 8192];
            assert!(gap.iter().all(|b| *b == FILL), "canary after page {i}");
        }
        out
    }
    /// One bulk QUERY into the page list at `list` with capacity `word`.
    /// Ok(kind, bytes) or Err(device error); nothing is written on an error.
    fn bulk_word(
        &mut self,
        function: u32,
        args: &[u32],
        list: u64,
        word: u32,
    ) -> Result<(u32, Vec<u8>), u32> {
        self.queries += 1;
        self.fill();
        let mut payload = vec![function];
        payload.extend_from_slice(args);
        payload.resize(4, 0);
        let (error, length, kind) = if let Some((generation, _)) = self.queue {
            let records = [(11, payload)];
            let index = self.publish(generation, &records, list, word);
            let (status, error) = self.outcome(index);
            let entry = QUEUE_PAGE + 256 + u64::from(index % 16) * 64;
            assert_eq!(status & 2, 2);
            (error, self.ram(entry + 56), self.ram(entry + 60))
        } else {
            self.qemu.write(RESULT_ADDR_LO, list as u32);
            self.qemu.write(RESULT_ADDR_HI, 0);
            self.qemu.write(RESULT_CAPACITY, word);
            self.sequence += 1;
            self.qemu.batch(self.sequence, &[(11, payload)]);
            let error = self.wait(self.sequence);
            (
                error,
                self.qemu.read(RESULT_BYTES),
                self.qemu.read(RESULT_TYPE),
            )
        };
        if error != 0 {
            assert_eq!(self.gather(0), [], "a failed bulk query wrote");
            return Err(error);
        }
        assert!(length > 0 && length <= word & !PAGES);
        Ok((kind, self.gather(length as usize)))
    }
    fn bulk(&mut self, function: u32, args: &[u32], capacity: u32) -> (u32, Vec<u8>) {
        self.bulk_word(function, args, LIST, PAGES | capacity)
            .unwrap_or_else(|e| panic!("bulk query {function:#x} {args:x?}: error {e}"))
    }
    fn wait(&mut self, sequence: u32) -> u32 {
        let deadline = Instant::now() + Duration::from_secs(10);
        while self.qemu.read(0x1120) != sequence {
            assert!(
                Instant::now() < deadline,
                "batch {sequence} did not complete"
            );
            std::thread::sleep(Duration::from_millis(2));
        }
        if self.qemu.read(0x111c) & 4 != 0 {
            self.qemu.read(0x1124)
        } else {
            0
        }
    }
    fn ram(&mut self, address: u64) -> u32 {
        let reply = self.qemu.command(&format!("readl {address:#x}"));
        u32::from_str_radix(
            reply
                .split_whitespace()
                .nth(1)
                .unwrap()
                .trim_start_matches("0x"),
            16,
        )
        .unwrap()
    }
    /// Switch to the queued transport (queue.rs has the full protocol tests).
    fn enable_queue(&mut self) {
        let generation = self.qemu.read(0x103c);
        assert_eq!(self.qemu.read(0x1180), 1);
        self.qemu.command(&format!("memset {QUEUE_PAGE:#x} 4096 0"));
        self.qemu.write(0x1184, QUEUE_PAGE as u32);
        self.qemu.write(0x1188, 0);
        self.qemu.write(0x118c, generation);
        assert_eq!(self.qemu.read(0x118c), 1);
        self.queue = Some((generation, 0));
    }
    /// Publish one synchronous entry and return its index.
    fn publish(
        &mut self,
        generation: u32,
        records: &[(u32, Vec<u32>)],
        list: u64,
        word: u32,
    ) -> u32 {
        let (_, index) = self.queue.unwrap();
        let mut bytes = Vec::new();
        for (opcode, arguments) in records {
            for value in [
                *opcode,
                32 + arguments.len() as u32 * 4,
                1,
                1,
                1,
                0,
                0,
                generation,
            ]
            .iter()
            .chain(arguments)
            {
                bytes.extend_from_slice(&value.to_le_bytes());
            }
        }
        self.qemu.command(&format!(
            "write {QUEUE_BUFFER:#x} {} {}",
            bytes.len(),
            hex(&bytes)
        ));
        self.sequence += 1;
        let entry = QUEUE_PAGE + 256 + u64::from(index % 16) * 64;
        for (offset, value) in [
            (0, QUEUE_BUFFER as u32),
            (4, 0),
            (8, bytes.len() as u32),
            (12, self.sequence),
            (16, generation),
            (20, 0),
            (24, 0),
            (28, list as u32),
            (32, 0),
            (36, word),
            (40, 0),
            (44, 0),
            (48, 0),
            (52, 0),
        ] {
            self.qemu
                .command(&format!("writel {:#x} {value:#x}", entry + offset));
        }
        let produced = index.wrapping_add(1);
        self.queue = Some((generation, produced));
        self.qemu
            .command(&format!("writel {:#x} {produced:#x}", QUEUE_PAGE + 64));
        self.qemu.write(0x1190, produced);
        index
    }
    fn outcome(&mut self, index: u32) -> (u32, u32) {
        let deadline = Instant::now() + Duration::from_secs(10);
        while (self.ram(QUEUE_PAGE + 12).wrapping_sub(index + 1) as i32) < 0 {
            assert!(Instant::now() < deadline, "entry {index} did not complete");
            std::thread::sleep(Duration::from_millis(2));
        }
        let entry = QUEUE_PAGE + 256 + u64::from(index % 16) * 64;
        (self.ram(entry + 48), self.ram(entry + 52))
    }
    /// A synchronous queued batch without a result.
    fn queued(&mut self, records: &[(u32, Vec<u32>)]) {
        let (generation, _) = self.queue.unwrap();
        let index = self.publish(generation, records, 0, 0);
        assert_eq!(self.outcome(index), (2, 0));
    }
    /// The SHAPE answer of a level and the separate level parameters it
    /// replaces, which must agree.
    fn shape(&mut self, target: u32, level: u32) -> [u32; 5] {
        self.queries += 1;
        let (kind, bytes) = query(
            &mut self.qemu,
            &mut self.sequence,
            GET_TEX_IMAGE,
            &[target, level | SHAPE, 0],
        );
        assert_eq!((kind, bytes.len()), (RESULT_INT, 20));
        let shape: [u32; 5] = words(&bytes).try_into().unwrap();
        // The separate parameters are a check here, not round trips of a read.
        let queries = self.queries;
        let separate = [
            self.word(LEVEL_PARAMETER, &[target, level, WIDTH]),
            self.word(LEVEL_PARAMETER, &[target, level, HEIGHT]),
            1,
            self.word(LEVEL_PARAMETER, &[target, level, DEPTH_SIZE]),
            if shape[3] != 0 {
                self.word(LEVEL_PARAMETER, &[target, level, STENCIL_SIZE])
            } else {
                0
            },
        ];
        assert_eq!(shape, separate, "shape of {target:#x} level {level}");
        self.queries = queries;
        shape
    }
    /// A whole level read the way a bulk frontend does: one SHAPE, then UNITS
    /// reads of at most 4 MiB. Returns the level's wire bytes.
    fn bulk_level(&mut self, target: u32, flags: u32, kind: u32) -> Vec<u8> {
        let [width, height, ..] = self.shape(target, 0);
        let pixel = if flags == READ_FLOAT { 16 } else { 4 };
        let total = width * height;
        let per_query = MAX_BULK / pixel / UNIT * UNIT;
        let mut out = Vec::new();
        let mut first = 0;
        while first < total {
            let count = (total - first).min(per_query);
            let units = count.div_ceil(UNIT);
            let (k, bytes) = self.bulk(
                GET_TEX_IMAGE,
                &[target, flags | UNITS | units << 16, first],
                units * UNIT * pixel,
            );
            assert_eq!((k, bytes.len() as u32), (kind, units * UNIT * pixel));
            // Past the level end the result is zero.
            assert!(bytes[(count * pixel) as usize..].iter().all(|b| *b == 0));
            out.extend_from_slice(&bytes[..(count * pixel) as usize]);
            first += count;
        }
        out
    }
    /// The same level read the way older frontends do: WIDTH, HEIGHT and
    /// DEPTH_SIZE, then 64 KiB GetTexImage chunks.
    fn legacy_level(&mut self, target: u32, flags: u32, kind: u32) -> Vec<u8> {
        let width = self.word(LEVEL_PARAMETER, &[target, 0, WIDTH]);
        let height = self.word(LEVEL_PARAMETER, &[target, 0, HEIGHT]);
        self.word(LEVEL_PARAMETER, &[target, 0, DEPTH_SIZE]);
        let pixel = if flags == READ_FLOAT { 16 } else { 4 };
        let total = width * height;
        let mut out = Vec::new();
        let mut first = 0;
        while first < total {
            let count = (total - first).min(65536 / pixel);
            let (k, bytes) = self.legacy(
                GET_TEX_IMAGE,
                &[target, flags | count << 16, first],
                count * pixel,
            );
            assert_eq!(k, kind);
            out.extend_from_slice(&bytes);
            first += count;
        }
        out
    }
    /// ReadPixels of a rectangle as tiles of at most 64 KiB, as older
    /// frontends make it, in the order of a single read.
    fn legacy_pixels(&mut self, mode: u32, x: u32, y: u32, w: u32, h: u32) -> Vec<u8> {
        let pixel = if mode == READ_PIXELS_FLOAT { 16 } else { 4 };
        let rows = (65536 / pixel / w).max(1);
        let mut out = Vec::new();
        let mut row = 0;
        while row < h {
            let n = rows.min(h - row);
            out.extend(
                self.legacy(
                    READ_PIXELS,
                    &[x | mode, y + row, w | n << 16],
                    w * n * pixel,
                )
                .1,
            );
            row += n;
        }
        out
    }
}

/// One RGBA8 wire pixel of an RGB565 texel: the high bits are the stored value.
fn exact565(pixel: &[u8], word: u16) {
    let w = u32::from(word);
    for (byte, value, bits) in [
        (pixel[0], w >> 11, 5),
        (pixel[1], (w >> 5) & 63, 6),
        (pixel[2], w & 31, 5),
    ] {
        assert_eq!(
            u32::from(byte) >> (8 - bits),
            value,
            "{pixel:?} for {word:#06x}"
        );
    }
    assert_eq!(pixel[3], 255);
}

#[test]
#[ignore = "requires native host GPU and the bulk-read QEMU producer; diskless acceptance"]
fn qemu_bulk_reads_return_whole_surfaces_exactly() {
    let (_device, _queue, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!("dg-bulk-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("g.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let qemu = Qemu::start(&directory, &socket);
    let mut gl = Device {
        qemu,
        sequence: 0,
        queries: 0,
        queue: None,
    };
    assert_ne!(gl.qemu.read(CAPS2) & CAP2_BULK_READ, 0);
    let list: Vec<u8> = (0..1024)
        .flat_map(|i| ((frame(i) >> 12) as u32).to_le_bytes())
        .collect();
    gl.qemu
        .command(&format!("write {LIST:#x} 4096 {}", hex(&list)));
    gl.qemu
        .command(&format!("write {QUEUE_LIST:#x} 4096 {}", hex(&list)));
    gl.submit(&[
        (1, vec![0]),
        (3, vec![W, H]),
        (5, vec![]),
        call(DISABLE, &[DITHER]),
    ]);

    // A 1024x768 RGBA8 level: one SHAPE and one 3 MiB read, exactly the
    // pattern and exactly the bytes of 48 chunked reads after 3 parameters.
    let pattern: Vec<u8> = (0..W * H).flat_map(|i| rgba(i % W, i / W)).collect();
    gl.submit(&[
        call(BIND, &[TEXTURE_2D, 10]),
        data_call(
            IMAGE_2D,
            &[TEXTURE_2D, 0, RGBA8, W, H, 0, RGBA, UNSIGNED_BYTE],
            &pattern,
        ),
    ]);
    assert_eq!(gl.error(), 0);
    assert_eq!(gl.shape(TEXTURE_2D, 0), [W, H, 1, 0, 0]);
    let before = gl.queries;
    let legacy = gl.legacy_level(TEXTURE_2D, 0, RESULT_INT);
    let legacy_trips = gl.queries - before;
    let before = gl.queries;
    let bulk = gl.bulk_level(TEXTURE_2D, 0, RESULT_INT);
    let bulk_trips = gl.queries - before;
    assert!(bulk == pattern, "bulk 1024x768 RGBA8 level differs");
    assert!(legacy == pattern, "chunked 1024x768 RGBA8 level differs");
    assert_eq!((legacy_trips, bulk_trips), (51, 2));
    eprintln!(
        "1024x768 RGBA8 GetTexImage: {legacy_trips} QUERY round trips chunked, {bulk_trips} bulk"
    );

    // A 1023x767 rectangle uploaded as BGRA reads back as RGBA wire pixels;
    // its last unit is zero past the level end.
    let (rw, rh) = (1023, 767);
    let bgra: Vec<u8> = (0..rw * rh)
        .flat_map(|i| {
            let [r, g, b, a] = rgba(i % rw, i / rw);
            [b, g, r, a]
        })
        .collect();
    gl.submit(&[
        call(BIND, &[RECT, 11]),
        data_call(
            IMAGE_2D,
            &[RECT, 0, RGBA8, rw, rh, 0, BGRA, UNSIGNED_BYTE],
            &bgra,
        ),
    ]);
    assert_eq!(gl.error(), 0);
    let rect = gl.bulk_level(RECT, 0, RESULT_INT);
    assert!(
        rect == (0..rw * rh)
            .flat_map(|i| rgba(i % rw, i / rw))
            .collect::<Vec<_>>()
    );
    assert!(rect == gl.legacy_level(RECT, 0, RESULT_INT));

    // RGB565 keeps every stored bit; float reads of the RGBA8 level span
    // three 4 MiB queries and equal 192 chunked ones.
    let shorts: Vec<u16> = (0..W * H)
        .map(|i| (i.wrapping_mul(2654435761) >> 16) as u16)
        .collect();
    gl.submit(&[
        call(BIND, &[TEXTURE_2D, 12]),
        data_call(
            IMAGE_2D,
            &[TEXTURE_2D, 0, RGB565, W, H, 0, RGB, PACKED_565],
            &shorts
                .iter()
                .flat_map(|s| s.to_le_bytes())
                .collect::<Vec<_>>(),
        ),
    ]);
    assert_eq!(gl.error(), 0);
    let wire = gl.bulk_level(TEXTURE_2D, 0, RESULT_INT);
    for (pixel, word) in wire.chunks(4).zip(&shorts) {
        exact565(pixel, *word);
    }
    assert!(wire == gl.legacy_level(TEXTURE_2D, 0, RESULT_INT));
    gl.submit(&[call(BIND, &[TEXTURE_2D, 10])]);
    let before = gl.queries;
    let floats = gl.bulk_level(TEXTURE_2D, READ_FLOAT, RESULT_FLOAT);
    assert_eq!(gl.queries - before, 1 + 3);
    assert!(floats == gl.legacy_level(TEXTURE_2D, READ_FLOAT, RESULT_FLOAT));
    for (i, pixel) in words(&floats).chunks(4).enumerate().step_by(997) {
        let expected = rgba(i as u32 % W, i as u32 / W);
        for c in 0..4 {
            let v = f32::from_bits(pixel[c]);
            // The native float conversion may round (Mesa answers half floats).
            assert!((v * 255. - f32::from(expected[c])).abs() < 0.5, "{i}: {v}");
        }
    }

    // Depth and packed depth-stencil levels.
    let (dw, dh) = (1023, 767);
    let depths: Vec<f32> = (0..dw * dh).map(|i| i as f32 / (dw * dh) as f32).collect();
    gl.submit(&[
        call(BIND, &[TEXTURE_2D, 13]),
        call(PARAMETER, &[TEXTURE_2D, MIN_FILTER, NEAREST]),
        data_call(
            IMAGE_2D,
            &[
                TEXTURE_2D,
                0,
                DEPTH_COMPONENT24,
                dw,
                dh,
                0,
                DEPTH_COMPONENT,
                FLOAT,
            ],
            &depths
                .iter()
                .flat_map(|d| d.to_le_bytes())
                .collect::<Vec<_>>(),
        ),
    ]);
    assert_eq!(gl.error(), 0);
    let shape = gl.shape(TEXTURE_2D, 0);
    assert!(shape[3] >= 24 && shape[4] == 0, "{shape:?}");
    let depth = gl.bulk_level(TEXTURE_2D, READ_DEPTH, RESULT_FLOAT);
    assert!(depth == gl.legacy_level(TEXTURE_2D, READ_DEPTH, RESULT_FLOAT));
    for (value, expected) in words(&depth).iter().zip(&depths) {
        assert!((f32::from_bits(*value) - expected).abs() < 1e-6);
    }
    let (sw, sh) = (333u32, 257u32);
    let packed: Vec<u32> = (0..sw * sh)
        .map(|i| (i.wrapping_mul(40503) & 0xff_ffff) << 8 | (i & 255))
        .collect();
    gl.submit(&[
        call(BIND, &[RECT, 14]),
        data_call(
            IMAGE_2D,
            &[
                RECT,
                0,
                DEPTH24_STENCIL8,
                sw,
                sh,
                0,
                DEPTH_STENCIL,
                UNSIGNED_INT_24_8,
            ],
            &packed
                .iter()
                .flat_map(|w| w.to_le_bytes())
                .collect::<Vec<_>>(),
        ),
    ]);
    assert_eq!(gl.error(), 0);
    let shape = gl.shape(RECT, 0);
    assert!(shape[3] >= 24 && shape[4] == 8, "{shape:?}");
    let words24 = gl.bulk_level(RECT, READ_DEPTH | READ_FLOAT, RESULT_INT);
    assert_eq!(words(&words24), packed);

    // Unit counts around pages, 64 KiB chunks and the 4 MiB limit, from a
    // 1024x1024 level, and a last unit past the level end.
    let square: Vec<u8> = (0..1024 * 1024)
        .flat_map(|i| rgba(i % 1024, i / 1024))
        .collect();
    gl.submit(&[
        call(BIND, &[TEXTURE_2D, 15]),
        data_call(
            IMAGE_2D,
            &[TEXTURE_2D, 0, RGBA8, 1024, 1024, 0, RGBA, UNSIGNED_BYTE],
            &square,
        ),
    ]);
    for (units, first) in [
        (1, 0),
        (1, 7),
        (15, 1),
        (16, 0),
        (17, 333),
        (255, 9),
        (256, 0),
        (257, 4096),
        (4095, 1),
        (16383, 64),
        (16384, 0),
    ] {
        let (_, bytes) = gl.bulk(
            GET_TEX_IMAGE,
            &[TEXTURE_2D, UNITS | units << 16, first],
            units * UNIT * 4,
        );
        let start = first as usize * 4;
        assert!(
            bytes == square[start..start + bytes.len()],
            "{units} units from {first}"
        );
    }
    let (_, tail) = gl.bulk(
        GET_TEX_IMAGE,
        &[TEXTURE_2D, UNITS | 1 << 16, 1024 * 1024 - 10],
        UNIT * 4,
    );
    assert!(tail[..40] == square[square.len() - 40..] && tail[40..].iter().all(|b| *b == 0));

    // A compressed image: 512 KiB of DXT1 blocks in one query.
    let blocks: Vec<u8> = (0..512 * 1024u32)
        .map(|i| (i.wrapping_mul(31) + i / 8) as u8)
        .collect();
    gl.submit(&[
        call(BIND, &[TEXTURE_2D, 16]),
        data_call(
            COMPRESSED_IMAGE_2D,
            &[TEXTURE_2D, 0, DXT1, 1024, 1024, 0, blocks.len() as u32],
            &blocks,
        ),
    ]);
    assert_eq!(gl.error(), 0);
    let words_total = blocks.len() as u32 / 4;
    let (_, compressed) = gl.bulk(
        GET_COMPRESSED_IMAGE,
        &[TEXTURE_2D, UNITS | (words_total / UNIT) << 16, 0],
        blocks.len() as u32,
    );
    assert!(compressed == blocks);
    let (_, chunk) = gl.legacy(
        GET_COMPRESSED_IMAGE,
        &[TEXTURE_2D, 16384 << 16, 16384],
        65536,
    );
    assert!(chunk == blocks[65536..131072]);

    // The window: the RGBA8 level drawn 1:1, read whole in one query (48
    // chunked), and odd rectangles; depth from a slanted quad, stencil and
    // float reads, each equal to the chunked reads.
    let mut draw = vec![
        call(BIND, &[TEXTURE_2D, 10]),
        call(PARAMETER, &[TEXTURE_2D, MIN_FILTER, NEAREST]),
        call(PARAMETER, &[TEXTURE_2D, MAG_FILTER, NEAREST]),
        call(TEX_ENV_I, &[TEXTURE_ENV, TEXTURE_ENV_MODE, REPLACE]),
        call(ENABLE, &[TEXTURE_2D]),
        call(BEGIN, &[7]),
    ];
    for (s, t) in [(0f32, 0f32), (1., 0.), (1., 1.), (0., 1.)] {
        draw.push(call(TEXCOORD2F, &[f(s), f(t)]));
        draw.push(call(VERTEX2F, &[f(2. * s - 1.), f(2. * t - 1.)]));
    }
    draw.extend([call(END, &[]), call(DISABLE, &[TEXTURE_2D])]);
    gl.submit(&draw);
    assert_eq!(gl.error(), 0);
    let before = gl.queries;
    let (kind, window) = gl.bulk(READ_PIXELS, &[0, 0, W | H << 16], W * H * 4);
    assert_eq!((kind, gl.queries - before), (RESULT_INT, 1));
    assert!(window == pattern, "whole-window ReadPixels differs");
    let before = gl.queries;
    assert!(gl.legacy_pixels(0, 0, 0, W, H) == pattern);
    eprintln!(
        "1024x768 RGBA8 ReadPixels: {} QUERY round trips chunked, 1 bulk",
        gl.queries - before
    );
    for (x, y, w, h) in [
        (3, 5, 1021, 761),
        (0, 767, 1023, 1),
        (1023, 0, 1, 768),
        (500, 400, 33, 31),
    ] {
        let (_, bytes) = gl.bulk(READ_PIXELS, &[x, y, w | h << 16], w * h * 4);
        let expected: Vec<u8> = (0..w * h)
            .flat_map(|i| rgba(x + i % w, y + i / w))
            .collect();
        assert!(bytes == expected, "ReadPixels {x},{y} {w}x{h}");
    }
    let mut slope = vec![
        call(CLEAR_DEPTH, &[0, 0x3ff0_0000]),
        call(CLEAR_STENCIL, &[0x5a]),
        call(CLEAR, &[DEPTH_BIT | STENCIL_BIT]),
        call(ENABLE, &[DEPTH_TEST]),
        call(BEGIN, &[7]),
    ];
    for (x, y, z) in [
        (-1f32, -1f32, -0.9f32),
        (1., -1., 0.8),
        (1., 1., 0.3),
        (-1., 1., -0.2),
    ] {
        slope.push(call(VERTEX3F, &[f(x), f(y), f(z)]));
    }
    slope.extend([call(END, &[]), call(DISABLE, &[DEPTH_TEST])]);
    gl.submit(&slope);
    assert_eq!(gl.error(), 0);
    let (kind, depth) = gl.bulk(READ_PIXELS, &[READ_PIXELS_DEPTH, 0, W | H << 16], W * H * 4);
    assert_eq!(kind, RESULT_FLOAT);
    assert!(depth == gl.legacy_pixels(READ_PIXELS_DEPTH, 0, 0, W, H));
    let values = words(&depth);
    assert!(
        values[0] != values[values.len() - 1],
        "depth is not slanted"
    );
    let (kind, stencil) = gl.bulk(
        READ_PIXELS,
        &[READ_PIXELS_STENCIL, 0, W | H << 16],
        W * H * 4,
    );
    assert_eq!(kind, RESULT_INT);
    assert!(words(&stencil).iter().all(|s| *s == 0x5a));
    assert!(stencil == gl.legacy_pixels(READ_PIXELS_STENCIL, 0, 0, W, H));
    let (kind, float) = gl.bulk(
        READ_PIXELS,
        &[READ_PIXELS_FLOAT, 0, W | 256 << 16],
        MAX_BULK,
    );
    assert_eq!(kind, RESULT_FLOAT);
    assert!(float == gl.legacy_pixels(READ_PIXELS_FLOAT, 0, 0, W, 256));

    // Malformed destinations: refused before execution, nothing written,
    // and the device keeps serving reads.
    let read = [TEXTURE_2D, UNITS | 12288 << 16, 0];
    let required = W * H * 4;
    gl.submit(&[call(BIND, &[TEXTURE_2D, 10])]);
    for (word, list, error) in [
        (PAGES | (MAX_BULK + 4096), LIST, ERROR_BATCH),
        (PAGES | (required - 4), LIST, ERROR_BATCH),
        (PAGES, LIST, ERROR_BATCH),
        (required, LIST, ERROR_BATCH),
        (PAGES | required, LIST + 4, ERROR_DMA),
        (PAGES | required, 0x400_0000, ERROR_DMA),
    ] {
        assert_eq!(
            gl.bulk_word(GET_TEX_IMAGE, &read, list, word).err(),
            Some(error),
            "{word:#x} at {list:#x}"
        );
    }
    for bad in [0u32, 0x1_0000, 0xfffff] {
        // Zero, past the 64 MiB of RAM, and the BIOS ROM at 4 GiB - 4 KiB.
        gl.qemu
            .command(&format!("writel {:#x} {bad:#x}", LIST + 400 * 4));
        assert_eq!(
            gl.bulk_word(GET_TEX_IMAGE, &read, LIST, PAGES | required)
                .err(),
            Some(ERROR_DMA),
            "frame {bad:#x}"
        );
    }
    gl.qemu.command(&format!(
        "writel {:#x} {:#x}",
        LIST + 400 * 4,
        frame(400) >> 12
    ));
    assert!(gl.bulk(GET_TEX_IMAGE, &read, required).1 == pattern);
    // The older encodings keep their limits on a contiguous destination.
    gl.queries += 1;
    gl.sequence += 1;
    let mut payload = vec![GET_TEX_IMAGE, TEXTURE_2D, 16385 << 16, 0];
    payload.resize(4, 0);
    gl.qemu.write(RESULT_ADDR_LO, 0x200000);
    gl.qemu.write(RESULT_CAPACITY, 65536);
    gl.qemu.batch(gl.sequence, &[(11, payload)]);
    assert_eq!(gl.wait(gl.sequence), ERROR_UNSUPPORTED);
    assert_eq!(gl.error(), 0);

    // The queued transport: the same reads, a malformed list, and the
    // completion's recheck of the list.
    gl.enable_queue();
    let (kind, bytes) = gl
        .bulk_word(GET_TEX_IMAGE, &read, QUEUE_LIST, PAGES | required)
        .unwrap();
    assert!(kind == RESULT_INT && bytes == pattern);
    // The slanted quad covered the window: draw the level 1:1 again.
    gl.queued(&draw);
    let (_, window) = gl
        .bulk_word(
            READ_PIXELS,
            &[0, 0, W | H << 16],
            QUEUE_LIST,
            PAGES | required,
        )
        .unwrap();
    assert!(window == pattern);
    gl.qemu
        .command(&format!("writel {:#x} 0", QUEUE_LIST + 3 * 4));
    assert_eq!(
        gl.bulk_word(GET_TEX_IMAGE, &read, QUEUE_LIST, PAGES | required)
            .err(),
        Some(ERROR_DMA)
    );
    gl.qemu.command(&format!(
        "writel {:#x} {:#x}",
        QUEUE_LIST + 3 * 4,
        frame(3) >> 12
    ));
    gl.queued(&[call(BIND, &[TEXTURE_2D, 15])]);
    let (_, whole) = gl
        .bulk_word(
            GET_TEX_IMAGE,
            &[TEXTURE_2D, UNITS | 16384 << 16, 0],
            QUEUE_LIST,
            PAGES | MAX_BULK,
        )
        .unwrap();
    assert!(whole == square);
    drop(server);
    let _ = std::fs::remove_dir_all(&directory);
}
