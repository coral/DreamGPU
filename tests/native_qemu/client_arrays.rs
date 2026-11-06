//! Indexed client-array draws as the frontend sends them with buffer draws
//! (DG_CAP_GL_VERTEX_BUFFER_OBJECT): one buffer draw without buffer slots
//! that carries each array's vertices base..base+vertices-1 once and the
//! indices inline. Every image must equal, pixel for pixel, the per-index
//! expansion (DrawArrays/DrawArraysEXT/the attribute draw) that older hosts
//! and display lists receive: a Quake III-like multitexture batch (FLOAT 3
//! positions, UNSIGNED_BYTE colours, two units of FLOAT 2 coordinates,
//! UNSIGNED_INT triangles) drawn in two blended passes as a locked
//! EXT_compiled_vertex_array surface would be, and mixed formats (SHORT,
//! DOUBLE, BYTE normals with lighting, generic attributes of a vertex
//! program) through strips, fans, loops and points by UNSIGNED_BYTE and
//! UNSIGNED_SHORT indices from a nonzero base. Current values set before a
//! draw survive it as after the expansion. An inline index outside the client
//! vertices fails the whole batch (the frontend never sends one); a buffer
//! draw compiled into a display list is GL_INVALID_OPERATION while the
//! compiled expansion replays the same image.
use super::fragment_program::{Device, start};
use super::textures::{call, data_call, words};

const DRAW_ARRAYS: u32 = 0x1d5;
const DRAW_ARRAYS_EXT: u32 = 0x1d6;
const ATTRIBUTE_DRAW: u32 = 0xaec; // VertexAttribPointerARB
const BUFFER_DRAW: u32 = 0x1f5; // DrawRangeElements
const CLEAR_COLOR: u32 = 0x0a3;
const CLEAR: u32 = 0x09a;
const COLOR4F: u32 = 0x0db;
const ACTIVE: u32 = 0x006;
const MULTI: u32 = 0x5cf;
const BIND_TEXTURE: u32 = 0x04e;
const TEX_PARAMETER_I: u32 = 0x8df;
const TEX_IMAGE_2D: u32 = 0x8d4;
const TEX_ENV_I: u32 = 0x8c6;
const ENABLE: u32 = 0x209;
const DISABLE: u32 = 0x1c5;
const BLEND_FUNC: u32 = 0x078;
const GET_FLOAT: u32 = 0x305;
const READ_PIXELS: u32 = 0x7a4;
const GEN_LISTS: u32 = 0x29f;
const NEW_LIST: u32 = 0x638;
const END_LIST: u32 = 0x21b;
const CALL_LIST: u32 = 0x092;
const BIND_PROGRAM: u32 = 69;
const PROGRAM_STRING: u32 = 1766;

const TEXTURE0: u32 = 0x84c0;
const TEXTURE_2D: u32 = 0x0de1;
const BLEND: u32 = 0x0be2;
const LIGHTING: u32 = 0x0b50;
const LIGHT0: u32 = 0x4000;
const NORMALIZE: u32 = 0x0ba1;
const COLOR_MATERIAL: u32 = 0x0b57;
const VERTEX_PROGRAM: u32 = 0x8620;
const ASCII: u32 = 0x8875;
const CURRENT_COLOR: u32 = 0x0b00;
const CURRENT_COORDS: u32 = 0x0b03;
const COMPILE: u32 = 0x1300;
const INVALID_OPERATION: u32 = 0x0502;
const DG_GL_ERROR_BATCH: u32 = 1;

const BYTE: u32 = 0x1400;
const UNSIGNED_BYTE: u32 = 0x1401;
const SHORT: u32 = 0x1402;
const UNSIGNED_SHORT: u32 = 0x1403;
const UNSIGNED_INT: u32 = 0x1405;
const FLOAT: u32 = 0x1406;
const DOUBLE: u32 = 0x140a;
const NORMALIZED: u32 = 0x0100_0000;
const ARRAY_RAW: u32 = 0x8000_0000;

