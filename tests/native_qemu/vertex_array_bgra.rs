//! ARB_vertex_array_bgra through the device: the BGRA buffer draw
//! (DrawRangeElementsEXT) reading BGRA colours, secondary colours and generic
//! attributes of an ARB vertex program from buffers, tight and interleaved at
//! offsets and strides, from a nonzero first vertex, through element buffers
//! and inline indices, beside RGBA buffer slots and RGBA client sections;
//! the forms the frontend sends for BGRA client memory (reordered RGBA
//! sections) and ArrayElement (scalar colours) drawing the same pixels; reads
//! beyond a buffer and missing buffers as GL errors after which the context
//! keeps drawing; and display lists, which record the BGRA buffer draw as
//! INVALID_OPERATION while compiling the frontend's client forms. Every colour
//! has different red and blue, so a byte-order error shows. No batch in this
//! file may fail.
use super::fragment_program::{Device, start};
use super::textures::{call, data_call, words};

const BUFFER_DATA: u32 = 137; // BufferDataARB
const BUFFER_DRAW: u32 = 501; // the buffer draw (DrawRangeElements)
const BGRA_DRAW: u32 = 503; // the BGRA buffer draw (DrawRangeElementsEXT)
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

const VP: u32 = 0x8620;
const ASCII: u32 = 0x8875;
const COLOR_SUM: u32 = 0x8458;
const COMPILE: u32 = 0x1300;
const COMPILE_AND_EXECUTE: u32 = 0x1301;
const INVALID_OPERATION: u32 = 0x0502;
const STATIC_DRAW: u32 = 0x88e4;
const UNSIGNED_BYTE: u32 = 0x1401;
const UNSIGNED_SHORT: u32 = 0x1403;
const FLOAT: u32 = 0x1406;
const NORMALIZED: u32 = 0x0100_0000;
const BGRA: u32 = 0x0200_0000;
const ARRAY_RAW: u32 = 0x8000_0000;
const QUADS: u32 = 7;

const BLACK: [u8; 4] = [0, 0, 0, 255];
const SLATE: [u8; 4] = [51, 102, 153, 255];
/// Colours whose red and blue differ.
const AMBER: [u8; 4] = [0xe0, 0x80, 0x20, 0xff];
const TEAL: [u8; 4] = [0x10, 0x90, 0xd0, 0xff];
const PLUM: [u8; 4] = [0x90, 0x30, 0x60, 0xff];

/// Counter-clockwise corners of the drawable and of its halves.
const FULL: [f32; 8] = [-1., -1., 1., -1., 1., 1., -1., 1.];
const LEFT: [f32; 8] = [-1., -1., 0., -1., 0., 1., -1., 1.];
const RIGHT: [f32; 8] = [0., -1., 1., -1., 1., 1., 0., 1.];

/// Clip-space position from attribute 0, colour from attribute 1.
const PASS: &[u8] = b"!!ARBvp1.0\nMOV result.position, vertex.attrib[0];\n\
MOV result.color, vertex.attrib[1];\nEND";

