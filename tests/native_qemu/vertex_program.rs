//! ARB_vertex_program through the device: positions transformed by env-param
//! matrices, generic attribute arrays of every component type and size 1-4
//! (normalized and not) in the attribute draw, current generic values inside
//! and outside Begin/End, attribute 0 aliasing the vertex position, program
//! matrices and state.matrix.mvp, per-vertex point size, two-sided colours,
//! relative addressing, parse and host-detected failures, invalid-program array
//! draws, display lists, ENABLE_BIT/CURRENT_BIT, combined vertex and fragment
//! programs, a program exactly as WineD3D generates it for a vs_1_1 shader,
//! and sharing/isolation between contexts. Every GL error stays a GL error:
//! no batch in this file may fail.
use super::fragment_program::{Device, start};
use super::textures::{call, data_call, words};

const VP: u32 = 0x8620;
const FP: u32 = 0x8804;
const ASCII: u32 = 0x8875;
const POINT_SIZE: u32 = 0x8642;
const TWO_SIDE: u32 = 0x8643;
const MAX_ATTRIBS: u32 = 0x8869;
const CURRENT_ATTRIB: u32 = 0x8626;
const PROGRAM_LENGTH: u32 = 0x8627;
const PROGRAM_BINDING: u32 = 0x8677;
const MAX_ENV: u32 = 0x88b5;
const MAX_NATIVE_PARAMETERS: u32 = 0x88ab;
const MAX_ADDRESS_REGISTERS: u32 = 0x88b1;
const ERROR_POSITION: u32 = 0x864b;
const MATRIX0: u32 = 0x88c0;
const MODELVIEW: u32 = 0x1700;
const ENABLE_BIT: u32 = 0x2000;
const CURRENT_BIT: u32 = 0x0001;
const INVALID_OPERATION: u32 = 0x0502;
const COMPILE: u32 = 0x1300;
const DG_GL_RESULT_BOOL: u32 = 1;
const DG_GL_RESULT_FLOAT: u32 = 3;

const BIND_PROGRAM: u32 = 69;
const DELETE_PROGRAMS: u32 = 420;
const GET_PROGRAM: u32 = 974;
const GET_PROGRAM_STRING: u32 = 993;
const ENV: u32 = 1732;
const LOCAL: u32 = 1743;
const PROGRAM_STRING: u32 = 1766;
const ATTRIB: u32 = 2661; // VertexAttrib4fARB
const ATTRIB_DRAW: u32 = 2796; // the attribute draw (VertexAttribPointerARB)
const GET_ATTRIB: u32 = 1122; // GetVertexAttribfvARB
const DRAW_ARRAYS: u32 = 469;
const ENABLE: u32 = 0x209;
const DISABLE: u32 = 0x1c5;
const IS_ENABLED: u32 = 0x4ba;
const GET_INTEGER: u32 = 0x329;
const PUSH: u32 = 0x775;
const POP: u32 = 0x6af;
const MATRIX_MODE: u32 = 0x549;
const LOAD_MATRIX: u32 = 0x503;
const LOAD_IDENTITY: u32 = 0x500;
const SCALE: u32 = 0x7ee;
const CLEAR_COLOR: u32 = 0xa3;
const CLEAR: u32 = 0x9a;
const BEGIN: u32 = 0x01a;
const END: u32 = 0x216;
const GEN_LISTS: u32 = 671;
const NEW_LIST: u32 = 1592;
const END_LIST: u32 = 539;
const CALL_LIST: u32 = 146;

const FLOAT: u32 = 0x1406;
const QUADS: u32 = 7;
const POINTS: u32 = 0;
const BLACK: [u8; 4] = [0, 0, 0, 255];
const RED: [u8; 4] = [255, 0, 0, 255];
const GREEN: [u8; 4] = [0, 255, 0, 255];
const BLUE: [u8; 4] = [0, 0, 255, 255];
const SLATE: [u8; 4] = [51, 102, 153, 255];