const POINTS: u32 = 0;
const LINE_LOOP: u32 = 2;
const TRIANGLES: u32 = 4;
const TRIANGLE_STRIP: u32 = 5;
const TRIANGLE_FAN: u32 = 6;
const QUADS: u32 = 7;

/// The drawable of context 2: large enough for a batch of small triangles.
const SIZE: u32 = 32;
const CONTEXT: u32 = 2;

fn f(v: f32) -> u32 {
    v.to_bits()
}
fn descriptor(kind: u32, size: u32) -> u32 {
    kind | size << 16
}
fn unit(kind: u32) -> usize {
    match kind {
        BYTE | UNSIGNED_BYTE => 1,
        SHORT | UNSIGNED_SHORT => 2,
        DOUBLE => 8,
        _ => 4,
    }
}

/// One enabled slot (0-15 DrawArraysEXT, 16-31 generic attributes 0-15) and
/// its vertices' wire bytes, tight.
struct Array {
    slot: usize,
    descriptor: u32,
    bytes: Vec<u8>,
}
impl Array {
    fn element(&self) -> usize {
        ((self.descriptor >> 16) & 255) as usize * unit(self.descriptor & 65535)
    }
}
fn array(slot: usize, kind: u32, size: u32, bytes: Vec<u8>) -> Array {
    Array {
        slot,
        descriptor: descriptor(kind, size),
        bytes,
    }
}
fn masks(arrays: &[Array]) -> (u32, u32) {
    let (mut mask, mut generic) = (ARRAY_RAW, 0);
    for a in arrays {
        if a.slot < 16 {
            mask |= 1 << a.slot;
        } else {
            generic |= 1 << (a.slot - 16);
        }
    }
    (mask, generic)
}
/// Descriptors of `slots` slots, then each slot's section of the vertices
/// `vertices` yields, at its component alignment.
fn sections(arrays: &[Array], slots: usize, vertices: &[usize]) -> Vec<u8> {
    let mut descriptors = vec![0u32; slots];
    for a in arrays {
        descriptors[a.slot] = a.descriptor;
    }
    let mut out: Vec<u8> = descriptors.iter().flat_map(|d| d.to_le_bytes()).collect();
    let mut sorted: Vec<&Array> = arrays.iter().collect();
    sorted.sort_by_key(|a| a.slot);
    for a in sorted {
        let (element, align) = (a.element(), unit(a.descriptor & 65535));
        out.resize(out.len().div_ceil(align) * align, 0);
        for &v in vertices {
            out.extend_from_slice(&a.bytes[v * element..(v + 1) * element]);
        }
    }
    out
}
/// The sections zero padded to a word.
fn payload(arrays: &[Array], slots: usize, vertices: &[usize]) -> Vec<u8> {
    let mut out = sections(arrays, slots, vertices);
    out.resize(out.len().div_ceil(4) * 4, 0);
    out
}
/// The per-index expansion of an indexed client draw, as ClientDraw sends it:
/// DrawArrays (seven slots), DrawArraysEXT (sixteen) or the attribute draw.
fn expanded(mode: u32, arrays: &[Array], indices: &[u32], base: u32) -> (u32, Vec<u32>) {
    let (mask, generic) = masks(arrays);
    let vertices: Vec<usize> = indices.iter().map(|&i| (i - base) as usize).collect();
    let count = indices.len() as u32;
    if generic != 0 {
        data_call(
            ATTRIBUTE_DRAW,
            &[mode, 0, count, mask, generic],
            &payload(arrays, 32, &vertices),
        )
    } else if mask & !0x8000_007f != 0 {
        data_call(
            DRAW_ARRAYS_EXT,
            &[mode, 0, count, mask],
            &payload(arrays, 16, &vertices),
        )
    } else {
        data_call(
            DRAW_ARRAYS,
            &[mode, 0, count, mask],
            &payload(arrays, 7, &vertices),
        )
    }
}
/// The buffer draw without buffer slots: client sections of the vertices
/// base.. and the inline indices of `kind`.
fn ranged(mode: u32, arrays: &[Array], indices: &[u32], base: u32, kind: u32) -> (u32, Vec<u32>) {
    let (mask, generic) = masks(arrays);
    let vertices = arrays[0].bytes.len() / arrays[0].element();
    let mut bytes = sections(arrays, 32, &(0..vertices).collect::<Vec<_>>());
    // The indices follow at their own alignment.
    let size = unit(kind);
    bytes.resize(bytes.len().div_ceil(size) * size, 0);
    for &i in indices {
        bytes.extend_from_slice(&i.to_le_bytes()[..size]);
    }
    bytes.resize(bytes.len().div_ceil(4) * 4, 0);
    data_call(
        BUFFER_DRAW,
        &[
            mode,
            base,
            indices.len() as u32,
            mask,
            generic,
            0,
            vertices as u32,
            kind,
            0,
            0,
        ],
        &bytes,
    )
}

