//! ARB_fragment_program through the device: TEX/TXP sampling of units whose
//! fixed-function texturing is disabled, env versus per-object local
//! parameters, program matrices, parse and host-detected load failures that
//! keep the bound program, invalid-program draws, enable/disable fallback to
//! fixed function, deletion of the bound program, ENABLE_BIT, display lists,
//! and program sharing/isolation between contexts. Every GL error stays a GL
//! error: no batch in this file may fail.
use super::textures::{call, data_call, words};
use super::*;

const FP: u32 = 0x8804;
const ASCII: u32 = 0x8875;
const TEXTURE_2D: u32 = 0x0de1;
const PROGRAM_LENGTH: u32 = 0x8627;
const PROGRAM_FORMAT: u32 = 0x8876;
const PROGRAM_BINDING: u32 = 0x8677;
const PROGRAM_INSTRUCTIONS: u32 = 0x88a0;
const UNDER_NATIVE_LIMITS: u32 = 0x88b6;
const MAX_ENV: u32 = 0x88b5;
const MAX_LOCAL: u32 = 0x88b4;
const MAX_NATIVE_ALU: u32 = 0x880e;
const ERROR_POSITION: u32 = 0x864b;
const ERROR_STRING: u32 = 0x8874;
const MAX_COORDS: u32 = 0x8871;
const MAX_IMAGE_UNITS: u32 = 0x8872;
const CURRENT_MATRIX: u32 = 0x8641;
const CURRENT_MATRIX_DEPTH: u32 = 0x8640;
const MAX_PROGRAM_MATRICES: u32 = 0x862f;
const MATRIX0: u32 = 0x88c0;
const MODELVIEW: u32 = 0x1700;
const ENABLE_BIT: u32 = 0x2000;
const ALL_ATTRIB_BITS: u32 = 0x000f_ffff;
const INVALID_OPERATION: u32 = 0x0502;
const COMPILE: u32 = 0x1300;
const DG_GL_RESULT_BOOL: u32 = 1;
const DG_GL_RESULT_STRING: u32 = 5;

const BIND_PROGRAM: u32 = 69;
const DELETE_PROGRAMS: u32 = 420;
const GEN_PROGRAMS: u32 = 677;
const GET_ENV: u32 = 968;
const GET_PROGRAM: u32 = 974;
const GET_LOCAL: u32 = 977;
const GET_PROGRAM_STRING: u32 = 993;
const IS_PROGRAM: u32 = 1230;
const ENV: u32 = 1732;
const LOCAL: u32 = 1743;
const PROGRAM_STRING: u32 = 1766;
const ENABLE: u32 = 0x209;
const DISABLE: u32 = 0x1c5;
const IS_ENABLED: u32 = 0x4ba;
const GET_INTEGER: u32 = 0x329;
const GET_FLOAT: u32 = 0x305;
const GET_STRING: u32 = 0x405;
const GET_ERROR: u32 = 0x2fc;
const PUSH: u32 = 0x775;
const POP: u32 = 0x6af;
const MATRIX_MODE: u32 = 0x549;
const LOAD_MATRIX: u32 = 0x503;
const CLEAR_COLOR: u32 = 0xa3;
const CLEAR: u32 = 0x9a;
const GEN_LISTS: u32 = 671;
const NEW_LIST: u32 = 1592;
const END_LIST: u32 = 539;
const CALL_LIST: u32 = 146;

const RED: [u8; 4] = [255, 0, 0, 255];
const GREEN: [u8; 4] = [0, 255, 0, 255];
const BLUE: [u8; 4] = [0, 0, 255, 255];
const WHITE: [u8; 4] = [255, 255, 255, 255];
/// TEX through unit 0 at its interpolated first coordinate set.
const SAMPLE: &[u8] = b"!!ARBfp1.0\nTEX result.color, fragment.texcoord[0], texture[0], 2D;\nEND";
/// TXP divides by q first.
const PROJECTED: &[u8] = b"!!ARBfp1.0\nTXP result.color, fragment.texcoord, texture[0], 2D;\nEND";
/// env[2] + local[5].
const PARAMETERS: &[u8] = b"!!ARBfp1.0\nPARAM e = program.env[2];\nPARAM l = program.local[5];\n\
ADD result.color, e, l;\nEND";
const CONSTANT_BLUE: &[u8] = b"!!ARBfp1.0\nMOV result.color, {0, 0, 1, 1};\nEND";
/// Row 0 of program matrix 0.
const MATRIX: &[u8] = b"!!ARBfp1.0\nPARAM m[4] = { state.matrix.program[0] };\n\
MOV result.color, m[0];\nEND";

