//! ARB_vertex_buffer_object through the device: named buffers feeding
//! positions (FLOAT 2/3), colours (UNSIGNED_BYTE 4, FLOAT 4/3), texture
//! coordinates and generic attributes of an ARB vertex program at nonzero
//! offsets and interleaved strides; array draws from a nonzero base; element
//! buffers of every index type at aligned offsets (one also holding the
//! vertices); client sections mixed with buffer attributes in array and
//! indexed draws; BufferSubData updates and whole-buffer "unmap" uploads;
//! zero-size and zero-defined storage; arrays whose buffer is too short left
//! unread while the rest draws without error; element ranges past their
//! buffer, indices outside client vertices, missing or deleted names and
//! budget exhaustion as GL errors after which the context keeps drawing; sharing and isolation between contexts; and display lists,
//! which never compile buffer commands and record a buffer draw as
//! INVALID_OPERATION. Every GL error stays a GL error: no batch in this file
//! may fail.
use super::fragment_program::{Device, start};
use super::textures::{call, data_call, words};

const BUFFER_DATA: u32 = 137; // BufferDataARB
const BUFFER_SUB_DATA: u32 = 144; // BufferSubDataARB
const DELETE_BUFFERS: u32 = 402; // DeleteBuffersARB
const BUFFER_DRAW: u32 = 501; // the buffer draw (DrawRangeElements)
const DRAW_ARRAYS: u32 = 469;
const BIND_PROGRAM: u32 = 69;
const PROGRAM_STRING: u32 = 1766;
const GEN_LISTS: u32 = 671;
const NEW_LIST: u32 = 1592;
const END_LIST: u32 = 539;
const CALL_LIST: u32 = 146;
const ENABLE: u32 = 0x209;
const DISABLE: u32 = 0x1c5;
const CLEAR_COLOR: u32 = 0xa3;
const CLEAR: u32 = 0x9a;
const COLOR4F: u32 = 0x0db;
const BEGIN: u32 = 0x01a;
const END: u32 = 0x216;
const VERTEX2F: u32 = 0x9dc;
const BIND_TEXTURE: u32 = 0x04e;
const TEX_PARAMETER_I: u32 = 0x8df;
const TEX_IMAGE_2D: u32 = 0x8d4;

const VP: u32 = 0x8620;
const ASCII: u32 = 0x8875;
const TEXTURE_2D: u32 = 0x0de1;
const COMPILE: u32 = 0x1300;
const COMPILE_AND_EXECUTE: u32 = 0x1301;
const INVALID_VALUE: u32 = 0x0501;
const INVALID_OPERATION: u32 = 0x0502;
const OUT_OF_MEMORY: u32 = 0x0505;
const STREAM_DRAW: u32 = 0x88e0;
const STATIC_DRAW: u32 = 0x88e4;
const DYNAMIC_DRAW: u32 = 0x88e8;
const DYNAMIC_COPY: u32 = 0x88ea;
const BYTE: u32 = 0x1400;
const UNSIGNED_BYTE: u32 = 0x1401;
const SHORT: u32 = 0x1402;
const UNSIGNED_SHORT: u32 = 0x1403;
const UNSIGNED_INT: u32 = 0x1405;
const FLOAT: u32 = 0x1406;
const NORMALIZED: u32 = 0x0100_0000;
const ARRAY_RAW: u32 = 0x8000_0000;
const MAX_BUFFER_BYTES: u32 = 0x0800_0000;
const TRIANGLES: u32 = 4;
const QUADS: u32 = 7;

const BLACK: [u8; 4] = [0, 0, 0, 255];
const RED: [u8; 4] = [255, 0, 0, 255];
const GREEN: [u8; 4] = [0, 255, 0, 255];
const BLUE: [u8; 4] = [0, 0, 255, 255];
const WHITE: [u8; 4] = [255, 255, 255, 255];
const SLATE: [u8; 4] = [51, 102, 153, 255];
const MAGENTA: [u8; 4] = [255, 0, 255, 255];
/// Zero-defined UNSIGNED_BYTE colours.
const ZERO: [u8; 4] = [0, 0, 0, 0];

/// Counter-clockwise corners of the drawable and of its halves.
const FULL: [f32; 8] = [-1., -1., 1., -1., 1., 1., -1., 1.];
const LEFT: [f32; 8] = [-1., -1., 0., -1., 0., 1., -1., 1.];
const RIGHT: [f32; 8] = [0., -1., 1., -1., 1., 1., 0., 1.];

/// Clip-space position from attribute 0, colour from attribute 1.
const PASS: &[u8] = b"!!ARBvp1.0\nMOV result.position, vertex.attrib[0];\n\
MOV result.color, vertex.attrib[1];\nEND";

/// The current colour (Color4f) an unread colour array leaves in effect.
fn current(rgba: [u8; 4]) -> (u32, Vec<u32>) {
    call(COLOR4F, &rgba.map(|c| f(f32::from(c) / 255.)))
}
fn f(v: f32) -> u32 {
    v.to_bits()
}
fn f32s(values: &[f32]) -> Vec<u8> {
    values.iter().flat_map(|v| v.to_le_bytes()).collect()
}
fn u16s(values: &[u16]) -> Vec<u8> {
    values.iter().flat_map(|v| v.to_le_bytes()).collect()
}
fn u32s(values: &[u32]) -> Vec<u8> {
    values.iter().flat_map(|v| v.to_le_bytes()).collect()
}
/// Left quad (vertices 0-3) then right quad (vertices 4-7).
fn halves() -> Vec<u8> {
    f32s(&[LEFT, RIGHT].concat())
}
/// `left` for vertices 0-3, `right` for vertices 4-7.
fn half_colors(left: [u8; 4], right: [u8; 4]) -> Vec<u8> {
    [left.repeat(4), right.repeat(4)].concat()
}
fn descriptor(kind: u32, size: u32) -> u32 {
    kind | size << 16
}
fn unit(kind: u32) -> usize {
    match kind {
        BYTE | UNSIGNED_BYTE => 1,
        SHORT | UNSIGNED_SHORT => 2,
        0x140a => 8,
        _ => 4,
    }
}
fn index_bytes(kind: u32) -> usize {
    match kind {
        UNSIGNED_BYTE => 1,
        UNSIGNED_SHORT => 2,
        _ => 4,
    }
}

