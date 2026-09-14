// SPDX-License-Identifier: GPL-2.0-or-later
//! Allocation-free host command engine. QEMU owns DMA, scheduling and dirty tracking.
#![cfg_attr(not(test), no_std)]

#[cfg(not(test))]
#[panic_handler]
fn panic(_: &core::panic::PanicInfo<'_>) -> ! {
    extern "C" {
        fn abort() -> !;
    }
    // A violated host invariant cannot unwind through QEMU's C ABI.
    unsafe { abort() }
}

// Precompiled core/compiler_builtins can retain DWARF personality references
// even when this crate uses panic=abort. We do not link an unwinding runtime:
// an unexpected unwind through this freestanding C boundary is fatal, never
// reported as handled or allowed to continue with partially mutated resources.
#[cfg(not(test))]
#[no_mangle]
pub extern "C" fn rust_eh_personality(
    _version: i32,
    _actions: i32,
    _exception_class: u64,
    _exception: *mut core::ffi::c_void,
    _context: *mut core::ffi::c_void,
) -> i32 {
    extern "C" {
        fn abort() -> !;
    }
    unsafe { abort() }
}

mod arrays;
mod batch;
mod composition;
mod cursor;
mod data;
mod desktop;
mod device;
mod evaluator;
mod export;
mod gl;
#[allow(
    non_snake_case,
    non_upper_case_globals,
    non_camel_case_types,
    dead_code
)]
mod gl_api;
mod gl_validation;
mod image;
mod lists;
mod pixel_image;
mod pixels;
mod primary;
mod publication;
mod query;
mod resource;
mod scalar;
mod selection;
mod state;
mod stipple;
mod submission;
mod texture;
mod vector;
mod worker;

const COMMAND_BYTES: usize = 40;
const MAX_COMMANDS: usize = 64;
const MAX_WORK: u64 = 64 * 1024 * 1024;
const ROW_QUANTUM: u32 = 2048;
const COUNT: u32 = 1;
const COMMAND: u32 = 3;
const BOUNDS: u32 = 4;
const WORK_LIMIT: u32 = 5;

#[derive(Clone, Copy)]
struct Command {
    op: u32,
    bpp: u32,
    src: u32,
    dst: u32,
    ss: u32,
    ds: u32,
    width: u32,
    height: u32,
    color: u32,
    reserved: u32,
}
impl Command {
    fn decode(bytes: &[u8]) -> Self {
        let word = |i| u32::from_le_bytes(bytes[i..i + 4].try_into().unwrap());
        Self {
            op: word(0),
            bpp: word(4),
            src: word(8),
            dst: word(12),
            ss: word(16),
            ds: word(20),
            width: word(24),
            height: word(28),
            color: word(32),
            reserved: word(36),
        }
    }
    fn rect(&self, offset: u32, stride: u32, vram: u64) -> bool {
        if self.width == 0 || self.height == 0 {
            return false;
        }
        let row = u64::from(self.width) * u64::from(self.bpp);
        if row > u64::from(stride) {
            return false;
        }
        let end = u64::from(offset) + u64::from(self.height - 1) * u64::from(stride) + row;
        offset.is_multiple_of(self.bpp) && stride.is_multiple_of(self.bpp) && end <= vram
    }
}

fn validate(commands: &[u8], vram: u64) -> Result<u64, u32> {
    if commands.is_empty()
        || !commands.len().is_multiple_of(COMMAND_BYTES)
        || commands.len() / COMMAND_BYTES > MAX_COMMANDS
    {
        return Err(COUNT);
    }
    let mut work = 0u64;
    for bytes in commands.as_chunks::<COMMAND_BYTES>().0 {
        let c = Command::decode(bytes);
        if !matches!(c.op, 1..=3)
            || !matches!(c.bpp, 1 | 2 | 4)
            || c.reserved != 0
            || (c.op != 2 && (c.src != 0 || c.ss != 0))
            || (c.op != 1 && c.color != 0)
        {
            return Err(COMMAND);
        }
        if !c.rect(c.dst, c.ds, vram) || (c.op == 2 && (c.ss != c.ds || !c.rect(c.src, c.ss, vram)))
        {
            return Err(BOUNDS);
        }
        work += u64::from(c.width) * u64::from(c.height) * u64::from(c.bpp);
        if work > MAX_WORK {
            return Err(WORK_LIMIT);
        }
    }
    Ok(work)
}

/// Progress remains in QEMU's existing migration fields; no Rust pointers persist.
#[repr(C)]
#[derive(Default, Debug, Clone, Copy, PartialEq, Eq)]
pub struct Progress {
    pub command: u32,
    pub row: u32,
    pub column: u32,
}

#[repr(C)]
#[derive(Default, Debug)]
pub struct Work {
    pub bytes: u64,
    pub chunks: u32,
    pub complete: u32,
}

