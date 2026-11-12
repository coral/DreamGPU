//! ARB_depth_texture, ARB_shadow and ARB_fragment_program_shadow (the second
//! capability word's DG_CAP2_GL_DEPTH_TEXTURE) through the device, with exact
//! pixels: the CAPS2 register with and without a GL engine; depth images of
//! TEXTURE_2D, TEXTURE_1D and TEXTURE_RECTANGLE uploaded (also as zero-defined
//! storage and sub-images) and read back as float depth and packed words; the
//! compare modes and functions and every DEPTH_TEXTURE_MODE through fixed
//! function sampling, with LINEAR filtering averaging comparison results;
//! CopyTexImage/CopyTexSubImage from the window's depth buffer; a framebuffer
//! object rendering into a depth texture attachment that is then sampled;
//! SHADOW1D/SHADOW2D/SHADOWRECT program lookups (TEX and TXP); COMBINE
//! environments; display lists; level and object parameters; and the spec's
//! GL errors, which keep the batch.
use super::textures::{call, data_call, query, query_capacity, words};
use super::*;

const TEXTURE_1D: u32 = 0x0de0;
const TEXTURE_2D: u32 = 0x0de1;
const RECT: u32 = 0x84f5;
const MIN_FILTER: u32 = 0x2801;
const MAG_FILTER: u32 = 0x2800;
const WRAP_S: u32 = 0x2802;
const WRAP_T: u32 = 0x2803;
const CLAMP_TO_EDGE: u32 = 0x812f;
const NEAREST: u32 = 0x2600;
const LINEAR: u32 = 0x2601;
const DEPTH_COMPONENT: u32 = 0x1902;
const DEPTH_COMPONENT16: u32 = 0x81a5;
const DEPTH_COMPONENT24: u32 = 0x81a6;
const DEPTH_STENCIL: u32 = 0x84f9;
const DEPTH24_STENCIL8: u32 = 0x88f0;
const UNSIGNED_INT_24_8: u32 = 0x84fa;
const UNSIGNED_SHORT: u32 = 0x1403;
const FLOAT: u32 = 0x1406;
const RGBA8: u32 = 0x8058;
const RGBA: u32 = 0x1908;
const UNSIGNED_BYTE: u32 = 0x1401;
const COMPARE_MODE: u32 = 0x884c;
const COMPARE_FUNC: u32 = 0x884d;
const COMPARE_R_TO_TEXTURE: u32 = 0x884e;
const DEPTH_TEXTURE_MODE: u32 = 0x884b;
const TEXTURE_DEPTH_SIZE: u32 = 0x884a;
const TEXTURE_STENCIL_SIZE: u32 = 0x88f1;
const LUMINANCE: u32 = 0x1909;
const INTENSITY: u32 = 0x8049;
const ALPHA: u32 = 0x1906;
const LEQUAL: u32 = 0x0203;
const GEQUAL: u32 = 0x0206;
const LESS: u32 = 0x0201;
const TEXTURE_ENV: u32 = 0x2300;
const TEXTURE_ENV_MODE: u32 = 0x2200;
const MODULATE: u32 = 0x2100;
const COMBINE: u32 = 0x8570;
const COMBINE_RGB: u32 = 0x8571;
const COMBINE_ALPHA: u32 = 0x8572;
const SOURCE0_RGB: u32 = 0x8580;
const SOURCE0_ALPHA: u32 = 0x8588;
const OPERAND0_RGB: u32 = 0x8590;
const OPERAND0_ALPHA: u32 = 0x8598;
const TEXTURE: u32 = 0x1702;
const REPLACE: u32 = 0x1e01;
const ONE_MINUS_SRC_COLOR: u32 = 0x0301;
const SRC_ALPHA: u32 = 0x0302;
const DEPTH_TEST: u32 = 0x0b71;
const FRAMEBUFFER: u32 = 0x8d40;
const COLOR0: u32 = 0x8ce0;
const DEPTH_ATTACHMENT: u32 = 0x8d00;
const STENCIL_ATTACHMENT: u32 = 0x8d20;
const COMPLETE: u32 = 0x8cd5;
const FP: u32 = 0x8804;
const ASCII: u32 = 0x8875;
const COMPILE: u32 = 0x1300;
const DEPTH_BIT: u32 = 0x0100;
const COLOR_BIT: u32 = 0x4000;
const STENCIL_BIT: u32 = 0x0400;
const INVALID_ENUM: u32 = 0x0500;
const INVALID_OPERATION: u32 = 0x0502;
const RESULT_INT: u32 = 2;
const RESULT_FLOAT: u32 = 3;
const READ_DEPTH: u32 = 0x10000; // ReadPixels mode
const TEXTURE_READ_DEPTH: u32 = 0x4000; // GetTexImage level-word bits
const TEXTURE_READ_FLOAT: u32 = 0x8000;
const CAPS: u32 = 0x1008;
const CAPS2: u32 = 0x1194;

