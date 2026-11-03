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
fn generic(descriptor: u32) -> Option<Attribute> {
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
            generic(descriptor)?
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
    fn generic_descriptor(descriptor: u32) -> Option<Attribute> {
        generic(descriptor)
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