fn buffer(name: u32, contents: &[u8], usage: u32) -> (u32, Vec<u32>) {
    data_call(BUFFER_DATA, &[name, contents.len() as u32, usage], contents)
}
/// Zero-defined storage: BufferData without a payload.
fn storage(name: u32, size: u32, usage: u32) -> (u32, Vec<u32>) {
    data_call(BUFFER_DATA, &[name, size, usage], &[])
}
fn sub_data(name: u32, offset: u32, contents: &[u8]) -> (u32, Vec<u32>) {
    data_call(BUFFER_SUB_DATA, &[name, offset], contents)
}
fn delete(names: &[u32]) -> (u32, Vec<u32>) {
    data_call(DELETE_BUFFERS, &[names.len() as u32], &u32s(names))
}
fn clear() -> Vec<(u32, Vec<u32>)> {
    vec![
        call(CLEAR_COLOR, &[f(0.), f(0.), f(0.), f(1.)]),
        call(CLEAR, &[0x4000]),
    ]
}
fn string(source: &[u8]) -> (u32, Vec<u32>) {
    data_call(PROGRAM_STRING, &[VP, ASCII, source.len() as u32], source)
}

/// Where one slot of the buffer draw reads: a buffer reference (name, byte
/// offset, stride) or a client section of the record's vertices.
enum Source {
    Buffer(u32, u32, u32),
    Client(Vec<u8>),
}
/// One enabled slot: 0-15 DrawArraysEXT, 16-31 generic attributes 0-15.
struct Slot {
    slot: usize,
    descriptor: u32,
    source: Source,
}
fn buffered(slot: usize, descriptor: u32, name: u32, offset: u32, stride: u32) -> Slot {
    Slot {
        slot,
        descriptor,
        source: Source::Buffer(name, offset, stride),
    }
}
fn client(slot: usize, descriptor: u32, data: Vec<u8>) -> Slot {
    Slot {
        slot,
        descriptor,
        source: Source::Client(data),
    }
}
/// FLOAT 2 positions from a tight buffer.
fn positions(name: u32) -> Slot {
    buffered(0, descriptor(FLOAT, 2), name, 0, 0)
}
/// UNSIGNED_BYTE 4 colours from a tight buffer.
fn colors(name: u32) -> Slot {
    buffered(1, descriptor(UNSIGNED_BYTE, 4), name, 0, 0)
}
enum Indices {
    /// An array draw.
    None,
    /// Inline indices of a type.
    Inline(u32, Vec<u32>),
    /// Type, element buffer and byte offset.
    Element(u32, u32, u32),
}
/// The buffer draw record (gl.h, DG_CAP_GL_VERTEX_BUFFER_OBJECT): ten scalar
/// words, thirty-two descriptors, the buffer references in slot order, the
/// client sections of `vertices` vertices in slot order at their component
/// alignment, then inline indices at their size alignment, zero padded.
fn draw(mode: u32, base: u32, count: u32, indices: Indices, slots: &[Slot]) -> (u32, Vec<u32>) {
    let mut sorted: Vec<&Slot> = slots.iter().collect();
    sorted.sort_by_key(|s| s.slot);
    let mut descriptors = [0u32; 32];
    let (mut mask, mut generic, mut buffers) = (ARRAY_RAW, 0u32, 0u32);
    let mut vertices = None;
    for s in &sorted {
        descriptors[s.slot] = s.descriptor;
        if s.slot < 16 {
            mask |= 1 << s.slot;
        } else {
            generic |= 1 << (s.slot - 16);
        }
        match &s.source {
            Source::Buffer(..) => buffers |= 1 << s.slot,
            Source::Client(data) => {
                let element = ((s.descriptor >> 16) & 255) as usize * unit(s.descriptor & 65535);
                assert_eq!(data.len() % element, 0);
                let n = (data.len() / element) as u32;
                assert!(vertices.is_none_or(|v| v == n), "client sections differ");
                vertices = Some(n);
            }
        }
    }
    let vertices = vertices.unwrap_or(0);
    if matches!(indices, Indices::None) && vertices != 0 {
        assert_eq!(
            vertices, count,
            "an array draw's client section is count vertices"
        );
    }
    let mut bytes: Vec<u8> = descriptors.iter().flat_map(|d| d.to_le_bytes()).collect();
    for s in &sorted {
        if let Source::Buffer(name, offset, stride) = s.source {
            bytes.extend(u32s(&[name, offset, stride]));
        }
    }
    for s in &sorted {
        if let Source::Client(data) = &s.source {
            let unit = unit(s.descriptor & 65535);
            bytes.resize(bytes.len().div_ceil(unit) * unit, 0);
            bytes.extend_from_slice(data);
        }
    }
    let (kind, element, element_offset) = match indices {
        Indices::None => (0, 0, 0),
        Indices::Inline(kind, values) => {
            assert_eq!(
                values.len(),
                count as usize,
                "inline indices are count indices"
            );
            let size = index_bytes(kind);
            bytes.resize(bytes.len().div_ceil(size) * size, 0);
            for v in values {
                bytes.extend_from_slice(&v.to_le_bytes()[..size]);
            }
            (kind, 0, 0)
        }
        Indices::Element(kind, name, offset) => (kind, name, offset),
    };
    bytes.resize(bytes.len().div_ceil(4) * 4, 0);
    data_call(
        BUFFER_DRAW,
        &[
            mode,
            base,
            count,
            mask,
            generic,
            buffers,
            vertices,
            kind,
            element,
            element_offset,
        ],
        &bytes,
    )
}
/// A client DrawArrays (seven RAW slots) of one FLOAT 2 quad in one colour.
fn client_quad(corners: [f32; 8], rgba: [u8; 4]) -> (u32, Vec<u32>) {
    let mut bytes = vec![0u8; 28];
    bytes[..4].copy_from_slice(&descriptor(FLOAT, 2).to_le_bytes());
    bytes[4..8].copy_from_slice(&descriptor(UNSIGNED_BYTE, 4).to_le_bytes());
    bytes.extend(f32s(&corners));
    bytes.extend(rgba.repeat(4));
    data_call(DRAW_ARRAYS, &[QUADS, 0, 4, ARRAY_RAW | 3], &bytes)
}
/// An immediate-mode quad in one colour.
fn immediate_quad(corners: [f32; 8], rgba: [f32; 4]) -> Vec<(u32, Vec<u32>)> {
    let mut out = vec![call(COLOR4F, &rgba.map(f)), call(BEGIN, &[QUADS])];
    for v in corners.chunks(2) {
        out.push(call(VERTEX2F, &[f(v[0]), f(v[1])]));
    }
    out.push(call(END, &[]));
    out
}