fn clear() -> Vec<(u32, Vec<u32>)> {
    vec![
        call(CLEAR_COLOR, &[f(0.1), f(0.2), f(0.3), f(1.)]),
        call(CLEAR, &[0x4000]),
    ]
}
/// A size x size RGBA texture of `name` on `unit`, LINEAR, MODULATE.
fn texture(unit: u32, name: u32, size: u32, seed: u32) -> Vec<(u32, Vec<u32>)> {
    let texels: Vec<u8> = (0..size * size * 4)
        .map(|i| (i.wrapping_mul(seed) >> 1) as u8 | 0x40)
        .collect();
    vec![
        call(ACTIVE, &[TEXTURE0 + unit]),
        call(BIND_TEXTURE, &[TEXTURE_2D, name]),
        call(TEX_PARAMETER_I, &[TEXTURE_2D, 0x2801, 0x2601]),
        call(TEX_PARAMETER_I, &[TEXTURE_2D, 0x2800, 0x2601]),
        data_call(
            TEX_IMAGE_2D,
            &[TEXTURE_2D, 0, 0x1908, size, size, 0, 0x1908, 0x1401],
            &texels,
        ),
        call(ENABLE, &[TEXTURE_2D]),
        call(TEX_ENV_I, &[0x2300, 0x2200, 0x2100]),
    ]
}

/// Deterministic bytes and floats.
struct Noise(u32);
impl Noise {
    fn byte(&mut self) -> u8 {
        self.0 = self.0.wrapping_mul(1_103_515_245).wrapping_add(12345);
        (self.0 >> 16) as u8
    }
    fn bytes(&mut self, n: usize) -> Vec<u8> {
        (0..n).map(|_| self.byte()).collect()
    }
    fn unit(&mut self) -> f32 {
        f32::from(self.byte()) / 255.
    }
}
fn floats(values: impl IntoIterator<Item = f32>) -> Vec<u8> {
    values.into_iter().flat_map(f32::to_le_bytes).collect()
}

