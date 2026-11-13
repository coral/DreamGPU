//! The GL 1.2 core names (the second capability word's DG_CAP2_GL_CORE_1_2)
//! through the device: the CAPS2 bit; TEXTURE_MIN_LOD and TEXTURE_MAX_LOD as
//! scalar parameter records and GetTexParameterfv queries, and their effect on
//! the level a minified draw samples; RESCALE_NORMAL as an IsEnabled name; the
//! MAX_ELEMENTS_VERTICES/INDICES hints. Older hosts rejected each of these.
use super::textures::{call, data_call, query, query_capacity, words};
use super::*;

const TEXTURE_2D: u32 = 0x0de1;
const RGBA: u32 = 0x1908;
const UNSIGNED_BYTE: u32 = 0x1401;
const MIN_FILTER: u32 = 0x2801;
const MAG_FILTER: u32 = 0x2800;
const NEAREST: u32 = 0x2600;
const NEAREST_MIPMAP_NEAREST: u32 = 0x2700;
const MIN_LOD: u32 = 0x813a;
const MAX_LOD: u32 = 0x813b;
const RESCALE_NORMAL: u32 = 0x803a;
const MAX_ELEMENTS_VERTICES: u32 = 0x80e8;
const MAX_ELEMENTS_INDICES: u32 = 0x80e9;
const TEXTURE_ENV: u32 = 0x2300;
const TEXTURE_ENV_MODE: u32 = 0x2200;
const REPLACE: u32 = 0x1e01;
const DITHER: u32 = 0x0bd0;
const CAPS: u32 = 0x1008;
const CAPS2: u32 = 0x1194;
const CAP2_CORE_1_2: u32 = 8;

const BIND: u32 = 0x04e;
const PARAMETER_F: u32 = 0x8dd;
const PARAMETER_I: u32 = 0x8df;
const IMAGE_2D: u32 = 0x8d4;
const TEX_ENV_I: u32 = 0x8c6;
const ENABLE: u32 = 0x209;
const DISABLE: u32 = 0x1c5;
const BEGIN: u32 = 0x01a;
const END: u32 = 0x216;
const TEXCOORD2F: u32 = 0x883;
const VERTEX2F: u32 = 0x9dc;
const GET_TEX_PARAMETER_FV: u32 = 0x418;
const GET_INTEGER: u32 = 0x329;
const IS_ENABLED: u32 = 0x4ba;
const GET_ERROR: u32 = 0x2fc;
const READ_PIXELS: u32 = 0x7a4;

/// The drawable: a 128x128 texture drawn over it is minified by 2 in x and 4
/// in y, so NEAREST_MIPMAP_NEAREST samples level 2 without LOD clamps.
const W: u32 = 64;
const H: u32 = 32;
/// One colour per level of the 128x128 chain (levels 0..=7).
const LEVELS: [[u8; 4]; 8] = [
    [255, 0, 0, 255],
    [0, 255, 0, 255],
    [0, 0, 255, 255],
    [255, 255, 0, 255],
    [0, 255, 255, 255],
    [255, 0, 255, 255],
    [255, 255, 255, 255],
    [128, 128, 128, 255],
];

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
    /// A RESULT_BOOL answer: one byte.
    fn boolean(&mut self, function: u32, args: &[u32]) -> u8 {
        query(&mut self.qemu, &mut self.sequence, function, args).1[0]
    }
    fn float(&mut self, function: u32, args: &[u32]) -> f32 {
        f32::from_bits(self.word(function, args))
    }
    /// The read framebuffer's centre pixel after drawing the texture over
    /// the whole drawable.
    fn sampled(&mut self) -> [u8; 4] {
        let mut records = vec![call(ENABLE, &[TEXTURE_2D]), call(BEGIN, &[7])];
        for (s, t) in [(0f32, 0f32), (1., 0.), (1., 1.), (0., 1.)] {
            records.push(call(TEXCOORD2F, &[s.to_bits(), t.to_bits()]));
            records.push(call(
                VERTEX2F,
                &[(2. * s - 1.).to_bits(), (2. * t - 1.).to_bits()],
            ));
        }
        records.push(call(END, &[]));
        records.push(call(DISABLE, &[TEXTURE_2D]));
        self.submit(&records);
        let bytes = query_capacity(
            &mut self.qemu,
            &mut self.sequence,
            READ_PIXELS,
            &[0, 0, W | (H << 16)],
            W * H * 4,
        )
        .1;
        let pixels = bytes.as_chunks::<4>().0;
        pixels[(H / 2 * W + W / 2) as usize]
    }
}

