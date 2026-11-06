//! ARB_texture_rectangle (the GeForce FX's conditional non-power-of-two
//! textures) through the device, with exact pixels: a 37x23 rectangle sampled
//! 1:1 with NEAREST and with LINEAR at texel boundaries, including its last row
//! and column and CLAMP_TO_EDGE beyond them; defaults, limits, the spec's GL
//! errors (which keep the batch alive), subimages, CopyTexImage2D and
//! CopyTexSubImage2D, readback, proxies, rendering into a rectangle through a
//! framebuffer object and sampling it back, ARB_fragment_program RECT sampling
//! (TEX and TXP), enable precedence, per-unit bindings with TEXTURE_BIT,
//! display lists, deletion and the 2D target left power-of-two agnostic.
use super::textures::{call, data_call, query, query_capacity, words};
use super::*;

const RECT: u32 = 0x84f5;
const BINDING_RECT: u32 = 0x84f6;
const PROXY_RECT: u32 = 0x84f7;
const MAX_RECT: u32 = 0x84f8;
const TEXTURE_2D: u32 = 0x0de1;
const TEXTURE_3D: u32 = 0x806f;
const TEXTURE0: u32 = 0x84c0;
const MIN_FILTER: u32 = 0x2801;
const MAG_FILTER: u32 = 0x2800;
const WRAP_S: u32 = 0x2802;
const WRAP_T: u32 = 0x2803;
const BASE_LEVEL: u32 = 0x813c;
const NEAREST: u32 = 0x2600;
const LINEAR: u32 = 0x2601;
const NEAREST_MIPMAP_NEAREST: u32 = 0x2700;
const CLAMP: u32 = 0x2900;
const REPEAT: u32 = 0x2901;
const MIRRORED_REPEAT: u32 = 0x8370;
const MIRROR_CLAMP: u32 = 0x8742;
const CLAMP_TO_EDGE: u32 = 0x812f;
const CLAMP_TO_BORDER: u32 = 0x812d;
const RGBA8: u32 = 0x8058;
const RGBA: u32 = 0x1908;
const UNSIGNED_BYTE: u32 = 0x1401;
const TEXTURE_WIDTH: u32 = 0x1000;
const TEXTURE_HEIGHT: u32 = 0x1001;
const TEXTURE_RESIDENT: u32 = 0x8067;
const TEXTURE_BIT: u32 = 0x40000;
const SCISSOR_TEST: u32 = 0x0c11;
const FRAMEBUFFER: u32 = 0x8d40;
const COLOR0: u32 = 0x8ce0;
const COMPLETE: u32 = 0x8cd5;
const FP: u32 = 0x8804;
const ASCII: u32 = 0x8875;
const INVALID_ENUM: u32 = 0x0500;
const INVALID_VALUE: u32 = 0x0501;
const INVALID_OPERATION: u32 = 0x0502;
const DG_GL_RESULT_BOOL: u32 = 1;

const BIND: u32 = 0x04e;
const PARAMETER: u32 = 0x8df;
const IMAGE_2D: u32 = 0x8d4;
const SUB_IMAGE_2D: u32 = 0x8f5;
const COPY_IMAGE_2D: u32 = 0x156;
const COPY_SUB_IMAGE_2D: u32 = 0x15a;
const IMAGE_3D: u32 = 0x8d7;
const ENABLE: u32 = 0x209;
const DISABLE: u32 = 0x1c5;
const ACTIVE: u32 = 0x006;
const PUSH: u32 = 0x775;
const POP: u32 = 0x6af;
const DELETE: u32 = 0x1b0;
const CLEAR_COLOR: u32 = 0x0a3;
const CLEAR: u32 = 0x09a;
const SCISSOR: u32 = 0x7f0;
const COLOR4F: u32 = 0x0db;
const BEGIN: u32 = 0x01a;
const END: u32 = 0x216;
const TEXCOORD4F: u32 = 0x8a9;
const VERTEX2F: u32 = 0x9dc;
const BIND_FRAMEBUFFER: u32 = 0x03c;
const FRAMEBUFFER_TEXTURE_2D: u32 = 0x27f;
const CHECK_STATUS: u32 = 0x094;
const BIND_PROGRAM: u32 = 69;
const GEN_PROGRAMS: u32 = 677;
const PROGRAM_STRING: u32 = 1766;
const GET_TEX_IMAGE: u32 = 0x414;
const LEVEL_PARAMETER: u32 = 0x416;
const GET_PARAMETER: u32 = 0x41d;
const IS_ENABLED: u32 = 0x4ba;
const GET_INTEGER: u32 = 0x329;
const GET_ERROR: u32 = 0x2fc;
const READ_PIXELS: u32 = 0x7a4;
const GEN_LISTS: u32 = 671;
const NEW_LIST: u32 = 1592;
const END_LIST: u32 = 539;
const CALL_LIST: u32 = 0x092;