const BIND: u32 = 0x04e;
const PARAMETER: u32 = 0x8df;
const PARAMETER_F: u32 = 0x8dd;
const IMAGE_1D: u32 = 0x8d3;
const IMAGE_2D: u32 = 0x8d4;
const SUB_IMAGE_1D: u32 = 0x8f3;
const SUB_IMAGE_2D: u32 = 0x8f5;
const COPY_IMAGE_1D: u32 = 0x154;
const COPY_IMAGE_2D: u32 = 0x156;
const COPY_SUB_IMAGE_2D: u32 = 0x15a;
const TEX_ENV_I: u32 = 0x8c6;
const ENABLE: u32 = 0x209;
const DISABLE: u32 = 0x1c5;
const CLEAR: u32 = 0x09a;
const CLEAR_DEPTH: u32 = 0x0a7;
const CLEAR_STENCIL: u32 = 0x0b5;
const DEPTH_FUNC: u32 = 0x1b9;
const COLOR4F: u32 = 0x0db;
const BEGIN: u32 = 0x01a;
const END: u32 = 0x216;
const TEXCOORD4F: u32 = 0x8a9;
const VERTEX3F: u32 = 0x9ea;
const BIND_FRAMEBUFFER: u32 = 0x03c;
const FRAMEBUFFER_TEXTURE_1D: u32 = 0x27d;
const FRAMEBUFFER_TEXTURE_2D: u32 = 0x27f;
const CHECK_STATUS: u32 = 0x094;
const BIND_PROGRAM: u32 = 69;
const GEN_PROGRAMS: u32 = 677;
const PROGRAM_STRING: u32 = 1766;
const GET_TEX_IMAGE: u32 = 0x414;
const LEVEL_PARAMETER: u32 = 0x416;
const GET_PARAMETER: u32 = 0x41d;
const GET_ERROR: u32 = 0x2fc;
const READ_PIXELS: u32 = 0x7a4;
const GEN_LISTS: u32 = 671;
const NEW_LIST: u32 = 1592;
const END_LIST: u32 = 539;
const CALL_LIST: u32 = 0x092;

/// The drawable and the 2D depth textures.
const W: u32 = 16;
const H: u32 = 8;

/// Depth steps of every test texture: texel (x, y) holds the centre of step
/// `step(x, y)` of sixteen, so a reference `k / 16` lies exactly between two
/// steps and every comparison has one exact answer at any depth precision.
fn step(x: u32, y: u32) -> u32 {
    (7 * x + 5 * y) % 16
}
fn depth_of(step: u32) -> f32 {
    (step as f32 + 0.5) / 16.
}
fn floats(values: impl IntoIterator<Item = f32>) -> Vec<u8> {
    values.into_iter().flat_map(f32::to_le_bytes).collect()
}
fn depth_pixels(w: u32, h: u32) -> Vec<u8> {
    floats((0..h).flat_map(|y| (0..w).map(move |x| depth_of(step(x, y)))))
}
fn f(v: f32) -> u32 {
    v.to_bits()
}
fn double(v: f64) -> [u32; 2] {
    let bits = v.to_bits();
    [bits as u32, (bits >> 32) as u32]
}

