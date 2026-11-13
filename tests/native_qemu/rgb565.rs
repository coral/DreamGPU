//! Exact R5G6B5 storage (the second capability word's DG_CAP2_GL_RGB565)
//! through the device, with exact pixels: the CAPS2 register with and without
//! a GL engine; an RGB565 image of all 65536 5/6/5 words uploaded and read
//! back (RGBA8 wire bytes and float32); 1D, rectangle and 3D images; a
//! framebuffer object rendering into an RGB565 texture and an RGB565
//! renderbuffer (clears of exact and of bit-replicated 8-bit colours, a
//! texture sampled 1:1, DrawPixels); CopyTexImage/CopyTexSubImage into RGB565
//! from an RGB565 framebuffer object and from the RGBA8 window; display
//! lists; and RGB5, which stays what the native GL makes of it.
//!
//! A 5/6/5 component is exact when the high bits of its RGBA8 wire byte are
//! the stored value (the frontend packs UNSIGNED_SHORT_5_6_5 from them) and
//! the byte is the value's expansion to within one step (native GL rounds or
//! replicates bits).
use super::textures::{call, data_call, query, query_capacity, words};
use super::*;

const TEXTURE_1D: u32 = 0x0de0;
const TEXTURE_2D: u32 = 0x0de1;
const TEXTURE_3D: u32 = 0x806f;
const RECT: u32 = 0x84f5;
const RGB565: u32 = 0x8d62;
const RGB5: u32 = 0x8050;
const RGB8: u32 = 0x8051;
const RGB: u32 = 0x1907;
const RGBA: u32 = 0x1908;
const UNSIGNED_BYTE: u32 = 0x1401;
const PACKED_565: u32 = 0x8363;
const MIN_FILTER: u32 = 0x2801;
const MAG_FILTER: u32 = 0x2800;
const NEAREST: u32 = 0x2600;
const TEXTURE_ENV: u32 = 0x2300;
const TEXTURE_ENV_MODE: u32 = 0x2200;
const REPLACE: u32 = 0x1e01;
const SCISSOR_TEST: u32 = 0x0c11;
const DITHER: u32 = 0x0bd0;
const COLOR_BIT: u32 = 0x4000;
const FRAMEBUFFER: u32 = 0x8d40;
const RENDERBUFFER: u32 = 0x8d41;
const COLOR0: u32 = 0x8ce0;
const COMPLETE: u32 = 0x8cd5;
const COMPILE: u32 = 0x1300;
const INTERNAL_FORMAT: u32 = 0x1003;
const RED_SIZE: u32 = 0x805c;
const GREEN_SIZE: u32 = 0x805d;
const BLUE_SIZE: u32 = 0x805e;
const RENDERBUFFER_INTERNAL_FORMAT: u32 = 0x8d44;
const RENDERBUFFER_GREEN_SIZE: u32 = 0x8d51;
const RESULT_INT: u32 = 2;
const RESULT_FLOAT: u32 = 3;
const TEXTURE_READ_FLOAT: u32 = 0x8000;
const CAPS: u32 = 0x1008;
const CAPS2: u32 = 0x1194;
const CAP2_DEPTH_TEXTURE: u32 = 1;
const CAP2_RGB565: u32 = 2;

const BIND: u32 = 0x04e;
const PARAMETER: u32 = 0x8df;
const IMAGE_1D: u32 = 0x8d3;
const IMAGE_2D: u32 = 0x8d4;
const IMAGE_3D: u32 = 0x8d7;
const COPY_IMAGE_2D: u32 = 0x156;
const COPY_SUB_IMAGE_2D: u32 = 0x15a;
const TEX_ENV_I: u32 = 0x8c6;
const ENABLE: u32 = 0x209;
const DISABLE: u32 = 0x1c5;
const CLEAR: u32 = 0x09a;
const CLEAR_COLOR: u32 = 0x0a3;
const SCISSOR: u32 = 0x7f0;
const BEGIN: u32 = 0x01a;
const END: u32 = 0x216;
const TEXCOORD2F: u32 = 0x883;
const VERTEX2F: u32 = 0x9dc;
const RASTER_POS4D: u32 = 0x795;
const DRAW_PIXELS: u32 = 0x1f2;
const BIND_FRAMEBUFFER: u32 = 0x03c;
const BIND_RENDERBUFFER: u32 = 0x048;
const RENDERBUFFER_STORAGE: u32 = 0x7b2;
const FRAMEBUFFER_TEXTURE_2D: u32 = 0x27f;
const FRAMEBUFFER_RENDERBUFFER: u32 = 0x277;
const CHECK_STATUS: u32 = 0x094;
const GET_RENDERBUFFER: u32 = 0x3f3;
const GET_TEX_IMAGE: u32 = 0x414;
const LEVEL_PARAMETER: u32 = 0x416;
const GET_ERROR: u32 = 0x2fc;
const READ_PIXELS: u32 = 0x7a4;
const GEN_LISTS: u32 = 671;
const NEW_LIST: u32 = 1592;
const END_LIST: u32 = 539;
const CALL_LIST: u32 = 0x092;