/// Position from attribute 0 through the env-parameter rows, colour from 3.
const TRANSFORM: &[u8] = b"!!ARBvp1.0\nPARAM m[4] = { program.env[0..3] };\n\
ATTRIB p = vertex.attrib[0];\nDP4 result.position.x, m[0], p;\nDP4 result.position.y, m[1], p;\n\
DP4 result.position.z, m[2], p;\nDP4 result.position.w, m[3], p;\n\
MOV result.color, vertex.attrib[3];\nEND";
/// Clip-space position from attribute 0, colour from attribute 1.
const PASS: &[u8] = b"!!ARBvp1.0\nMOV result.position, vertex.attrib[0];\n\
MOV result.color, vertex.attrib[1];\nEND";
/// The conventional position name: attribute 0 aliases it.
const POSITION: &[u8] = b"!!ARBvp1.0\nMOV result.position, vertex.position;\n\
MOV result.color, {1, 0, 0, 1};\nEND";
/// Through state.matrix.mvp (projection times modelview).
const MVP: &[u8] = b"!!ARBvp1.0\nPARAM mvp[4] = { state.matrix.mvp };\n\
DP4 result.position.x, mvp[0], vertex.attrib[0];\nDP4 result.position.y, mvp[1], vertex.attrib[0];\n\
DP4 result.position.z, mvp[2], vertex.attrib[0];\nDP4 result.position.w, mvp[3], vertex.attrib[0];\n\
MOV result.color, {0, 0, 1, 1};\nEND";
/// Colour from row 1 of program matrix 0.
const PROGRAM_MATRIX: &[u8] = b"!!ARBvp1.0\nPARAM m[4] = { state.matrix.program[0] };\n\
MOV result.position, vertex.attrib[0];\nMOV result.color, m[1];\nEND";
/// A point of size env[0].x.
const SIZED: &[u8] = b"!!ARBvp1.0\nMOV result.position, vertex.attrib[0];\n\
MOV result.pointsize, program.env[0].x;\nMOV result.color, {0, 1, 0, 1};\nEND";
/// Red front, green back colours.
const SIDES: &[u8] = b"!!ARBvp1.0\nMOV result.position, vertex.attrib[0];\n\
MOV result.color.front.primary, {1, 0, 0, 1};\nMOV result.color.back.primary, {0, 1, 0, 1};\nEND";
/// Relative addressing: colour C[A0.x + 1] with A0.x from attribute 1.
const RELATIVE: &[u8] = b"!!ARBvp1.0\nPARAM C[4] = { program.env[0..3] };\nADDRESS A0;\n\
ARL A0.x, vertex.attrib[1].x;\nMOV result.position, vertex.attrib[0];\n\
MOV result.color, C[A0.x + 1];\nEND";
/// Feeds a fragment program through texture coordinate set 0.
const VARYING: &[u8] = b"!!ARBvp1.0\nMOV result.position, vertex.attrib[0];\n\
MOV result.texcoord[0], vertex.attrib[1];\nEND";
const FRAGMENT_VARYING: &[u8] = b"!!ARBfp1.0\nMOV result.color, fragment.texcoord[0];\nEND";
/// WineD3D's ARB backend (arb_program_shader.c, no NV options, clip-plane
/// emulation in texcoord 7) for the installed probe's vs_1_1
/// `dp4 oPos.x..w, v0, c0..c3; mov oD0, v1; mov oT0, v2` with v1 a D3DCOLOR
/// (tools/d3d/vshader-probe.inc): exactly the text the guest sends.
const WINE_VS11: &[u8] = b"!!ARBvp1.0\nTEMP TMP_OUT;\n\
PARAM helper_const = { 0.0, 1.0, 2.0, 9.99999994e-09};\nTEMP TA;\nTEMP TB;\n\
PARAM C0 = program.env[0];\nPARAM C1 = program.env[1];\nPARAM C2 = program.env[2];\n\
PARAM C3 = program.env[3];\nPARAM posFixup = program.local[0];\n\
MOV result.color.secondary, helper_const.xxxy;\nDP4 TMP_OUT.x, vertex.attrib[0], C0;\n\
DP4 TMP_OUT.y, vertex.attrib[0], C1;\nDP4 TMP_OUT.z, vertex.attrib[0], C2;\n\
DP4 TMP_OUT.w, vertex.attrib[0], C3;\nMOV result.color.primary, vertex.attrib[1].zyxw;\n\
MOV result.texcoord[0], vertex.attrib[2];\n\
ADD result.fogcoord, posFixup.x, -posFixup.x;\nMOV TA, helper_const.x;\n\
MOV result.texcoord[7], TA;\nMUL TA, posFixup, TMP_OUT.w;\nADD TMP_OUT.x, TMP_OUT.x, TA.z;\n\
MAD TMP_OUT.y, TMP_OUT.y, posFixup.y, TA.w;\nMAD TMP_OUT.z, TMP_OUT.z, helper_const.z, -TMP_OUT.w;\n\
MOV result.position, TMP_OUT;\nEND\n";
/// The same backend for the probe's vs_2_0 `mov r0, v0; dp4 oPos.x..w, r0,
/// c0..c3; mul oD0, v1, c4`.
const WINE_VS20: &[u8] = b"!!ARBvp1.0\nTEMP TMP_OUT;\n\
PARAM helper_const = { 0.0, 1.0, 2.0, 9.99999994e-09};\nTEMP TA;\nTEMP TB;\nTEMP R0;\n\
PARAM C0 = program.env[0];\nPARAM C1 = program.env[1];\nPARAM C2 = program.env[2];\n\
PARAM C3 = program.env[3];\nPARAM C4 = program.env[4];\nPARAM posFixup = program.local[0];\n\
MOV result.color.secondary, helper_const.xxxy;\nMOV R0, vertex.attrib[0];\n\
DP4 TMP_OUT.x, R0, C0;\nDP4 TMP_OUT.y, R0, C1;\nDP4 TMP_OUT.z, R0, C2;\nDP4 TMP_OUT.w, R0, C3;\n\
MUL result.color.primary, vertex.attrib[1].zyxw, C4;\n\
ADD result.fogcoord, posFixup.x, -posFixup.x;\nMOV TA, helper_const.x;\n\
MOV result.texcoord[7], TA;\nMUL TA, posFixup, TMP_OUT.w;\nADD TMP_OUT.x, TMP_OUT.x, TA.z;\n\
MAD TMP_OUT.y, TMP_OUT.y, posFixup.y, TA.w;\nMAD TMP_OUT.z, TMP_OUT.z, helper_const.z, -TMP_OUT.w;\n\
MOV result.position, TMP_OUT;\nEND\n";
/// WineD3D's adapter probe (directx.c test_arb_vs_offset_limit).
const WINE_OFFSET_PROBE: &[u8] = b"!!ARBvp1.0\nPARAM C[66] = { program.env[0..65] };\nADDRESS A0;\
PARAM zero = {0.0, 0.0, 0.0, 0.0};\nARL A0.x, zero.x;\nMOV result.position, C[A0.x + 65];\nEND\n";