trait Halves {
    fn halves_for(&mut self, context: u32, left: [u8; 4], right: [u8; 4], label: &str);
    fn halves(&mut self, left: [u8; 4], right: [u8; 4], label: &str);
    /// Clear, then `records`, then expect the halves and no GL error.
    fn draws(&mut self, records: Vec<(u32, Vec<u32>)>, left: [u8; 4], right: [u8; 4], label: &str);
    /// Clear, then `records`, which must raise exactly `code` and draw
    /// nothing; then a client draw proves the context still renders.
    fn fails(&mut self, records: Vec<(u32, Vec<u32>)>, code: u32, label: &str);
}
impl Halves for Device {
    fn halves_for(&mut self, context: u32, left: [u8; 4], right: [u8; 4], label: &str) {
        for (x, y) in [(0, 0), (3, 7), (1, 4)] {
            assert_eq!(
                self.pixel_for(context, x, y),
                left,
                "{label}: left at {x},{y}"
            );
        }
        for (x, y) in [(4, 0), (7, 7), (6, 3)] {
            assert_eq!(
                self.pixel_for(context, x, y),
                right,
                "{label}: right at {x},{y}"
            );
        }
    }
    fn halves(&mut self, left: [u8; 4], right: [u8; 4], label: &str) {
        self.halves_for(1, left, right, label)
    }
    fn draws(&mut self, records: Vec<(u32, Vec<u32>)>, left: [u8; 4], right: [u8; 4], label: &str) {
        let mut all = clear();
        all.extend(records);
        self.submit(&all);
        assert_eq!(self.error(), 0, "{label}");
        self.halves(left, right, label);
    }
    fn fails(&mut self, records: Vec<(u32, Vec<u32>)>, code: u32, label: &str) {
        let mut all = clear();
        all.extend(records);
        self.submit(&all);
        assert_eq!(self.error(), code, "{label}");
        assert_eq!(self.error(), 0, "{label}: one error");
        self.expect(BLACK, label);
        self.submit(&[client_quad(FULL, SLATE)]);
        assert_eq!(self.error(), 0, "{label}: then a client draw");
        self.expect(SLATE, label);
    }
}

