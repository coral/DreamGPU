// SPDX-License-Identifier: GPL-2.0-or-later
//! Validate bounded desktop ownership operations before platform or DMA work.
const BATCH: u32 = 1;
const LIMIT: u32 = 9;
const DESKTOP: u32 = 10;

pub fn rect(x: u32, y: u32, w: u32, h: u32, lw: u32, lh: u32) -> bool {
    w != 0 && h != 0 && x <= lw && y <= lh && w <= lw - x && h <= lh - y
}

fn validate(r: &[u8; 64], pw: u32, ph: u32, vram: u32, work: u64) -> Result<u64, u32> {
    let word = |i| u32::from_le_bytes(r[i..i + 4].try_into().unwrap());
    let wide = |i| u64::from_le_bytes(r[i..i + 8].try_into().unwrap());
    let (op, flags, x, y, w, h, sx, sy, offset, stride, epoch, frame) = (
        word(0),
        word(4),
        word(8),
        word(12),
        word(16),
        word(20),
        word(24),
        word(28),
        word(40),
        word(44),
        wide(48),
        wide(56),
    );
    let image = op == 6 || op == 8;
    let cpu = matches!(op, 1 | 2 | 3 | 7);
    if !(1..=8).contains(&op)
        || !matches!(word(32), 0 | 16)
        || word(36) != 0
        || (word(32) == 16 && matches!(op, 4 | 5))
        || (flags != 0 && (op != 6 || flags != 1))
    {
        return Err(BATCH);
    }
    if op == 8 {
        if x != 0 || y != 0 || w != 0 || h != 0 || sx != 0 || sy != 0 {
            return Err(BATCH);
        }
    } else if !rect(x, y, w, h, pw, ph) {
        return Err(DESKTOP);
    }
    if matches!(op, 1 | 3) && (x != 0 || y != 0 || w != pw || h != ph) {
        return Err(DESKTOP);
    }
    if op == 5 && !rect(sx, sy, w, h, pw, ph) {
        return Err(DESKTOP);
    }
    if image {
        if offset >= 96 || epoch == 0 || frame == 0 || stride != 0 {
            return Err(DESKTOP);
        }
    } else if epoch != 0 || frame != 0 || (!cpu && (offset != 0 || stride != 0)) {
        return Err(BATCH);
    }
    if op != 5 && op != 6 && (sy != 0 || (sx != 0 && op != 4)) {
        return Err(BATCH);
    }
    if cpu {
        let row = u64::from(w) * if word(32) == 16 { 2 } else { 4 };
        if u64::from(stride) < row {
            return Err(DESKTOP);
        }
        let end = u64::from(offset) + u64::from(h - 1) * u64::from(stride) + row;
        let export_bytes = ((u64::from(w) * 4 + 255) & !255)
            .checked_mul(u64::from(h))
            .and_then(|v| v.checked_add(65535))
            .ok_or(DESKTOP)?
            & !65535;
        if offset & if word(32) == 16 { 1 } else { 3 } != 0
            || stride & 3 != 0
            || u64::from(stride) < row
            || end > u64::from(vram)
            || export_bytes > 64 * 1024 * 1024
        {
            return Err(DESKTOP);
        }
    }
    let added = u64::from(w)
        .checked_mul(u64::from(h))
        .and_then(|v| v.checked_mul(4))
        .ok_or(LIMIT)?;
    let total = work.checked_add(added).ok_or(LIMIT)?;
    if total > 64 * 1024 * 1024 {
        return Err(LIMIT);
    }
    Ok(total)
}

/// # Safety
/// `record` references 64 immutable bytes; `work` is valid, writable and disjoint.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_desktop_validate(
    record: *const u8,
    width: u32,
    height: u32,
    vram: u32,
    work: *mut u64,
) -> u32 {
    if record.is_null() || work.is_null() {
        return BATCH;
    }
    let record = unsafe { &*record.cast::<[u8; 64]>() };
    match validate(record, width, height, vram, unsafe { *work }) {
        Ok(total) => {
            unsafe {
                *work = total;
            }
            0
        }
        Err(error) => error,
    }
}

#[no_mangle]
pub extern "C" fn dreamgpu_desktop_rect(
    x: u32,
    y: u32,
    w: u32,
    h: u32,
    width: u32,
    height: u32,
) -> u32 {
    u32::from(rect(x, y, w, h, width, height))
}