fn f(v: f32) -> u32 {
    v.to_bits()
}
fn string(target: u32, source: &[u8]) -> (u32, Vec<u32>) {
    data_call(
        PROGRAM_STRING,
        &[target, ASCII, source.len() as u32],
        source,
    )
}
fn env(index: u32, v: [f32; 4]) -> (u32, Vec<u32>) {
    call(ENV, &[VP, index, f(v[0]), f(v[1]), f(v[2]), f(v[3])])
}
fn attrib(index: u32, v: [f32; 4]) -> (u32, Vec<u32>) {
    call(ATTRIB, &[index, f(v[0]), f(v[1]), f(v[2]), f(v[3])])
}
fn clear() -> Vec<(u32, Vec<u32>)> {
    vec![
        call(CLEAR_COLOR, &[f(0.), f(0.), f(0.), f(1.)]),
        call(CLEAR, &[0x4000]),
    ]
}
fn component_bytes(kind: u32) -> usize {
    match kind {
        0x1400 | 0x1401 => 1,
        0x1402 | 0x1403 => 2,
        0x140a => 8,
        _ => 4,
    }
}

/// One array of the attribute draw: slot 0-15 conventional (DrawArraysEXT
/// order), 16-31 generic attributes 0-15; `data` holds every vertex.
struct Array {
    slot: usize,
    kind: u32,
    size: u32,
    normalized: bool,
    data: Vec<u8>,
}
fn floats(slot: usize, size: u32, values: &[f32]) -> Array {
    Array {
        slot,
        kind: FLOAT,
        size,
        normalized: false,
        data: values.iter().flat_map(|v| v.to_le_bytes()).collect(),
    }
}
/// The four corners of the clip-space rectangle [x0, x1] x [y0, y1].
fn corners(slot: usize, x0: f32, y0: f32, x1: f32, y1: f32) -> Array {
    floats(slot, 2, &[x0, y0, x1, y0, x1, y1, x0, y1])
}
fn full(slot: usize) -> Array {
    corners(slot, -1., -1., 1., 1.)
}
/// One generic attribute repeated for each of `count` vertices.
fn repeated(
    slot: usize,
    kind: u32,
    size: u32,
    normalized: bool,
    vertex: &[u8],
    count: usize,
) -> Array {
    Array {
        slot,
        kind,
        size,
        normalized,
        data: vertex.repeat(count),
    }
}
/// The attribute draw record (gl.h, DG_CAP_GL_VERTEX_PROGRAM).
fn draw(mode: u32, count: u32, arrays: &[Array]) -> (u32, Vec<u32>) {
    let mut descriptors = [0u32; 32];
    let (mut mask, mut generic) = (0x8000_0000u32, 0u32);
    let mut sorted: Vec<&Array> = arrays.iter().collect();
    sorted.sort_by_key(|a| a.slot);
    for a in &sorted {
        assert_eq!(
            a.data.len(),
            count as usize * a.size as usize * component_bytes(a.kind)
        );
        descriptors[a.slot] = a.kind | a.size << 16 | if a.normalized { 1 << 24 } else { 0 };
        if a.slot < 16 {
            mask |= 1 << a.slot;
        } else {
            generic |= 1 << (a.slot - 16);
        }
    }
    let mut bytes: Vec<u8> = descriptors.iter().flat_map(|d| d.to_le_bytes()).collect();
    for a in &sorted {
        let unit = component_bytes(a.kind);
        bytes.resize(bytes.len().div_ceil(unit) * unit, 0);
        bytes.extend_from_slice(&a.data);
    }
    bytes.resize(bytes.len().div_ceil(4) * 4, 0);
    data_call(ATTRIB_DRAW, &[mode, 0, count, mask, generic], &bytes)
}
/// A seven-slot DrawArrays of FLOAT 2 positions only.
fn draw_positions(x0: f32, y0: f32, x1: f32, y1: f32) -> (u32, Vec<u32>) {
    let mut bytes = vec![0u8; 28];
    bytes[..4].copy_from_slice(&(FLOAT | 2 << 16).to_le_bytes());
    for v in [x0, y0, x1, y0, x1, y1, x0, y1] {
        bytes.extend(v.to_le_bytes());
    }
    data_call(DRAW_ARRAYS, &[QUADS, 0, 4, 0x8000_0001], &bytes)
}