#[test]
#[ignore = "requires native host GPU and vertex buffer object QEMU producer; diskless acceptance"]
fn qemu_buffer_draws_source_every_attribute_kind_from_named_buffers() {
    let (mut d, _server, _directory) = start("vbo");
    assert_ne!(d.qemu.read(0x1008) & 0x100_0000, 0);
    assert_eq!(d.error(), 0);

    // Tight FLOAT 2 positions and UNSIGNED_BYTE 4 colours from two buffers;
    // the first BufferData of a name creates its object.
    d.submit(&[
        buffer(1, &f32s(&FULL), STATIC_DRAW),
        buffer(2, &SLATE.repeat(4), STREAM_DRAW),
    ]);
    assert_eq!(d.error(), 0);
    let mut records = clear();
    records.push(draw(QUADS, 0, 4, Indices::None, &[positions(1), colors(2)]));
    d.submit(&records);
    assert_eq!(d.error(), 0);
    d.expect(SLATE, "FLOAT 2 positions, UNSIGNED_BYTE 4 colours");

    // Interleaved {x, y, z, rgba} vertices of stride 16 after an 8-byte
    // prefix: FLOAT 3 positions at 8, colours at 20.
    let mut interleaved = vec![0xee; 8];
    for (n, v) in [LEFT, RIGHT].concat().chunks(2).enumerate() {
        interleaved.extend(f32s(&[v[0], v[1], 0.5]));
        interleaved.extend(if n < 4 { RED } else { GREEN });
    }
    let position3 = || buffered(0, descriptor(FLOAT, 3), 3, 8, 16);
    let color4 = || buffered(1, descriptor(UNSIGNED_BYTE, 4), 3, 20, 16);
    d.submit(&[buffer(3, &interleaved, DYNAMIC_DRAW)]);
    d.draws(
        vec![draw(QUADS, 0, 8, Indices::None, &[position3(), color4()])],
        RED,
        GREEN,
        "interleaved FLOAT 3 positions and colours",
    );
    // Array draws from a nonzero first vertex.
    d.draws(
        vec![draw(QUADS, 4, 4, Indices::None, &[position3(), color4()])],
        BLACK,
        GREEN,
        "first vertex 4",
    );
    d.draws(
        vec![draw(QUADS, 0, 4, Indices::None, &[position3(), color4()])],
        RED,
        BLACK,
        "first vertex 0, count 4",
    );

    // FLOAT 4 colours after a 16-byte prefix, FLOAT 3 colours tight.
    let mut float4 = vec![0u8; 16];
    float4.extend(f32s(&[0.2, 0.4, 0.6, 1.].repeat(4)));
    float4.extend(f32s(&[1., 0., 1., 1.].repeat(4)));
    d.submit(&[
        buffer(4, &float4, STATIC_DRAW),
        buffer(
            5,
            &f32s(&[[0., 0., 1.].repeat(4), [1., 0., 1.].repeat(4)].concat()),
            STATIC_DRAW,
        ),
    ]);
    d.draws(
        vec![draw(
            QUADS,
            0,
            8,
            Indices::None,
            &[position3(), buffered(1, descriptor(FLOAT, 4), 4, 16, 0)],
        )],
        SLATE,
        MAGENTA,
        "FLOAT 4 colours at offset 16",
    );
    d.draws(
        vec![draw(
            QUADS,
            0,
            8,
            Indices::None,
            &[position3(), buffered(1, descriptor(FLOAT, 3), 5, 0, 0)],
        )],
        BLUE,
        MAGENTA,
        "FLOAT 3 colours",
    );

    // Texture coordinates from a buffer of interleaved {x, y, s, t} sample a
    // 2x2 NEAREST texture: red, green / blue, white (bottom row first).
    let mut xyst = Vec::new();
    for (x, y, s, t) in [
        (-1f32, -1f32, 0f32, 0f32),
        (1., -1., 1., 0.),
        (1., 1., 1., 1.),
        (-1., 1., 0., 1.),
    ] {
        xyst.extend(f32s(&[x, y, s, t]));
    }
    let mut records = vec![
        call(BIND_TEXTURE, &[TEXTURE_2D, 30]),
        call(TEX_PARAMETER_I, &[TEXTURE_2D, 0x2801, 0x2600]),
        call(TEX_PARAMETER_I, &[TEXTURE_2D, 0x2800, 0x2600]),
        data_call(
            TEX_IMAGE_2D,
            &[TEXTURE_2D, 0, 0x1908, 2, 2, 0, 0x1908, UNSIGNED_BYTE],
            &[RED, GREEN, BLUE, WHITE].concat(),
        ),
        buffer(6, &xyst, STATIC_DRAW),
    ];
    records.extend(clear());
    records.extend([
        call(ENABLE, &[TEXTURE_2D]),
        call(COLOR4F, &[f(1.); 4]),
        draw(
            QUADS,
            0,
            4,
            Indices::None,
            &[
                buffered(0, descriptor(FLOAT, 2), 6, 0, 16),
                buffered(3, descriptor(FLOAT, 2), 6, 8, 16),
            ],
        ),
        call(DISABLE, &[TEXTURE_2D]),
    ]);
    d.submit(&records);
    assert_eq!(d.error(), 0);
    for ((x, y), texel) in [
        ((1, 1), RED),
        ((6, 1), GREEN),
        ((1, 6), BLUE),
        ((6, 6), WHITE),
    ] {
        assert_eq!(
            d.pixel_for(1, x, y),
            texel,
            "buffer texture coordinates at {x},{y}"
        );
    }

    // Element buffers of every index type at nonzero aligned offsets; the
    // skipped prefix holds indices the draw must never read.
    d.submit(&[
        buffer(7, &[9, 9, 9, 4, 5, 6, 7], STATIC_DRAW),
        buffer(8, &u16s(&[0xffff, 0xffff, 0xffff, 0, 1, 2, 3]), STATIC_DRAW),
        buffer(
            9,
            &u32s(&[u32::MAX, 4, 5, 6, 4, 6, 7, 0, 1, 2, 0, 2, 3]),
            DYNAMIC_COPY,
        ),
    ]);
    d.draws(
        vec![draw(
            QUADS,
            0,
            4,
            Indices::Element(UNSIGNED_BYTE, 7, 3),
            &[position3(), color4()],
        )],
        BLACK,
        GREEN,
        "UNSIGNED_BYTE element buffer at offset 3",
    );
    d.draws(
        vec![draw(
            QUADS,
            0,
            4,
            Indices::Element(UNSIGNED_SHORT, 8, 6),
            &[position3(), color4()],
        )],
        RED,
        BLACK,
        "UNSIGNED_SHORT element buffer at offset 6",
    );
    d.draws(
        vec![draw(
            TRIANGLES,
            0,
            12,
            Indices::Element(UNSIGNED_INT, 9, 4),
            &[position3(), color4()],
        )],
        RED,
        GREEN,
        "UNSIGNED_INT element buffer at offset 4",
    );
    d.draws(
        vec![draw(
            TRIANGLES,
            0,
            6,
            Indices::Element(UNSIGNED_INT, 9, 28),
            &[position3(), color4()],
        )],
        RED,
        BLACK,
        "UNSIGNED_INT element buffer at offset 28",
    );
    // One buffer holds both the vertices and, after them, their indices.
    let mut both = f32s(&FULL);
    both.extend(u16s(&[0, 1, 2, 3]));
    d.submit(&[buffer(10, &both, STATIC_DRAW)]);
    d.draws(
        vec![draw(
            QUADS,
            0,
            4,
            Indices::Element(UNSIGNED_SHORT, 10, 32),
            &[positions(10), colors(2)],
        )],
        SLATE,
        SLATE,
        "vertices and indices in one buffer",
    );
    // Inline indices with every attribute in buffers carry no vertices.
    d.draws(
        vec![draw(
            QUADS,
            0,
            4,
            Indices::Inline(UNSIGNED_BYTE, vec![4, 5, 6, 7]),
            &[position3(), color4()],
        )],
        BLACK,
        GREEN,
        "inline UNSIGNED_BYTE indices",
    );
    d.draws(
        vec![draw(
            QUADS,
            0,
            8,
            Indices::Inline(UNSIGNED_SHORT, vec![4, 5, 6, 7, 0, 1, 2, 3]),
            &[position3(), color4()],
        )],
        RED,
        GREEN,
        "inline UNSIGNED_SHORT indices",
    );

    // Generic attributes of an ARB vertex program from buffers: positions
    // (attribute 0) and a normalized UNSIGNED_BYTE 4 colour.
    d.submit(&[
        call(BIND_PROGRAM, &[VP, 1]),
        string(PASS),
        call(ENABLE, &[VP]),
    ]);
    let generic_positions = || buffered(16, descriptor(FLOAT, 2), 1, 0, 0);
    d.draws(
        vec![draw(
            QUADS,
            0,
            4,
            Indices::None,
            &[
                generic_positions(),
                buffered(17, descriptor(UNSIGNED_BYTE, 4) | NORMALIZED, 2, 0, 0),
            ],
        )],
        SLATE,
        SLATE,
        "generic attributes 0 and 1",
    );
    // A normalized UNSIGNED_SHORT 4 colour at offset 4, stride 12.
    let mut shorts = u16s(&[7, 7]);
    for _ in 0..4 {
        shorts.extend(u16s(&[0, 65535, 65535, 65535, 7, 7]));
    }
    d.submit(&[buffer(11, &shorts, STATIC_DRAW)]);
    d.draws(
        vec![draw(
            QUADS,
            0,
            4,
            Indices::Element(UNSIGNED_SHORT, 8, 6),
            &[
                generic_positions(),
                buffered(17, descriptor(UNSIGNED_SHORT, 4) | NORMALIZED, 11, 4, 12),
            ],
        )],
        [0, 255, 255, 255],
        [0, 255, 255, 255],
        "normalized UNSIGNED_SHORT generic attribute at offset 4, stride 12",
    );
    // The conventional position aliases attribute 0; FLOAT 4 attribute 1.
    d.draws(
        vec![draw(
            QUADS,
            0,
            8,
            Indices::None,
            &[position3(), buffered(17, descriptor(FLOAT, 4), 4, 16, 0)],
        )],
        SLATE,
        MAGENTA,
        "vertex position and FLOAT 4 generic attribute",
    );
    // Unnormalized SHORT 2 attribute 1: (1, 1, 0, 1) is yellow.
    d.submit(&[buffer(12, &u16s(&[1, 1].repeat(4)), STATIC_DRAW)]);
    d.draws(
        vec![draw(
            QUADS,
            0,
            4,
            Indices::None,
            &[
                generic_positions(),
                buffered(17, descriptor(SHORT, 2), 12, 0, 0),
            ],
        )],
        [255, 255, 0, 255],
        [255, 255, 0, 255],
        "unnormalized SHORT 2 generic attribute",
    );
    d.submit(&[call(DISABLE, &[VP])]);

    // Native contexts hold no buffer binding afterwards: client arrays and
    // immediate mode still read the record, not a stale buffer.
    d.draws(
        vec![client_quad(LEFT, BLUE)],
        BLUE,
        BLACK,
        "client draw after buffer draws",
    );
    d.draws(
        immediate_quad(RIGHT, [0., 1., 0., 1.]),
        BLACK,
        GREEN,
        "immediate mode after buffer draws",
    );
    assert_eq!(d.error(), 0);
}

