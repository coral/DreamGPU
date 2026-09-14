// SPDX-License-Identifier: GPL-2.0-or-later
//! RGB565 primary memory conversion at the CPU/compositor ownership boundary.

/// # Safety
/// `rgba` spans pixels*4 writable bytes, `primary` spans pixels*2 writable bytes.
/// Buffers are disjoint and owned by the caller for the call. QEMU validates the
/// row/VRAM bounds and limits each call to its existing work quantum.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_primary16_transfer(
    rgba: *mut u8,
    primary: *mut u8,
    pixels: u32,
    writeback: u32,
) {
    let rgba = unsafe { core::slice::from_raw_parts_mut(rgba, pixels as usize * 4) };
    let primary = unsafe { core::slice::from_raw_parts_mut(primary, pixels as usize * 2) };
    for (color, packed) in rgba
        .as_chunks_mut::<4>()
        .0
        .iter_mut()
        .zip(primary.as_chunks_mut::<2>().0.iter_mut())
    {
        if writeback != 0 {
            let value = ((color[2] as u16 >> 3) << 11)
                | ((color[1] as u16 >> 2) << 5)
                | (color[0] as u16 >> 3);
            packed.copy_from_slice(&value.to_le_bytes());
        } else {
            let value = u16::from_le_bytes([packed[0], packed[1]]);
            let r = (value >> 11) as u8;
            let g = ((value >> 5) & 63) as u8;
            let b = (value & 31) as u8;
            color.copy_from_slice(&[
                (b << 3) | (b >> 2),
                (g << 2) | (g >> 4),
                (r << 3) | (r >> 2),
                255,
            ]);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn all_rgb565_values_roundtrip_and_preserve_sentinels() {
        for value in 0..=u16::MAX {
            let bytes = value.to_le_bytes();
            let mut primary = [17, bytes[0], bytes[1], 91];
            let mut rgba = [29; 6];
            unsafe {
                dreamgpu_primary16_transfer(
                    rgba.as_mut_ptr().add(1),
                    primary.as_mut_ptr().add(1),
                    1,
                    0,
                )
            };
            assert_eq!(rgba[4], 255);
            let r = ((value >> 11) & 31) as u8;
            let g = ((value >> 5) & 63) as u8;
            let b = (value & 31) as u8;
            assert_eq!(
                &rgba[1..4],
                &[
                    (b << 3) | (b >> 2),
                    (g << 2) | (g >> 4),
                    (r << 3) | (r >> 2)
                ]
            );
            primary[1..3].fill(0);
            unsafe {
                dreamgpu_primary16_transfer(
                    rgba.as_mut_ptr().add(1),
                    primary.as_mut_ptr().add(1),
                    1,
                    1,
                )
            };
            assert_eq!(primary, [17, bytes[0], bytes[1], 91]);
            assert_eq!((rgba[0], rgba[5]), (29, 29));
        }
    }
}