trait Image {
    /// Every pixel of context 2's drawable, bottom row first.
    fn image(&mut self) -> Vec<u8>;
    /// Clear, then `records`, without a GL error: the image.
    fn drawn(&mut self, records: Vec<(u32, Vec<u32>)>, label: &str) -> Vec<u8>;
}
impl Image for Device {
    fn image(&mut self) -> Vec<u8> {
        let mut out = Vec::new();
        for y in 0..SIZE {
            let (_, row) = self.query_for(CONTEXT, READ_PIXELS, &[0, y, SIZE | (1 << 16)]);
            assert_eq!(row.len(), SIZE as usize * 4);
            out.extend(row);
        }
        out
    }
    fn drawn(&mut self, records: Vec<(u32, Vec<u32>)>, label: &str) -> Vec<u8> {
        let mut all = clear();
        all.extend(records);
        self.submit_for(CONTEXT, &all);
        assert_eq!(self.error_for(CONTEXT), 0, "{label}");
        self.image()
    }
}
/// The expansion and the buffer draw of the same draw produce one image,
/// which shows more than the clear colour.
fn same(
    d: &mut Device,
    before: &[(u32, Vec<u32>)],
    old: (u32, Vec<u32>),
    new: (u32, Vec<u32>),
    label: &str,
) {
    let mut records = before.to_vec();
    records.push(old);
    let expected = d.drawn(records, label);
    let mut records = before.to_vec();
    records.push(new);
    let got = d.drawn(records, label);
    let distinct: std::collections::HashSet<&[u8]> = expected.chunks(4).collect();
    assert!(distinct.len() > 2, "{label}: the draw shows {distinct:?}");
    if got != expected {
        let first = got
            .chunks(4)
            .zip(expected.chunks(4))
            .position(|(a, b)| a != b)
            .unwrap();
        panic!(
            "{label}: pixel {},{} is {:?}, the expansion drew {:?}",
            first as u32 % SIZE,
            first as u32 / SIZE,
            &got[first * 4..first * 4 + 4],
            &expected[first * 4..first * 4 + 4]
        );
    }
}
fn open(d: &mut Device) {
    d.submit_for(CONTEXT, &[(1, vec![0]), (3, vec![SIZE, SIZE]), (5, vec![])]);
    assert_eq!(d.error_for(CONTEXT), 0);
}

/// A columns x rows grid of jittered vertices across the drawable with
/// colours and two coordinate sets; triangle indices from `base`.
struct Batch {
    positions: Vec<u8>,
    colors: Vec<u8>,
    texcoords: Vec<u8>,
    lightmaps: Vec<u8>,
    indices: Vec<u32>,
}
fn quake(columns: u32, rows: u32, base: u32) -> Batch {
    let mut noise = Noise(7);
    let mut positions = Vec::new();
    for y in 0..rows {
        for x in 0..columns {
            let jitter = |n: &mut Noise| (n.unit() - 0.5) * 0.04;
            positions.extend([
                -1.05 + 2.1 * x as f32 / (columns - 1) as f32 + jitter(&mut noise),
                -1.05 + 2.1 * y as f32 / (rows - 1) as f32 + jitter(&mut noise),
                noise.unit() - 0.5,
            ]);
        }
    }
    let vertices = (columns * rows) as usize;
    let texcoords = (0..vertices * 2)
        .map(|_| noise.unit() * 3. - 1.)
        .collect::<Vec<_>>();
    let lightmaps = (0..vertices * 2).map(|_| noise.unit()).collect::<Vec<_>>();
    let mut indices = Vec::new();
    for y in 0..rows - 1 {
        for x in 0..columns - 1 {
            let v = base + y * columns + x;
            indices.extend([v, v + columns, v + 1, v + 1, v + columns, v + columns + 1]);
        }
    }
    Batch {
        positions: floats(positions),
        colors: noise.bytes(vertices * 4),
        texcoords: floats(texcoords),
        lightmaps: floats(lightmaps),
        indices,
    }
}