fn f(v: f32) -> u32 {
    v.to_bits()
}
fn f32s(values: &[f32]) -> Vec<u8> {
    values.iter().flat_map(|v| v.to_le_bytes()).collect()
}
/// The memory bytes of a BGRA array element that reads as `rgba`.
fn bgra(rgba: [u8; 4]) -> [u8; 4] {
    [rgba[2], rgba[1], rgba[0], rgba[3]]
}
/// `left` for vertices 0-3, `right` for vertices 4-7, each as BGRA bytes.
fn bgra_halves(left: [u8; 4], right: [u8; 4]) -> Vec<u8> {
    [bgra(left).repeat(4), bgra(right).repeat(4)].concat()
}
fn descriptor(kind: u32, size: u32) -> u32 {
    kind | size << 16
}
fn unit(kind: u32) -> usize {
    match kind {
        UNSIGNED_BYTE => 1,
        UNSIGNED_SHORT => 2,
        _ => 4,
    }
}
fn buffer(name: u32, contents: &[u8]) -> (u32, Vec<u32>) {
    data_call(
        BUFFER_DATA,
        &[name, contents.len() as u32, STATIC_DRAW],
        contents,
    )
}
fn clear() -> Vec<(u32, Vec<u32>)> {
    vec![
        call(CLEAR_COLOR, &[f(0.), f(0.), f(0.), f(1.)]),
        call(CLEAR, &[0x4000]),
    ]
}

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
/// A BGRA colour (slot 1) or secondary colour (slot 4) buffer slot.
fn bgra_slot(slot: usize, name: u32, offset: u32, stride: u32) -> Slot {
    buffered(
        slot,
        descriptor(UNSIGNED_BYTE, 4) | BGRA,
        name,
        offset,
        stride,
    )
}
enum Indices {
    None,
    /// Inline UNSIGNED_SHORT indices.
    Inline(Vec<u16>),
    /// An UNSIGNED_SHORT element buffer and byte offset.
    Element(u32, u32),
}
/// A buffer draw record of `function` (gl.h): ten scalar words, thirty-two
/// descriptors, the references in slot order, the client sections in slot
/// order at their component alignment, then inline indices, zero padded.
fn draw(function: u32, base: u32, count: u32, indices: Indices, slots: &[Slot]) -> (u32, Vec<u32>) {
    let mut sorted: Vec<&Slot> = slots.iter().collect();
    sorted.sort_by_key(|s| s.slot);
    let mut descriptors = [0u32; 32];
    let (mut mask, mut generic, mut buffers) = (ARRAY_RAW, 0u32, 0u32);
    let mut vertices = 0;
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
                vertices = (data.len() / element) as u32;
            }
        }
    }
    let mut bytes: Vec<u8> = descriptors.iter().flat_map(|d| d.to_le_bytes()).collect();
    for s in &sorted {
        if let Source::Buffer(name, offset, stride) = s.source {
            bytes.extend([name, offset, stride].iter().flat_map(|w| w.to_le_bytes()));
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
        Indices::Inline(values) => {
            bytes.resize(bytes.len().div_ceil(2) * 2, 0);
            bytes.extend(values.iter().flat_map(|v| v.to_le_bytes()));
            (UNSIGNED_SHORT, 0, 0)
        }
        Indices::Element(name, offset) => (UNSIGNED_SHORT, name, offset),
    };
    bytes.resize(bytes.len().div_ceil(4) * 4, 0);
    data_call(
        function,
        &[
            QUADS,
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
/// The frontend's form of a BGRA client draw: a seven-slot DrawArrays with
/// the colours reordered into an RGBA section.
fn client_quads(corners: &[f32], rgba: &[[u8; 4]]) -> (u32, Vec<u32>) {
    let n = (corners.len() / 2) as u32;
    let mut bytes = vec![0u8; 28];
    bytes[..4].copy_from_slice(&descriptor(FLOAT, 2).to_le_bytes());
    bytes[4..8].copy_from_slice(&descriptor(UNSIGNED_BYTE, 4).to_le_bytes());
    bytes.extend(f32s(corners));
    for c in rgba {
        bytes.extend(c.repeat(4));
    }
    data_call(DRAW_ARRAYS, &[QUADS, 0, n, ARRAY_RAW | 3], &bytes)
}
/// The frontend's ArrayElement form: one Color4f before each vertex.
fn element_quad(corners: [f32; 8], rgba: [u8; 4]) -> Vec<(u32, Vec<u32>)> {
    let mut out = vec![call(BEGIN, &[QUADS])];
    for v in corners.chunks(2) {
        out.push(call(COLOR4F, &rgba.map(|c| f(f32::from(c) / 255.))));
        out.push(call(VERTEX2F, &[f(v[0]), f(v[1])]));
    }
    out.push(call(END, &[]));
    out
}

trait Halves {
    fn halves(&mut self, left: [u8; 4], right: [u8; 4], label: &str);
    /// Clear, then `records`, then expect the halves and no GL error.
    fn draws(&mut self, records: Vec<(u32, Vec<u32>)>, left: [u8; 4], right: [u8; 4], label: &str);
    /// Clear, then `records`, which must raise exactly `code` and draw
    /// nothing; then a client draw proves the context still renders.
    fn fails(&mut self, records: Vec<(u32, Vec<u32>)>, code: u32, label: &str);
}
impl Halves for Device {
    fn halves(&mut self, left: [u8; 4], right: [u8; 4], label: &str) {
        for (x, y) in [(0, 0), (3, 7), (1, 4)] {
            assert_eq!(self.pixel_for(1, x, y), left, "{label}: left at {x},{y}");
        }
        for (x, y) in [(4, 0), (7, 7), (6, 3)] {
            assert_eq!(self.pixel_for(1, x, y), right, "{label}: right at {x},{y}");
        }
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
        self.submit(&[client_quads(&FULL, &[SLATE])]);
        assert_eq!(self.error(), 0, "{label}: then a client draw");
        self.expect(SLATE, label);
    }
}

/// Buffer 1: FLOAT 2 positions of the left then the right quad.
fn halves_buffer() -> (u32, Vec<u32>) {
    buffer(1, &f32s(&[LEFT, RIGHT].concat()))
}

#[test]
#[ignore = "requires native host GPU and BGRA vertex array QEMU producer; diskless acceptance"]
fn qemu_bgra_buffer_slots_read_colours_secondary_colours_and_generic_attributes() {
    let (mut d, _server, _directory) = start("bgra");
    assert_ne!(d.qemu.read(0x1008) & 0x400_0000, 0);
    d.qemu.write(0x1130, BGRA_DRAW);
    assert_eq!(d.qemu.read(0x1134), 0x8000_000a);
    assert_eq!(d.error(), 0);
    d.submit(&[halves_buffer(), buffer(2, &bgra_halves(AMBER, TEAL))]);
    assert_eq!(d.error(), 0);

    // Tight BGRA colours read as RGBA; the same bytes as an RGBA buffer slot
    // in the buffer draw swap red and blue.
    d.draws(
        vec![draw(
            BGRA_DRAW,
            0,
            8,
            Indices::None,
            &[positions(1), bgra_slot(1, 2, 0, 0)],
        )],
        AMBER,
        TEAL,
        "tight BGRA colours",
    );
    d.draws(
        vec![draw(
            BUFFER_DRAW,
            0,
            8,
            Indices::None,
            &[
                positions(1),
                buffered(1, descriptor(UNSIGNED_BYTE, 4), 2, 0, 0),
            ],
        )],
        bgra(AMBER),
        bgra(TEAL),
        "the same bytes as RGBA",
    );

    // Interleaved {x, y, pad, bgra} of stride 16 after a 12-byte prefix, from
    // first vertex 4.
    let mut interleaved = vec![0xee; 12];
    for (n, v) in [LEFT, RIGHT].concat().chunks(2).enumerate() {
        interleaved.extend(f32s(v));
        interleaved.extend([0xee; 4]);
        interleaved.extend(bgra(if n < 4 { PLUM } else { TEAL }));
    }
    d.submit(&[buffer(3, &interleaved)]);
    let strided = || {
        [
            buffered(0, descriptor(FLOAT, 2), 3, 12, 16),
            bgra_slot(1, 3, 24, 16),
        ]
    };
    d.draws(
        vec![draw(BGRA_DRAW, 0, 8, Indices::None, &strided())],
        PLUM,
        TEAL,
        "interleaved",
    );
    d.draws(
        vec![draw(BGRA_DRAW, 4, 4, Indices::None, &strided())],
        BLACK,
        TEAL,
        "interleaved from first vertex 4",
    );

    // Mixed with COLOR_SUM: an RGBA client colour plus a BGRA buffer
    // secondary colour, then a BGRA buffer colour plus an RGB client
    // secondary colour (the frontend's form of a BGRA client one).
    let primary = [0x10u8, 0x20, 0x30, 0xff];
    let secondary = [0x40u8, 0x06, 0x05, 0x00];
    let sum = [0x50, 0x26, 0x35, 0xff];
    d.submit(&[
        buffer(4, &bgra(secondary).repeat(8)),
        call(ENABLE, &[COLOR_SUM]),
    ]);
    d.draws(
        vec![draw(
            BGRA_DRAW,
            0,
            8,
            Indices::None,
            &[
                positions(1),
                client(1, descriptor(UNSIGNED_BYTE, 4), primary.repeat(8)),
                bgra_slot(4, 4, 0, 0),
            ],
        )],
        sum,
        sum,
        "RGBA client colour plus BGRA buffer secondary colour",
    );
    d.submit(&[buffer(5, &bgra(primary).repeat(8))]);
    d.draws(
        vec![draw(
            BGRA_DRAW,
            0,
            8,
            Indices::None,
            &[
                positions(1),
                bgra_slot(1, 5, 0, 0),
                client(4, descriptor(UNSIGNED_BYTE, 3), secondary[..3].repeat(8)),
            ],
        )],
        sum,
        sum,
        "BGRA buffer colour plus RGB client secondary colour",
    );
    // BGRA colour and secondary colour from one interleaved buffer.
    let mut both = Vec::new();
    for _ in 0..8 {
        both.extend(bgra(primary));
        both.extend(bgra(secondary));
    }
    d.submit(&[buffer(6, &both)]);
    d.draws(
        vec![draw(
            BGRA_DRAW,
            0,
            8,
            Indices::None,
            &[positions(1), bgra_slot(1, 6, 0, 8), bgra_slot(4, 6, 4, 8)],
        )],
        sum,
        sum,
        "BGRA colour and secondary colour interleaved",
    );
    d.submit(&[call(DISABLE, &[COLOR_SUM])]);

    // Element buffer 7 (UNSIGNED_SHORT at offset 2) and inline indices with a
    // client position section beside BGRA buffer colours.
    let indices: Vec<u8> = [0xeeeeu16, 4, 5, 6, 7, 0, 1, 2, 3]
        .iter()
        .flat_map(|i| i.to_le_bytes())
        .collect();
    d.submit(&[buffer(7, &indices)]);
    d.draws(
        vec![draw(
            BGRA_DRAW,
            0,
            8,
            Indices::Element(7, 2),
            &[positions(1), bgra_slot(1, 2, 0, 0)],
        )],
        AMBER,
        TEAL,
        "element buffer",
    );
    d.draws(
        vec![draw(
            BGRA_DRAW,
            4,
            4,
            Indices::Inline(vec![6, 7, 4, 5]),
            &[
                client(0, descriptor(FLOAT, 2), f32s(&RIGHT)),
                bgra_slot(1, 2, 0, 0),
            ],
        )],
        BLACK,
        TEAL,
        "inline indices and a client position section",
    );

    // A vertex program reads generic attribute 1 as BGRA (normalized) beside
    // generic attribute 0 from buffer 1.
    d.submit(&[
        call(BIND_PROGRAM, &[VP, 1]),
        data_call(PROGRAM_STRING, &[VP, ASCII, PASS.len() as u32], PASS),
        call(ENABLE, &[VP]),
    ]);
    let generic = |colour: Slot| {
        draw(
            BGRA_DRAW,
            0,
            8,
            Indices::None,
            &[buffered(16, descriptor(FLOAT, 2), 1, 0, 0), colour],
        )
    };
    d.draws(
        vec![generic(buffered(
            17,
            descriptor(UNSIGNED_BYTE, 4) | NORMALIZED | BGRA,
            2,
            0,
            0,
        ))],
        AMBER,
        TEAL,
        "generic BGRA attribute",
    );
    d.draws(
        vec![generic(buffered(
            17,
            descriptor(UNSIGNED_BYTE, 4) | NORMALIZED,
            2,
            0,
            0,
        ))],
        bgra(AMBER),
        bgra(TEAL),
        "the same bytes as a generic RGBA attribute",
    );
    d.draws(
        vec![generic(client(
            17,
            descriptor(UNSIGNED_BYTE, 4) | NORMALIZED,
            [AMBER.repeat(4), TEAL.repeat(4)].concat(),
        ))],
        AMBER,
        TEAL,
        "the frontend's reordered generic client section",
    );
    d.submit(&[call(DISABLE, &[VP])]);
    assert_eq!(d.error(), 0);
}

#[test]
#[ignore = "requires native host GPU and BGRA vertex array QEMU producer; diskless acceptance"]
fn qemu_bgra_buffer_draw_errors_lists_and_frontend_client_forms() {
    let (mut d, _server, _directory) = start("bgra-errors");
    d.submit(&[halves_buffer(), buffer(2, &bgra_halves(AMBER, TEAL))]);
    assert_eq!(d.error(), 0);
    let tight = || {
        draw(
            BGRA_DRAW,
            0,
            8,
            Indices::None,
            &[positions(1), bgra_slot(1, 2, 0, 0)],
        )
    };
    d.draws(vec![tight()], AMBER, TEAL, "BGRA buffer draw");
    // The frontend's forms of the same BGRA data in client memory and through
    // ArrayElement draw the same pixels.
    d.draws(
        vec![client_quads(&[LEFT, RIGHT].concat(), &[AMBER, TEAL])],
        AMBER,
        TEAL,
        "reordered client section",
    );
    let mut elements = element_quad(LEFT, AMBER);
    elements.extend(element_quad(RIGHT, TEAL));
    d.draws(elements, AMBER, TEAL, "ArrayElement colours");

    // A BGRA read one byte beyond its buffer, a missing buffer and a deleted
    // name are GL errors; the context keeps drawing.
    d.submit(&[buffer(8, &bgra_halves(AMBER, TEAL)[..31])]);
    d.fails(
        vec![draw(
            BGRA_DRAW,
            0,
            8,
            Indices::None,
            &[positions(1), bgra_slot(1, 8, 0, 0)],
        )],
        INVALID_OPERATION,
        "one byte short",
    );
    d.draws(
        vec![draw(
            BGRA_DRAW,
            0,
            7,
            Indices::None,
            &[positions(1), bgra_slot(1, 8, 0, 0)],
        )],
        AMBER,
        BLACK,
        "seven vertices fit (one quad)",
    );
    d.fails(
        vec![draw(
            BGRA_DRAW,
            0,
            8,
            Indices::None,
            &[positions(1), bgra_slot(1, 9, 0, 0)],
        )],
        INVALID_OPERATION,
        "missing buffer",
    );
    d.fails(
        vec![draw(
            BGRA_DRAW,
            0,
            8,
            Indices::None,
            &[positions(1), bgra_slot(1, 2, 4, 4)],
        )],
        INVALID_OPERATION,
        "offset and stride beyond",
    );
    d.draws(
        vec![tight()],
        AMBER,
        TEAL,
        "the BGRA buffer draw still works",
    );

    // Display lists: the BGRA buffer draw compiles as INVALID_OPERATION and
    // draws nothing; the frontend's reordered client draw compiles as usual.
    let list = words(&d.query(GEN_LISTS, &[2]).1)[1];
    assert_ne!(list, 0);
    assert_eq!(d.word(NEW_LIST, &[list, COMPILE]), 0);
    d.submit(&[client_quads(&LEFT, &[PLUM]), tight()]);
    assert_eq!(d.word(END_LIST, &[]), 0);
    assert_eq!(d.error(), 0, "compiled without an error");
    let mut records = clear();
    records.push(call(CALL_LIST, &[list]));
    d.submit(&records);
    assert_eq!(
        d.error(),
        INVALID_OPERATION,
        "the compiled BGRA buffer draw"
    );
    assert_eq!(d.error(), 0);
    d.halves(PLUM, BLACK, "the client draw only");
    assert_eq!(d.word(NEW_LIST, &[list + 1, COMPILE_AND_EXECUTE]), 0);
    let mut records = clear();
    records.extend([tight(), client_quads(&RIGHT, &[PLUM])]);
    d.submit(&records);
    assert_eq!(d.word(END_LIST, &[]), 0);
    assert_eq!(d.error(), INVALID_OPERATION, "executed while compiling");
    assert_eq!(d.error(), 0);
    d.halves(BLACK, PLUM, "only the client draw executed");
    let mut records = clear();
    records.push(call(CALL_LIST, &[list + 1]));
    d.submit(&records);
    assert_eq!(d.error(), INVALID_OPERATION, "replayed");
    assert_eq!(d.error(), 0);
    d.halves(BLACK, PLUM, "only the client draw replayed");
    d.draws(
        vec![tight()],
        AMBER,
        TEAL,
        "BGRA buffer draws outside lists still work",
    );
}