/// The drawable and the main rectangle: odd, non-power-of-two sizes.
const W: u32 = 37;
const H: u32 = 23;

/// Every texel distinct; R and G step by two along x and y and B by two along
/// both, so LINEAR filtering exactly between texels has exact integer results.
fn texel(x: u32, y: u32) -> [u8; 4] {
    [(2 * x) as u8, (2 * y) as u8, (100 + 2 * (x + y)) as u8, 255]
}
fn pattern(w: u32, h: u32, texel: impl Fn(u32, u32) -> [u8; 4]) -> Vec<u8> {
    (0..h)
        .flat_map(|y| (0..w).map(move |x| (x, y)))
        .flat_map(|(x, y)| texel(x, y))
        .collect()
}
fn f(v: f32) -> u32 {
    v.to_bits()
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
    fn integer(&mut self, pname: u32) -> u32 {
        self.word(GET_INTEGER, &[pname])
    }
    fn error(&mut self) -> u32 {
        self.word(GET_ERROR, &[])
    }
    fn enabled(&mut self, cap: u32) -> bool {
        query(&mut self.qemu, &mut self.sequence, IS_ENABLED, &[cap])
            == (DG_GL_RESULT_BOOL, vec![1])
    }
    /// The drawable's (or the read framebuffer's) pixels, bottom row first.
    fn pixels(&mut self, w: u32, h: u32) -> Vec<u8> {
        query_capacity(
            &mut self.qemu,
            &mut self.sequence,
            READ_PIXELS,
            &[0, 0, w | (h << 16)],
            w * h * 4,
        )
        .1
    }
    /// A rectangle level's texels, bottom row first.
    fn texels(&mut self, target: u32, w: u32, h: u32) -> Vec<u8> {
        query_capacity(
            &mut self.qemu,
            &mut self.sequence,
            GET_TEX_IMAGE,
            &[target, (w * h) << 16, 0],
            w * h * 4,
        )
        .1
    }
}
/// Every pixel of a `w`x`h` image against `expected`, naming the first miss.
fn compare(actual: &[u8], w: u32, h: u32, expected: impl Fn(u32, u32) -> [u8; 4], label: &str) {
    assert_eq!(actual.len(), (w * h * 4) as usize, "{label}");
    for y in 0..h {
        for x in 0..w {
            let at = ((y * w + x) * 4) as usize;
            assert_eq!(
                actual[at..at + 4],
                expected(x, y),
                "{label}: pixel {x},{y} of {w}x{h}"
            );
        }
    }
}
/// A quad over the whole drawable whose corners carry texture coordinates
/// `s0..s1` and `t0..t1` (divided by `q`) and a white colour.
fn quad(s0: f32, t0: f32, s1: f32, t1: f32, q: f32) -> Vec<(u32, Vec<u32>)> {
    let mut out = vec![call(COLOR4F, &[f(1.); 4]), call(BEGIN, &[7])];
    for (x, y, s, t) in [
        (-1f32, -1f32, s0, t0),
        (1., -1., s1, t0),
        (1., 1., s1, t1),
        (-1., 1., s0, t1),
    ] {
        out.push(call(TEXCOORD4F, &[f(s * q), f(t * q), f(0.), f(q)]));
        out.push(call(VERTEX2F, &[f(x), f(y)]));
    }
    out.push(call(END, &[]));
    out
}
/// The rectangle mapped texel for pixel over a `w`x`h` drawable.
fn one_to_one() -> Vec<(u32, Vec<u32>)> {
    quad(0., 0., W as f32, H as f32, 1.)
}
fn rectangle(name: u32, filter: u32, w: u32, h: u32, pixels: &[u8]) -> Vec<(u32, Vec<u32>)> {
    vec![
        call(BIND, &[RECT, name]),
        call(PARAMETER, &[RECT, MIN_FILTER, filter]),
        call(PARAMETER, &[RECT, MAG_FILTER, filter]),
        data_call(
            IMAGE_2D,
            &[RECT, 0, RGBA8, w, h, 0, RGBA, UNSIGNED_BYTE],
            pixels,
        ),
    ]
}
fn clear(rgba: [f32; 4]) -> Vec<(u32, Vec<u32>)> {
    vec![call(CLEAR_COLOR, &rgba.map(f)), call(CLEAR, &[0x4000])]
}