#[test]
#[ignore = "requires native host GPU and vertex buffer object QEMU producer; diskless acceptance"]
fn qemu_client_buffer_draws_match_the_expansion_pixel_for_pixel() {
    let (mut d, _server, _directory) = start("ranged");
    assert_ne!(d.qemu.read(0x1008) & 0x100_0000, 0);
    open(&mut d);

    // The Quake III-like batch: 15 x 20 vertices from base 40, 1596
    // UNSIGNED_INT indices, two modulated units, then a second blended pass
    // with other colours, as two draws of one locked surface.
    let b = quake(15, 20, 40);
    let mut setup = texture(0, 11, 8, 2_654_435_761);
    setup.extend(texture(1, 12, 4, 40_503));
    setup.push(call(ACTIVE, &[TEXTURE0]));
    d.submit_for(CONTEXT, &setup);
    assert_eq!(d.error_for(CONTEXT), 0);
    let quake_arrays = |colors: &[u8]| {
        vec![
            array(0, FLOAT, 3, b.positions.clone()),
            array(1, UNSIGNED_BYTE, 4, colors.to_vec()),
            array(3, FLOAT, 2, b.texcoords.clone()),
            array(7, FLOAT, 2, b.lightmaps.clone()),
        ]
    };
    let first = quake_arrays(&b.colors);
    let mut noise = Noise(99);
    let second = quake_arrays(&noise.bytes(b.colors.len()));
    let (old, new) = (
        expanded(TRIANGLES, &first, &b.indices, 40),
        ranged(TRIANGLES, &first, &b.indices, 40, UNSIGNED_INT),
    );
    // 300 vertices of 32 bytes and the indices: a third of the expansion.
    assert_eq!(new.1.len() * 4, 48 + 128 + 300 * 32 + 1596 * 4);
    assert_eq!(old.1.len() * 4, 24 + 64 + 1596 * 32);
    same(
        &mut d,
        &[],
        old.clone(),
        new.clone(),
        "Quake III-like batch",
    );
    let blend = [call(ENABLE, &[BLEND]), call(BLEND_FUNC, &[0x0306, 0x0300])];
    // Each image starts unblended: the first pass replaces the clear colour.
    let mut passes = vec![call(DISABLE, &[BLEND]), old.clone()];
    passes.extend(blend.clone());
    passes.push(expanded(TRIANGLES, &second, &b.indices, 40));
    let expected = d.drawn(passes, "two expanded passes");
    let mut passes = vec![call(DISABLE, &[BLEND]), new.clone()];
    passes.extend(blend);
    passes.push(ranged(TRIANGLES, &second, &b.indices, 40, UNSIGNED_INT));
    assert!(
        d.drawn(passes, "two ranged passes") == expected,
        "two passes"
    );
    d.submit_for(CONTEXT, &[call(DISABLE, &[BLEND])]);

    // Current values set before the draw survive it on both paths.
    let current = |d: &mut Device| {
        let color = words(&d.query_for(CONTEXT, GET_FLOAT, &[CURRENT_COLOR]).1);
        d.submit_for(CONTEXT, &[call(ACTIVE, &[TEXTURE0 + 1])]);
        let coords = words(&d.query_for(CONTEXT, GET_FLOAT, &[CURRENT_COORDS]).1);
        d.submit_for(CONTEXT, &[call(ACTIVE, &[TEXTURE0])]);
        (color, coords)
    };
    let set = [
        call(COLOR4F, &[f(0.25), f(0.5), f(0.75), f(1.)]),
        call(MULTI, &[TEXTURE0 + 1, f(0.125), f(0.375), f(0.), f(1.)]),
    ];
    for draw in [old, new] {
        let mut records = set.to_vec();
        records.push(draw);
        d.submit_for(CONTEXT, &records);
        assert_eq!(
            current(&mut d),
            (
                [0.25f32, 0.5, 0.75, 1.].map(f32::to_bits).to_vec(),
                [0.125f32, 0.375, 0., 1.].map(f32::to_bits).to_vec()
            )
        );
    }
    let mut disable = vec![call(ACTIVE, &[TEXTURE0 + 1]), call(DISABLE, &[TEXTURE_2D])];
    disable.extend([call(ACTIVE, &[TEXTURE0]), call(DISABLE, &[TEXTURE_2D])]);
    d.submit_for(CONTEXT, &disable);

    // Mixed formats through every kind of primitive, by UNSIGNED_BYTE and
    // UNSIGNED_SHORT indices of 12 vertices from base 9: FLOAT 2 positions,
    // UNSIGNED_BYTE 3 colours, SHORT 4 and DOUBLE 2 coordinates.
    let mut noise = Noise(3);
    let vertices = 12;
    let corners = (0..vertices)
        .flat_map(|v| {
            let angle = v as f32 * 0.52;
            [angle.cos() * 0.9, angle.sin() * 0.9]
        })
        .collect::<Vec<_>>();
    let doubles: Vec<u8> = (0..vertices * 2)
        .flat_map(|_| (f64::from(noise.byte()) / 64.).to_le_bytes())
        .collect();
    let mixed = vec![
        array(0, FLOAT, 2, floats(corners.clone())),
        array(1, UNSIGNED_BYTE, 3, noise.bytes(vertices * 3)),
        array(
            3,
            SHORT,
            4,
            (0..vertices * 4)
                .flat_map(|i| ((i % 5) as i16 - 1).to_le_bytes())
                .collect(),
        ),
        array(8, DOUBLE, 2, doubles),
    ];
    let mut setup = texture(0, 13, 4, 77);
    setup.extend(texture(2, 14, 2, 1_234_567));
    setup.push(call(ACTIVE, &[TEXTURE0]));
    d.submit_for(CONTEXT, &setup);
    let indices: Vec<u32> = (0..40u32).map(|i| 9 + (i * 5 + i / 3) % 12).collect();
    for mode in [
        TRIANGLES,
        TRIANGLE_STRIP,
        TRIANGLE_FAN,
        LINE_LOOP,
        POINTS,
        QUADS,
    ] {
        for kind in [UNSIGNED_BYTE, UNSIGNED_SHORT] {
            same(
                &mut d,
                &[],
                expanded(mode, &mixed, &indices, 9),
                ranged(mode, &mixed, &indices, 9, kind),
                &format!("mixed formats, mode {mode}, index type {kind:#x}"),
            );
        }
    }
    let mut disable = vec![call(ACTIVE, &[TEXTURE0 + 2]), call(DISABLE, &[TEXTURE_2D])];
    disable.extend([call(ACTIVE, &[TEXTURE0]), call(DISABLE, &[TEXTURE_2D])]);
    d.submit_for(CONTEXT, &disable);

    // Lit FLOAT 3 positions with BYTE normals (signed-normalized by native
    // GL from the client section) and colour material.
    let normals: Vec<u8> = (0..vertices * 3)
        .map(|i| [90u8, 200, 127][i % 3] ^ (i as u8))
        .collect();
    let lit = vec![
        array(
            0,
            FLOAT,
            3,
            floats(corners.chunks(2).flat_map(|c| [c[0], c[1], 0.])),
        ),
        array(1, UNSIGNED_BYTE, 4, noise.bytes(vertices * 4)),
        array(2, BYTE, 3, normals),
    ];
    let lighting = [
        call(ENABLE, &[LIGHTING]),
        call(ENABLE, &[LIGHT0]),
        call(ENABLE, &[NORMALIZE]),
        call(ENABLE, &[COLOR_MATERIAL]),
    ];
    same(
        &mut d,
        &lighting,
        expanded(TRIANGLE_FAN, &lit, &indices, 9),
        ranged(TRIANGLE_FAN, &lit, &indices, 9, UNSIGNED_SHORT),
        "lit BYTE normals",
    );
    d.submit_for(CONTEXT, &[call(DISABLE, &[LIGHTING])]);

    // Generic attributes of a vertex program: position from attribute 0
    // (FLOAT 2), colour from a normalized UNSIGNED_BYTE 4 attribute 3.
    let source: &[u8] = b"!!ARBvp1.0\nMOV result.position, vertex.attrib[0];\n\
MOV result.color, vertex.attrib[3];\nEND";
    d.submit_for(
        CONTEXT,
        &[
            call(BIND_PROGRAM, &[VERTEX_PROGRAM, 31]),
            data_call(
                PROGRAM_STRING,
                &[VERTEX_PROGRAM, ASCII, source.len() as u32],
                source,
            ),
            call(ENABLE, &[VERTEX_PROGRAM]),
        ],
    );
    assert_eq!(d.error_for(CONTEXT), 0);
    let generic = vec![
        array(16, FLOAT, 2, floats(corners)),
        Array {
            slot: 19,
            descriptor: descriptor(UNSIGNED_BYTE, 4) | NORMALIZED,
            bytes: noise.bytes(vertices * 4),
        },
    ];
    same(
        &mut d,
        &[],
        expanded(TRIANGLES, &generic, &indices[..39], 9),
        ranged(TRIANGLES, &generic, &indices[..39], 9, UNSIGNED_BYTE),
        "generic attributes",
    );
    d.submit_for(CONTEXT, &[call(DISABLE, &[VERTEX_PROGRAM])]);
}