trait VertexDevice {
    fn vertex_program(&mut self, pname: u32) -> u32;
    fn current(&mut self, index: u32) -> [u32; 4];
    fn mode_enabled(&mut self, cap: u32) -> bool;
    fn vertex_source(&mut self) -> Vec<u8>;
}
impl VertexDevice for Device {
    fn vertex_program(&mut self, pname: u32) -> u32 {
        self.word(GET_PROGRAM, &[VP, pname])
    }
    fn current(&mut self, index: u32) -> [u32; 4] {
        let (kind, bytes) = self.query(GET_ATTRIB, &[index, CURRENT_ATTRIB]);
        assert_eq!(kind, DG_GL_RESULT_FLOAT);
        words(&bytes).try_into().unwrap()
    }
    fn mode_enabled(&mut self, cap: u32) -> bool {
        self.query(IS_ENABLED, &[cap]) == (DG_GL_RESULT_BOOL, vec![1])
    }
    fn vertex_source(&mut self) -> Vec<u8> {
        let length = self.vertex_program(PROGRAM_LENGTH) as usize;
        let mut out = Vec::new();
        while out.len() < length {
            out.extend(
                self.query(GET_PROGRAM_STRING, &[VP, 4, out.len() as u32 / 4])
                    .1,
            );
        }
        out.truncate(length);
        out
    }
}
fn quad_attribute(index: u32, color: [f32; 4]) -> Vec<(u32, Vec<u32>)> {
    // Immediate mode: the colour first, then attribute 0 provokes each vertex.
    let mut out = vec![call(BEGIN, &[QUADS])];
    for (x, y) in [(-1f32, -1f32), (1., -1.), (1., 1.), (-1., 1.)] {
        out.push(attrib(index, color));
        out.push(attrib(0, [x, y, 0., 1.]));
    }
    out.push(call(END, &[]));
    out
}