#[test]
#[ignore = "requires native host GPU and vertex buffer object QEMU producer; diskless acceptance"]
fn qemu_buffer_draws_mix_client_sections_and_follow_buffer_updates() {
    let (mut d, _server, _directory) = start("vbo-mixed");
    // Positions of both halves (buffer 1) and their colours (buffer 2).
    d.submit(&[
        buffer(1, &halves(), STATIC_DRAW),
        buffer(2, &half_colors(RED, GREEN), STATIC_DRAW),
    ]);
    assert_eq!(d.error(), 0);

    // An array draw from base 4: buffer positions, a client colour section of
    // vertices 4-7.
    d.draws(
        vec![draw(
            QUADS,
            4,
            4,
            Indices::None,
            &[
                positions(1),
                client(1, descriptor(UNSIGNED_BYTE, 4), BLUE.repeat(4)),
            ],
        )],
        BLACK,
        BLUE,
        "buffer positions, client colours from base 4",
    );
    // Client positions of vertices 4-7, buffer colours read from vertex 4.
    d.draws(
        vec![draw(
            QUADS,
            4,
            4,
            Indices::None,
            &[client(0, descriptor(FLOAT, 2), f32s(&FULL)), colors(2)],
        )],
        GREEN,
        GREEN,
        "client positions, buffer colours from base 4",
    );
    // Indexed with inline indices inside the client vertices 4-7.
    d.draws(
        vec![draw(
            QUADS,
            4,
            4,
            Indices::Inline(UNSIGNED_SHORT, vec![4, 5, 6, 7]),
            &[
                positions(1),
                client(1, descriptor(UNSIGNED_BYTE, 4), MAGENTA.repeat(4)),
            ],
        )],
        BLACK,
        MAGENTA,
        "inline indices, client colours of vertices 4-7",
    );
    d.draws(
        vec![draw(
            QUADS,
            2,
            4,
            Indices::Inline(UNSIGNED_BYTE, vec![7, 6, 5, 4]),
            &[
                client(
                    0,
                    descriptor(FLOAT, 2),
                    f32s(&[&[9f32; 4][..], &RIGHT[..]].concat()),
                ),
                colors(2),
            ],
        )],
        BLACK,
        GREEN,
        "inline indices, client positions of vertices 2-7",
    );
    // Indexed through an element buffer, client colours of vertices 4-7.
    d.submit(&[buffer(3, &u32s(&[0, 4, 5, 6, 7]), STATIC_DRAW)]);
    d.draws(
        vec![draw(
            QUADS,
            4,
            4,
            Indices::Element(UNSIGNED_INT, 3, 4),
            &[
                positions(1),
                client(1, descriptor(UNSIGNED_BYTE, 4), SLATE.repeat(4)),
            ],
        )],
        BLACK,
        SLATE,
        "element buffer, client colours of vertices 4-7",
    );
    // An element buffer with every attribute in client sections.
    d.draws(
        vec![draw(
            QUADS,
            4,
            4,
            Indices::Element(UNSIGNED_INT, 3, 4),
            &[
                client(0, descriptor(FLOAT, 2), f32s(&LEFT)),
                client(1, descriptor(UNSIGNED_BYTE, 4), BLUE.repeat(4)),
            ],
        )],
        BLUE,
        BLACK,
        "element buffer, client sections only",
    );
    // Mixed generic attributes of a vertex program: client attribute 0 from
    // base 4, buffer attribute 1.
    d.submit(&[
        call(BIND_PROGRAM, &[VP, 1]),
        string(PASS),
        call(ENABLE, &[VP]),
    ]);
    d.draws(
        vec![draw(
            QUADS,
            4,
            4,
            Indices::None,
            &[
                client(16, descriptor(FLOAT, 2), f32s(&LEFT)),
                buffered(17, descriptor(UNSIGNED_BYTE, 4) | NORMALIZED, 2, 0, 0),
            ],
        )],
        GREEN,
        BLACK,
        "client generic attribute 0, buffer attribute 1, base 4",
    );
    // Element-buffer indices 4-7: the client attribute holds vertices 4-7.
    d.draws(
        vec![draw(
            QUADS,
            4,
            4,
            Indices::Element(UNSIGNED_INT, 3, 4),
            &[
                buffered(16, descriptor(FLOAT, 2), 1, 0, 0),
                client(
                    17,
                    descriptor(FLOAT, 4),
                    f32s(&[0.2, 0.4, 0.6, 1.].repeat(4)),
                ),
            ],
        )],
        BLACK,
        SLATE,
        "buffer generic attribute 0, client attribute 1, element buffer",
    );
    d.submit(&[call(DISABLE, &[VP])]);

    // BufferSubData between draws: a whole update, then only vertices 4-7.
    let both = || draw(QUADS, 0, 8, Indices::None, &[positions(1), colors(2)]);
    d.draws(vec![both()], RED, GREEN, "before BufferSubData");
    d.submit(&[sub_data(2, 0, &BLUE.repeat(8))]);
    d.draws(vec![both()], BLUE, BLUE, "whole BufferSubData");
    d.submit(&[sub_data(2, 16, &WHITE.repeat(4))]);
    d.draws(vec![both()], BLUE, WHITE, "BufferSubData of vertices 4-7");
    // An empty BufferSubData within the buffer changes nothing.
    d.submit(&[sub_data(2, 32, &[])]);
    d.draws(vec![both()], BLUE, WHITE, "empty BufferSubData at the end");

    // Map/unmap is frontend state: an unmap re-sends the whole buffer with
    // BufferSubData, and the next draw reads it.
    d.submit(&[
        sub_data(1, 0, &f32s(&[RIGHT, LEFT].concat())),
        sub_data(2, 0, &half_colors(GREEN, RED)),
    ]);
    d.draws(vec![both()], RED, GREEN, "whole-buffer unmap uploads");
    // BufferData again replaces size and contents, the name staying valid.
    d.submit(&[buffer(1, &f32s(&LEFT), DYNAMIC_DRAW)]);
    d.draws(
        vec![draw(QUADS, 0, 4, Indices::None, &[positions(1), colors(2)])],
        GREEN,
        BLACK,
        "BufferData replaces contents and size",
    );
    // Zero-defined storage, then filled by BufferSubData.
    d.submit(&[storage(2, 16, STATIC_DRAW)]);
    d.draws(
        vec![draw(QUADS, 0, 4, Indices::None, &[positions(1), colors(2)])],
        ZERO,
        BLACK,
        "zero-defined colours",
    );
    d.submit(&[sub_data(2, 0, &SLATE.repeat(4))]);
    d.draws(
        vec![draw(QUADS, 0, 4, Indices::None, &[positions(1), colors(2)])],
        SLATE,
        BLACK,
        "zero-defined storage filled",
    );

    // Zero-size storage: an object no attribute fits, which an empty
    // BufferSubData at offset zero accepts. Its arrays are never read
    // (9c93d17): as positions nothing draws, as colours the positions draw
    // in the current colour; neither is a GL error.
    d.submit(&[storage(4, 0, STATIC_DRAW), sub_data(4, 0, &[])]);
    assert_eq!(d.error(), 0);
    d.draws(
        vec![draw(QUADS, 0, 4, Indices::None, &[positions(4), colors(2)])],
        BLACK,
        BLACK,
        "a zero-size position buffer draws nothing",
    );
    d.draws(
        vec![
            current(MAGENTA),
            draw(QUADS, 0, 4, Indices::None, &[positions(1), colors(4)]),
        ],
        MAGENTA,
        BLACK,
        "a zero-size colour buffer: positions drawn in the current colour",
    );
    d.submit(&[sub_data(4, 0, &[1])]);
    assert_eq!(d.error(), INVALID_VALUE);
    d.submit(&[buffer(4, &f32s(&RIGHT), STATIC_DRAW)]);
    d.draws(
        vec![draw(QUADS, 0, 4, Indices::None, &[positions(4), colors(2)])],
        BLACK,
        SLATE,
        "the zero-size buffer given storage",
    );
}