#[test]
#[ignore = "requires native host GPU and vertex buffer object QEMU producer; diskless acceptance"]
fn qemu_client_buffer_draws_reject_outside_indices_and_stay_out_of_lists() {
    let (mut d, _server, _directory) = start("ranged-failures");
    open(&mut d);
    let b = quake(6, 5, 3);
    let arrays = vec![
        array(0, FLOAT, 3, b.positions.clone()),
        array(1, UNSIGNED_BYTE, 4, b.colors.clone()),
    ];
    let good = ranged(TRIANGLES, &arrays, &b.indices, 3, UNSIGNED_SHORT);
    let image = d.drawn(vec![good.clone()], "admitted");
    let blank = d.drawn(vec![], "cleared");
    assert_ne!(image, blank);

    // An index one below or one past the client vertices fails the whole
    // batch, its clear included; the next batch draws again.
    d.submit_for(CONTEXT, &[call(CLEAR_COLOR, &[f(1.), f(1.), f(1.), f(1.)])]);
    let before = d.image();
    for outside in [2, 3 + 30] {
        let mut indices = b.indices.clone();
        indices[17] = outside;
        let mut records = clear();
        records.push(ranged(TRIANGLES, &arrays, &indices, 3, UNSIGNED_SHORT));
        assert_eq!(
            d.rejected_for(CONTEXT, &records),
            DG_GL_ERROR_BATCH,
            "{outside}"
        );
        assert!(d.image() == before, "nothing of the batch executed");
    }
    assert!(d.drawn(vec![good.clone()], "after the failures") == image);

    // A compiled buffer draw is GL_INVALID_OPERATION and draws nothing when
    // the list runs; the compiled expansion (what the frontend compiles)
    // replays the image.
    let list = words(&d.query_for(CONTEXT, GEN_LISTS, &[2]).1)[1];
    assert_ne!(list, 0);
    assert_eq!(d.word_for(CONTEXT, NEW_LIST, &[list, COMPILE]), 0);
    d.submit_for(CONTEXT, &[good]);
    assert_eq!(d.word_for(CONTEXT, END_LIST, &[]), 0);
    assert_eq!(d.error_for(CONTEXT), 0, "compiling records no error yet");
    let mut records = clear();
    records.push(call(CALL_LIST, &[list]));
    d.submit_for(CONTEXT, &records);
    assert_eq!(d.error_for(CONTEXT), INVALID_OPERATION);
    assert!(d.image() == blank, "the compiled buffer draw drew nothing");
    assert_eq!(d.word_for(CONTEXT, NEW_LIST, &[list + 1, COMPILE]), 0);
    d.submit_for(CONTEXT, &[expanded(TRIANGLES, &arrays, &b.indices, 3)]);
    assert_eq!(d.word_for(CONTEXT, END_LIST, &[]), 0);
    assert!(d.drawn(vec![call(CALL_LIST, &[list + 1])], "compiled expansion") == image);
}