/// The drawable, the framebuffer object and the sampled texture.
const W: u32 = 64;
const H: u32 = 32;

/// Every green with every red; blue runs with both, so all 32 blues occur.
fn pattern(x: u32, y: u32) -> u16 {
    (y << 11 | x << 5 | ((x + y) & 31)) as u16
}
/// Column clears: green x, and reds and blues that run through all 32 values.
fn column(x: u32) -> u16 {
    (((7 * x + 3) & 31) << 11 | x << 5 | ((13 * x + 5) & 31)) as u16
}
fn components(word: u16) -> [u32; 3] {
    let w = u32::from(word);
    [w >> 11, (w >> 5) & 63, w & 31]
}
fn shorts(values: impl IntoIterator<Item = u16>) -> Vec<u8> {
    values.into_iter().flat_map(u16::to_le_bytes).collect()
}
/// A word's bit-replicated RGBA8 colour.
fn bytes(word: u16) -> [u8; 4] {
    let [r, g, b] = components(word);
    [
        (r << 3 | r >> 2) as u8,
        (g << 2 | g >> 4) as u8,
        (b << 3 | b >> 2) as u8,
        255,
    ]
}
fn f(v: f32) -> u32 {
    v.to_bits()
}
/// `word`'s components as exact floats, or with `bytes` as the 8-bit
/// bit-replicated colour a Direct3D clear of that word passes through GL.
fn color(word: u16, bytes: bool) -> [u32; 4] {
    let [r, g, b] = components(word);
    let channel = |v: u32, bits: u32| {
        let max = (1 << bits) - 1;
        if bytes {
            let byte = (v << (8 - bits)) | (v >> (2 * bits - 8));
            byte as f32 / 255.
        } else {
            v as f32 / max as f32
        }
    };
    [f(channel(r, 5)), f(channel(g, 6)), f(channel(b, 5)), f(1.)]
}

/// One RGBA8 wire pixel against a 5/6/5 word.
fn exact(pixel: [u8; 4], word: u16, label: &str) {
    let [r, g, b] = components(word);
    for (byte, value, bits) in [(pixel[0], r, 5), (pixel[1], g, 6), (pixel[2], b, 5)] {
        let expansion = (value as f32 * 255. / ((1 << bits) - 1) as f32).round() as i32;
        assert!(
            u32::from(byte) >> (8 - bits) == value && (i32::from(byte) - expansion).abs() <= 1,
            "{label}: {pixel:?} for {word:#06x}"
        );
    }
    assert_eq!(pixel[3], 255, "{label}: alpha of {word:#06x}");
}
/// One float32 RGBA pixel against a 5/6/5 word: each component within a
/// quarter step of its value.
fn exact_float(pixel: &[f32], word: u16, label: &str) {
    let [r, g, b] = components(word);
    for (value, stored, bits) in [(pixel[0], r, 5), (pixel[1], g, 6), (pixel[2], b, 5)] {
        let steps = value * ((1 << bits) - 1) as f32;
        assert!(
            (steps - stored as f32).abs() < 0.25,
            "{label}: {pixel:?} for {word:#06x}"
        );
    }
}