/// The fixed-function result of a comparison under `mode` with a white
/// MODULATE environment: LUMINANCE (c,c,c,1), INTENSITY (c,c,c,c), ALPHA
/// (1,1,1,c).
fn shade(mode: u32, value: u8) -> [u8; 4] {
    match mode {
        LUMINANCE => [value, value, value, 255],
        INTENSITY => [value; 4],
        ALPHA => [255, 255, 255, value],
        _ => unreachable!(),
    }
}
fn passes(function: u32, r: f32, depth: f32) -> bool {
    match function {
        LEQUAL => r <= depth,
        GEQUAL => r >= depth,
        _ => unreachable!(),
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
    fn expect_error(&mut self, error: u32, label: &str) {
        assert_eq!(self.error(), error, "{label}");
        assert_eq!(self.error(), 0, "{label}: one error");
    }
    fn parameter(&mut self, target: u32, pname: u32) -> u32 {
        self.word(GET_PARAMETER, &[target, pname])
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
    /// The read framebuffer's depth as float32, bottom row first.
    fn depth(&mut self) -> Vec<u32> {
        let (kind, bytes) = query_capacity(
            &mut self.qemu,
            &mut self.sequence,
            READ_PIXELS,
            &[READ_DEPTH, 0, W | (H << 16)],
            W * H * 4,
        );
        assert_eq!(kind, RESULT_FLOAT);
        words(&bytes)
    }
    /// `count` texels of a depth level: float32 depth, or with `packed` the
    /// UNSIGNED_INT_24_8 words of a DEPTH_STENCIL level.
    fn depth_texels(&mut self, target: u32, level: u32, count: u32, packed: bool) -> Vec<u32> {
        let flags = TEXTURE_READ_DEPTH | if packed { TEXTURE_READ_FLOAT } else { 0 };
        let (kind, bytes) = query_capacity(
            &mut self.qemu,
            &mut self.sequence,
            GET_TEX_IMAGE,
            &[target, level | flags | (count << 16), 0],
            count * 4,
        );
        assert_eq!(kind, if packed { RESULT_INT } else { RESULT_FLOAT });
        words(&bytes)
    }
}
/// Every pixel of the drawable against `expected`, naming the first miss.
fn compare(actual: &[[u8; 4]], expected: impl Fn(u32, u32) -> [u8; 4], label: &str) {
    assert_eq!(actual.len(), (W * H) as usize, "{label}");
    for y in 0..H {
        for x in 0..W {
            assert_eq!(
                actual[(y * W + x) as usize],
                expected(x, y),
                "{label}: pixel {x},{y}"
            );
        }
    }
}
/// A white quad over the whole drawable at window depth 0.5 whose corners
/// carry `s0..s1`, `t0..t1` and the reference `r`, all multiplied by `q`.
fn quad(s0: f32, t0: f32, s1: f32, t1: f32, r: f32, q: f32) -> Vec<(u32, Vec<u32>)> {
    let mut out = vec![call(COLOR4F, &[f(1.); 4]), call(BEGIN, &[7])];
    for (x, y, s, t) in [
        (-1f32, -1f32, s0, t0),
        (1., -1., s1, t0),
        (1., 1., s1, t1),
        (-1., 1., s0, t1),
    ] {
        out.push(call(TEXCOORD4F, &[f(s * q), f(t * q), f(r * q), f(q)]));
        out.push(call(VERTEX3F, &[f(x), f(y), f(0.)]));
    }
    out.push(call(END, &[]));
    out
}
/// Texel for pixel: normalized coordinates of a 16x8 texture.
fn one_to_one(r: f32) -> Vec<(u32, Vec<u32>)> {
    quad(0., 0., 1., 1., r, 1.)
}
/// An untextured quad over pixels `x0..x1` x `y0..y1` at window depth `z`.
fn block(x0: u32, y0: u32, x1: u32, y1: u32, z: f32) -> Vec<(u32, Vec<u32>)> {
    let nx = |x: u32| x as f32 * 2. / W as f32 - 1.;
    let ny = |y: u32| y as f32 * 2. / H as f32 - 1.;
    let mut out = vec![
        call(COLOR4F, &[f(0.), f(0.), f(1.), f(1.)]),
        call(BEGIN, &[7]),
    ];
    for (x, y) in [(x0, y0), (x1, y0), (x1, y1), (x0, y1)] {
        out.push(call(VERTEX3F, &[f(nx(x)), f(ny(y)), f(2. * z - 1.)]));
    }
    out.push(call(END, &[]));
    out
}
fn sampling(target: u32, name: u32, filter: u32) -> Vec<(u32, Vec<u32>)> {
    vec![
        call(BIND, &[target, name]),
        call(PARAMETER, &[target, MIN_FILTER, filter]),
        call(PARAMETER, &[target, MAG_FILTER, filter]),
        call(PARAMETER, &[target, WRAP_S, CLAMP_TO_EDGE]),
        call(PARAMETER, &[target, WRAP_T, CLAMP_TO_EDGE]),
    ]
}
fn compare_state(target: u32, function: u32, mode: u32) -> Vec<(u32, Vec<u32>)> {
    vec![
        call(PARAMETER, &[target, COMPARE_MODE, COMPARE_R_TO_TEXTURE]),
        call(PARAMETER, &[target, COMPARE_FUNC, function]),
        call(PARAMETER, &[target, DEPTH_TEXTURE_MODE, mode]),
    ]
}

#[test]
#[ignore = "requires native host GPU and depth texture QEMU producer; diskless acceptance"]
fn qemu_depth_textures_compare_copy_attach_program_and_read_exactly() {
    let (_device, _queue, host) = native_gpu();
    let directory =
        std::env::temp_dir().join(format!("dg-depth-{}", uuid::Uuid::new_v4().simple()));
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
    // CAPS2 holds DG_CAP2_GL_DEPTH_TEXTURE and DG_CAP2_GL_RGB565 (rgb565.rs);
    // the next register is unused.
    assert_eq!(gl.qemu.read(CAPS2), 1 | 2);
    assert_eq!(gl.qemu.read(CAPS2 + 4), 0);
    gl.submit(&[
        (1, vec![0]),
        (3, vec![W, H]),
        (5, vec![]),
        call(TEX_ENV_I, &[TEXTURE_ENV, TEXTURE_ENV_MODE, MODULATE]),
    ]);

    // A float depth image of the 2D target; defaults; readback as depth.
    let mut records = sampling(TEXTURE_2D, 10, NEAREST);
    records.push(data_call(
        IMAGE_2D,
        &[
            TEXTURE_2D,
            0,
            DEPTH_COMPONENT24,
            W,
            H,
            0,
            DEPTH_COMPONENT,
            FLOAT,
        ],
        &depth_pixels(W, H),
    ));
    gl.submit(&records);
    assert_eq!(gl.error(), 0);
    assert_eq!(gl.parameter(TEXTURE_2D, COMPARE_MODE), 0);
    assert_eq!(gl.parameter(TEXTURE_2D, COMPARE_FUNC), LEQUAL);
    assert_eq!(gl.parameter(TEXTURE_2D, DEPTH_TEXTURE_MODE), LUMINANCE);
    assert_eq!(gl.level(TEXTURE_2D, 0, TEXTURE_DEPTH_SIZE), 24);
    assert_eq!(gl.level(TEXTURE_2D, 0, TEXTURE_STENCIL_SIZE), 0);
    let stored = gl.depth_texels(TEXTURE_2D, 0, W * H, false);
    for (i, &bits) in stored.iter().enumerate() {
        let (x, y) = (i as u32 % W, i as u32 / W);
        let expected = depth_of(step(x, y));
        assert!(
            (f32::from_bits(bits) - expected).abs() < 1e-6,
            "texel {x},{y}: {}",
            f32::from_bits(bits)
        );
    }

    // Without comparison the depth is a LUMINANCE texel. Converting 24-bit
    // depth to an 8-bit colour may round or truncate (GL fixes only the
    // precision), so this one image allows a step; comparisons are exact.
    let mut records = vec![call(ENABLE, &[TEXTURE_2D])];
    records.extend(one_to_one(0.));
    gl.submit(&records);
    for (i, pixel) in gl.pixels().into_iter().enumerate() {
        let (x, y) = (i as u32 % W, i as u32 / W);
        let v = depth_of(step(x, y)) * 255.;
        assert!(
            pixel[3] == 255
                && pixel[..3].iter().all(|&c| c == pixel[0])
                && (f32::from(pixel[0]) - v).abs() < 1.,
            "depth as luminance {x},{y}: {pixel:?} for {v}"
        );
    }

    // Every function, mode and a range of references, compared exactly.
    for function in [LEQUAL, GEQUAL] {
        for mode in [LUMINANCE, INTENSITY, ALPHA] {
            for k in [0u32, 5, 8, 13, 16] {
                let r = k as f32 / 16.;
                let mut records = compare_state(TEXTURE_2D, function, mode);
                records.extend(one_to_one(r));
                gl.submit(&records);
                assert_eq!(gl.error(), 0);
                compare(
                    &gl.pixels(),
                    |x, y| {
                        shade(
                            mode,
                            if passes(function, r, depth_of(step(x, y))) {
                                255
                            } else {
                                0
                            },
                        )
                    },
                    &format!("2D {function:#x} {mode:#x} r={r}"),
                );
            }
        }
    }
    assert_eq!(gl.parameter(TEXTURE_2D, COMPARE_MODE), COMPARE_R_TO_TEXTURE);
    assert_eq!(gl.parameter(TEXTURE_2D, COMPARE_FUNC), GEQUAL);
    assert_eq!(gl.parameter(TEXTURE_2D, DEPTH_TEXTURE_MODE), ALPHA);

    // A projective lookup compares r/q; float parameters name the same state.
    let mut records = vec![
        call(PARAMETER_F, &[TEXTURE_2D, COMPARE_FUNC, f(LEQUAL as f32)]),
        call(
            PARAMETER_F,
            &[TEXTURE_2D, DEPTH_TEXTURE_MODE, f(INTENSITY as f32)],
        ),
    ];
    records.extend(quad(0., 0., 1., 1., 0.5, 4.));
    gl.submit(&records);
    compare(
        &gl.pixels(),
        |x, y| shade(INTENSITY, if step(x, y) >= 8 { 255 } else { 0 }),
        "projective r/q",
    );

    // Texture environments combine the comparison like any texel: REPLACE of
    // ONE_MINUS_SRC_COLOR inverts it in colour, SRC_ALPHA keeps it in alpha.
    let mut records = vec![
        call(TEX_ENV_I, &[TEXTURE_ENV, TEXTURE_ENV_MODE, COMBINE]),
        call(TEX_ENV_I, &[TEXTURE_ENV, COMBINE_RGB, REPLACE]),
        call(TEX_ENV_I, &[TEXTURE_ENV, COMBINE_ALPHA, REPLACE]),
        call(TEX_ENV_I, &[TEXTURE_ENV, SOURCE0_RGB, TEXTURE]),
        call(TEX_ENV_I, &[TEXTURE_ENV, SOURCE0_ALPHA, TEXTURE]),
        call(TEX_ENV_I, &[TEXTURE_ENV, OPERAND0_RGB, ONE_MINUS_SRC_COLOR]),
        call(TEX_ENV_I, &[TEXTURE_ENV, OPERAND0_ALPHA, SRC_ALPHA]),
    ];
    records.extend(one_to_one(0.5));
    records.push(call(TEX_ENV_I, &[TEXTURE_ENV, TEXTURE_ENV_MODE, MODULATE]));
    gl.submit(&records);
    assert_eq!(gl.error(), 0);
    compare(
        &gl.pixels(),
        |x, y| {
            let v = if step(x, y) >= 8 { 255 } else { 0 };
            [255 - v, 255 - v, 255 - v, v]
        },
        "combine",
    );

    // LINEAR averages the comparisons of its texels: a quarter texel to the
    // right weighs a texel 3/4 and its right neighbour 1/4 (the last column
    // clamps to itself).
    let mut records = vec![
        call(PARAMETER, &[TEXTURE_2D, MIN_FILTER, LINEAR]),
        call(PARAMETER, &[TEXTURE_2D, MAG_FILTER, LINEAR]),
    ];
    records.extend(quad(0.25 / 16., 0., 1. + 0.25 / 16., 1., 0.5, 1.));
    gl.submit(&records);
    compare(
        &gl.pixels(),
        |x, y| {
            let pass = |x: u32| f32::from(u8::from(step(x.min(W - 1), y) >= 8));
            let v = (255. * (0.75 * pass(x) + 0.25 * pass(x + 1))).round() as u8;
            shade(INTENSITY, v)
        },
        "linear comparison",
    );
    gl.submit(&[
        call(PARAMETER, &[TEXTURE_2D, MIN_FILTER, NEAREST]),
        call(PARAMETER, &[TEXTURE_2D, MAG_FILTER, NEAREST]),
    ]);

    // Zero-defined storage and sub-images of the other depth types.
    let ushort: Vec<u8> = (0..6u16)
        .flat_map(|i| (i * 13107).to_le_bytes()) // 0, 0.2, .., 1.0
        .collect();
    gl.submit(&[
        call(BIND, &[TEXTURE_2D, 11]),
        data_call(
            IMAGE_2D,
            &[
                TEXTURE_2D,
                0,
                DEPTH_COMPONENT16,
                4,
                4,
                0,
                DEPTH_COMPONENT,
                FLOAT,
            ],
            &[],
        ),
        data_call(
            SUB_IMAGE_2D,
            &[TEXTURE_2D, 0, 1, 2, 3, 2, DEPTH_COMPONENT, UNSIGNED_SHORT],
            &ushort,
        ),
    ]);
    assert_eq!(gl.error(), 0);
    assert!(gl.level(TEXTURE_2D, 0, TEXTURE_DEPTH_SIZE) >= 16);
    let texels = gl.depth_texels(TEXTURE_2D, 0, 16, false);
    for (i, &bits) in texels.iter().enumerate() {
        let (x, y) = (i as u32 % 4, i as u32 / 4);
        let expected = if (1..4).contains(&x) && (2..4).contains(&y) {
            ((y - 2) * 3 + x - 1) as f32 * 0.2
        } else {
            0.
        };
        assert!(
            (f32::from_bits(bits) - expected).abs() < 1e-4,
            "16-bit texel {x},{y}"
        );
    }

    // Packed depth/stencil: words in, words out; depth reads its 24 bits.
    let packed: Vec<u32> = (0..12u32)
        .map(|i| (i * 0x0015_5555) << 8 | (i * 21))
        .collect();
    gl.submit(&[
        call(BIND, &[TEXTURE_2D, 12]),
        data_call(
            IMAGE_2D,
            &[
                TEXTURE_2D,
                0,
                DEPTH24_STENCIL8,
                4,
                3,
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
    assert_eq!(gl.level(TEXTURE_2D, 0, TEXTURE_DEPTH_SIZE), 24);
    assert_eq!(gl.level(TEXTURE_2D, 0, TEXTURE_STENCIL_SIZE), 8);
    assert_eq!(gl.depth_texels(TEXTURE_2D, 0, 12, true), packed);
    let depths = gl.depth_texels(TEXTURE_2D, 0, 12, false);
    for (word, bits) in packed.iter().zip(depths) {
        let expected = (word >> 8) as f64 / 16_777_215.;
        assert!((f64::from(f32::from_bits(bits)) - expected).abs() < 1e-7);
    }

    // CopyTexImage2D/CopyTexSubImage2D define depth images from the window's
    // depth buffer: bit-identical to the window's own depth readback.
    let mut records = vec![
        call(DISABLE, &[TEXTURE_2D]),
        call(CLEAR_DEPTH, &double(1.)),
        call(CLEAR, &[DEPTH_BIT | COLOR_BIT]),
        call(ENABLE, &[DEPTH_TEST]),
        call(DEPTH_FUNC, &[LESS]),
    ];
    records.extend(block(0, 0, 8, 8, 0.25));
    records.extend(block(8, 0, 16, 4, 0.75));
    records.push(call(DISABLE, &[DEPTH_TEST]));
    gl.submit(&records);
    let window = gl.depth();
    let window_z = |x: u32, y: u32| f32::from_bits(window[(y * W + x) as usize]);
    assert!((window_z(0, 0) - 0.25).abs() < 1e-6);
    assert!((window_z(15, 0) - 0.75).abs() < 1e-6);
    assert_eq!(window_z(15, 7), 1.);
    gl.submit(&[
        call(BIND, &[TEXTURE_2D, 13]),
        call(
            COPY_IMAGE_2D,
            &[TEXTURE_2D, 0, DEPTH_COMPONENT24, 0, 0, W, H, 0],
        ),
        call(BIND, &[TEXTURE_2D, 14]),
        data_call(
            IMAGE_2D,
            &[
                TEXTURE_2D,
                0,
                DEPTH_COMPONENT24,
                8,
                4,
                0,
                DEPTH_COMPONENT,
                FLOAT,
            ],
            &[],
        ),
        call(COPY_SUB_IMAGE_2D, &[TEXTURE_2D, 0, 2, 1, 6, 2, 5, 3]),
    ]);
    assert_eq!(gl.error(), 0);
    gl.submit(&[call(BIND, &[TEXTURE_2D, 13])]);
    assert_eq!(gl.depth_texels(TEXTURE_2D, 0, W * H, false), window);
    gl.submit(&[call(BIND, &[TEXTURE_2D, 14])]);
    let sub = gl.depth_texels(TEXTURE_2D, 0, 32, false);
    for (i, &bits) in sub.iter().enumerate() {
        let (x, y) = (i as u32 % 8, i as u32 / 8);
        let expected = if (2..7).contains(&x) && (1..4).contains(&y) {
            window[((y - 1 + 2) * W + x - 2 + 6) as usize]
        } else {
            0
        };
        assert_eq!(bits, expected, "CopyTexSubImage2D texel {x},{y}");
    }
    // Sampling the copy compares against what was drawn.
    let mut records = sampling(TEXTURE_2D, 13, NEAREST);
    records.extend(compare_state(TEXTURE_2D, LEQUAL, LUMINANCE));
    records.push(call(ENABLE, &[TEXTURE_2D]));
    records.extend(one_to_one(0.5));
    gl.submit(&records);
    compare(
        &gl.pixels(),
        |x, _| shade(LUMINANCE, if x < 8 { 0 } else { 255 }),
        "copied window depth",
    );

    // Render into a depth texture attachment of a framebuffer object, read it
    // back exactly, then sample it with comparison in the window.
    let mut records = vec![
        call(DISABLE, &[TEXTURE_2D]),
        call(BIND, &[TEXTURE_2D, 20]),
        data_call(
            IMAGE_2D,
            &[TEXTURE_2D, 0, RGBA8, W, H, 0, RGBA, UNSIGNED_BYTE],
            &[],
        ),
        call(BIND, &[TEXTURE_2D, 21]),
        data_call(
            IMAGE_2D,
            &[
                TEXTURE_2D,
                0,
                DEPTH_COMPONENT24,
                W,
                H,
                0,
                DEPTH_COMPONENT,
                FLOAT,
            ],
            &[],
        ),
        call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 5]),
        call(
            FRAMEBUFFER_TEXTURE_2D,
            &[FRAMEBUFFER, COLOR0, TEXTURE_2D, 20, 0],
        ),
        call(
            FRAMEBUFFER_TEXTURE_2D,
            &[FRAMEBUFFER, DEPTH_ATTACHMENT, TEXTURE_2D, 21, 0],
        ),
        call(CLEAR_DEPTH, &double(1.)),
        call(CLEAR, &[DEPTH_BIT | COLOR_BIT]),
        call(ENABLE, &[DEPTH_TEST]),
    ];
    records.extend(block(0, 0, 12, 6, 0.75));
    records.extend(block(4, 2, 16, 8, 0.25));
    records.push(call(DISABLE, &[DEPTH_TEST]));
    gl.submit(&records);
    assert_eq!(gl.word(CHECK_STATUS, &[FRAMEBUFFER]), COMPLETE);
    assert_eq!(gl.error(), 0);
    let rendered = gl.depth();
    let drawn = |x: u32, y: u32| {
        if (4..16).contains(&x) && (2..8).contains(&y) {
            0.25
        } else if x < 12 && y < 6 {
            0.75
        } else {
            1.
        }
    };
    for y in 0..H {
        for x in 0..W {
            let z = f32::from_bits(rendered[(y * W + x) as usize]);
            assert!(
                (z - drawn(x, y)).abs() < 1e-6,
                "rendered depth {x},{y}: {z}"
            );
        }
    }
    gl.submit(&[call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 0])]);
    assert_eq!(gl.depth_texels(TEXTURE_2D, 0, W * H, false), rendered);
    let mut records = sampling(TEXTURE_2D, 21, NEAREST);
    records.extend(compare_state(TEXTURE_2D, GEQUAL, INTENSITY));
    records.push(call(ENABLE, &[TEXTURE_2D]));
    records.extend(one_to_one(0.5));
    gl.submit(&records);
    compare(
        &gl.pixels(),
        |x, y| shade(INTENSITY, if drawn(x, y) < 0.5 { 255 } else { 0 }),
        "rendered depth attachment sampled",
    );
    // A packed DEPTH_STENCIL texture attaches as depth and stencil.
    gl.submit(&[
        call(DISABLE, &[TEXTURE_2D]),
        call(BIND, &[TEXTURE_2D, 22]),
        data_call(
            IMAGE_2D,
            &[
                TEXTURE_2D,
                0,
                DEPTH24_STENCIL8,
                W,
                H,
                0,
                DEPTH_STENCIL,
                UNSIGNED_INT_24_8,
            ],
            &[],
        ),
        call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 5]),
        call(
            FRAMEBUFFER_TEXTURE_2D,
            &[FRAMEBUFFER, DEPTH_ATTACHMENT, TEXTURE_2D, 22, 0],
        ),
        call(
            FRAMEBUFFER_TEXTURE_2D,
            &[FRAMEBUFFER, STENCIL_ATTACHMENT, TEXTURE_2D, 22, 0],
        ),
        call(CLEAR_DEPTH, &double(0.5)),
        call(CLEAR_STENCIL, &[0x5a]),
        call(CLEAR, &[DEPTH_BIT | STENCIL_BIT]),
        call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 0]),
    ]);
    assert_eq!(gl.error(), 0);
    let cleared = gl.depth_texels(TEXTURE_2D, 0, W * H, true);
    assert!(
        cleared
            .iter()
            .all(|w| w & 0xff == 0x5a && (w >> 8).abs_diff(0x80_0000) <= 1)
    );

    // A rectangle depth texture: unnormalized coordinates, exact comparisons.
    let mut records = sampling(RECT, 30, NEAREST);
    records.push(data_call(
        IMAGE_2D,
        &[RECT, 0, DEPTH_COMPONENT24, W, H, 0, DEPTH_COMPONENT, FLOAT],
        &depth_pixels(W, H),
    ));
    records.extend(compare_state(RECT, LEQUAL, LUMINANCE));
    records.push(call(ENABLE, &[RECT]));
    records.extend(quad(0., 0., W as f32, H as f32, 0.375, 1.));
    gl.submit(&records);
    assert_eq!(gl.error(), 0);
    compare(
        &gl.pixels(),
        |x, y| shade(LUMINANCE, if step(x, y) >= 6 { 255 } else { 0 }),
        "rectangle comparison",
    );
    assert_eq!(gl.level(RECT, 0, TEXTURE_DEPTH_SIZE), 24);
    // A rectangle copy from the window depth (still 0.25 / 0.75 / 1.0).
    gl.submit(&[
        call(DISABLE, &[RECT]),
        call(BIND, &[RECT, 31]),
        call(COPY_IMAGE_2D, &[RECT, 0, DEPTH_COMPONENT24, 0, 0, W, H, 0]),
    ]);
    assert_eq!(gl.error(), 0);
    assert_eq!(gl.depth_texels(RECT, 0, W * H, false), window);
    // A zero-defined packed rectangle (WineD3D's NPOT depth-stencil surface)
    // attaches beside a colour rectangle of its size; depth and stencil clear
    // and read back.
    gl.submit(&[
        call(BIND, &[RECT, 32]),
        data_call(
            IMAGE_2D,
            &[
                RECT,
                0,
                DEPTH24_STENCIL8,
                W,
                H,
                0,
                DEPTH_STENCIL,
                UNSIGNED_INT_24_8,
            ],
            &[],
        ),
    ]);
    assert_eq!(
        gl.depth_texels(RECT, 0, W * H, true),
        vec![0; (W * H) as usize]
    );
    gl.submit(&[
        call(BIND, &[RECT, 33]),
        data_call(
            IMAGE_2D,
            &[RECT, 0, RGBA8, W, H, 0, RGBA, UNSIGNED_BYTE],
            &[],
        ),
        call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 8]),
        call(FRAMEBUFFER_TEXTURE_2D, &[FRAMEBUFFER, COLOR0, RECT, 33, 0]),
        call(
            FRAMEBUFFER_TEXTURE_2D,
            &[FRAMEBUFFER, DEPTH_ATTACHMENT, RECT, 32, 0],
        ),
        call(
            FRAMEBUFFER_TEXTURE_2D,
            &[FRAMEBUFFER, STENCIL_ATTACHMENT, RECT, 32, 0],
        ),
    ]);
    assert_eq!(gl.word(CHECK_STATUS, &[FRAMEBUFFER]), COMPLETE);
    let mut records = vec![
        call(CLEAR_DEPTH, &double(1.)),
        call(CLEAR_STENCIL, &[0x33]),
        call(CLEAR, &[DEPTH_BIT | STENCIL_BIT]),
        call(DISABLE, &[RECT]),
        call(ENABLE, &[DEPTH_TEST]),
    ];
    records.extend(block(2, 1, 9, 7, 0.75));
    records.extend([
        call(DISABLE, &[DEPTH_TEST]),
        call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 0]),
        call(BIND, &[RECT, 32]),
    ]);
    gl.submit(&records);
    assert_eq!(gl.error(), 0);
    let packed_rect = gl.depth_texels(RECT, 0, W * H, true);
    for (i, word) in packed_rect.iter().enumerate() {
        let (x, y) = (i as u32 % W, i as u32 / W);
        let depth = if (2..9).contains(&x) && (1..7).contains(&y) {
            0.75
        } else {
            1.
        };
        assert_eq!(word & 0xff, 0x33, "rectangle stencil {x},{y}");
        let z = (word >> 8) as f64 / 16_777_215.;
        assert!((z - depth).abs() < 1e-6, "rectangle depth {x},{y}: {z}");
    }

    // A 1D depth texture: zero-defined, a float sub-image, a copy, sampling.
    let row = floats((0..W).map(|x| depth_of(step(x, 0))));
    let mut records = vec![call(DISABLE, &[RECT])];
    records.extend(sampling(TEXTURE_1D, 40, NEAREST));
    records.push(data_call(
        IMAGE_1D,
        &[
            TEXTURE_1D,
            0,
            DEPTH_COMPONENT24,
            W,
            1,
            0,
            DEPTH_COMPONENT,
            FLOAT,
        ],
        &[],
    ));
    records.push(data_call(
        SUB_IMAGE_1D,
        &[TEXTURE_1D, 0, 0, 0, W, 1, DEPTH_COMPONENT, FLOAT],
        &row,
    ));
    records.extend(compare_state(TEXTURE_1D, LEQUAL, LUMINANCE));
    records.push(call(ENABLE, &[TEXTURE_1D]));
    // The reference of a 1D lookup is r. Native fixed function may take t
    // instead (Mesa's does), so this lookup gives t the value of r; the
    // SHADOW1D program below separates them.
    records.extend(quad(0., 0.5, 1., 0.5, 0.5, 1.));
    gl.submit(&records);
    assert_eq!(gl.error(), 0);
    compare(
        &gl.pixels(),
        |x, _| shade(LUMINANCE, if step(x, 0) >= 8 { 255 } else { 0 }),
        "1D comparison",
    );
    let one = gl.depth_texels(TEXTURE_1D, 0, W, false);
    assert!(
        one.iter()
            .enumerate()
            .all(|(x, &b)| (f32::from_bits(b) - depth_of(step(x as u32, 0))).abs() < 1e-6)
    );
    gl.submit(&[
        call(DISABLE, &[TEXTURE_1D]),
        call(BIND, &[TEXTURE_1D, 41]),
        call(COPY_IMAGE_1D, &[TEXTURE_1D, 0, DEPTH_COMPONENT, 0, 0, W, 0]),
    ]);
    assert_eq!(gl.error(), 0);
    let copied = gl.depth_texels(TEXTURE_1D, 0, W, false);
    assert!(
        copied
            .iter()
            .enumerate()
            .all(|(x, &b)| (f32::from_bits(b) - window_z(x as u32, 0)).abs() < 1e-4)
    );
    // A 1D depth level attaches to a framebuffer object.
    gl.submit(&[
        call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 6]),
        call(
            FRAMEBUFFER_TEXTURE_1D,
            &[FRAMEBUFFER, DEPTH_ATTACHMENT, TEXTURE_1D, 41, 0],
        ),
        call(BIND, &[TEXTURE_1D, 42]),
        data_call(
            IMAGE_1D,
            &[TEXTURE_1D, 0, RGBA8, W, 1, 0, RGBA, UNSIGNED_BYTE],
            &[],
        ),
        call(
            FRAMEBUFFER_TEXTURE_1D,
            &[FRAMEBUFFER, COLOR0, TEXTURE_1D, 42, 0],
        ),
    ]);
    assert_eq!(gl.word(CHECK_STATUS, &[FRAMEBUFFER]), COMPLETE);
    gl.submit(&[call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 0])]);
    assert_eq!(gl.error(), 0);

    // ARB_fragment_program_shadow: SHADOW2D, SHADOWRECT and SHADOW1D lookups
    // compare like fixed function, TXP after dividing by q.
    let program = words(&query(&mut gl.qemu, &mut gl.sequence, GEN_PROGRAMS, &[1]).1)[0];
    for (target, name, instruction, s1, t1, q, r, expected) in [
        (
            TEXTURE_2D,
            10,
            "TEX result.color, fragment.texcoord[0], texture[0], SHADOW2D;",
            1.,
            1.,
            1.,
            0.5,
            (|x, y| step(x, y) >= 8) as fn(u32, u32) -> bool,
        ),
        (
            TEXTURE_2D,
            10,
            "TXP result.color, fragment.texcoord[0], texture[0], SHADOW2D;",
            1.,
            1.,
            2.,
            0.75,
            |x, y| step(x, y) >= 12,
        ),
        (
            RECT,
            30,
            "TEX result.color, fragment.texcoord[0], texture[0], SHADOWRECT;",
            W as f32,
            H as f32,
            1.,
            0.25,
            |x, y| step(x, y) >= 4,
        ),
        (
            TEXTURE_1D,
            40,
            "TXP result.color, fragment.texcoord[0], texture[0], SHADOW1D;",
            1.,
            1.,
            3.,
            0.5,
            |x, _| step(x, 0) >= 8,
        ),
    ] {
        let source =
            format!("!!ARBfp1.0\nOPTION ARB_fragment_program_shadow;\n{instruction}\nEND\n");
        let mut records = sampling(target, name, NEAREST);
        records.extend(compare_state(target, LEQUAL, INTENSITY));
        records.extend([
            call(BIND_PROGRAM, &[FP, program]),
            data_call(
                PROGRAM_STRING,
                &[FP, ASCII, source.len() as u32],
                source.as_bytes(),
            ),
            call(ENABLE, &[FP]),
        ]);
        records.extend(quad(0., 0., s1, t1, r, q));
        records.push(call(DISABLE, &[FP]));
        gl.submit(&records);
        assert_eq!(gl.error(), 0, "{instruction}");
        compare(
            &gl.pixels(),
            |x, y| shade(INTENSITY, if expected(x, y) { 255 } else { 0 }),
            instruction,
        );
    }

    // Display lists capture the parameters; GetTexParameter answers them.
    let list = words(&query(&mut gl.qemu, &mut gl.sequence, GEN_LISTS, &[1, 0, 0]).1)[1];
    gl.submit(&[call(BIND, &[TEXTURE_2D, 10])]);
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
    gl.submit(&[
        call(PARAMETER, &[TEXTURE_2D, COMPARE_FUNC, GEQUAL]),
        call(PARAMETER, &[TEXTURE_2D, DEPTH_TEXTURE_MODE, ALPHA]),
        call(PARAMETER, &[TEXTURE_2D, COMPARE_MODE, 0]),
    ]);
    assert_eq!(
        words(&query(&mut gl.qemu, &mut gl.sequence, END_LIST, &[0; 3]).1),
        [0]
    );
    assert_eq!(gl.parameter(TEXTURE_2D, COMPARE_FUNC), LEQUAL);
    gl.submit(&[call(CALL_LIST, &[list])]);
    assert_eq!(gl.parameter(TEXTURE_2D, COMPARE_FUNC), GEQUAL);
    assert_eq!(gl.parameter(TEXTURE_2D, DEPTH_TEXTURE_MODE), ALPHA);
    assert_eq!(gl.parameter(TEXTURE_2D, COMPARE_MODE), 0);

    // The spec's errors are GL errors; the rest of each batch still runs.
    gl.submit(&[
        call(PARAMETER, &[TEXTURE_2D, DEPTH_TEXTURE_MODE, RGBA]),
        call(PARAMETER, &[TEXTURE_2D, COMPARE_FUNC, LEQUAL]),
    ]);
    gl.expect_error(INVALID_ENUM, "DEPTH_TEXTURE_MODE RGBA");
    assert_eq!(gl.parameter(TEXTURE_2D, COMPARE_FUNC), LEQUAL);
    gl.submit(&[call(PARAMETER, &[TEXTURE_2D, COMPARE_MODE, LEQUAL])]);
    gl.expect_error(INVALID_ENUM, "COMPARE_MODE LEQUAL");
    // Depth pixels into a colour image, colour pixels into a depth image.
    gl.submit(&[
        call(BIND, &[TEXTURE_2D, 20]),
        data_call(
            SUB_IMAGE_2D,
            &[TEXTURE_2D, 0, 0, 0, 1, 1, DEPTH_COMPONENT, FLOAT],
            &floats([0.5]),
        ),
    ]);
    gl.expect_error(INVALID_OPERATION, "depth pixels into colour");
    gl.submit(&[
        call(BIND, &[TEXTURE_2D, 21]),
        data_call(
            SUB_IMAGE_2D,
            &[TEXTURE_2D, 0, 0, 0, 1, 1, RGBA, UNSIGNED_BYTE],
            &[1, 2, 3, 4],
        ),
    ]);
    gl.expect_error(INVALID_OPERATION, "colour pixels into depth");
    gl.submit(&[data_call(
        IMAGE_2D,
        &[
            TEXTURE_2D,
            0,
            DEPTH_COMPONENT24,
            1,
            1,
            0,
            RGBA,
            UNSIGNED_BYTE,
        ],
        &[1, 2, 3, 4],
    )]);
    gl.expect_error(INVALID_OPERATION, "depth internal format, colour pixels");
    // A depth copy from a framebuffer object without depth.
    gl.submit(&[
        call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 7]),
        call(
            FRAMEBUFFER_TEXTURE_2D,
            &[FRAMEBUFFER, COLOR0, TEXTURE_2D, 20, 0],
        ),
        call(BIND, &[TEXTURE_2D, 23]),
        call(
            COPY_IMAGE_2D,
            &[TEXTURE_2D, 0, DEPTH_COMPONENT24, 0, 0, 4, 4, 0],
        ),
        call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 0]),
    ]);
    gl.expect_error(INVALID_OPERATION, "depth copy without a depth buffer");
    // The batch survived every error: the context still draws.
    let mut records = sampling(TEXTURE_2D, 10, NEAREST);
    records.extend(compare_state(TEXTURE_2D, LEQUAL, LUMINANCE));
    records.push(call(ENABLE, &[TEXTURE_2D]));
    records.extend(one_to_one(1.));
    gl.submit(&records);
    compare(&gl.pixels(), |_, _| shade(LUMINANCE, 0), "after errors");
    drop(server);
}