#[test]
#[ignore = "requires native host GPU and rectangle texture QEMU producer; diskless acceptance"]
fn qemu_texture_rectangle_samples_updates_copies_renders_and_programs_exactly() {
    let (_device, _queue, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!("dg-npot-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("g.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let qemu = Qemu::start(&directory, &socket);
    let mut gl = Device { qemu, sequence: 0 };
    assert_ne!(gl.qemu.read(0x1008) & 0x1000_0000, 0); // DG_CAP_GL_TEXTURE_RECTANGLE
    gl.submit(&[(1, vec![0]), (3, vec![W, H]), (5, vec![])]);
    assert_eq!(gl.integer(MAX_RECT), 2048);
    assert_eq!(gl.integer(BINDING_RECT), 0);
    assert!(!gl.enabled(RECT));

    // NEAREST, sampled 1:1: every pixel is its texel, the last row and column
    // included.
    let image = pattern(W, H, texel);
    let mut records = rectangle(10, NEAREST, W, H, &image);
    records.push(call(ENABLE, &[RECT]));
    records.extend(one_to_one());
    gl.submit(&records);
    assert_eq!(gl.error(), 0);
    assert!(gl.enabled(RECT));
    assert_eq!(gl.integer(BINDING_RECT), 10);
    let drawn = gl.pixels(W, H);
    compare(&drawn, W, H, texel, "nearest 1:1");
    assert_eq!(gl.texels(RECT, W, H), image);
    assert_eq!(gl.word(LEVEL_PARAMETER, &[RECT, 0, TEXTURE_WIDTH]), W);
    assert_eq!(gl.word(LEVEL_PARAMETER, &[RECT, 0, TEXTURE_HEIGHT]), H);
    assert_eq!(gl.word(GET_PARAMETER, &[RECT, TEXTURE_RESIDENT]), 1);

    // A new object starts with CLAMP_TO_EDGE wraps and a LINEAR minification
    // filter. LINEAR exactly between texels averages them; beyond the last
    // row and column CLAMP_TO_EDGE repeats the edge texels.
    gl.submit(&[call(BIND, &[RECT, 11])]);
    assert_eq!(gl.word(GET_PARAMETER, &[RECT, WRAP_S]), CLAMP_TO_EDGE);
    assert_eq!(gl.word(GET_PARAMETER, &[RECT, WRAP_T]), CLAMP_TO_EDGE);
    assert_eq!(gl.word(GET_PARAMETER, &[RECT, MIN_FILTER]), LINEAR);
    let mut records = rectangle(11, LINEAR, W, H, &image);
    records.extend(quad(0.5, 0.5, W as f32 + 0.5, H as f32 + 0.5, 1.));
    gl.submit(&records);
    let blended = gl.pixels(W, H);
    compare(
        &blended,
        W,
        H,
        |x, y| {
            let (x1, y1) = ((x + 1).min(W - 1), (y + 1).min(H - 1));
            let average = |channel: usize| {
                ((u32::from(texel(x, y)[channel])
                    + u32::from(texel(x1, y)[channel])
                    + u32::from(texel(x, y1)[channel])
                    + u32::from(texel(x1, y1)[channel]))
                    / 4) as u8
            };
            [average(0), average(1), average(2), 255]
        },
        "linear between texels, clamped at the edges",
    );
    // Far outside the image every pixel is the clamped corner texel.
    let mut records = vec![call(PARAMETER, &[RECT, MIN_FILTER, NEAREST])];
    records.extend(quad(
        W as f32 + 5.,
        H as f32 + 5.,
        W as f32 + 9.,
        H as f32 + 9.,
        1.,
    ));
    gl.submit(&records);
    let corner = texel(W - 1, H - 1);
    compare(&gl.pixels(W, H), W, H, |_, _| corner, "clamped corner");

    // The spec's errors are GL errors; the rest of each batch still runs.
    for (pname, value, error) in [
        (WRAP_S, REPEAT, INVALID_ENUM),
        (WRAP_T, MIRRORED_REPEAT, INVALID_ENUM),
        (WRAP_S, MIRROR_CLAMP, INVALID_ENUM),
        (MIN_FILTER, NEAREST_MIPMAP_NEAREST, INVALID_ENUM),
        (BASE_LEVEL, 1, INVALID_OPERATION),
        (WRAP_S, CLAMP, 0),
        (WRAP_T, CLAMP_TO_BORDER, 0),
        (WRAP_T, CLAMP_TO_EDGE, 0),
    ] {
        gl.submit(&[call(PARAMETER, &[RECT, pname, value])]);
        assert_eq!(gl.error(), error, "{pname:#x}={value:#x}");
    }
    assert_eq!(gl.word(GET_PARAMETER, &[RECT, WRAP_S]), CLAMP);
    gl.submit(&[call(PARAMETER, &[RECT, WRAP_S, CLAMP_TO_EDGE])]);
    // A rectangle name is no 2D texture, a 2D name no rectangle.
    let mut records = vec![call(BIND, &[TEXTURE_2D, 10])];
    records.extend(one_to_one());
    gl.submit(&records);
    assert_eq!(gl.error(), INVALID_OPERATION);
    assert_eq!(gl.integer(BINDING_RECT), 11);

    // Subimages reach the last row and column; readback and sampling agree.
    let patch = |x: u32, y: u32| [250 - x as u8, 7 * y as u8, 33, 255];
    let in_patch = |x: u32, y: u32| (32..W).contains(&x) && (20..H).contains(&y);
    let after = |x: u32, y: u32| {
        if in_patch(x, y) {
            patch(x - 32, y - 20)
        } else {
            texel(x, y)
        }
    };
    let mut records = vec![
        call(BIND, &[RECT, 10]),
        data_call(
            SUB_IMAGE_2D,
            &[RECT, 0, 32, 20, 5, 3, RGBA, UNSIGNED_BYTE],
            &pattern(5, 3, patch),
        ),
        data_call(
            SUB_IMAGE_2D,
            &[RECT, 0, 0, 0, 1, 1, RGBA, UNSIGNED_BYTE],
            &texel(0, 0),
        ),
    ];
    records.extend(one_to_one());
    gl.submit(&records);
    assert_eq!(gl.error(), 0);
    compare(&gl.texels(RECT, W, H), W, H, after, "subimage readback");
    compare(&gl.pixels(W, H), W, H, after, "subimage sampled");

    // CopyTexSubImage2D into the last rows and columns of rectangle 11, and
    // CopyTexImage2D of an odd-sized region into rectangle 12, from the
    // drawable that now shows `after`.
    gl.submit(&[
        call(BIND, &[RECT, 11]),
        call(COPY_SUB_IMAGE_2D, &[RECT, 0, 30, 18, 0, 0, 7, 5]),
        call(BIND, &[RECT, 12]),
        call(COPY_IMAGE_2D, &[RECT, 0, RGBA8, 3, 2, 31, 19, 0]),
    ]);
    assert_eq!(gl.error(), 0);
    gl.submit(&[call(BIND, &[RECT, 11])]);
    compare(
        &gl.texels(RECT, W, H),
        W,
        H,
        |x, y| {
            if x >= 30 && y >= 18 {
                after(x - 30, y - 18)
            } else {
                texel(x, y)
            }
        },
        "CopyTexSubImage2D",
    );
    gl.submit(&[call(BIND, &[RECT, 12])]);
    assert_eq!(gl.word(LEVEL_PARAMETER, &[RECT, 0, TEXTURE_WIDTH]), 31);
    assert_eq!(gl.word(LEVEL_PARAMETER, &[RECT, 0, TEXTURE_HEIGHT]), 19);
    compare(
        &gl.texels(RECT, 31, 19),
        31,
        19,
        |x, y| after(x + 3, y + 2),
        "CopyTexImage2D",
    );

    // Proxies: any size up to the limit; beyond it the proxy is empty.
    gl.submit(&[data_call(
        IMAGE_2D,
        &[PROXY_RECT, 0, RGBA8, 2048, 3, 0, RGBA, UNSIGNED_BYTE],
        &[],
    )]);
    assert_eq!(
        gl.word(LEVEL_PARAMETER, &[PROXY_RECT, 0, TEXTURE_WIDTH]),
        2048
    );
    assert_eq!(
        gl.word(LEVEL_PARAMETER, &[PROXY_RECT, 0, TEXTURE_HEIGHT]),
        3
    );
    gl.submit(&[data_call(
        IMAGE_2D,
        &[PROXY_RECT, 0, RGBA8, 4096, 3, 0, RGBA, UNSIGNED_BYTE],
        &[],
    )]);
    assert_eq!(gl.word(LEVEL_PARAMETER, &[PROXY_RECT, 0, TEXTURE_WIDTH]), 0);
    assert_eq!(gl.error(), 0);

    // Render into a zero-defined rectangle through a framebuffer object:
    // a clear, scissored clears of the last column and row, and a drawn quad;
    // then sample it 1:1 into the window.
    let base = [40u8, 80, 120, 255];
    let column = [255u8, 0, 0, 255];
    let row = [0u8, 255, 0, 255];
    let middle = [0u8, 0, 255, 255];
    let rendered = |x: u32, y: u32| {
        if y == H - 1 {
            row
        } else if x == W - 1 {
            column
        } else if (10..20).contains(&x) && (6..12).contains(&y) {
            middle
        } else {
            base
        }
    };
    gl.submit(&[
        call(BIND, &[RECT, 13]),
        data_call(
            IMAGE_2D,
            &[RECT, 0, RGBA8, W, H, 0, RGBA, UNSIGNED_BYTE],
            &[],
        ),
    ]);
    compare(&gl.texels(RECT, W, H), W, H, |_, _| [0; 4], "zero-defined");
    let mut records = vec![
        call(DISABLE, &[RECT]),
        call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 5]),
        call(FRAMEBUFFER_TEXTURE_2D, &[FRAMEBUFFER, COLOR0, RECT, 13, 0]),
    ];
    records.extend(clear([40. / 255., 80. / 255., 120. / 255., 1.]));
    records.push(call(ENABLE, &[SCISSOR_TEST]));
    records.push(call(SCISSOR, &[W - 1, 0, 1, H]));
    records.extend(clear([1., 0., 0., 1.]));
    records.push(call(SCISSOR, &[0, H - 1, W, 1]));
    records.extend(clear([0., 1., 0., 1.]));
    records.push(call(DISABLE, &[SCISSOR_TEST]));
    records.push(call(COLOR4F, &[f(0.), f(0.), f(1.), f(1.)]));
    records.push(call(BEGIN, &[7]));
    // Pixels 10..20 x 6..12 of the 37x23 viewport.
    for (x, y) in [(10f32, 6f32), (20., 6.), (20., 12.), (10., 12.)] {
        records.push(call(
            VERTEX2F,
            &[f(x * 2. / W as f32 - 1.), f(y * 2. / H as f32 - 1.)],
        ));
    }
    records.push(call(END, &[]));
    gl.submit(&records);
    assert_eq!(gl.word(CHECK_STATUS, &[FRAMEBUFFER]), COMPLETE);
    assert_eq!(gl.error(), 0);
    compare(
        &gl.pixels(W, H),
        W,
        H,
        rendered,
        "framebuffer object pixels",
    );
    // A rectangle has level zero only.
    gl.submit(&[call(
        FRAMEBUFFER_TEXTURE_2D,
        &[FRAMEBUFFER, COLOR0, RECT, 13, 1],
    )]);
    assert_eq!(gl.error(), INVALID_VALUE);
    let mut records = vec![
        call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 0]),
        call(PARAMETER, &[RECT, MIN_FILTER, NEAREST]),
        call(PARAMETER, &[RECT, MAG_FILTER, NEAREST]),
        call(ENABLE, &[RECT]),
    ];
    records.extend(one_to_one());
    gl.submit(&records);
    assert_eq!(gl.error(), 0);
    compare(
        &gl.pixels(W, H),
        W,
        H,
        rendered,
        "rendered rectangle sampled",
    );
    compare(
        &gl.texels(RECT, W, H),
        W,
        H,
        rendered,
        "rendered rectangle readback",
    );

    // ARB_fragment_program samples the rectangle through RECT with
    // unnormalized coordinates: TEX directly, TXP after dividing by q.
    let program = words(&query(&mut gl.qemu, &mut gl.sequence, GEN_PROGRAMS, &[1]).1)[0];
    for (source, q) in [
        (
            &b"!!ARBfp1.0\nTEX result.color, fragment.texcoord[0], texture[0], RECT;\nEND"[..],
            1.,
        ),
        (
            &b"!!ARBfp1.0\nTXP result.color, fragment.texcoord[0], texture[0], RECT;\nEND"[..],
            2.,
        ),
    ] {
        let mut records = vec![
            call(BIND, &[RECT, 10]),
            call(DISABLE, &[RECT]),
            call(BIND_PROGRAM, &[FP, program]),
            data_call(PROGRAM_STRING, &[FP, ASCII, source.len() as u32], source),
            call(ENABLE, &[FP]),
        ];
        records.extend(clear([0., 0., 0., 0.]));
        records.extend(quad(0., 0., W as f32, H as f32, q));
        records.push(call(DISABLE, &[FP]));
        gl.submit(&records);
        assert_eq!(gl.error(), 0, "q={q}");
        compare(
            &gl.pixels(W, H),
            W,
            H,
            after,
            &format!("fragment program q={q}"),
        );
    }

    // Precedence: an enabled rectangle wins over 2D and loses to 3D.
    let grey = [90u8, 90, 90, 255];
    let violet = [120u8, 0, 200, 255];
    let mut records = vec![
        call(BIND, &[TEXTURE_2D, 20]),
        call(PARAMETER, &[TEXTURE_2D, MIN_FILTER, NEAREST]),
        data_call(
            IMAGE_2D,
            &[TEXTURE_2D, 0, RGBA8, 1, 1, 0, RGBA, UNSIGNED_BYTE],
            &grey,
        ),
        call(ENABLE, &[TEXTURE_2D]),
        call(BIND, &[RECT, 10]),
        call(ENABLE, &[RECT]),
    ];
    records.extend(one_to_one());
    gl.submit(&records);
    compare(&gl.pixels(W, H), W, H, after, "rectangle over 2D");
    let mut records = vec![
        call(BIND, &[TEXTURE_3D, 21]),
        call(PARAMETER, &[TEXTURE_3D, MIN_FILTER, NEAREST]),
        data_call(
            IMAGE_3D,
            &[TEXTURE_3D, 0, RGBA8, 1, 1, 1, 0, RGBA, UNSIGNED_BYTE],
            &violet,
        ),
        call(ENABLE, &[TEXTURE_3D]),
    ];
    records.extend(one_to_one());
    gl.submit(&records);
    compare(&gl.pixels(W, H), W, H, |_, _| violet, "3D over rectangle");
    let mut records = vec![call(DISABLE, &[TEXTURE_3D]), call(DISABLE, &[RECT])];
    records.extend(one_to_one());
    gl.submit(&records);
    compare(&gl.pixels(W, H), W, H, |_, _| grey, "2D without rectangle");
    gl.submit(&[call(DISABLE, &[TEXTURE_2D])]);

    // A display list captures the bind, image and subimage for replay; the
    // proxy inside it executes at compile time.
    let list = words(&query(&mut gl.qemu, &mut gl.sequence, GEN_LISTS, &[1, 0, 0]).1)[1];
    assert_eq!(
        words(&query(&mut gl.qemu, &mut gl.sequence, NEW_LIST, &[list, 0x1300, 0]).1),
        [0]
    );
    let small = |x: u32, y: u32| [x as u8 * 50, y as u8 * 80, 7, 255];
    let mut records = rectangle(14, NEAREST, 3, 2, &pattern(3, 2, small));
    records.extend([
        data_call(
            SUB_IMAGE_2D,
            &[RECT, 0, 2, 1, 1, 1, RGBA, UNSIGNED_BYTE],
            &violet,
        ),
        data_call(
            IMAGE_2D,
            &[PROXY_RECT, 0, RGBA8, 5, 7, 0, RGBA, UNSIGNED_BYTE],
            &[],
        ),
    ]);
    gl.submit(&records);
    assert_eq!(
        words(&query(&mut gl.qemu, &mut gl.sequence, END_LIST, &[0; 3]).1),
        [0]
    );
    assert_eq!(gl.word(LEVEL_PARAMETER, &[PROXY_RECT, 0, TEXTURE_WIDTH]), 5);
    assert_eq!(gl.integer(BINDING_RECT), 10);
    // Texel boundaries stay clear of pixel centres; rows beyond t = 2 clamp.
    let mut records = vec![call(CALL_LIST, &[list]), call(ENABLE, &[RECT])];
    records.extend(quad(0., 0., 3., 2.2, 1.));
    gl.submit(&records);
    assert_eq!(gl.integer(BINDING_RECT), 14);
    let listed = |x: u32, y: u32| {
        let s = ((f64::from(x) + 0.5) * 3. / f64::from(W)) as u32;
        let t = (((f64::from(y) + 0.5) * 2.2 / f64::from(H)) as u32).min(1);
        if (s, t) == (2, 1) {
            violet
        } else {
            small(s, t)
        }
    };
    compare(
        &gl.pixels(W, H),
        W,
        H,
        listed,
        "rectangle from a display list",
    );

    // Per-unit bindings: TEXTURE_BIT saves both units' rectangles.
    gl.submit(&[
        call(BIND, &[RECT, 10]),
        call(ACTIVE, &[TEXTURE0 + 1]),
        call(BIND, &[RECT, 11]),
    ]);
    assert_eq!(gl.integer(BINDING_RECT), 11);
    gl.submit(&[
        call(PUSH, &[TEXTURE_BIT]),
        call(BIND, &[RECT, 0]),
        call(ACTIVE, &[TEXTURE0]),
        call(BIND, &[RECT, 12]),
    ]);
    assert_eq!(gl.integer(BINDING_RECT), 12);
    gl.submit(&[call(POP, &[])]);
    assert_eq!(gl.integer(0x84e0), TEXTURE0 + 1);
    assert_eq!(gl.integer(BINDING_RECT), 11);
    gl.submit(&[call(ACTIVE, &[TEXTURE0])]);
    assert_eq!(gl.integer(BINDING_RECT), 10);
    let mut records = one_to_one();
    records.insert(0, call(ENABLE, &[RECT]));
    gl.submit(&records);
    compare(&gl.pixels(W, H), W, H, after, "restored unit 0 rectangle");

    // Deleting reverts every unit holding the name to the default rectangle.
    let names: Vec<u8> = [10u32, 11].iter().flat_map(|n| n.to_le_bytes()).collect();
    gl.submit(&[data_call(DELETE, &[2], &names)]);
    assert_eq!(gl.integer(BINDING_RECT), 0);
    gl.submit(&[call(ACTIVE, &[TEXTURE0 + 1])]);
    assert_eq!(gl.integer(BINDING_RECT), 0);
    gl.submit(&[call(ACTIVE, &[TEXTURE0]), call(BIND, &[TEXTURE_2D, 10])]);
    assert_eq!(gl.integer(0x8069), 10);
    assert_eq!(gl.error(), 0);
    drop(gl);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}