#[test]
#[ignore = "requires native host GPU and GL 1.2 core QEMU producer; diskless acceptance"]
fn qemu_gl_1_2_core_names_reach_native_gl() {
    let (_device, _queue, host) = native_gpu();
    let directory =
        std::env::temp_dir().join(format!("dg-core12-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    {
        let mut plain = Qemu::start(&directory, Path::new(""));
        assert_eq!(plain.read(CAPS2) & CAP2_CORE_1_2, 0);
    }
    let socket = directory.join("g.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let qemu = Qemu::start(&directory, &socket);
    let mut gl = Device { qemu, sequence: 0 };
    assert_ne!(gl.qemu.read(CAPS) & 0x100, 0);
    assert_ne!(gl.qemu.read(CAPS2) & CAP2_CORE_1_2, 0);
    gl.submit(&[
        (1, vec![0]),
        (3, vec![W, H]),
        (5, vec![]),
        call(DISABLE, &[DITHER]),
    ]);

    // A full 128x128 chain, one colour per level.
    let mut records = vec![
        call(BIND, &[TEXTURE_2D, 10]),
        call(PARAMETER_I, &[TEXTURE_2D, MIN_FILTER, NEAREST_MIPMAP_NEAREST]),
        call(PARAMETER_I, &[TEXTURE_2D, MAG_FILTER, NEAREST]),
        call(TEX_ENV_I, &[TEXTURE_ENV, TEXTURE_ENV_MODE, REPLACE]),
    ];
    for (level, colour) in LEVELS.iter().enumerate() {
        let size = 128u32 >> level;
        let pixels: Vec<u8> = (0..size * size).flat_map(|_| *colour).collect();
        records.push(data_call(
            IMAGE_2D,
            &[
                TEXTURE_2D,
                level as u32,
                RGBA,
                size,
                size,
                0,
                RGBA,
                UNSIGNED_BYTE,
            ],
            &pixels,
        ));
    }
    gl.submit(&records);
    assert_eq!(gl.error(), 0);
    // Defaults: -1000/1000, and the draw samples level 2.
    assert_eq!(gl.float(GET_TEX_PARAMETER_FV, &[TEXTURE_2D, MIN_LOD]), -1000.);
    assert_eq!(gl.float(GET_TEX_PARAMETER_FV, &[TEXTURE_2D, MAX_LOD]), 1000.);
    assert_eq!(gl.sampled(), LEVELS[2]);
    // MAX_LOD 1 (an integer record) clamps it to level 1.
    gl.submit(&[call(PARAMETER_I, &[TEXTURE_2D, MAX_LOD, 1])]);
    assert_eq!(gl.error(), 0);
    assert_eq!(gl.float(GET_TEX_PARAMETER_FV, &[TEXTURE_2D, MAX_LOD]), 1.);
    assert_eq!(gl.sampled(), LEVELS[1]);
    // MIN_LOD 4 (a float record; above MAX_LOD is legal) clamps up past it.
    gl.submit(&[
        call(PARAMETER_I, &[TEXTURE_2D, MAX_LOD, 1000]),
        call(PARAMETER_F, &[TEXTURE_2D, MIN_LOD, 4f32.to_bits()]),
    ]);
    assert_eq!(gl.error(), 0);
    assert_eq!(gl.float(GET_TEX_PARAMETER_FV, &[TEXTURE_2D, MIN_LOD]), 4.);
    assert_eq!(gl.sampled(), LEVELS[4]);

    // RESCALE_NORMAL: Enable reached native GL before; now IsEnabled reads it.
    assert_eq!(gl.boolean(IS_ENABLED, &[RESCALE_NORMAL]), 0);
    gl.submit(&[call(ENABLE, &[RESCALE_NORMAL])]);
    assert_eq!(gl.boolean(IS_ENABLED, &[RESCALE_NORMAL]), 1);
    gl.submit(&[call(DISABLE, &[RESCALE_NORMAL])]);
    assert_eq!(gl.boolean(IS_ENABLED, &[RESCALE_NORMAL]), 0);
    // The draw-range hints are native and positive.
    for name in [MAX_ELEMENTS_VERTICES, MAX_ELEMENTS_INDICES] {
        let value = gl.word(GET_INTEGER, &[name]) as i32;
        eprintln!("native {name:#x}: {value}");
        assert!(value > 0, "{name:#x}: {value}");
    }
    assert_eq!(gl.error(), 0);
    drop(server);
}
