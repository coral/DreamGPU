// SPDX-License-Identifier: GPL-2.0-or-later
//! Compact immutable GL client arrays; descriptor bytes always remain LE.
use crate::gl_api::*;
#[derive(Clone, Copy, Default)]
pub(crate) struct Attribute {
    pub kind: u32,
    pub size: u32,
    pub unit: usize,
    pub offset: usize,
}
pub(crate) fn layout(mask: u32, count: u32, data: &[u8]) -> Option<[Attribute; 7]> {
    layout_slots::<7>(mask, count, data)
}
/// Sixteen-slot DrawArraysEXT layout: slots 7-13 are texture coordinates of
/// units 1-7 with the slot-3 rules; reserved slots 14-15 must stay disabled.
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
        || count > DG_GL_MAX_VERTICES
        || data.len() < N * 4
    {
        return None;
    }
    let mut result = [Attribute::default(); N];
    let mut end = N * 4;
    for (slot, a) in result.iter_mut().enumerate() {
        let i = if (7..=13).contains(&slot) { 3 } else { slot };
        let descriptor = u32::from_le_bytes(data[slot * 4..slot * 4 + 4].try_into().ok()?);
        if mask & (1 << slot) == 0 {
            if descriptor != 0 {
                return None;
            }
            continue;
        }
        a.kind = descriptor & 65535;
        a.size = descriptor >> 16;
        a.unit = match a.kind {
            GL_BYTE | GL_UNSIGNED_BYTE => 1,
            GL_SHORT | GL_UNSIGNED_SHORT => 2,
            GL_INT | GL_UNSIGNED_INT | GL_FLOAT => 4,
            GL_DOUBLE => 8,
            _ => return None,
        };
        let valid_size = match i {
            0 => (2..=4).contains(&a.size),
            1 => (3..=4).contains(&a.size),
            2 | 4 => a.size == 3,
            3 => (1..=4).contains(&a.size),
            _ => a.size == 1,
        };
        let valid_type = match i {
            1 | 4 => true,
            6 => a.kind == GL_UNSIGNED_BYTE,
            5 => matches!(a.kind, GL_SHORT | GL_INT | GL_FLOAT | GL_DOUBLE),
            2 => matches!(a.kind, GL_BYTE | GL_SHORT | GL_INT | GL_FLOAT | GL_DOUBLE),
            _ => matches!(a.kind, GL_SHORT | GL_INT | GL_FLOAT | GL_DOUBLE),
        };
        if !valid_size || !valid_type {
            return None;
        }
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
    #[test]
    fn extended_texture_unit_slots_follow_texcoord_rules() {
        let data = extended_packet();
        let mask = DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION | DG_GL_ARRAY_TEXCOORD | 0x200;
        let a = layout_extended(mask, 3, &data).unwrap();
        assert_eq!((a[0].offset, a[3].offset, a[9].offset), (64, 88, 112));
        assert_eq!((a[9].kind, a[9].size, a[9].unit), (GL_SHORT, 3, 2));
        // Reserved slots 14-15 and their mask bits must stay zero.
        assert!(layout_extended(mask | 1 << 14, 3, &data).is_none());
        let mut reserved = data.clone();
        reserved[56] = 1;
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