fn f(v: f32) -> u32 {
    v.to_bits()
}
fn string(source: &[u8]) -> (u32, Vec<u32>) {
    data_call(PROGRAM_STRING, &[FP, ASCII, source.len() as u32], source)
}
/// A full-drawable quad with one texture coordinate for every vertex.
fn quad(s: f32, t: f32, r: f32, q: f32) -> Vec<(u32, Vec<u32>)> {
    let mut out = vec![call(0x01a, &[7])];
    for (x, y) in [(-1f32, -1f32), (1., -1.), (1., 1.), (-1., 1.)] {
        out.push(call(0x8a9, &[f(s), f(t), f(r), f(q)]));
        out.push(call(0x9dc, &[f(x), f(y)]));
    }
    out.push(call(0x216, &[]));
    out
}
fn clear(rgba: [f32; 4]) -> Vec<(u32, Vec<u32>)> {
    vec![call(CLEAR_COLOR, &rgba.map(f)), call(CLEAR, &[0x4000])]
}
/// A 2x2 NEAREST RGBA texture: red, green / blue, white (bottom row first).
fn texture(name: u32) -> Vec<(u32, Vec<u32>)> {
    vec![
        call(0x04e, &[TEXTURE_2D, name]),
        call(0x8df, &[TEXTURE_2D, 0x2801, 0x2600]),
        call(0x8df, &[TEXTURE_2D, 0x2800, 0x2600]),
        data_call(
            0x8d4,
            &[TEXTURE_2D, 0, 0x1908, 2, 2, 0, 0x1908, 0x1401],
            &[RED, GREEN, BLUE, WHITE].concat(),
        ),
    ]
}

