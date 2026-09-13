// SPDX-License-Identifier: GPL-2.0-or-later
fn valid(data: &[u8], format: u32) -> bool {
    if !matches!(format, 1 | 2) || !data.len().is_multiple_of(8) {
        return false;
    }
    for pixel in data.as_chunks::<8>().0 {
        let a = u32::from_le_bytes(pixel[..4].try_into().unwrap());
        let b = u32::from_le_bytes(pixel[4..].try_into().unwrap());
        if format == 2 {
            if (a | b) & 0xff000000 != 0 {
                return false;
            }
        } else if b != 0
            || (a & 255) > (a >> 24)
            || ((a >> 8) & 255) > (a >> 24)
            || ((a >> 16) & 255) > (a >> 24)
        {
            return false;
        }
    }
    true
}

/// # Safety
/// `data` references `pixels * 8` immutable bytes from the host-owned DMA snapshot.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_cursor_validate(
    data: *const u8,
    pixels: u32,
    format: u32,
) -> u32 {
    if data.is_null() || pixels == 0 || pixels > 64 * 64 {
        return 0;
    }
    u32::from(valid(
        unsafe { core::slice::from_raw_parts(data, pixels as usize * 8) },
        format,
    ))
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn cursor_alpha_and_masks() {
        for (a, b, format, expected) in [
            (0x807f0080u32, 0u32, 1, true),
            (0x807f0081, 0, 1, false),
            (0x00800000, 0, 1, false),
            (0, 1, 1, false),
            (0x00ffffff, 0x00ffffff, 2, true),
            (0xff000000, 0, 2, false),
            (0, 0xff000000, 2, false),
            (0, 0, 99, false),
        ] {
            let mut p = [0; 8];
            p[..4].copy_from_slice(&a.to_le_bytes());
            p[4..].copy_from_slice(&b.to_le_bytes());
            assert_eq!(valid(&p, format), expected);
        }
        assert!(!valid(&[0; 7], 1));
    }
}