struct Device {
    qemu: Qemu,
    sequence: u32,
}
impl Device {
    fn submit(&mut self, records: &[(u32, Vec<u32>)]) {
        self.sequence += 1;
        self.qemu.batch(self.sequence, records);
        self.qemu.await_completion(self.sequence);
    }
    fn word(&mut self, function: u32, args: &[u32]) -> u32 {
        words(&query(&mut self.qemu, &mut self.sequence, function, args).1)[0]
    }
    fn error(&mut self) -> u32 {
        self.word(GET_ERROR, &[])
    }
    fn level(&mut self, target: u32, level: u32, pname: u32) -> u32 {
        self.word(LEVEL_PARAMETER, &[target, level, pname])
    }
    /// The read framebuffer's colour, bottom row first.
    fn pixels(&mut self) -> Vec<[u8; 4]> {
        let bytes = query_capacity(
            &mut self.qemu,
            &mut self.sequence,
            READ_PIXELS,
            &[0, 0, W | (H << 16)],
            W * H * 4,
        )
        .1;
        bytes.as_chunks::<4>().0.to_vec()
    }
    /// `count` RGBA8 texels of a level from `first`.
    fn texels(&mut self, target: u32, level: u32, first: u32, count: u32) -> Vec<[u8; 4]> {
        let (kind, bytes) = query_capacity(
            &mut self.qemu,
            &mut self.sequence,
            GET_TEX_IMAGE,
            &[target, level | (count << 16), first],
            count * 4,
        );
        assert_eq!(kind, RESULT_INT);
        bytes.as_chunks::<4>().0.to_vec()
    }
    /// `count` float32 RGBA texels of a level from `first`.
    fn float_texels(&mut self, target: u32, first: u32, count: u32) -> Vec<f32> {
        let (kind, bytes) = query_capacity(
            &mut self.qemu,
            &mut self.sequence,
            GET_TEX_IMAGE,
            &[target, TEXTURE_READ_FLOAT | (count << 16), first],
            count * 16,
        );
        assert_eq!(kind, RESULT_FLOAT);
        words(&bytes).into_iter().map(f32::from_bits).collect()
    }
    /// The read framebuffer against `expected(x, y)`.
    fn compare(&mut self, expected: impl Fn(u32, u32) -> u16, label: &str) {
        let pixels = self.pixels();
        assert_eq!(pixels.len(), (W * H) as usize, "{label}");
        for y in 0..H {
            for x in 0..W {
                exact(
                    pixels[(y * W + x) as usize],
                    expected(x, y),
                    &format!("{label} pixel {x},{y}"),
                );
            }
        }
    }
    /// A W x H level against `expected(x, y)`.
    fn compare_level(&mut self, target: u32, expected: impl Fn(u32, u32) -> u16, label: &str) {
        let texels = self.texels(target, 0, 0, W * H);
        for (i, &texel) in texels.iter().enumerate() {
            let (x, y) = (i as u32 % W, i as u32 / W);
            exact(texel, expected(x, y), &format!("{label} texel {x},{y}"));
        }
    }
    /// The storage of an RGB565 level: native RGB565 (5/6/5 bits) or, where
    /// the native GL has no RGB565, RGB8.
    fn storage(&mut self, target: u32, label: &str) -> bool {
        let format = self.level(target, 0, INTERNAL_FORMAT);
        let sizes = [RED_SIZE, GREEN_SIZE, BLUE_SIZE].map(|n| self.level(target, 0, n));
        match format {
            RGB565 => assert_eq!(sizes, [5, 6, 5], "{label}"),
            RGB8 => assert_eq!(sizes, [8, 8, 8], "{label}"),
            other => panic!("{label}: internal format {other:#x}"),
        }
        format == RGB565
    }
}
/// Column clears of the bound draw framebuffer: column x takes `column(x)`,
/// even columns as exact floats, odd ones as 8-bit colours.
fn column_clears() -> Vec<(u32, Vec<u32>)> {
    let mut records = vec![call(ENABLE, &[SCISSOR_TEST])];
    for x in 0..W {
        records.push(call(SCISSOR, &[x, 0, 1, H]));
        records.push(call(CLEAR_COLOR, &color(column(x), x % 2 == 1)));
        records.push(call(CLEAR, &[COLOR_BIT]));
    }
    records.push(call(DISABLE, &[SCISSOR_TEST]));
    records
}
/// The bound 2D texture sampled 1:1 over the whole drawable.
fn one_to_one() -> Vec<(u32, Vec<u32>)> {
    let mut records = vec![
        call(PARAMETER, &[TEXTURE_2D, MIN_FILTER, NEAREST]),
        call(PARAMETER, &[TEXTURE_2D, MAG_FILTER, NEAREST]),
        call(TEX_ENV_I, &[TEXTURE_ENV, TEXTURE_ENV_MODE, REPLACE]),
        call(ENABLE, &[TEXTURE_2D]),
        call(BEGIN, &[7]),
    ];
    for (s, t) in [(0f32, 0f32), (1., 0.), (1., 1.), (0., 1.)] {
        records.push(call(TEXCOORD2F, &[f(s), f(t)]));
        records.push(call(VERTEX2F, &[f(2. * s - 1.), f(2. * t - 1.)]));
    }
    records.push(call(END, &[]));
    records.push(call(DISABLE, &[TEXTURE_2D]));
    records
}
fn raster_origin() -> (u32, Vec<u32>) {
    let mut args = Vec::new();
    for v in [-1f64, -1., 0., 1.] {
        let bits = v.to_bits();
        args.extend([bits as u32, (bits >> 32) as u32]);
    }
    call(RASTER_POS4D, &args)
}