pub(super) struct Device {
    pub(super) qemu: Qemu,
    sequence: u32,
}
impl Device {
    pub(super) fn submit_for(&mut self, context: u32, records: &[(u32, Vec<u32>)]) {
        self.sequence += 1;
        self.qemu
            .batch_for(self.sequence, 1, context, context, 0, records);
        self.qemu.await_completion(self.sequence);
    }
    pub(super) fn submit(&mut self, records: &[(u32, Vec<u32>)]) {
        self.submit_for(1, records);
    }
    /// One QUERY record of `context`, through a fresh guarded result page.
    pub(super) fn query_for(
        &mut self,
        context: u32,
        function: u32,
        args: &[u32],
    ) -> (u32, Vec<u8>) {
        self.sequence += 1;
        let mut payload = vec![function];
        payload.extend_from_slice(args);
        payload.resize(4, 0);
        let capacity = 512;
        self.qemu
            .command(&format!("memset 0x200000 {} 0xcc", capacity + 16));
        self.qemu.write(0x115c, 0x200000);
        self.qemu.write(0x1160, 0);
        self.qemu.write(0x1164, capacity);
        self.qemu
            .batch_for(self.sequence, 1, context, context, 0, &[(11, payload)]);
        self.qemu.await_completion(self.sequence);
        let length = self.qemu.read(0x1168) as usize;
        let kind = self.qemu.read(0x116c);
        let response = self
            .qemu
            .command(&format!("read 0x200000 {}", capacity + 16));
        let hex = response
            .split_whitespace()
            .nth(1)
            .unwrap()
            .trim_start_matches("0x");
        let bytes = hex
            .as_bytes()
            .as_chunks::<2>()
            .0
            .iter()
            .map(|pair| u8::from_str_radix(std::str::from_utf8(pair).unwrap(), 16).unwrap())
            .collect::<Vec<_>>();
        assert!(length > 0 && length <= capacity as usize);
        assert!(bytes[length..].iter().all(|byte| *byte == 0xcc));
        (kind, bytes[..length].to_vec())
    }
    pub(super) fn query(&mut self, function: u32, args: &[u32]) -> (u32, Vec<u8>) {
        self.query_for(1, function, args)
    }
    pub(super) fn word_for(&mut self, context: u32, function: u32, args: &[u32]) -> u32 {
        words(&self.query_for(context, function, args).1)[0]
    }
    pub(super) fn word(&mut self, function: u32, args: &[u32]) -> u32 {
        self.word_for(1, function, args)
    }
    fn program_for(&mut self, context: u32, pname: u32) -> u32 {
        self.word_for(context, GET_PROGRAM, &[FP, pname])
    }
    fn program(&mut self, pname: u32) -> u32 {
        self.program_for(1, pname)
    }
    pub(super) fn error_for(&mut self, context: u32) -> u32 {
        self.word_for(context, GET_ERROR, &[])
    }
    pub(super) fn error(&mut self) -> u32 {
        self.error_for(1)
    }
    fn enabled(&mut self) -> bool {
        self.query(IS_ENABLED, &[FP]) == (DG_GL_RESULT_BOOL, vec![1])
    }
    pub(super) fn is_program_for(&mut self, context: u32, name: u32) -> bool {
        self.query_for(context, IS_PROGRAM, &[name]) == (DG_GL_RESULT_BOOL, vec![1])
    }
    fn parameter(&mut self, function: u32, index: u32) -> [f32; 4] {
        let values = words(&self.query(function, &[FP, index]).1);
        [0, 1, 2, 3].map(|i| f32::from_bits(values[i]))
    }
    /// The bound program's string through 4-word windows.
    fn source(&mut self) -> Vec<u8> {
        let length = self.program(PROGRAM_LENGTH) as usize;
        let mut out = Vec::new();
        let mut first = 0;
        while out.len() < length {
            out.extend(self.query(GET_PROGRAM_STRING, &[FP, 4, first]).1);
            first += 4;
        }
        assert!(out[length..].iter().all(|&b| b == 0));
        out.truncate(length);
        out
    }
    /// Exactly the frontend's record (glGetString: name, 0, 0 into a
    /// DG_GL_MAX_RESULT_BYTES buffer), checked by the guest drivers' result
    /// validation before the frontend ever sees it.
    pub(super) fn error_string(&mut self) -> String {
        let (kind, bytes) = self.query(GET_STRING, &[ERROR_STRING, 0, 0]);
        assert!(
            driver_accepts(kind, &bytes, 512),
            "the guest drivers reject this STRING result: {bytes:?}"
        );
        String::from_utf8(bytes[..bytes.len() - 1].to_vec()).unwrap()
    }
    pub(super) fn pixel_for(&mut self, context: u32, x: u32, y: u32) -> [u8; 4] {
        let (_, result) = self.query_for(context, 0x7a4, &[x, y, 1 | (1 << 16)]);
        result[..4].try_into().unwrap()
    }
    /// Every pixel of the 8x8 drawable.
    pub(super) fn expect_for(&mut self, context: u32, rgba: [u8; 4], label: &str) {
        for (x, y) in [(0, 0), (7, 0), (3, 4), (0, 7), (7, 7)] {
            assert_eq!(self.pixel_for(context, x, y), rgba, "{label} at {x},{y}");
        }
    }
    pub(super) fn expect(&mut self, rgba: [u8; 4], label: &str) {
        self.expect_for(1, rgba, label)
    }
    pub(super) fn generate(&mut self, count: u32) -> Vec<u32> {
        words(&self.query(GEN_PROGRAMS, &[count]).1)
    }
}

/// DgValidateGlResult (guest/nt/include/dg-gl-result.h, shared by the NT
/// miniport and the Win9x packet path): a STRING result is printable ASCII
/// (32..=126) up to one final NUL; anything else fails the guest query.
pub(super) fn driver_accepts(kind: u32, bytes: &[u8], capacity: usize) -> bool {
    kind == DG_GL_RESULT_STRING
        && !bytes.is_empty()
        && bytes.len() <= capacity
        && bytes[bytes.len() - 1] == 0
        && bytes[..bytes.len() - 1]
            .iter()
            .all(|b| (32..=126).contains(b))
}