#[test]
#[ignore = "requires native host GPU and vertex program QEMU producer; diskless acceptance"]
fn qemu_vertex_programs_transform_generic_attributes_and_alias_the_position() {
    let (mut d, _server, _directory) = start("vp");
    assert_ne!(d.qemu.read(0x1008) & 0x400000, 0);
    assert_eq!(d.word(GET_INTEGER, &[MAX_ATTRIBS]), 16);
    // Wine's VertexShaderVersion 2.0 needs 256 env and native parameters.
    assert!(d.vertex_program(MAX_ENV) >= 256);
    assert!(d.vertex_program(MAX_NATIVE_PARAMETERS) >= 256);
    assert!(d.vertex_program(MAX_ADDRESS_REGISTERS) >= 1);
    assert!(!d.mode_enabled(VP) && !d.mode_enabled(POINT_SIZE) && !d.mode_enabled(TWO_SIDE));

    // Env-parameter rows map the unit square onto the drawable, then onto
    // its left half; the colour is a normalized UNSIGNED_BYTE generic array.
    let color = [51u8, 102, 153, 255];
    let square = || corners(16, 0., 0., 1., 1.);
    let mut records = clear();
    records.extend([
        call(BIND_PROGRAM, &[VP, 1]),
        string(VP, TRANSFORM),
        call(ENABLE, &[VP]),
        env(0, [2., 0., 0., -1.]),
        env(1, [0., 2., 0., -1.]),
        env(2, [0., 0., 1., 0.]),
        env(3, [0., 0., 0., 1.]),
        draw(
            QUADS,
            4,
            &[square(), repeated(19, 0x1401, 4, true, &color, 4)],
        ),
    ]);
    d.submit(&records);
    assert_eq!(d.error(), 0);
    d.expect(SLATE, "env-matrix transform");
    assert!(d.mode_enabled(VP));
    assert_eq!(d.vertex_program(PROGRAM_BINDING), 1);
    assert_eq!(d.vertex_program(PROGRAM_LENGTH), TRANSFORM.len() as u32);
    assert_eq!(d.vertex_source(), TRANSFORM);
    let mut records = clear();
    records.extend([
        env(0, [1., 0., 0., -1.]),
        draw(
            QUADS,
            4,
            &[square(), repeated(19, 0x1401, 4, true, &color, 4)],
        ),
    ]);
    d.submit(&records);
    assert_eq!(d.pixel_for(1, 1, 4), SLATE);
    assert_eq!(d.pixel_for(1, 6, 4), BLACK, "new env rows, left half only");

    // Every generic component type and size 1-4, normalized or not; missing
    // components default to (0, 0, 0, 1).
    let i16s = |v: &[i16]| v.iter().flat_map(|x| x.to_le_bytes()).collect::<Vec<_>>();
    let u16s = |v: &[u16]| v.iter().flat_map(|x| x.to_le_bytes()).collect::<Vec<_>>();
    let i32s = |v: &[i32]| v.iter().flat_map(|x| x.to_le_bytes()).collect::<Vec<_>>();
    let u32s = |v: &[u32]| v.iter().flat_map(|x| x.to_le_bytes()).collect::<Vec<_>>();
    let f32s = |v: &[f32]| v.iter().flat_map(|x| x.to_le_bytes()).collect::<Vec<_>>();
    let f64s = |v: &[f64]| v.iter().flat_map(|x| x.to_le_bytes()).collect::<Vec<_>>();
    // Label, type, size, normalized, one vertex's bytes, expected pixel.
    type Case = (&'static str, u32, u32, bool, Vec<u8>, [u8; 4]);
    let cases: [Case; 10] = [
        (
            "FLOAT 3",
            FLOAT,
            3,
            false,
            f32s(&[1., 0., 1.]),
            [255, 0, 255, 255],
        ),
        ("DOUBLE 2", 0x140a, 2, false, f64s(&[0., 1.]), GREEN),
        (
            "SHORT 4",
            0x1402,
            4,
            false,
            i16s(&[1, 1, 0, 1]),
            [255, 255, 0, 255],
        ),
        (
            "UNSIGNED_SHORT 4 normalized",
            0x1403,
            4,
            true,
            u16s(&[0, 65535, 65535, 65535]),
            [0, 255, 255, 255],
        ),
        (
            "BYTE 3 normalized",
            0x1400,
            3,
            true,
            vec![0x80, 0x7f, 0x80],
            GREEN,
        ),
        ("INT 1", 0x1404, 1, false, i32s(&[1]), RED),
        ("UNSIGNED_INT 3", 0x1405, 3, false, u32s(&[0, 0, 1]), BLUE),
        (
            "UNSIGNED_BYTE 4 normalized",
            0x1401,
            4,
            true,
            vec![51, 102, 153, 204],
            [51, 102, 153, 204],
        ),
        ("UNSIGNED_BYTE 2", 0x1401, 2, false, vec![1, 0], RED),
        (
            "SHORT 1 normalized",
            0x1402,
            1,
            true,
            i16s(&[i16::MIN]),
            BLACK,
        ),
    ];
    d.submit(&[call(BIND_PROGRAM, &[VP, 2]), string(VP, PASS)]);
    for (label, kind, size, normalized, vertex, expected) in cases {
        let mut records = vec![call(CLEAR_COLOR, &[f(0.5); 4]), call(CLEAR, &[0x4000])];
        records.push(draw(
            QUADS,
            4,
            &[full(16), repeated(17, kind, size, normalized, &vertex, 4)],
        ));
        d.submit(&records);
        d.expect(expected, label);
    }
    assert_eq!(d.error(), 0);

    // Current values: set outside Begin/End, used without an array, kept
    // across an array draw of the same attribute.
    let current = [0.2f32, 0.4, 0.6, 0.8];
    let mut records = clear();
    records.extend([attrib(1, current), draw(QUADS, 4, &[full(16)])]);
    d.submit(&records);
    d.expect([51, 102, 153, 204], "current attribute 1");
    assert_eq!(d.current(1), current.map(f));
    d.submit(&[draw(
        QUADS,
        4,
        &[full(16), repeated(17, 0x1401, 4, true, &[255; 4], 4)],
    )]);
    d.expect([255; 4], "attribute 1 array");
    assert_eq!(
        d.current(1),
        current.map(f),
        "restored after the array draw"
    );
    // Inside Begin/End attribute 0 provokes each vertex after attribute 1.
    let mut records = clear();
    records.extend(quad_attribute(1, [0., 1., 0., 1.]));
    d.submit(&records);
    d.expect(GREEN, "immediate attributes, attribute 0 provokes");
    assert_eq!(d.current(1), [0f32, 1., 0., 1.].map(f));
    assert_eq!(d.error(), 0);

    // Attribute 0 is the position: its array supersedes the vertex array,
    // and vertex.attrib[0] reads the vertex array when it alone is enabled.
    let mut records = clear();
    records.extend([
        call(BIND_PROGRAM, &[VP, 3]),
        string(VP, POSITION),
        draw(QUADS, 4, &[corners(0, -1., -1., 0., 1.), full(16)]),
    ]);
    d.submit(&records);
    d.expect(RED, "generic 0 supersedes the vertex array");
    let mut records = clear();
    records.extend([
        call(BIND_PROGRAM, &[VP, 2]),
        attrib(1, [0., 0., 1., 1.]),
        draw_positions(-1., -1., 0., 1.),
    ]);
    d.submit(&records);
    assert_eq!(
        d.pixel_for(1, 1, 4),
        BLUE,
        "vertex.attrib[0] is the vertex array"
    );
    assert_eq!(d.pixel_for(1, 6, 4), BLACK);

    // state.matrix.mvp follows the modelview; program matrices are matrix modes.
    let mut records = clear();
    records.extend([
        call(BIND_PROGRAM, &[VP, 4]),
        string(VP, MVP),
        call(MATRIX_MODE, &[MODELVIEW]),
        call(SCALE, &[f(0.5), f(1.), f(1.)]),
        draw(QUADS, 4, &[full(16)]),
        call(LOAD_IDENTITY, &[]),
    ]);
    d.submit(&records);
    assert_eq!(d.pixel_for(1, 3, 4), BLUE);
    assert_eq!(d.pixel_for(1, 0, 4), BLACK, "mvp scaled by the modelview");
    // Column major: row 1 is the second element of every column.
    let matrix = [
        0f32, 0.2, 0., 0., 0., 0.4, 0., 0., 0., 0.6, 0., 0., 0., 0.8, 0., 1.,
    ];
    let mut records = vec![
        call(MATRIX_MODE, &[MATRIX0]),
        call(LOAD_MATRIX, &matrix.map(f)),
        call(MATRIX_MODE, &[MODELVIEW]),
        call(BIND_PROGRAM, &[VP, 5]),
        string(VP, PROGRAM_MATRIX),
    ];
    records.push(draw(QUADS, 4, &[full(16)]));
    d.submit(&records);
    d.expect([51, 102, 153, 204], "state.matrix.program[0] row 1");

    // Per-vertex point size only while VERTEX_PROGRAM_POINT_SIZE is enabled.
    let point = || floats(16, 2, &[0.125, 0.125]);
    let mut records = clear();
    records.extend([
        call(BIND_PROGRAM, &[VP, 6]),
        string(VP, SIZED),
        env(0, [3., 0., 0., 0.]),
        call(ENABLE, &[POINT_SIZE]),
        draw(POINTS, 1, &[point()]),
    ]);
    d.submit(&records);
    // An odd size centred on a pixel centre has no edge on a pixel centre.
    assert_eq!(d.pixel_for(1, 3, 3), GREEN, "result.pointsize 3");
    assert_eq!(d.pixel_for(1, 5, 5), GREEN);
    assert_eq!(d.pixel_for(1, 2, 2), BLACK);
    assert_eq!(d.pixel_for(1, 6, 6), BLACK);
    let mut records = clear();
    records.extend([call(DISABLE, &[POINT_SIZE]), draw(POINTS, 1, &[point()])]);
    d.submit(&records);
    assert_eq!(d.pixel_for(1, 4, 4), GREEN);
    assert_eq!(d.pixel_for(1, 3, 3), BLACK, "the fixed point size 1");

    // Two-sided colours: a clockwise (back-facing) quad.
    let clockwise = || floats(16, 2, &[-1., -1., -1., 1., 1., 1., 1., -1.]);
    let mut records = clear();
    records.extend([
        call(BIND_PROGRAM, &[VP, 7]),
        string(VP, SIDES),
        draw(QUADS, 4, &[clockwise()]),
    ]);
    d.submit(&records);
    d.expect(RED, "one-sided: the front colour");
    d.submit(&[call(ENABLE, &[TWO_SIDE]), draw(QUADS, 4, &[clockwise()])]);
    d.expect(GREEN, "VERTEX_PROGRAM_TWO_SIDE: the back colour");

    // ENABLE_BIT saves the three modes; CURRENT_BIT the current attributes.
    d.submit(&[
        call(ENABLE, &[POINT_SIZE]),
        call(PUSH, &[ENABLE_BIT | CURRENT_BIT]),
        call(DISABLE, &[VP]),
        call(DISABLE, &[POINT_SIZE]),
        call(DISABLE, &[TWO_SIDE]),
        attrib(2, [9., 8., 7., 6.]),
    ]);
    assert!(!d.mode_enabled(VP) && !d.mode_enabled(POINT_SIZE) && !d.mode_enabled(TWO_SIDE));
    d.submit(&[call(POP, &[])]);
    assert!(d.mode_enabled(VP) && d.mode_enabled(POINT_SIZE) && d.mode_enabled(TWO_SIDE));
    assert_eq!(d.current(2), [0f32, 0., 0., 1.].map(f));
    d.submit(&[
        call(PUSH, &[0x000f_ffff]),
        call(BIND_PROGRAM, &[VP, 2]),
        call(POP, &[]),
        call(DISABLE, &[POINT_SIZE]),
        call(DISABLE, &[TWO_SIDE]),
    ]);
    assert_eq!(
        d.vertex_program(PROGRAM_BINDING),
        2,
        "bindings are not attribute state"
    );

    // Relative addressing with an ADDRESS register.
    let mut records = clear();
    records.extend([
        call(BIND_PROGRAM, &[VP, 8]),
        string(VP, RELATIVE),
        env(3, [0., 1., 0., 1.]),
        draw(
            QUADS,
            4,
            &[
                full(16),
                repeated(17, FLOAT, 1, false, &2f32.to_le_bytes(), 4),
            ],
        ),
    ]);
    d.submit(&records);
    d.expect(GREEN, "C[A0.x + 1]");

    // Vertex and fragment programs together.
    let mut records = clear();
    records.extend([
        call(BIND_PROGRAM, &[VP, 9]),
        string(VP, VARYING),
        call(BIND_PROGRAM, &[FP, 10]),
        string(FP, FRAGMENT_VARYING),
        call(ENABLE, &[FP]),
        draw(
            QUADS,
            4,
            &[full(16), repeated(17, 0x1401, 4, true, &color, 4)],
        ),
        call(DISABLE, &[FP]),
    ]);
    d.submit(&records);
    d.expect(SLATE, "vertex program texcoord into a fragment program");
    assert_eq!(d.error(), 0);

    // The program WineD3D generates for a vs_1_1 transform with a D3DCOLOR
    // input parses natively and renders through the attribute draw.
    let bgra = [153u8, 102, 51, 255];
    let mut records = clear();
    records.extend([
        call(BIND_PROGRAM, &[VP, 11]),
        string(VP, WINE_VS11),
        env(0, [2., 0., 0., -1.]),
        env(1, [0., 2., 0., -1.]),
        env(2, [0., 0., 1., 0.]),
        env(3, [0., 0., 0., 1.]),
        call(LOCAL, &[VP, 0, f(1.), f(1.), f(0.), f(0.)]),
        draw(
            QUADS,
            4,
            &[
                square(),
                repeated(17, 0x1401, 4, true, &bgra, 4),
                floats(18, 2, &[0.; 8]),
            ],
        ),
    ]);
    d.submit(&records);
    assert_eq!(d.error(), 0);
    assert_eq!(d.word(GET_INTEGER, &[ERROR_POSITION]) as i32, -1);
    d.expect(SLATE, "WineD3D vs_1_1 program");
    // vs_2_0: colour 0x40, 0x80, 0x20 times an unclamped c4 = (2, 0.5, 2, 1).
    let mut records = clear();
    records.extend([
        call(BIND_PROGRAM, &[VP, 13]),
        string(VP, WINE_VS20),
        env(4, [2., 0.5, 2., 1.]),
        call(LOCAL, &[VP, 0, f(1.), f(1.), f(0.), f(0.)]),
        draw(
            QUADS,
            4,
            &[
                square(),
                repeated(17, 0x1401, 4, true, &[0x20, 0x80, 0x40, 255], 4),
            ],
        ),
    ]);
    d.submit(&records);
    assert_eq!(d.error(), 0);
    assert_eq!(d.word(GET_INTEGER, &[ERROR_POSITION]) as i32, -1);
    d.expect([0x80, 0x40, 0x40, 255], "WineD3D vs_2_0 program");
    // Its adapter probe: native GL may reject offsets beyond 63, as a GL error.
    d.submit(&[call(BIND_PROGRAM, &[VP, 12]), string(VP, WINE_OFFSET_PROBE)]);
    let probe = d.error();
    assert!(probe == 0 || probe == INVALID_OPERATION, "{probe:#x}");
    assert_eq!(d.error(), 0);
}

#[test]
#[ignore = "requires native host GPU and vertex program QEMU producer; diskless acceptance"]
fn qemu_vertex_program_failures_lists_and_sharing_stay_gl_errors() {
    let (mut d, _server, _directory) = start("vp-errors");
    let color = [51u8, 102, 153, 255];
    // A program that fails to parse: INVALID_OPERATION with a position and a
    // message, and the bound program renders exactly as before.
    let mut records = clear();
    records.extend([
        call(BIND_PROGRAM, &[VP, 1]),
        string(VP, PASS),
        call(ENABLE, &[VP]),
    ]);
    d.submit(&records);
    let broken = b"!!ARBvp1.0\nMOV result.position, vertex.attrib[0];\nBOGUS;\nEND";
    d.submit(&[
        string(VP, broken),
        draw(
            QUADS,
            4,
            &[full(16), repeated(17, 0x1401, 4, true, &color, 4)],
        ),
    ]);
    assert_eq!(d.error(), INVALID_OPERATION);
    let position = d.word(GET_INTEGER, &[ERROR_POSITION]) as i32;
    assert!((10..=broken.len() as i32).contains(&position), "{position}");
    assert!(!d.error_string().is_empty());
    d.expect(SLATE, "after a failed load");
    assert_eq!(d.vertex_source(), PASS);
    // A generic attribute beyond MAX_VERTEX_ATTRIBS fails on the host at its
    // offset, with a DreamGPU message.
    let beyond = b"!!ARBvp1.0\nMOV result.color, vertex.attrib[16];\nEND";
    d.submit(&[string(VP, beyond)]);
    assert_eq!(d.error(), INVALID_OPERATION);
    let at = beyond.windows(2).position(|w| w == b"16").unwrap() as u32;
    assert_eq!(d.word(GET_INTEGER, &[ERROR_POSITION]), at);
    assert!(
        d.error_string()
            .starts_with("DreamGPU: generic vertex attribute")
    );
    assert_eq!(d.vertex_source(), PASS);
    // An enabled program without a valid string fails array and immediate
    // draws as GL errors; the batch and the context survive.
    let mut records = clear();
    records.extend([
        call(BIND_PROGRAM, &[VP, 2]),
        draw(QUADS, 4, &[full(16)]),
        draw_positions(-1., -1., 1., 1.),
    ]);
    records.extend(quad_attribute(1, [1.; 4]));
    d.submit(&records);
    assert_eq!(d.error(), INVALID_OPERATION);
    assert_eq!(d.error(), 0);
    d.expect(BLACK, "an invalid vertex program draws nothing");
    // Disabled again, fixed function draws the vertex array.
    let mut records = clear();
    records.extend([
        call(DISABLE, &[VP]),
        call(0x0db, &[f(0.), f(0.), f(1.), f(1.)]),
        draw_positions(-1., -1., 1., 1.),
    ]);
    d.submit(&records);
    d.expect(BLUE, "fixed function");

    // Display lists: binds, env parameters, strings, immediate attributes and
    // attribute draws compile; deletion executes immediately.
    let list = words(&d.query(GEN_LISTS, &[2]).1)[1];
    assert_eq!(d.word(NEW_LIST, &[list, COMPILE]), 0);
    d.submit(&[
        call(BIND_PROGRAM, &[VP, 3]),
        string(VP, TRANSFORM),
        call(ENABLE, &[VP]),
        env(0, [2., 0., 0., -1.]),
        env(1, [0., 2., 0., -1.]),
        env(2, [0., 0., 1., 0.]),
        env(3, [0., 0., 0., 1.]),
        draw(
            QUADS,
            4,
            &[
                corners(16, 0., 0., 1., 1.),
                repeated(19, 0x1401, 4, true, &color, 4),
            ],
        ),
    ]);
    assert_eq!(d.word(END_LIST, &[]), 0);
    assert_eq!(
        d.vertex_program(PROGRAM_LENGTH),
        0,
        "compiled, not executed"
    );
    assert!(!d.mode_enabled(VP));
    let mut records = clear();
    records.push(call(CALL_LIST, &[list]));
    d.submit(&records);
    d.expect(SLATE, "a vertex program and attribute draw from a list");
    assert_eq!(d.vertex_program(PROGRAM_BINDING), 3);
    assert_eq!(d.vertex_source(), TRANSFORM);
    assert_eq!(d.word(NEW_LIST, &[list + 1, COMPILE]), 0);
    let mut records = vec![call(BIND_PROGRAM, &[VP, 4]), string(VP, PASS)];
    records.extend(quad_attribute(1, [0., 1., 0., 1.]));
    records.push(data_call(DELETE_PROGRAMS, &[1], &3u32.to_le_bytes()));
    d.submit(&records);
    assert_eq!(d.word(END_LIST, &[]), 0);
    assert_eq!(
        d.vertex_program(PROGRAM_BINDING),
        0,
        "deleted while compiling"
    );
    let mut records = clear();
    records.push(call(CALL_LIST, &[list + 1]));
    d.submit(&records);
    d.expect(GREEN, "immediate generic attributes from a list");
    assert_eq!(d.error(), 0);

    // Sharing: context 2 shares context 1's programs; context 3 does not.
    d.submit_for(2, &[(1, vec![1]), (3, vec![8, 8]), (5, vec![])]);
    d.submit_for(3, &[(1, vec![0]), (3, vec![8, 8]), (5, vec![])]);
    let mut records = clear();
    records.extend([
        call(BIND_PROGRAM, &[VP, 4]),
        call(ENABLE, &[VP]),
        draw(
            QUADS,
            4,
            &[full(16), repeated(17, 0x1401, 4, true, &color, 4)],
        ),
    ]);
    d.submit_for(2, &records);
    d.expect_for(2, SLATE, "a shared vertex program");
    assert!(d.is_program_for(2, 4) && !d.is_program_for(3, 4));
    d.submit_for(3, &[call(BIND_PROGRAM, &[VP, 4])]);
    assert_eq!(d.word_for(3, GET_PROGRAM, &[VP, PROGRAM_LENGTH]), 0);
    // Default vertex objects are private to each share group.
    d.submit_for(3, &[call(BIND_PROGRAM, &[VP, 0]), string(VP, PASS)]);
    assert_eq!(
        d.word_for(1, GET_PROGRAM, &[VP, PROGRAM_LENGTH]),
        PASS.len() as u32
    );
    d.submit(&[call(BIND_PROGRAM, &[VP, 0])]);
    assert_eq!(d.word_for(1, GET_PROGRAM, &[VP, PROGRAM_LENGTH]), 0);
    let mut records = clear();
    records.extend([
        call(ENABLE, &[VP]),
        draw(
            QUADS,
            4,
            &[
                full(16),
                repeated(17, 0x1401, 4, true, &[0, 0, 255, 255], 4),
            ],
        ),
    ]);
    d.submit_for(3, &records);
    d.expect_for(3, BLUE, "the private default vertex object");
    // Deleted in context 1 while bound in context 2: context 2 keeps it.
    d.submit(&[data_call(DELETE_PROGRAMS, &[1], &4u32.to_le_bytes())]);
    assert!(!d.is_program_for(2, 4));
    let mut records = clear();
    records.push(draw(
        QUADS,
        4,
        &[full(16), repeated(17, 0x1401, 4, true, &color, 4)],
    ));
    d.submit_for(2, &records);
    d.expect_for(2, SLATE, "a deleted but bound vertex program");
    assert_eq!(d.word_for(2, GET_PROGRAM, &[VP, PROGRAM_BINDING]), 4);
    d.submit_for(3, &[(2, vec![])]);
    d.submit_for(2, &[(2, vec![])]);
    assert_eq!(d.error_for(1), 0);
}