#[test]
#[ignore = "requires native host GPU and RGB565 QEMU producer; diskless acceptance"]
fn qemu_rgb565_images_targets_copies_and_lists_keep_six_bit_green() {
    let (_device, _queue, host) = native_gpu();
    let directory =
        std::env::temp_dir().join(format!("dg-rgb565-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();

    // Without a GL engine the device offers neither GL CAPS bits nor CAPS2.
    {
        let mut plain = Qemu::start(&directory, Path::new(""));
        assert_eq!(plain.read(CAPS) & 0x100, 0); // DG_CAP_GL_TRANSPORT
        assert_eq!(plain.read(CAPS2), 0);
    }
    let socket = directory.join("g.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let qemu = Qemu::start(&directory, &socket);
    let mut gl = Device { qemu, sequence: 0 };
    assert_ne!(gl.qemu.read(CAPS) & 0x100, 0);
    // CAPS2 holds depth textures, exact RGB565, bulk reads (bulk_read.rs) and
    // the GL 1.2 core names (core_1_2.rs); the next register is unused.
    assert_eq!(gl.qemu.read(CAPS2), CAP2_DEPTH_TEXTURE | CAP2_RGB565 | 4 | 8);
    assert_eq!(gl.qemu.read(CAPS2 + 4), 0);
    gl.submit(&[
        (1, vec![0]),
        (3, vec![W, H]),
        (5, vec![]),
        call(DISABLE, &[DITHER]),
    ]);

    // Every 5/6/5 word, uploaded and read back as RGBA8 bytes and as floats.
    let all: Vec<u16> = (0..=u16::MAX).collect();
    gl.submit(&[
        call(BIND, &[TEXTURE_2D, 10]),
        data_call(
            IMAGE_2D,
            &[TEXTURE_2D, 0, RGB565, 256, 256, 0, RGB, PACKED_565],
            &shorts(all.iter().copied()),
        ),
    ]);
    assert_eq!(gl.error(), 0);
    let native = gl.storage(TEXTURE_2D, "65536 words");
    eprintln!(
        "RGB565 storage: {}",
        if native { "native RGB565" } else { "RGB8" }
    );
    for first in (0..65536).step_by(16384) {
        let texels = gl.texels(TEXTURE_2D, 0, first, 16384);
        for (i, &texel) in texels.iter().enumerate() {
            exact(texel, all[first as usize + i], "65536 words");
        }
    }
    for first in (0..65536).step_by(4096) {
        let floats = gl.float_texels(TEXTURE_2D, first, 4096);
        for (i, pixel) in floats.chunks(4).enumerate() {
            exact_float(pixel, all[first as usize + i], "65536 words as floats");
        }
    }

    // 1D, rectangle and 3D images of the same storage.
    let greens: Vec<u16> = (0..64).map(column).collect();
    gl.submit(&[
        call(BIND, &[TEXTURE_1D, 11]),
        data_call(
            IMAGE_1D,
            &[TEXTURE_1D, 0, RGB565, 64, 1, 0, RGB, PACKED_565],
            &shorts(greens.iter().copied()),
        ),
        call(BIND, &[RECT, 12]),
        data_call(
            IMAGE_2D,
            &[RECT, 0, RGB565, W, H, 0, RGB, PACKED_565],
            &shorts((0..W * H).map(|i| pattern(i % W, i / W))),
        ),
        call(BIND, &[TEXTURE_3D, 13]),
        data_call(
            IMAGE_3D,
            &[TEXTURE_3D, 0, RGB565, 4, 4, 4, 0, RGB, PACKED_565],
            &shorts(greens.iter().copied()),
        ),
    ]);
    assert_eq!(gl.error(), 0);
    assert_eq!(gl.storage(TEXTURE_1D, "1D"), native);
    for (i, &texel) in gl.texels(TEXTURE_1D, 0, 0, 64).iter().enumerate() {
        exact(texel, greens[i], "1D");
    }
    let floats = gl.float_texels(TEXTURE_1D, 0, 64);
    for (i, pixel) in floats.chunks(4).enumerate() {
        exact_float(pixel, greens[i], "1D as floats");
    }
    assert_eq!(gl.storage(RECT, "rectangle"), native);
    gl.compare_level(RECT, pattern, "rectangle");
    assert_eq!(gl.storage(TEXTURE_3D, "3D"), native);
    for (i, &texel) in gl.texels(TEXTURE_3D, 0, 0, 64).iter().enumerate() {
        exact(texel, greens[i], "3D");
    }

    // A framebuffer object rendering into an RGB565 texture: column clears,
    // the pattern texture sampled 1:1, and DrawPixels.
    gl.submit(&[
        call(BIND, &[TEXTURE_2D, 20]),
        data_call(
            IMAGE_2D,
            &[TEXTURE_2D, 0, RGB565, W, H, 0, RGB, PACKED_565],
            &[],
        ),
        call(PARAMETER, &[TEXTURE_2D, MIN_FILTER, NEAREST]),
        call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 1]),
        call(
            FRAMEBUFFER_TEXTURE_2D,
            &[FRAMEBUFFER, COLOR0, TEXTURE_2D, 20, 0],
        ),
    ]);
    assert_eq!(gl.word(CHECK_STATUS, &[FRAMEBUFFER]), COMPLETE);
    gl.submit(&column_clears());
    assert_eq!(gl.error(), 0);
    gl.compare(|x, _| column(x), "texture attachment clears");
    gl.compare_level(TEXTURE_2D, |x, _| column(x), "cleared attachment");
    let mut records = vec![
        call(BIND, &[TEXTURE_2D, 21]),
        data_call(
            IMAGE_2D,
            &[TEXTURE_2D, 0, RGB565, W, H, 0, RGB, PACKED_565],
            &shorts((0..W * H).map(|i| pattern(i % W, i / W))),
        ),
    ];
    records.extend(one_to_one());
    gl.submit(&records);
    assert_eq!(gl.error(), 0);
    gl.compare(pattern, "texture sampled 1:1 into RGB565");
    let reversed = |x: u32, y: u32| pattern(W - 1 - x, H - 1 - y);
    gl.submit(&[
        raster_origin(),
        // GL 1.1 DrawPixels has no packed types: the 8-bit colours of the
        // words, as an application holding 565 data draws them.
        data_call(
            DRAW_PIXELS,
            &[W, H, RGBA, UNSIGNED_BYTE, W * H * 4, 0, 3, 1],
            &(0..W * H)
                .flat_map(|i| bytes(reversed(i % W, i / W)))
                .collect::<Vec<_>>(),
        ),
    ]);
    assert_eq!(gl.error(), 0);
    gl.compare(reversed, "DrawPixels into RGB565");

    // CopyTexImage and CopyTexSubImage from that RGB565 framebuffer object.
    gl.submit(&[
        call(BIND, &[TEXTURE_2D, 30]),
        call(COPY_IMAGE_2D, &[TEXTURE_2D, 0, RGB565, 0, 0, W, H, 0]),
    ]);
    assert_eq!(gl.error(), 0);
    assert_eq!(gl.storage(TEXTURE_2D, "copy"), native);
    gl.compare_level(TEXTURE_2D, reversed, "CopyTexImage from RGB565");
    gl.submit(&column_clears());
    gl.submit(&[call(
        COPY_SUB_IMAGE_2D,
        &[TEXTURE_2D, 0, 0, 0, 0, 0, W / 2, H],
    )]);
    assert_eq!(gl.error(), 0);
    gl.compare_level(
        TEXTURE_2D,
        |x, y| if x < W / 2 { column(x) } else { reversed(x, y) },
        "CopyTexSubImage from RGB565",
    );

    // An RGB565 renderbuffer: clears and the sampled pattern.
    gl.submit(&[
        call(BIND_RENDERBUFFER, &[RENDERBUFFER, 2]),
        call(RENDERBUFFER_STORAGE, &[RENDERBUFFER, RGB565, W, H]),
        call(
            FRAMEBUFFER_RENDERBUFFER,
            &[FRAMEBUFFER, COLOR0, RENDERBUFFER, 2],
        ),
    ]);
    assert_eq!(gl.error(), 0);
    assert_eq!(gl.word(CHECK_STATUS, &[FRAMEBUFFER]), COMPLETE);
    let rb_format = gl.word(
        GET_RENDERBUFFER,
        &[RENDERBUFFER, RENDERBUFFER_INTERNAL_FORMAT],
    );
    let rb_green = gl.word(GET_RENDERBUFFER, &[RENDERBUFFER, RENDERBUFFER_GREEN_SIZE]);
    assert_eq!(
        (rb_format, rb_green),
        if native { (RGB565, 6) } else { (RGB8, 8) }
    );
    gl.submit(&column_clears());
    gl.compare(|x, _| column(x), "renderbuffer clears");
    let mut records = vec![call(BIND, &[TEXTURE_2D, 21])];
    records.extend(one_to_one());
    gl.submit(&records);
    gl.compare(pattern, "texture sampled 1:1 into an RGB565 renderbuffer");

    // CopyTexImage into RGB565 from the RGBA8 window, cleared with exact
    // colours: the copy keeps every 5/6/5 value.
    gl.submit(&[call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 0])]);
    gl.submit(&column_clears());
    gl.compare(|x, _| column(x), "window clears");
    gl.submit(&[
        call(BIND, &[TEXTURE_2D, 31]),
        call(COPY_IMAGE_2D, &[TEXTURE_2D, 0, RGB565, 0, 0, W, H, 0]),
    ]);
    assert_eq!(gl.error(), 0);
    gl.compare_level(TEXTURE_2D, |x, _| column(x), "CopyTexImage from the window");

    // Display lists capture an RGB565 image; executing the list defines it.
    let list = words(&query(&mut gl.qemu, &mut gl.sequence, GEN_LISTS, &[1, 0, 0]).1)[1];
    gl.submit(&[call(BIND, &[TEXTURE_2D, 40])]);
    assert_eq!(
        words(
            &query(
                &mut gl.qemu,
                &mut gl.sequence,
                NEW_LIST,
                &[list, COMPILE, 0]
            )
            .1
        ),
        [0]
    );
    gl.submit(&[data_call(
        IMAGE_2D,
        &[TEXTURE_2D, 0, RGB565, W, H, 0, RGB, PACKED_565],
        &shorts((0..W * H).map(|i| pattern(i % W, i / W))),
    )]);
    assert_eq!(
        words(&query(&mut gl.qemu, &mut gl.sequence, END_LIST, &[0; 3]).1),
        [0]
    );
    assert_eq!(gl.level(TEXTURE_2D, 0, INTERNAL_FORMAT), RGBA);
    gl.submit(&[call(CALL_LIST, &[list])]);
    assert_eq!(gl.error(), 0);
    assert_eq!(gl.storage(TEXTURE_2D, "list"), native);
    gl.compare_level(TEXTURE_2D, pattern, "display list image");

    // RGB5 is unchanged: native GL's own storage, reported as RGB5. Greens
    // that survive five bits read back exactly either way.
    let safe: Vec<u16> = (0..64)
        .map(|x| {
            let [r, _, b] = components(column(x));
            let g = ((x & 31) * 126 + 31) / 62;
            (r << 11 | g << 5 | b) as u16
        })
        .collect();
    gl.submit(&[
        call(BIND, &[TEXTURE_1D, 50]),
        data_call(
            IMAGE_1D,
            &[TEXTURE_1D, 0, RGB5, 64, 1, 0, RGB, PACKED_565],
            &shorts(safe.iter().copied()),
        ),
    ]);
    assert_eq!(gl.error(), 0);
    assert_eq!(gl.level(TEXTURE_1D, 0, INTERNAL_FORMAT), RGB5);
    // Their bytes may be five-bit expansions; the packed words are exact.
    for (i, &texel) in gl.texels(TEXTURE_1D, 0, 0, 64).iter().enumerate() {
        let [r, g, b] = components(safe[i]);
        assert_eq!(
            [texel[0] >> 3, texel[1] >> 2, texel[2] >> 3].map(u32::from),
            [r, g, b],
            "RGB5 with five-bit-safe greens: {texel:?} for {:#06x}",
            safe[i]
        );
    }
    let green = gl.level(TEXTURE_1D, 0, GREEN_SIZE);
    eprintln!("native RGB5 green bits: {green}");
    assert!(green == 5 || green == 6, "RGB5 green bits {green}");
    drop(server);
}