fn execute(
    commands: &[u8],
    vram_bytes: u64,
    progress: &mut Progress,
    budget: u32,
    mut transfer: impl FnMut(u32, u32, u64, u64, u32, u32),
) -> Result<Work, u32> {
    // Revalidation also protects restored progress and makes this ABI independently safe
    // against malformed batches. At most 64 fixed records, with no allocation.
    validate(commands, vram_bytes)?;
    let count = (commands.len() / COMMAND_BYTES) as u32;
    if progress.command > count
        || (progress.command == count && (progress.row != 0 || progress.column != 0))
    {
        return Err(BOUNDS);
    }
    if progress.command < count {
        let c = Command::decode(&commands[progress.command as usize * COMMAND_BYTES..]);
        if progress.row >= c.height
            || u64::from(progress.column) >= u64::from(c.width) * u64::from(c.bpp)
            || !progress.column.is_multiple_of(c.bpp)
        {
            return Err(BOUNDS);
        }
    }
    let mut work = Work::default();
    while progress.command < count && work.bytes < u64::from(budget) && work.chunks < ROW_QUANTUM {
        let c = Command::decode(&commands[progress.command as usize * COMMAND_BYTES..]);
        let bytes = c.width * c.bpp; // Validated <= stride (u32).
        let reverse = c.op == 2 && c.dst > c.src;
        let y = if reverse {
            c.height - 1 - progress.row
        } else {
            progress.row
        };
        let n = (bytes - progress.column).min(budget - work.bytes as u32) & !(c.bpp - 1);
        if n == 0 {
            break;
        }
        let x = if reverse {
            bytes - progress.column - n
        } else {
            progress.column
        };
        let offset = u64::from(c.dst) + u64::from(y) * u64::from(c.ds) + u64::from(x);
        let source = u64::from(c.src) + u64::from(y) * u64::from(c.ss) + u64::from(x);
        // Guest RAM remains outside Rust's ownership/reference model: CPU threads
        // may access it concurrently with emulated DMA. The QEMU primitive applies
        // the transfer using its existing RAM semantics, then marks dirty metadata.
        transfer(c.op, c.bpp, source, offset, n, c.color);
        work.bytes += u64::from(n);
        work.chunks += 1;
        progress.column += n;
        if progress.column == bytes {
            progress.column = 0;
            progress.row += 1;
            if progress.row == c.height {
                progress.row = 0;
                progress.command += 1;
            }
        }
    }
    work.complete = u32::from(progress.command == count);
    Ok(work)
}

/// # Safety
/// `commands` references `count * 40` immutable bytes (count <= 64). `work`
/// is writable and does not alias commands. Pointers remain owned by the caller.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_2d_validate(
    commands: *const u8,
    count: u32,
    vram_bytes: u64,
    work: *mut u64,
) -> u32 {
    if work.is_null() {
        return BOUNDS;
    }
    unsafe {
        *work = 0;
    }
    if count == 0 || count > MAX_COMMANDS as u32 {
        return COUNT;
    }
    if commands.is_null() {
        return BOUNDS;
    }
    let commands = unsafe { core::slice::from_raw_parts(commands, count as usize * COMMAND_BYTES) };
    match validate(commands, vram_bytes) {
        Ok(bytes) => {
            unsafe {
                *work = bytes;
            }
            0
        }
        Err(error) => error,
    }
}

pub type Transfer = unsafe extern "C" fn(*mut core::ffi::c_void, u32, u32, u64, u64, u32, u32);

/// # Safety
/// Caller holds the QEMU device lock. Commands are an immutable DMA snapshot;
/// progress and result are exclusively writable, valid and disjoint. The transfer
/// callback performs all guest RAM access with QEMU semantics and marks dirty
/// metadata. It must not reenter this engine. No guest RAM pointer is passed to
/// Rust, and no command/progress pointer or callback is retained after return.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_2d_execute(
    commands: *const u8,
    count: u32,
    vram_bytes: u64,
    progress: *mut Progress,
    budget: u32,
    transfer: Option<Transfer>,
    opaque: *mut core::ffi::c_void,
    result: *mut Work,
) -> u32 {
    if result.is_null() {
        return BOUNDS;
    }
    unsafe {
        *result = Work::default();
    }
    if count == 0 || count > MAX_COMMANDS as u32 {
        return COUNT;
    }
    if commands.is_null() || progress.is_null() || transfer.is_none() {
        return BOUNDS;
    }
    let commands = unsafe { core::slice::from_raw_parts(commands, count as usize * COMMAND_BYTES) };
    let progress = unsafe { &mut *progress };
    match execute(
        commands,
        vram_bytes,
        progress,
        budget,
        |op, bpp, src, dst, bytes, color| unsafe {
            transfer.unwrap()(opaque, op, bpp, src, dst, bytes, color)
        },
    ) {
        Ok(work) => {
            unsafe {
                *result = work;
            }
            0
        }
        Err(error) => error,
    }
}

#[cfg(test)]
mod tests;