#[test]
#[ignore = "requires native host GPU and vertex buffer object QEMU producer; diskless acceptance"]
fn qemu_buffer_failures_stay_gl_errors_and_the_context_survives() {
    let (mut d, _server, _directory) = start("vbo-errors");
    // Four positions (32 bytes), eight positions (64 bytes), four colours.
    d.submit(&[
        buffer(1, &f32s(&FULL), STATIC_DRAW),
        buffer(2, &halves(), STATIC_DRAW),
        buffer(3, &SLATE.repeat(4), STATIC_DRAW),
        buffer(4, &u16s(&[0, 1, 2, 3]), STATIC_DRAW),
        buffer(5, &u16s(&[0, 1, 2, 7]), STATIC_DRAW),
    ]);
    assert_eq!(d.error(), 0);
    let full = |p: Slot| draw(QUADS, 0, 4, Indices::None, &[p, colors(3)]);

    // Attribute reads exactly to the end of the buffer draw; one beyond is unread.
    d.draws(
        vec![full(buffered(0, descriptor(FLOAT, 2), 1, 0, 8))],
        SLATE,
        SLATE,
        "stride 8 to the end",
    );
    d.submit(&[buffer(
        6,
        &[vec![0u8; 8], f32s(&FULL)].concat(),
        STATIC_DRAW,
    )]);
    d.draws(
        vec![full(buffered(0, descriptor(FLOAT, 2), 6, 8, 0))],
        SLATE,
        SLATE,
        "offset 8 to the end",
    );
    // Since 9c93d17 a buffered array whose buffer does not hold every drawn
    // vertex is never read: it stays disabled and the draw proceeds without
    // a GL error. Without positions nothing draws; an unread colour array
    // leaves the current colour.
    for (records, label) in [
        (
            vec![full(buffered(0, descriptor(FLOAT, 2), 1, 8, 0))],
            "offset beyond the buffer",
        ),
        (
            vec![full(buffered(0, descriptor(FLOAT, 2), 1, 0, 12))],
            "stride beyond the buffer",
        ),
        (
            vec![full(buffered(0, descriptor(FLOAT, 2), 1, 40, 0))],
            "offset past the end",
        ),
        (
            vec![draw(QUADS, 1, 4, Indices::None, &[positions(1), colors(3)])],
            "first vertex beyond the buffer",
        ),
        (
            vec![draw(
                QUADS,
                0,
                4,
                Indices::Element(UNSIGNED_SHORT, 5, 0),
                &[positions(1), colors(3)],
            )],
            "element-buffer index beyond the attribute buffer",
        ),
        (
            vec![draw(
                QUADS,
                0,
                4,
                Indices::Inline(UNSIGNED_SHORT, vec![0, 1, 2, 7]),
                &[positions(1), colors(3)],
            )],
            "inline index beyond the attribute buffer",
        ),
    ] {
        d.draws(records, BLACK, BLACK, &format!("{label}: positions unread"));
    }
    d.draws(
        vec![
            current(MAGENTA),
            draw(
                QUADS,
                0,
                4,
                Indices::None,
                &[
                    positions(1),
                    buffered(1, descriptor(UNSIGNED_BYTE, 4), 3, 4, 0),
                ],
            ),
        ],
        MAGENTA,
        MAGENTA,
        "colour offset beyond the buffer: positions drawn in the current colour",
    );
    // Unread slots are disabled for their draw only: the same buffers that
    // fit draw again.
    d.draws(
        vec![full(positions(1))],
        SLATE,
        SLATE,
        "fitting buffers after unread ones",
    );

    // An element range beyond its buffer: exactly to the end draws.
    d.draws(
        vec![draw(
            QUADS,
            0,
            4,
            Indices::Element(UNSIGNED_SHORT, 4, 0),
            &[positions(1), colors(3)],
        )],
        SLATE,
        SLATE,
        "element range to the end",
    );
    d.fails(
        vec![draw(
            QUADS,
            0,
            4,
            Indices::Element(UNSIGNED_SHORT, 4, 2),
            &[positions(1), colors(3)],
        )],
        INVALID_OPERATION,
        "element range beyond the buffer",
    );
    d.fails(
        vec![draw(
            QUADS,
            0,
            1,
            Indices::Element(UNSIGNED_INT, 4, 8),
            &[positions(1), colors(3)],
        )],
        INVALID_OPERATION,
        "element offset at the end",
    );
    // An element-buffer index outside the client vertices.
    let client_colors = || client(1, descriptor(UNSIGNED_BYTE, 4), GREEN.repeat(4));
    d.draws(
        vec![draw(
            QUADS,
            0,
            4,
            Indices::Element(UNSIGNED_SHORT, 4, 0),
            &[positions(2), client_colors()],
        )],
        GREEN,
        BLACK,
        "element indices within the client vertices",
    );
    d.fails(
        vec![draw(
            QUADS,
            0,
            4,
            Indices::Element(UNSIGNED_SHORT, 5, 0),
            &[positions(2), client_colors()],
        )],
        INVALID_OPERATION,
        "element index above the client vertices",
    );
    d.fails(
        vec![draw(
            QUADS,
            1,
            4,
            Indices::Element(UNSIGNED_SHORT, 4, 0),
            &[positions(2), client_colors()],
        )],
        INVALID_OPERATION,
        "element index below the client base",
    );

    // Missing buffers: an attribute, a colour and an element buffer name.
    d.fails(
        vec![full(positions(77))],
        INVALID_OPERATION,
        "a missing attribute buffer",
    );
    d.fails(
        vec![draw(
            QUADS,
            0,
            4,
            Indices::None,
            &[positions(1), colors(78)],
        )],
        INVALID_OPERATION,
        "a missing colour buffer",
    );
    d.fails(
        vec![draw(
            QUADS,
            0,
            4,
            Indices::Element(UNSIGNED_SHORT, 79, 0),
            &[positions(1), colors(3)],
        )],
        INVALID_OPERATION,
        "a missing element buffer",
    );
    // A draw of no vertices reads no buffer at all.
    d.draws(
        vec![draw(QUADS, 0, 0, Indices::None, &[positions(77)])],
        BLACK,
        BLACK,
        "count zero",
    );

    // BufferSubData beyond the buffer or of an unknown name is INVALID_VALUE
    // and stores nothing.
    for (records, label) in [
        (
            vec![sub_data(3, 12, &BLUE.repeat(2))],
            "subdata beyond the buffer",
        ),
        (vec![sub_data(3, 17, &[])], "an empty subdata past the end"),
        (vec![sub_data(80, 0, &BLUE)], "subdata of an unknown name"),
    ] {
        d.submit(&records);
        assert_eq!(d.error(), INVALID_VALUE, "{label}");
        assert_eq!(d.error(), 0, "{label}");
    }
    d.draws(
        vec![full(positions(1))],
        SLATE,
        SLATE,
        "unchanged by failed subdata",
    );

    // Deletion: drawing a deleted name is INVALID_OPERATION; deleting zero
    // or an unknown name is not an error; the name can be created again.
    d.submit(&[delete(&[0, 81, 1])]);
    assert_eq!(d.error(), 0);
    d.fails(
        vec![full(positions(1))],
        INVALID_OPERATION,
        "a deleted buffer",
    );
    d.submit(&[sub_data(1, 0, &[0; 4])]);
    assert_eq!(d.error(), INVALID_VALUE, "subdata of a deleted buffer");
    d.submit(&[buffer(1, &f32s(&LEFT), STATIC_DRAW)]);
    d.draws(
        vec![full(positions(1))],
        SLATE,
        BLACK,
        "a deleted name created again",
    );

    // Budget: DG_GL_MAX_BUFFER_BYTES of zero-defined storage fits once; a
    // second exhausts the shared resource budget, OUT_OF_MEMORY, and leaves
    // that buffer an empty object.
    d.submit(&[storage(60, MAX_BUFFER_BYTES, STATIC_DRAW)]);
    assert_eq!(d.error(), 0);
    d.draws(
        vec![draw(
            QUADS,
            0,
            4,
            Indices::None,
            // Buffer 1 now covers the left half only; buffer 6 the drawable.
            &[buffered(0, descriptor(FLOAT, 2), 6, 8, 0), colors(60)],
        )],
        ZERO,
        ZERO,
        "zero-defined maximum buffer",
    );
    d.submit(&[storage(61, MAX_BUFFER_BYTES, STATIC_DRAW)]);
    assert_eq!(d.error(), OUT_OF_MEMORY);
    assert_eq!(d.error(), 0);
    // An empty object still exists: its positions are unread, nothing draws.
    d.draws(
        vec![full(positions(61))],
        BLACK,
        BLACK,
        "an exhausted buffer is empty",
    );
    d.submit(&[sub_data(61, 0, &[0; 4])]);
    assert_eq!(d.error(), INVALID_VALUE);
    // Re-specifying an existing buffer beyond the budget empties it.
    d.submit(&[storage(1, MAX_BUFFER_BYTES, STATIC_DRAW)]);
    assert_eq!(d.error(), OUT_OF_MEMORY);
    d.draws(
        vec![full(positions(1))],
        BLACK,
        BLACK,
        "a failed replacement leaves no store",
    );
    // Deleting the large buffer returns its budget.
    d.submit(&[
        delete(&[60]),
        storage(61, MAX_BUFFER_BYTES, STATIC_DRAW),
        buffer(1, &f32s(&FULL), STATIC_DRAW),
    ]);
    assert_eq!(d.error(), 0);
    d.draws(
        vec![full(positions(1))],
        SLATE,
        SLATE,
        "after the budget returned",
    );
    d.submit(&[delete(&[61])]);
    assert_eq!(d.error(), 0);
}