pub(super) fn start(label: &str) -> (Device, GpuServer, PathBuf) {
    let (_device, _queue, host) = native_gpu();
    let directory =
        std::env::temp_dir().join(format!("dg-fp-{label}-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("g.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let qemu = Qemu::start(&directory, &socket);
    let mut device = Device { qemu, sequence: 0 };
    assert_ne!(device.qemu.read(0x1008) & 0x200000, 0);
    device.submit(&[(1, vec![0]), (3, vec![8, 8]), (5, vec![])]);
    (device, server, directory)
}

#[test]
#[ignore = "requires native host GPU and fragment program QEMU producer; diskless acceptance"]
fn qemu_fragment_programs_sample_parameters_errors_lists_and_fallback() {
    let (mut d, _server, _directory) = start("programs");
    let units = d.word(GET_INTEGER, &[MAX_COORDS]);
    assert!((2..=8).contains(&units), "{units}");
    let images = d.word(GET_INTEGER, &[MAX_IMAGE_UNITS]);
    assert!((2..=8).contains(&images), "{images}");
    assert!(d.program(MAX_ENV) >= 24 && d.program(MAX_LOCAL) >= 24);
    assert!(d.program(MAX_NATIVE_ALU) >= 48);
    assert_eq!(d.word(GET_INTEGER, &[ERROR_POSITION]) as i32, -1);
    assert_eq!(d.error_string(), "");
    assert!(!d.enabled());

    // Generated names are reserved, not objects, until bound.
    let names = d.generate(3);
    assert_eq!(names.len(), 3);
    assert!(names.iter().all(|&n| n != 0 && !d.is_program_for(1, n)));
    let (sample, projected, parameters) = (names[0], names[1], names[2]);

    // TEX samples unit 0 although fixed-function TEXTURE_2D stays disabled.
    let mut records = texture(30);
    records.extend([
        call(DISABLE, &[TEXTURE_2D]),
        call(BIND_PROGRAM, &[FP, sample]),
        string(SAMPLE),
        call(ENABLE, &[FP]),
    ]);
    records.extend(quad(0.25, 0.25, 0., 1.));
    d.submit(&records);
    d.expect(RED, "TEX");
    assert!(d.enabled() && d.is_program_for(1, sample));
    assert_eq!(d.error(), 0);
    assert_eq!(d.program(PROGRAM_BINDING), sample);
    assert_eq!(d.program(PROGRAM_FORMAT), ASCII);
    assert_eq!(d.program(PROGRAM_LENGTH), SAMPLE.len() as u32);
    assert_eq!(d.program(UNDER_NATIVE_LIMITS), 1);
    assert!(d.program(PROGRAM_INSTRUCTIONS) >= 1);
    assert_eq!(d.source(), SAMPLE);
    d.submit(&quad(0.75, 0.75, 0., 1.));
    d.expect(WHITE, "TEX upper right texel");

    // TXP divides by q: (1.5, 0.5) / 2 selects the lower right texel.
    let mut records = vec![call(BIND_PROGRAM, &[FP, projected]), string(PROJECTED)];
    records.extend(quad(1.5, 0.5, 0., 2.));
    d.submit(&records);
    d.expect(GREEN, "TXP");

    // Environment parameters belong to the context; locals to each object.
    let mut records = vec![
        call(BIND_PROGRAM, &[FP, parameters]),
        string(PARAMETERS),
        call(ENV, &[FP, 2, f(0.4), f(0.), f(0.2), f(0.)]),
        call(LOCAL, &[FP, 5, f(0.2), f(0.6), f(0.), f(1.)]),
    ];
    records.extend(quad(0., 0., 0., 1.));
    d.submit(&records);
    d.expect([153, 153, 51, 255], "env + local");
    assert_eq!(d.parameter(GET_ENV, 2), [0.4, 0., 0.2, 0.]);
    assert_eq!(d.parameter(GET_LOCAL, 5), [0.2, 0.6, 0., 1.]);
    d.submit(&[call(BIND_PROGRAM, &[FP, sample])]);
    assert_eq!(d.parameter(GET_LOCAL, 5), [0.; 4]);
    assert_eq!(d.parameter(GET_ENV, 2), [0.4, 0., 0.2, 0.]);
    let mut records = vec![
        call(BIND_PROGRAM, &[FP, parameters]),
        call(ENV, &[FP, 2, f(0.), f(0.), f(0.8), f(0.)]),
    ];
    records.extend(quad(0., 0., 0., 1.));
    d.submit(&records);
    d.expect([51, 153, 204, 255], "new env, preserved local");

    // Program matrices: MATRIX0_ARB is an ordinary matrix mode.
    let matrix = [
        0.2f32, 0., 0., 0., 0.4, 1., 0., 0., 0.6, 0., 1., 0., 1., 0., 0., 1.,
    ];
    let mut records = vec![
        call(BIND_PROGRAM, &[FP, 40]),
        string(MATRIX),
        call(MATRIX_MODE, &[MATRIX0]),
        call(LOAD_MATRIX, &matrix.map(f)),
    ];
    d.submit(&records);
    assert_eq!(d.word(GET_INTEGER, &[0x0ba0]), MATRIX0);
    assert_eq!(d.word(GET_INTEGER, &[CURRENT_MATRIX_DEPTH]), 1);
    assert!(d.word(GET_INTEGER, &[MAX_PROGRAM_MATRICES]) >= 8);
    let current = words(&d.query(GET_FLOAT, &[CURRENT_MATRIX]).1);
    assert_eq!(current, matrix.map(f));
    records = vec![call(MATRIX_MODE, &[MODELVIEW])];
    records.extend(quad(0., 0., 0., 1.));
    d.submit(&records);
    d.expect([51, 102, 153, 255], "program matrix row 0");

    // A string that fails to parse: INVALID_OPERATION, an error position
    // and string, and the bound program renders exactly as before.
    let broken =
        b"!!ARBfp1.0\nTEX result.color, fragment.texcoord[0], texture[0], 2D;\nBOGUS;\nEND";
    let mut records = vec![call(BIND_PROGRAM, &[FP, sample]), string(broken)];
    records.extend(quad(0.25, 0.25, 0., 1.));
    d.submit(&records);
    assert_eq!(d.error(), INVALID_OPERATION);
    // Native GL chooses the exact offset (Mesa reports the token before the
    // bad statement); it lies after the valid header and first instruction.
    let position = d.word(GET_INTEGER, &[ERROR_POSITION]) as i32;
    assert!(
        (SAMPLE.len() as i32 - 20..=broken.len() as i32).contains(&position),
        "{position}"
    );
    assert!(!d.error_string().is_empty());
    d.expect(RED, "after a failed load");
    // A semantic error (an undeclared temporary, as WineD3D once generated):
    // the native message reaches the guest through the drivers' validation.
    let undeclared = b"!!ARBfp1.0\nMOV result.color, TA;\nEND";
    d.submit(&[string(undeclared)]);
    assert_eq!(d.error(), INVALID_OPERATION);
    let position = d.word(GET_INTEGER, &[ERROR_POSITION]) as i32;
    assert!(
        (10..=undeclared.len() as i32).contains(&position),
        "{position}"
    );
    let message = d.error_string();
    assert!(!message.is_empty(), "{message}");
    assert_eq!(d.source(), SAMPLE);
    assert_eq!(d.source(), SAMPLE);
    assert_eq!(d.program(UNDER_NATIVE_LIMITS), 1);

    // Host-detected failures: a non-ASCII byte and a texture image unit beyond
    // the reported limit fail at their offsets before native GL sees them.
    let accented = b"!!ARBfp1.0\n# caf\xc3\xa9\nEND";
    let beyond =
        format!("!!ARBfp1.0\nTEX result.color, fragment.texcoord, texture[{images}], 2D;\nEND");
    for (source, at) in [
        (
            accented.to_vec(),
            accented.iter().position(|&b| b >= 0x80).unwrap() as u32,
        ),
        (
            beyond.clone().into_bytes(),
            beyond.find('[').unwrap() as u32 + 1,
        ),
    ] {
        let mut records = vec![string(&source)];
        records.extend(quad(0.25, 0.25, 0., 1.));
        d.submit(&records);
        assert_eq!(d.error(), INVALID_OPERATION);
        assert_eq!(d.word(GET_INTEGER, &[ERROR_POSITION]), at);
        assert!(d.error_string().starts_with("DreamGPU: "));
        d.expect(RED, "after a host-rejected load");
        assert_eq!(d.source(), SAMPLE);
    }
    // An untransferred (too long) string fails at its length.
    d.submit(&[data_call(PROGRAM_STRING, &[FP, ASCII, 70000], &[])]);
    assert_eq!(d.error(), INVALID_OPERATION);
    assert_eq!(d.word(GET_INTEGER, &[ERROR_POSITION]), 70000);
    // The next native load clears the error state again.
    d.submit(&[string(SAMPLE)]);
    assert_eq!(d.error(), 0);
    assert_eq!(d.word(GET_INTEGER, &[ERROR_POSITION]) as i32, -1);
    assert_eq!(d.error_string(), "");

    // Disabling the program falls back to fixed function exactly.
    let mut records = vec![
        call(DISABLE, &[FP]),
        call(0x0db, &[f(0.), f(0.), f(1.), f(1.)]),
    ];
    records.extend(quad(0.25, 0.25, 0., 1.));
    d.submit(&records);
    d.expect(BLUE, "fixed function, texturing disabled");
    let mut records = vec![call(0x0db, &[f(1.); 4]), call(ENABLE, &[TEXTURE_2D])];
    records.extend(quad(0.75, 0.75, 0., 1.));
    d.submit(&records);
    d.expect(WHITE, "fixed function, texture modulated by white");
    d.submit(&[call(DISABLE, &[TEXTURE_2D])]);

    // ENABLE_BIT carries the program enable; bindings are not attribute state.
    d.submit(&[
        call(ENABLE, &[FP]),
        call(PUSH, &[ENABLE_BIT]),
        call(DISABLE, &[FP]),
    ]);
    assert!(!d.enabled());
    d.submit(&[call(POP, &[])]);
    assert!(d.enabled());
    d.submit(&[
        call(PUSH, &[ALL_ATTRIB_BITS]),
        call(BIND_PROGRAM, &[FP, projected]),
        call(POP, &[]),
    ]);
    assert_eq!(d.program(PROGRAM_BINDING), projected);

    // An enabled program object without a valid program rejects drawing as a
    // GL error; the batch and context survive.
    let mut records = clear([0., 0., 0., 1.]);
    records.push(call(BIND_PROGRAM, &[FP, 77]));
    records.extend(quad(0.25, 0.25, 0., 1.));
    d.submit(&records);
    assert_eq!(d.error(), INVALID_OPERATION);
    d.expect([0, 0, 0, 255], "an invalid program draws nothing");
    assert!(d.is_program_for(1, 77));
    assert_eq!(d.program(PROGRAM_LENGTH), 0);

    // Deleting the bound program reverts to the default object.
    d.submit(&[
        call(BIND_PROGRAM, &[FP, projected]),
        data_call(
            DELETE_PROGRAMS,
            &[3],
            &[[0u8; 4], 77u32.to_le_bytes(), projected.to_le_bytes()].concat(),
        ),
    ]);
    assert_eq!(d.program(PROGRAM_BINDING), 0);
    assert!(!d.is_program_for(1, projected) && !d.is_program_for(1, 77));
    assert_eq!(d.program(PROGRAM_LENGTH), 0);
    assert_eq!(d.error(), 0);
    let mut records = clear([0., 0., 0., 1.]);
    records.extend(quad(0.25, 0.25, 0., 1.));
    d.submit(&records);
    assert_eq!(d.error(), INVALID_OPERATION);
    d.expect([0, 0, 0, 255], "the unloaded default program");
    // The deleted name is free again: rebinding creates a new, empty object.
    d.submit(&[call(BIND_PROGRAM, &[FP, projected])]);
    assert_eq!(
        (d.program(PROGRAM_BINDING), d.program(PROGRAM_LENGTH)),
        (projected, 0)
    );

    // With ARB_vertex_program (vertex_program.rs) the vertex program modes are
    // ordinary native state.
    d.submit(&[
        call(ENABLE, &[0x8620]),
        call(DISABLE, &[0x8620]),
        call(DISABLE, &[0x8642]),
    ]);
    assert!(!d.query(IS_ENABLED, &[0x8620]).1.contains(&1));
    assert_eq!(d.error(), 0);

    // Display lists: binds, parameters and strings compile; deletion executes
    // immediately, even while compiling.
    let list = words(&d.query(GEN_LISTS, &[2]).1)[1];
    assert_ne!(list, 0);
    d.submit(&[
        call(BIND_PROGRAM, &[FP, 6]),
        call(BIND_PROGRAM, &[FP, parameters]),
    ]);
    assert!(d.is_program_for(1, 6));
    assert_eq!(d.word(NEW_LIST, &[list, COMPILE]), 0);
    d.submit(&[
        call(BIND_PROGRAM, &[FP, sample]),
        call(ENV, &[FP, 2, f(0.), f(0.4), f(0.), f(0.)]),
        data_call(DELETE_PROGRAMS, &[1], &6u32.to_le_bytes()),
    ]);
    assert!(!d.is_program_for(1, 6));
    assert_eq!(d.word(END_LIST, &[]), 0);
    assert_eq!(d.program(PROGRAM_BINDING), parameters);
    assert_eq!(d.parameter(GET_ENV, 2), [0., 0., 0.8, 0.]);
    d.submit(&[call(CALL_LIST, &[list])]);
    assert_eq!(d.program(PROGRAM_BINDING), sample);
    assert_eq!(d.parameter(GET_ENV, 2), [0., 0.4, 0., 0.]);
    assert_eq!(d.word(NEW_LIST, &[list + 1, COMPILE]), 0);
    d.submit(&[string(CONSTANT_BLUE)]);
    assert_eq!(d.word(END_LIST, &[]), 0);
    assert_eq!(d.source(), SAMPLE, "compiled, not executed");
    let mut records = vec![call(BIND_PROGRAM, &[FP, 50]), call(CALL_LIST, &[list + 1])];
    records.extend(quad(0.25, 0.25, 0., 1.));
    d.submit(&records);
    d.expect(BLUE, "a program string loaded by a display list");
    assert_eq!(d.source(), CONSTANT_BLUE);
    assert_eq!(d.error(), 0);
}

#[test]
#[ignore = "requires native host GPU and fragment program QEMU producer; diskless acceptance"]
fn qemu_fragment_programs_share_with_lists_and_isolate_other_contexts() {
    let (mut d, _server, _directory) = start("sharing");
    // Context 2 shares context 1's objects; context 3 is independent.
    d.submit_for(2, &[(1, vec![1]), (3, vec![8, 8]), (5, vec![])]);
    d.submit_for(3, &[(1, vec![0]), (3, vec![8, 8]), (5, vec![])]);
    let mut records = texture(30);
    records.extend([
        call(BIND_PROGRAM, &[FP, 10]),
        string(SAMPLE),
        call(ENABLE, &[FP]),
    ]);
    d.submit(&records);
    assert!(d.is_program_for(2, 10));
    assert!(!d.is_program_for(3, 10));
    let mut records = vec![
        call(0x04e, &[TEXTURE_2D, 30]),
        call(BIND_PROGRAM, &[FP, 10]),
        call(ENABLE, &[FP]),
    ];
    records.extend(quad(0.75, 0.25, 0., 1.));
    d.submit_for(2, &records);
    d.expect_for(2, GREEN, "a shared program");
    // The same number in an unshared context is a different, empty object.
    d.submit_for(3, &[call(BIND_PROGRAM, &[FP, 10])]);
    assert_eq!(d.program_for(3, PROGRAM_LENGTH), 0);
    assert_eq!(d.program_for(1, PROGRAM_LENGTH), SAMPLE.len() as u32);
    // Default objects are private to each share group.
    d.submit_for(3, &[call(BIND_PROGRAM, &[FP, 0]), string(CONSTANT_BLUE)]);
    d.submit(&[call(BIND_PROGRAM, &[FP, 0])]);
    assert_eq!(d.program_for(1, PROGRAM_LENGTH), 0);
    assert_eq!(d.program_for(3, PROGRAM_LENGTH), CONSTANT_BLUE.len() as u32);
    let mut records = vec![call(ENABLE, &[FP])];
    records.extend(quad(0., 0., 0., 1.));
    d.submit_for(3, &records);
    d.expect_for(3, BLUE, "the private default object");
    // Deleted in context 1 while bound in context 2: the name disappears for
    // both, but context 2 keeps rendering with the object until it rebinds.
    d.submit(&[data_call(DELETE_PROGRAMS, &[1], &10u32.to_le_bytes())]);
    assert!(!d.is_program_for(1, 10) && !d.is_program_for(2, 10));
    let mut records = clear([0., 0., 0., 1.]);
    records.extend(quad(0.25, 0.25, 0., 1.));
    d.submit_for(2, &records);
    d.expect_for(2, RED, "a deleted but bound program");
    assert_eq!(d.program_for(2, PROGRAM_BINDING), 10);
    assert_eq!(d.error_for(2), 0);
    d.submit_for(2, &[call(BIND_PROGRAM, &[FP, 0])]);
    assert_eq!(d.program_for(2, PROGRAM_BINDING), 0);
    // Destroying the contexts releases every object; the device stays usable.
    d.submit_for(3, &[(2, vec![])]);
    d.submit_for(2, &[(2, vec![])]);
    assert_eq!(d.error(), 0);
}
