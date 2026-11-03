// SPDX-License-Identifier: GPL-2.0-or-later
//! Compact immutable GL client arrays; descriptor bytes always remain LE.
use crate::gl_api::*;
#[derive(Clone, Copy, Default)]
pub(crate) struct Attribute {
    pub kind: u32,
    pub size: u32,
    pub unit: usize,
    pub offset: usize,
    /// Generic attributes of the attribute draw only: a normalized integer type.
    pub normalized: bool,
}
pub(crate) fn layout(mask: u32, count: u32, data: &[u8]) -> Option<[Attribute; 7]> {
    layout_slots::<7>(mask, count, data)
}
/// Sixteen-slot DrawArraysEXT layout: slots 7-13 are texture coordinates of
/// units 1-7 with the slot-3 rules, slot 14 is the fog coordinate (one FLOAT
/// or DOUBLE); reserved slot 15 must stay disabled.
pub(crate) fn layout_extended(mask: u32, count: u32, data: &[u8]) -> Option<[Attribute; 16]> {
    layout_slots::<16>(mask, count, data)
}
fn layout_slots<const N: usize>(mask: u32, count: u32, data: &[u8]) -> Option<[Attribute; N]> {
    let allowed = if N == 16 {
        DG_GL_ARRAY_EXT_MASK
    } else {
        DG_GL_ARRAY_MASK
    };
    if (N != 7 && N != 16)
        || mask != ((mask & allowed) | DG_GL_ARRAY_RAW)
        || mask & DG_GL_ARRAY_POSITION == 0
    {
        return None;
    }
    layout_descriptors::<N>(|slot| mask & (1 << slot) != 0, count, data)
}
/// The attribute draw (ARB_vertex_program): the sixteen DrawArraysEXT slots,
/// then generic attributes 0-15 (slots 16-31). Position may be absent while
/// generic attribute 0 is present.
pub(crate) fn layout_attribs(
    mask: u32,
    generic: u32,
    count: u32,
    data: &[u8],
) -> Option<[Attribute; 32]> {
    if mask != ((mask & DG_GL_ARRAY_EXT_MASK) | DG_GL_ARRAY_RAW)
        || generic & !DG_GL_ATTRIB_MASK != 0
        || (mask & DG_GL_ARRAY_POSITION == 0 && generic & 1 == 0)
    {
        return None;
    }
    layout_descriptors::<32>(
        |slot| {
            if slot < 16 {
                mask & (1 << slot) != 0
            } else {
                generic & (1 << (slot - 16)) != 0
            }
        },
        count,
        data,
    )
}
/// One buffer slot of the buffer draw: guest buffer name, byte offset and
/// stride (zero for tight).
#[derive(Clone, Copy, Default, PartialEq, Eq, Debug)]
pub(crate) struct Reference {
    pub name: u32,
    pub offset: u32,
    pub stride: u32,
}
/// The buffer draw (ARB_vertex_buffer_object): thirty-two attribute draw
/// slots, client slots with their payload offsets, buffer slots with their
/// references, and the inline indices (offset and size; size zero for an
/// array draw, offset zero with an element buffer).
#[derive(Clone, Copy)]
pub(crate) struct BufferLayout {
    pub slots: [Attribute; 32],
    pub references: [Reference; 32],
    pub indices: usize,
    pub index_size: usize,
}
/// The index size of an index type word: zero for an array draw.
pub(crate) fn buffer_index_bytes(kind: u32) -> Option<usize> {
    match kind {
        0 => Some(0),
        GL_UNSIGNED_BYTE => Some(1),
        GL_UNSIGNED_SHORT => Some(2),
        GL_UNSIGNED_INT => Some(4),
        _ => None,
    }
}
/// The buffer draw's complete record shape (guest/include/gl-arrays.h,
/// DgRawBufferLayout): scalar words, descriptors, references, sections, inline
/// indices and zero padding. With `indices_checked`, every inline index must
/// also lie within the client vertices; execution re-parses an admitted record
/// without repeating that scan.
pub(crate) fn layout_buffer(
    a: &[u32; 10],
    data: &[u8],
    indices_checked: bool,
) -> Option<BufferLayout> {
    let [mode, base, count, mask, generic, buffered, vertices, kind, element, element_offset] = *a;
    let enabled = (mask & DG_GL_ARRAY_EXT_MASK) | (generic << 16);
    let client = enabled & !buffered;
    let index_size = buffer_index_bytes(kind)?;
    if mode > GL_POLYGON
        || mask != ((mask & DG_GL_ARRAY_EXT_MASK) | DG_GL_ARRAY_RAW)
        || generic & !DG_GL_ATTRIB_MASK != 0
        || (mask & DG_GL_ARRAY_POSITION == 0 && generic & 1 == 0)
        || buffered & !enabled != 0
        || count > DG_GL_MAX_BUFFER_ELEMENTS
        || data.len() < 128
    {
        return None;
    }
    let last = |n: u32| n == 0 || u64::from(base) + u64::from(n) - 1 <= u64::from(u32::MAX);
    if index_size == 0 {
        if element != 0
            || element_offset != 0
            || vertices != if client != 0 { count } else { 0 }
            || vertices > DG_GL_MAX_VERTICES
            || !last(count)
        {
            return None;
        }
    } else if client != 0 {
        if vertices == 0 || vertices > DG_GL_MAX_VERTICES || !last(vertices) {
            return None;
        }
    } else if vertices != 0 || base != 0 {
        return None;
    }
    if index_size != 0
        && (if element != 0 {
            !(element_offset as usize).is_multiple_of(index_size)
        } else {
            element_offset != 0 || count > DG_GL_MAX_INDICES
        })
    {
        return None;
    }
    let word = |at: usize| -> Option<u32> {
        Some(u32::from_le_bytes(data.get(at..at + 4)?.try_into().ok()?))
    };
    let mut layout = BufferLayout {
        slots: [Attribute::default(); 32],
        references: [Reference::default(); 32],
        indices: 0,
        index_size,
    };
    let mut at = 128;
    for slot in 0..32 {
        let descriptor = word(slot * 4)?;
        if enabled & (1 << slot) == 0 {
            if descriptor != 0 {
                return None;
            }
            continue;
        }
        let attribute = if slot < 16 {
            conventional(slot, descriptor)?
        } else {
            generic_attribute(descriptor)?
        };
        layout.slots[slot] = attribute;
        if buffered & (1 << slot) != 0 {
            let r = Reference {
                name: word(at)?,
                offset: word(at + 4)?,
                stride: word(at + 8)?,
            };
            let unit = attribute.unit as u32;
            if r.name == 0
                || r.stride > DG_GL_MAX_BUFFER_STRIDE
                || !r.offset.is_multiple_of(unit)
                || !r.stride.is_multiple_of(unit)
            {
                return None;
            }
            layout.references[slot] = r;
            at += DG_GL_BUFFER_REFERENCE_BYTES as usize;
        }
    }
    let mut end = at;
    for (slot, a) in layout.slots.iter_mut().enumerate() {
        if client & (1 << slot) == 0 {
            continue;
        }
        a.offset = (end + a.unit - 1) & !(a.unit - 1);
        if data.get(end..a.offset)?.iter().any(|&v| v != 0) {
            return None;
        }
        end = a
            .offset
            .checked_add(vertices as usize * a.size as usize * a.unit)?;
        if end > data.len() {
            return None;
        }
    }
    if index_size != 0 && element == 0 {
        let offset = (end + index_size - 1) & !(index_size - 1);
        if data.get(end..offset)?.iter().any(|&v| v != 0) {
            return None;
        }
        end = offset.checked_add(count as usize * index_size)?;
        let bytes = data.get(offset..end)?;
        if indices_checked && client != 0 {
            let high = u64::from(base) + u64::from(vertices);
            if !bytes.chunks_exact(index_size).all(|i| {
                let v = u64::from(index_value(i));
                v >= u64::from(base) && v < high
            }) {
                return None;
            }
        }
        layout.indices = offset;
    }
    let total = (end + 3) & !3;
    if total != data.len() || data[end..].iter().any(|&v| v != 0) {
        return None;
    }
    Some(layout)
}
/// One little-endian index of 1, 2 or 4 bytes.
pub(crate) fn index_value(bytes: &[u8]) -> u32 {
    match bytes.len() {
        1 => u32::from(bytes[0]),
        2 => u32::from(u16::from_le_bytes([bytes[0], bytes[1]])),
        _ => u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]),
    }
}
fn unit_bytes(kind: u32) -> Option<usize> {
    Some(match kind {
        GL_BYTE | GL_UNSIGNED_BYTE => 1,
        GL_SHORT | GL_UNSIGNED_SHORT => 2,
        GL_INT | GL_UNSIGNED_INT | GL_FLOAT => 4,
        GL_DOUBLE => 8,
        _ => return None,
    })
}
/// Conventional slot `slot`'s rules: slots 7-13 follow slot 3 (texture
/// coordinates of units 1-7), slot 14 is the fog coordinate.
fn conventional(slot: usize, descriptor: u32) -> Option<Attribute> {
    let i = if (7..=13).contains(&slot) { 3 } else { slot };
    let kind = descriptor & 65535;
    let size = descriptor >> 16;
    let unit = unit_bytes(kind)?;
    let valid_size = match i {
        0 => (2..=4).contains(&size),
        1 => (3..=4).contains(&size),
        2 | 4 => size == 3,
        3 => (1..=4).contains(&size),
        _ => size == 1,
    };
    let valid_type = match i {
        1 | 4 => true,
        14 => matches!(kind, GL_FLOAT | GL_DOUBLE),
        6 => kind == GL_UNSIGNED_BYTE,
        5 => matches!(kind, GL_SHORT | GL_INT | GL_FLOAT | GL_DOUBLE),
        2 => matches!(kind, GL_BYTE | GL_SHORT | GL_INT | GL_FLOAT | GL_DOUBLE),
        _ => matches!(kind, GL_SHORT | GL_INT | GL_FLOAT | GL_DOUBLE),
    };
    (valid_size && valid_type).then_some(Attribute {
        kind,
        size,
        unit,
        ..Attribute::default()
    })
}
/// A generic attribute: any GL1.1 component type, one to four components,
/// normalized only for an integer type; every other bit zero.
fn generic_attribute(descriptor: u32) -> Option<Attribute> {
    let kind = descriptor & 65535;
    let size = (descriptor >> 16) & 255;
    let normalized = descriptor & DG_GL_ATTRIB_NORMALIZED != 0;
    if descriptor & !(0x00ff_ffff | DG_GL_ATTRIB_NORMALIZED) != 0
        || !(1..=4).contains(&size)
        || (normalized && matches!(kind, GL_FLOAT | GL_DOUBLE))
    {
        return None;
    }
    Some(Attribute {
        kind,
        size,
        unit: unit_bytes(kind)?,
        normalized,
        ..Attribute::default()
    })
}
fn layout_descriptors<const N: usize>(
    enabled: impl Fn(usize) -> bool,
    count: u32,
    data: &[u8],
) -> Option<[Attribute; N]> {
    if count > DG_GL_MAX_VERTICES || data.len() < N * 4 {
        return None;
    }
    let mut result = [Attribute::default(); N];
    let mut end = N * 4;
    for (slot, a) in result.iter_mut().enumerate() {
        let descriptor = u32::from_le_bytes(data[slot * 4..slot * 4 + 4].try_into().ok()?);
        if !enabled(slot) {
            if descriptor != 0 {
                return None;
            }
            continue;
        }
        *a = if slot < 16 {
            conventional(slot, descriptor)?
        } else {
            generic_attribute(descriptor)?
        };
        a.offset = (end + a.unit - 1) & !(a.unit - 1);
        if data.get(end..a.offset)?.iter().any(|&v| v != 0) {
            return None;
        }
        end = a
            .offset
            .checked_add(count as usize * a.size as usize * a.unit)?;
        if end > data.len() {
            return None;
        }
    }
    let total = (end + 3) & !3;
    if total != data.len() || data[end..].iter().any(|&v| v != 0) {
        return None;
    }
    Some(result)
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;
    pub(crate) fn packet() -> Vec<u8> {
        let mut data = vec![0; 180];
        for (i, (kind, size)) in [
            (GL_DOUBLE, 2),
            (GL_UNSIGNED_BYTE, 4),
            (GL_SHORT, 3),
            (GL_FLOAT, 2),
            (GL_BYTE, 3),
            (GL_DOUBLE, 1),
            (GL_UNSIGNED_BYTE, 1),
        ]
        .into_iter()
        .enumerate()
        {
            data[i * 4..i * 4 + 4].copy_from_slice(&(kind | (size << 16)).to_le_bytes());
        }
        data[176..179].copy_from_slice(&[0, 7, 255]);
        data
    }
    /// Position (FLOAT 2), unit 0 (FLOAT 2) and unit 3 (SHORT 3) for 3 vertices.
    pub(crate) fn extended_packet() -> Vec<u8> {
        let mut data = vec![0; 132];
        for (slot, kind, size) in [(0, GL_FLOAT, 2), (3, GL_FLOAT, 2), (9, GL_SHORT, 3)] {
            data[slot * 4..slot * 4 + 4].copy_from_slice(&(kind | (size << 16)).to_le_bytes());
        }
        data
    }
    /// Unit 0 coordinates (FLOAT 2), generic 0 (FLOAT 2), generic 3
    /// (UNSIGNED_BYTE 4, normalized) and generic 15 (SHORT 1), 3 vertices.
    pub(crate) fn attribute_packet() -> Vec<u8> {
        let mut data = vec![0; 196];
        for (slot, descriptor) in [
            (3, GL_FLOAT | (2 << 16)),
            (16, GL_FLOAT | (2 << 16)),
            (19, GL_UNSIGNED_BYTE | (4 << 16) | DG_GL_ATTRIB_NORMALIZED),
            (31, GL_SHORT | (1 << 16)),
        ] {
            data[slot * 4..slot * 4 + 4].copy_from_slice(&descriptor.to_le_bytes());
        }
        data
    }
    #[test]
    fn attribute_draws_add_generic_slots_and_relax_only_the_position() {
        let data = attribute_packet();
        let mask = DG_GL_ARRAY_RAW | DG_GL_ARRAY_TEXCOORD;
        let generic = 1 | 1 << 3 | 1 << 15;
        let a = layout_attribs(mask, generic, 3, &data).unwrap();
        assert_eq!(
            (a[3].offset, a[16].offset, a[19].offset, a[31].offset),
            (128, 152, 176, 188)
        );
        assert!(a[19].normalized && !a[16].normalized && !a[31].normalized);
        assert_eq!((a[31].kind, a[31].size, a[31].unit), (GL_SHORT, 1, 2));
        // Position or generic attribute 0 must provoke.
        assert!(layout_attribs(mask, generic & !1, 3, &data).is_none());
        // Reserved generic mask bits, a missing or stray descriptor.
        assert!(layout_attribs(mask, generic | 1 << 16, 3, &data).is_none());
        assert!(layout_attribs(mask, generic | 1 << 4, 3, &data).is_none());
        assert!(layout_attribs(mask, generic & !(1 << 15), 3, &data).is_none());
        // Conventional slots keep their DrawArraysEXT rules, slot 15 included.
        assert!(layout_attribs(mask | 1 << 15, generic, 3, &data).is_none());
        let mut bad = data.clone();
        bad[12..16].copy_from_slice(&(GL_UNSIGNED_BYTE | (2 << 16)).to_le_bytes());
        assert!(layout_attribs(mask, generic, 3, &bad).is_none());
        // Generic descriptors: every component type, sizes 1-4, normalized
        // integers only, no other bits.
        for kind in [
            GL_BYTE,
            GL_UNSIGNED_BYTE,
            GL_SHORT,
            GL_UNSIGNED_SHORT,
            GL_INT,
            GL_UNSIGNED_INT,
            GL_FLOAT,
            GL_DOUBLE,
        ] {
            for size in 1..=4 {
                assert!(generic_descriptor(kind | (size << 16)).is_some());
            }
            assert_eq!(
                generic_descriptor(kind | (1 << 16) | DG_GL_ATTRIB_NORMALIZED).is_some(),
                !matches!(kind, GL_FLOAT | GL_DOUBLE)
            );
        }
        for descriptor in [
            GL_FLOAT,
            GL_FLOAT | (5 << 16),
            GL_FLOAT | (1 << 16) | 1 << 25,
            GL_FLOAT | (1 << 16) | 1 << 31,
            0x1407 | (1 << 16),
            GL_HALF_FLOAT | (2 << 16),
        ] {
            assert!(generic_descriptor(descriptor).is_none(), "{descriptor:#x}");
        }
        // Exact length, zero final padding and no stray disabled descriptor.
        for offset in [20, 100, 194, 195] {
            let mut bad = data.clone();
            bad[offset] = 1;
            assert!(layout_attribs(mask, generic, 3, &bad).is_none(), "{offset}");
        }
        assert!(layout_attribs(mask, generic, 3, &data[..192]).is_none());
        assert!(layout_attribs(mask, generic, DG_GL_MAX_VERTICES + 1, &data).is_none());
        // A position array with generic attributes but no generic 0 is fine.
        let mut both = vec![0u8; 128 + 24 + 4];
        both[..4].copy_from_slice(&(GL_FLOAT | (2 << 16)).to_le_bytes());
        both[124..128].copy_from_slice(&(GL_UNSIGNED_BYTE | (1 << 16)).to_le_bytes());
        let b = layout_attribs(DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION, 1 << 15, 3, &both).unwrap();
        assert_eq!((b[0].offset, b[31].offset), (128, 152));
        // Generic slots never exist in the older layouts.
        assert!(layout_extended(mask | DG_GL_ARRAY_POSITION, 3, &data).is_none());
    }
    /// A buffer draw: position (FLOAT 3) from buffer 4 at offset 8, stride
    /// 20; colour (UNSIGNED_BYTE 4) and generic 1 (SHORT 2) client sections of
    /// `vertices` vertices; inline UNSIGNED_SHORT `indices` when indexed.
    pub(crate) fn buffer_packet(vertices: u32, indices: &[u16]) -> Vec<u8> {
        let mut data = vec![0u8; 128];
        for (slot, descriptor) in [
            (0, GL_FLOAT | (3 << 16)),
            (1, GL_UNSIGNED_BYTE | (4 << 16)),
            (17, GL_SHORT | (2 << 16)),
        ] {
            data[slot * 4..slot * 4 + 4].copy_from_slice(&descriptor.to_le_bytes());
        }
        for word in [4u32, 8, 20] {
            data.extend_from_slice(&word.to_le_bytes());
        }
        data.extend((0..vertices * 4).map(|v| v as u8 | 1));
        data.extend((0..vertices * 2).flat_map(|v| (v as u16 + 1).to_le_bytes()));
        if !indices.is_empty() {
            data.resize((data.len() + 1) & !1, 0);
            data.extend(indices.iter().flat_map(|i| i.to_le_bytes()));
        }
        data.resize((data.len() + 3) & !3, 0);
        data
    }
    pub(crate) const BUFFER_MASK: u32 = DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION | DG_GL_ARRAY_COLOR;
    #[test]
    fn buffer_draws_place_references_sections_and_indices() {
        // An array draw of 3 vertices from vertex 5.
        let data = buffer_packet(3, &[]);
        let a = [GL_TRIANGLES, 5, 3, BUFFER_MASK, 2, 1, 3, 0, 0, 0];
        let l = layout_buffer(&a, &data, true).unwrap();
        assert_eq!(
            l.references[0],
            Reference {
                name: 4,
                offset: 8,
                stride: 20
            }
        );
        assert_eq!((l.slots[1].offset, l.slots[17].offset), (140, 152));
        assert_eq!((l.index_size, l.indices, data.len()), (0, 0, 164));
        // Without client sections an array draw carries no vertices.
        let mut both = data[..140].to_vec();
        both[4..8].fill(0);
        both[68..72].fill(0);
        let only = [
            GL_POINTS,
            9,
            100,
            DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION,
            0,
            1,
            0,
            0,
            0,
            0,
        ];
        assert!(layout_buffer(&only, &both, true).is_some());
        for (word, value) in [(6, 100), (1, u32::MAX - 98)] {
            let mut bad = only;
            bad[word] = value;
            assert!(layout_buffer(&bad, &both, true).is_none(), "{word}");
        }
        // Indexed with inline indices within base 5, 3 vertices.
        let data = buffer_packet(3, &[5, 7, 6, 5]);
        let a = [
            GL_TRIANGLE_STRIP,
            5,
            4,
            BUFFER_MASK,
            2,
            1,
            3,
            GL_UNSIGNED_SHORT,
            0,
            0,
        ];
        let l = layout_buffer(&a, &data, true).unwrap();
        assert_eq!((l.index_size, l.indices, data.len()), (2, 164, 172));
        // An index outside the client vertices is malformed when checked.
        for index in [4u16, 8] {
            let bad = buffer_packet(3, &[5, index, 6, 5]);
            assert!(layout_buffer(&a, &bad, true).is_none(), "{index}");
            assert!(layout_buffer(&a, &bad, false).is_some(), "{index}");
        }
        // An element buffer: no inline indices, an aligned offset.
        let data = buffer_packet(3, &[]);
        let e = [
            GL_TRIANGLES,
            5,
            6,
            BUFFER_MASK,
            2,
            1,
            3,
            GL_UNSIGNED_INT,
            9,
            12,
        ];
        assert!(layout_buffer(&e, &data, true).is_some());
        let mut bad = e;
        bad[9] = 6;
        assert!(layout_buffer(&bad, &data, true).is_none());
        // Every buffer slot and every word of the record shape.
        let a = [GL_TRIANGLES, 5, 3, BUFFER_MASK, 2, 1, 3, 0, 0, 0];
        let data = buffer_packet(3, &[]);
        for (word, value) in [
            (0, GL_POLYGON + 1),
            (3, BUFFER_MASK & !DG_GL_ARRAY_RAW),
            (3, BUFFER_MASK | 1 << 15),
            (4, 2 | 1 << 16),
            (5, 1 | 1 << 3),
            (6, 2),
            (7, GL_UNSIGNED_BYTE + 1),
            (8, 1),
            (9, 4),
            (2, DG_GL_MAX_BUFFER_ELEMENTS + 1),
        ] {
            let mut bad = a;
            bad[word] = value;
            assert!(
                layout_buffer(&bad, &data, true).is_none(),
                "{word} {value:#x}"
            );
        }
        // References: nonzero name, bounded and aligned stride and offset.
        for (at, value) in [
            (128, 0u32),
            (132, 6),
            (136, 22),
            (136, DG_GL_MAX_BUFFER_STRIDE + 4),
        ] {
            let mut bad = data.clone();
            bad[at..at + 4].copy_from_slice(&value.to_le_bytes());
            assert!(layout_buffer(&a, &bad, true).is_none(), "{at} {value}");
        }
        // Stray descriptors, zero padding, exact length.
        for at in [8, 72, 124] {
            let mut bad = data.clone();
            bad[at] = 1;
            assert!(layout_buffer(&a, &bad, true).is_none(), "{at}");
        }
        assert!(layout_buffer(&a, &data[..160], true).is_none());
        let one = buffer_packet(3, &[5]);
        let indexed = [
            GL_POINTS,
            5,
            1,
            BUFFER_MASK,
            2,
            1,
            3,
            GL_UNSIGNED_SHORT,
            0,
            0,
        ];
        assert!(layout_buffer(&indexed, &one, true).is_some());
        let mut bad = one.clone();
        bad[166] = 1;
        assert!(layout_buffer(&indexed, &bad, true).is_none());
        // A buffer slot must be enabled; a buffered slot keeps its slot rules.
        let mut bad = a;
        bad[5] = 1 | 1 << 2;
        assert!(layout_buffer(&bad, &data, true).is_none());
        let mut position = data.clone();
        position[..4].copy_from_slice(&(GL_UNSIGNED_BYTE | (3 << 16)).to_le_bytes());
        assert!(layout_buffer(&a, &position, true).is_none());
        // Indexed draws without client sections carry no base or vertices.
        let mut solo = data[..140].to_vec();
        solo[4..8].fill(0);
        solo[68..72].fill(0);
        let i = [
            GL_TRIANGLES,
            0,
            3,
            DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION,
            0,
            1,
            0,
            GL_UNSIGNED_BYTE,
            3,
            1,
        ];
        assert!(layout_buffer(&i, &solo, true).is_some());
        for word in [1, 6] {
            let mut bad = i;
            bad[word] = 1;
            assert!(layout_buffer(&bad, &solo, true).is_none(), "{word}");
        }
    }
    fn generic_descriptor(descriptor: u32) -> Option<Attribute> {
        generic_attribute(descriptor)
    }
    #[test]
    fn fog_coordinate_slot_is_one_float_or_double() {
        let mask = DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION | DG_GL_ARRAY_FOG_COORD;
        let packet = |kind: u32, size: u32, unit: usize| {
            let mut data = vec![0u8; 64];
            data[..4].copy_from_slice(&(GL_FLOAT | (2 << 16)).to_le_bytes());
            data[56..60].copy_from_slice(&(kind | (size << 16)).to_le_bytes());
            data.resize(64 + 3 * 8, 0);
            let at = (data.len() + unit - 1) & !(unit - 1);
            data.resize(at + 3 * size as usize * unit, 0);
            data.resize((data.len() + 3) & !3, 0);
            data
        };
        let a = layout_extended(mask, 3, &packet(GL_FLOAT, 1, 4)).unwrap();
        assert_eq!((a[14].kind, a[14].size, a[14].offset), (GL_FLOAT, 1, 88));
        let a = layout_extended(mask, 3, &packet(GL_DOUBLE, 1, 8)).unwrap();
        assert_eq!((a[14].kind, a[14].offset), (GL_DOUBLE, 88));
        assert!(layout_extended(mask, 3, &packet(GL_FLOAT, 2, 4)).is_none());
        for kind in [GL_SHORT, GL_INT, GL_UNSIGNED_BYTE] {
            let unit = if kind == GL_SHORT {
                2
            } else if kind == GL_INT {
                4
            } else {
                1
            };
            assert!(layout_extended(mask, 3, &packet(kind, 1, unit)).is_none());
        }
        // The seven-slot DrawArrays layout never carries a fog coordinate.
        assert!(layout(mask, 3, &packet(GL_FLOAT, 1, 4)).is_none());
    }
    #[test]
    fn extended_texture_unit_slots_follow_texcoord_rules() {
        let data = extended_packet();
        let mask = DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION | DG_GL_ARRAY_TEXCOORD | 0x200;
        let a = layout_extended(mask, 3, &data).unwrap();
        assert_eq!((a[0].offset, a[3].offset, a[9].offset), (64, 88, 112));
        assert_eq!((a[9].kind, a[9].size, a[9].unit), (GL_SHORT, 3, 2));
        // Reserved slot 15 and its mask bit must stay zero; the fog slot needs
        // a descriptor once enabled.
        assert!(layout_extended(mask | 1 << 15, 3, &data).is_none());
        assert!(layout_extended(mask | DG_GL_ARRAY_FOG_COORD, 3, &data).is_none());
        let mut reserved = data.clone();
        reserved[60] = 1;
        assert!(layout_extended(mask, 3, &reserved).is_none());
        // Texture units use the slot-3 rules: no unsigned or byte coordinates.
        for kind in [GL_UNSIGNED_BYTE, GL_UNSIGNED_SHORT, GL_BYTE] {
            let mut bad = data.clone();
            bad[36..40].copy_from_slice(&(kind | (3 << 16)).to_le_bytes());
            assert!(layout_extended(mask, 3, &bad).is_none());
        }
        // An enabled unit needs a descriptor; a disabled one must not have one.
        assert!(layout_extended(mask & !0x200, 3, &data).is_none());
        assert!(layout_extended(mask | 0x100, 3, &data).is_none());
        // Trailing padding is zero and the length is exact.
        let mut padded = data.clone();
        padded[131] = 1;
        assert!(layout_extended(mask, 3, &padded).is_none());
        assert!(layout_extended(mask, 3, &data[..128]).is_none());
        // The seven-slot layout never admits units 1-7.
        assert!(layout(mask, 3, &data).is_none());
    }
    #[test]
    fn exact_native_types_alignment_and_malformed_admission() {
        let data = packet();
        let mask = DG_GL_ARRAY_RAW | 127;
        let a = layout(mask, 3, &data).unwrap();
        assert_eq!(a.map(|a| a.offset), [32, 80, 92, 112, 136, 152, 176]);
        assert_eq!(a.map(|a| a.unit), [8, 1, 2, 4, 1, 8, 1]);
        for offset in [28, 31, 110, 111, 145, 151, 179] {
            let mut bad = data.clone();
            bad[offset] = 1;
            assert!(layout(mask, 3, &bad).is_none());
        }
        for length in [0, 27, 179, 181] {
            let mut bad = data.clone();
            bad.resize(length, 0);
            assert!(layout(mask, 3, &bad).is_none());
        }
        for (word, value) in [
            (0, GL_UNSIGNED_BYTE | (2 << 16)),
            (1, GL_FLOAT | (2 << 16)),
            (2, GL_FLOAT | (4 << 16)),
            (6, GL_FLOAT | (1 << 16)),
            (4, 0x8000_0000),
        ] {
            let mut bad = data.clone();
            bad[word * 4..word * 4 + 4].copy_from_slice(&value.to_le_bytes());
            assert!(layout(mask, 3, &bad).is_none());
        }
        assert!(layout(mask | 128, 3, &data).is_none());
        assert!(layout(mask & !2, 3, &data).is_none());
        assert!(layout(mask, DG_GL_MAX_VERTICES + 1, &data).is_none());
    }
}