#[cfg(test)]
mod tests {
    use super::*;
    fn record(words: [u32; 16]) -> [u8; 64] {
        let mut r = [0; 64];
        for (i, w) in words.iter().enumerate() {
            r[i * 4..i * 4 + 4].copy_from_slice(&w.to_le_bytes());
        }
        r
    }
    #[test]
    fn rgb565_primary_bounds_use_two_bytes_but_budget_full_compositor_storage() {
        for op in [1, 2, 3, 7] {
            let r = record([op, 0, 0, 0, 64, 32, 0, 0, 16, 0, 0, 128, 0, 0, 0, 0]);
            assert_eq!(validate(&r, 64, 32, 4096, 0), Ok(8192));
            assert_eq!(validate(&r, 64, 32, 4095, 0), Err(DESKTOP));
            let mut short = r;
            short[44..48].copy_from_slice(&124u32.to_le_bytes());
            assert_eq!(validate(&short, 64, 32, 4096, 0), Err(DESKTOP));
        }
        for format in [1, 15, 24, 32, u32::MAX] {
            let r = record([1, 0, 0, 0, 64, 32, 0, 0, format, 0, 0, 256, 0, 0, 0, 0]);
            assert_eq!(validate(&r, 64, 32, 8192, 0), Err(BATCH));
        }
        for op in [4, 5] {
            let r = record([op, 0, 0, 0, 64, 32, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0]);
            assert_eq!(validate(&r, 64, 32, 8192, 0), Err(BATCH));
        }
        let r = record([2, 0, 0, 0, 1, 1, 0, 0, 16, 0, 2, 4, 0, 0, 0, 0]);
        assert_eq!(validate(&r, 64, 32, 4, 0), Ok(4));
    }
    #[test]
    fn desktop_roles_and_epoch_guards() {
        for op in [1, 2, 3, 7] {
            let r = record([op, 0, 0, 0, 64, 64, 0, 0, 0, 0, 0, 256, 0, 0, 0, 0]);
            assert_eq!(validate(&r, 64, 64, 16384, 0), Ok(16384));
            assert_eq!(validate(&r, 64, 64, 16383, 0), Err(DESKTOP));
        }
        assert_eq!(
            validate(
                &record([8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 95, 0, 1, 0, 1, 0]),
                64,
                64,
                0,
                0
            ),
            Ok(0)
        );
        assert_eq!(
            validate(
                &record([8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 96, 0, 1, 0, 1, 0]),
                64,
                64,
                0,
                0
            ),
            Err(DESKTOP)
        );
        assert_eq!(
            validate(
                &record([6, 1, 0, 0, 64, 64, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0]),
                64,
                64,
                0,
                0
            ),
            Ok(16384)
        );
        assert_eq!(
            validate(
                &record([6, 1, 0, 0, 64, 64, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0]),
                64,
                64,
                0,
                0
            ),
            Err(DESKTOP)
        );
        assert_eq!(
            validate(
                &record([4, 0, 0, 0, 64, 64, 0xff123456, 0, 0, 0, 0, 0, 0, 0, 0, 0]),
                64,
                64,
                0,
                0
            ),
            Ok(16384)
        );
    }
    #[test]
    fn desktop_rejects_overflow_partial_primary_and_budget() {
        assert!(!rect(u32::MAX, 0, 2, 1, u32::MAX, 1));
        assert_eq!(
            validate(
                &record([
                    1,
                    0,
                    0,
                    0,
                    u32::MAX,
                    u32::MAX,
                    0,
                    0,
                    0,
                    0,
                    u32::MAX - 3,
                    u32::MAX,
                    0,
                    0,
                    0,
                    0
                ]),
                u32::MAX,
                u32::MAX,
                u32::MAX,
                0
            ),
            Err(DESKTOP)
        );
        assert_eq!(
            validate(
                &record([1, 0, 0, 0, 63, 64, 0, 0, 0, 0, 0, 256, 0, 0, 0, 0]),
                64,
                64,
                16384,
                0
            ),
            Err(DESKTOP)
        );
        assert_eq!(
            validate(
                &record([5, 0, 0, 0, 64, 64, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0]),
                64,
                64,
                0,
                0
            ),
            Err(DESKTOP)
        );
        assert_eq!(
            validate(
                &record([4, 0, 0, 0, u32::MAX, u32::MAX, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]),
                u32::MAX,
                u32::MAX,
                0,
                0
            ),
            Err(LIMIT)
        );
        assert_eq!(
            validate(
                &record([4, 0, 0, 0, 64, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]),
                64,
                64,
                0,
                64 * 1024 * 1024
            ),
            Err(LIMIT)
        );
    }
}