#[test]
#[ignore = "requires native host GPU and vertex buffer object QEMU producer; diskless acceptance"]
fn qemu_buffers_share_between_contexts_and_stay_out_of_display_lists() {
    let (mut d, _server, _directory) = start("vbo-shared");
    d.submit(&[
        buffer(1, &f32s(&FULL), STATIC_DRAW),
        buffer(2, &SLATE.repeat(4), STATIC_DRAW),
    ]);
    let full = || draw(QUADS, 0, 4, Indices::None, &[positions(1), colors(2)]);

    // Context 2 shares context 1's buffers (wglShareLists); context 3 does not.
    d.submit_for(2, &[(1, vec![1]), (3, vec![8, 8]), (5, vec![])]);
    d.submit_for(3, &[(1, vec![0]), (3, vec![8, 8]), (5, vec![])]);
    let mut records = clear();
    records.push(full());
    d.submit_for(2, &records);
    assert_eq!(d.error_for(2), 0);
    d.expect_for(2, SLATE, "a shared buffer");
    d.submit_for(3, &records);
    assert_eq!(
        d.error_for(3),
        INVALID_OPERATION,
        "an unshared context has no buffer 1"
    );
    d.expect_for(3, BLACK, "nothing drawn from an unshared name");
    // Context 3's own buffers of the same names are separate objects.
    d.submit_for(
        3,
        &[
            buffer(1, &f32s(&LEFT), STATIC_DRAW),
            buffer(2, &BLUE.repeat(4), STATIC_DRAW),
        ],
    );
    d.submit_for(3, &records);
    assert_eq!(d.error_for(3), 0);
    d.halves_for(3, BLUE, BLACK, "context 3's own buffers");
    d.draws(vec![full()], SLATE, SLATE, "context 1 keeps its buffers");
    // An update in context 2 is visible to context 1.
    d.submit_for(2, &[sub_data(2, 0, &GREEN.repeat(4))]);
    d.draws(vec![full()], GREEN, GREEN, "a shared update");
    // Deletion in context 2 deletes the shared object.
    d.submit_for(2, &[delete(&[2])]);
    assert_eq!(d.error_for(2), 0);
    d.fails(
        vec![full()],
        INVALID_OPERATION,
        "deleted through the sharing context",
    );
    d.submit_for(3, &records);
    assert_eq!(d.error_for(3), 0);
    d.halves_for(3, BLUE, BLACK, "context 3 unaffected");
    d.submit(&[buffer(2, &SLATE.repeat(4), STATIC_DRAW)]);
    // Releasing the last context of a share group releases its buffers.
    d.submit_for(3, &[(2, vec![])]);
    d.submit_for(2, &[(2, vec![])]);
    d.draws(
        vec![full()],
        SLATE,
        SLATE,
        "after other contexts were destroyed",
    );

    // Display lists: buffer commands execute immediately and are never
    // recorded; a buffer draw compiles as INVALID_OPERATION and draws
    // nothing; client draws compile as usual.
    let list = words(&d.query(GEN_LISTS, &[3]).1)[1];
    assert_ne!(list, 0);
    assert_eq!(d.word(NEW_LIST, &[list, COMPILE]), 0);
    d.submit(&[
        buffer(3, &f32s(&FULL), STATIC_DRAW),
        buffer(4, &RED.repeat(4), STATIC_DRAW),
        client_quad(LEFT, GREEN),
        draw(QUADS, 0, 4, Indices::None, &[positions(3), colors(4)]),
    ]);
    assert_eq!(d.word(END_LIST, &[]), 0);
    assert_eq!(d.error(), 0, "compiled without an error");
    let red = || draw(QUADS, 0, 4, Indices::None, &[positions(3), colors(4)]);
    d.draws(vec![red()], RED, RED, "BufferData executed while compiling");
    d.submit(&[sub_data(4, 0, &BLUE.repeat(4))]);
    let mut records = clear();
    records.push(call(CALL_LIST, &[list]));
    d.submit(&records);
    assert_eq!(d.error(), INVALID_OPERATION, "the compiled buffer draw");
    assert_eq!(d.error(), 0);
    d.halves(GREEN, BLACK, "the client draw only");
    d.draws(vec![red()], BLUE, BLUE, "BufferData was not replayed");

    // BufferSubData and DeleteBuffers inside a list execute immediately.
    assert_eq!(d.word(NEW_LIST, &[list + 1, COMPILE]), 0);
    d.submit(&[sub_data(4, 0, &WHITE.repeat(4)), delete(&[3])]);
    assert_eq!(d.word(END_LIST, &[]), 0);
    assert_eq!(d.error(), 0);
    d.fails(vec![red()], INVALID_OPERATION, "deleted while compiling");
    d.submit(&[buffer(3, &f32s(&FULL), STATIC_DRAW)]);
    d.draws(
        vec![red()],
        WHITE,
        WHITE,
        "BufferSubData executed while compiling",
    );
    d.submit(&[
        sub_data(4, 0, &GREEN.repeat(4)),
        call(CALL_LIST, &[list + 1]),
    ]);
    assert_eq!(d.error(), 0);
    d.draws(vec![red()], GREEN, GREEN, "neither command was replayed");

    // COMPILE_AND_EXECUTE: the buffer draw fails now and again on replay,
    // around a client draw that executes and replays.
    assert_eq!(d.word(NEW_LIST, &[list + 2, COMPILE_AND_EXECUTE]), 0);
    let mut records = clear();
    records.extend([red(), client_quad(RIGHT, BLUE)]);
    d.submit(&records);
    assert_eq!(d.word(END_LIST, &[]), 0);
    assert_eq!(d.error(), INVALID_OPERATION, "executed while compiling");
    assert_eq!(d.error(), 0);
    d.halves(BLACK, BLUE, "only the client draw executed");
    let mut records = clear();
    records.push(call(CALL_LIST, &[list + 2]));
    d.submit(&records);
    assert_eq!(d.error(), INVALID_OPERATION, "replayed");
    assert_eq!(d.error(), 0);
    d.halves(BLACK, BLUE, "only the client draw replayed");
    d.draws(
        vec![red()],
        GREEN,
        GREEN,
        "buffer draws outside lists still work",
    );
}
