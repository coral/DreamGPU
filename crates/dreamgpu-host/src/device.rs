// SPDX-License-Identifier: GPL-2.0-or-later
//! Device transactions own immutable host DMA snapshots. No guest RAM references.
use crate::gl_api::*;
use core::{ffi::c_void, ptr, slice};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Memory {
    pub opaque: *mut c_void,
    pub allocate: unsafe extern "C" fn(*mut c_void, u32) -> *mut u8,
    pub free: unsafe extern "C" fn(*mut c_void, *mut u8),
    pub read: unsafe extern "C" fn(*mut c_void, u64, *mut u8, u32) -> u32,
    pub ram: unsafe extern "C" fn(*mut c_void, u64, u32, u32) -> u32,
}
struct Snapshot {
    memory: Memory,
    data: *mut u8,
}
impl Snapshot {
    unsafe fn capture(memory: Memory, address: u64, bytes: u32) -> Option<Self> {
        address.checked_add(bytes as u64)?;
        let data = unsafe { (memory.allocate)(memory.opaque, bytes) };
        if data.is_null() {
            return None;
        }
        let owned = Self { memory, data };
        if unsafe { (memory.read)(memory.opaque, address, data, bytes) } == 0 {
            return None;
        }
        Some(owned)
    }
    fn release(mut self) {
        self.data = ptr::null_mut();
    }
}
impl Drop for Snapshot {
    fn drop(&mut self) {
        if !self.data.is_null() {
            unsafe { (self.memory.free)(self.memory.opaque, self.data) };
        }
    }
}
fn word(data: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes(data[offset..offset + 4].try_into().unwrap())
}

/// All pointers denote private host storage, not directly mapped guest RAM.
/// DMA callbacks initialize the entire destination on success and retain no pointer.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dreamgpu_device_2d_capture(
    memory: *const Memory,
    address: u64,
    count: u32,
    commands: *mut u8,
    vram: u64,
    work: *mut u64,
) -> u32 {
    if count == 0 || count > DG_MAX_COMMANDS {
        return DG_ERROR_BATCH_COUNT;
    }
    let bytes = count * DG_COMMAND_BYTES;
    if address.checked_add(bytes as u64).is_none()
        || unsafe { ((*memory).read)((*memory).opaque, address, commands, bytes) } == 0
    {
        return DG_ERROR_DMA;
    }
    unsafe { crate::dreamgpu_2d_validate(commands, count, vram, work) }
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct CursorRequest {
    pub address: u64,
    pub operation: u32,
    pub flags: u32,
    pub bytes: u32,
    pub width: u32,
    pub height: u32,
    pub hot_x: u32,
    pub hot_y: u32,
    pub format: u32,
}
fn cursor_shape(r: &CursorRequest) -> bool {
    r.width != 0
        && r.width <= DG_CURSOR_MAX_DIMENSION
        && r.height != 0
        && r.height <= DG_CURSOR_MAX_DIMENSION
        && r.hot_x < r.width
        && r.hot_y < r.height
        && (r.format == DG_CURSOR_ARGB_PREMULTIPLIED || r.format == DG_CURSOR_AND_XOR)
}
/// Destination is the exclusive fixed-size host cursor snapshot, updated only on success.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dreamgpu_device_cursor(
    memory: *const Memory,
    request: *const CursorRequest,
    destination: *mut u8,
) -> u32 {
    let r = unsafe { *request };
    if r.flags & !DG_CURSOR_FLAGS_MASK != 0 {
        return DG_CURSOR_ERROR_FLAGS;
    }
    if r.operation == DG_CURSOR_MOVE {
        return 0;
    }
    if r.operation != DG_CURSOR_SHAPE || !cursor_shape(&r) || r.bytes != r.width * r.height * 8 {
        return DG_CURSOR_ERROR_SHAPE;
    }
    let m = unsafe { *memory };
    if r.address.checked_add(r.bytes as u64).is_none()
        || unsafe { (m.ram)(m.opaque, r.address, r.bytes, 0) } == 0
    {
        return DG_CURSOR_ERROR_DMA;
    }
    let Some(snapshot) = (unsafe { Snapshot::capture(m, r.address, r.bytes) }) else {
        return DG_CURSOR_ERROR_DMA;
    };
    if unsafe {
        crate::cursor::dreamgpu_cursor_validate(snapshot.data, r.width * r.height, r.format)
    } == 0
    {
        return DG_CURSOR_ERROR_SHAPE;
    }
    unsafe {
        ptr::write_bytes(
            destination,
            0,
            (DG_CURSOR_MAX_DIMENSION * DG_CURSOR_MAX_DIMENSION * 8) as usize,
        );
        ptr::copy_nonoverlapping(snapshot.data, destination, r.bytes as usize);
    }
    0
}

#[repr(C)]
pub struct Restore {
    pub cursor: CursorRequest,
    pub cursor_status: u32,
    pub gl_status: u32,
    pub status: u32,
    pub count: u32,
    pub command: u32,
    pub row: u32,
    pub column: u32,
    pub vram: u64,
}
/// Serialized host snapshots have full fixed capacities; counts are checked before slicing.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dreamgpu_device_restore(
    r: *const Restore,
    pixels: *const u8,
    commands: *const u8,
    work: *mut u64,
) -> u32 {
    let r = unsafe { &*r };
    let c = &r.cursor;
    if c.flags & !DG_CURSOR_FLAGS_MASK != 0
        || r.cursor_status & DG_STATUS_BUSY != 0
        || c.width > DG_CURSOR_MAX_DIMENSION
        || c.height > DG_CURSOR_MAX_DIMENSION
        || (c.width == 0) != (c.height == 0)
        || (c.width != 0
            && (!cursor_shape(c)
                || unsafe {
                    crate::cursor::dreamgpu_cursor_validate(pixels, c.width * c.height, c.format)
                } == 0))
        || r.gl_status & DG_STATUS_BUSY != 0
    {
        return 0;
    }
    if r.status & DG_STATUS_BUSY != 0 {
        if unsafe { crate::dreamgpu_2d_validate(commands, r.count, r.vram, work) } != 0
            || r.command >= r.count
        {
            return 0;
        }
        let data = unsafe {
            slice::from_raw_parts(
                commands.add((r.command * DG_COMMAND_BYTES) as usize),
                DG_COMMAND_BYTES as usize,
            )
        };
        let bpp = word(data, DG_CMD_BPP as usize);
        if r.row >= word(data, DG_CMD_HEIGHT as usize)
            || r.column >= word(data, DG_CMD_WIDTH as usize) * bpp
            || r.column % bpp != 0
        {
            return 0;
        }
    }
    1
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct GlRequest {
    pub address: u64,
    pub result_address: u64,
    pub bytes: u32,
    pub generation: u32,
    pub current_generation: u32,
    pub bpp: u32,
    pub busy_2d: u32,
    pub result_capacity: u32,
    pub sequence: u32,
    pub client: u32,
    pub stream: u32,
}
/// Identity handed to the engine with a snapshot, or alone with a preset error.
#[repr(C)]
#[derive(Clone, Copy, Default, Debug, PartialEq, Eq)]
pub struct Enqueue {
    pub sequence: u32,
    pub client: u32,
    pub stream: u32,
    pub error: u32,
}
#[repr(C)]
#[derive(Default)]
pub struct GlResult {
    pub sensitive: u32,
    pub records: u32,
    pub result_capacity: u32,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GlCallbacks {
    pub memory: Memory,
    pub validate: unsafe extern "C" fn(*mut c_void, *const u8, u32, *mut u32) -> u32,
    pub diagnostics_begin: unsafe extern "C" fn(*mut c_void, u32),
    pub diagnostic_record: unsafe extern "C" fn(*mut c_void, *const u8),
    // Success transfers snapshot ownership. Failure retains it in this transaction.
    pub enqueue: unsafe extern "C" fn(*mut c_void, *mut u8, u32, u32, *const Enqueue) -> u32,
}
/// Callback table and request are copied before callbacks. The read callback cannot
/// retain the allocated buffer; validator/diagnostics may only read their snapshot.
/// Successful validation guarantees complete bounded records, including QUERY payloads.
/// Enqueue returns zero iff it has taken ownership, including on asynchronous failure.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dreamgpu_device_gl_submit(
    callbacks: *const GlCallbacks,
    request: *const GlRequest,
    output: *mut GlResult,
) -> u32 {
    let c = unsafe { *callbacks };
    let r = unsafe { *request };
    let mut out = GlResult::default();
    let error = unsafe { gl_submit(c, r, &mut out) };
    unsafe { output.write(out) };
    error
}
pub(crate) unsafe fn gl_submit(c: GlCallbacks, r: GlRequest, out: &mut GlResult) -> u32 {
    if r.bytes == 0 || r.bytes > DG_GL_MAX_BYTES || r.bytes & 3 != 0 {
        return DG_GL_ERROR_BATCH;
    }
    if r.generation != r.current_generation {
        return DG_GL_ERROR_GENERATION;
    }
    let Some(snapshot) = (unsafe { Snapshot::capture(c.memory, r.address, r.bytes) }) else {
        return DG_GL_ERROR_DMA;
    };
    // Recognize invalid coherence operations before the full record validator.
    {
        let bytes = unsafe { slice::from_raw_parts(snapshot.data, r.bytes as usize) };
        let mut offset = 0;
        while offset + DG_GL_HEADER_BYTES as usize <= bytes.len() {
            let record = &bytes[offset..];
            let size = word(record, DG_GL_OFF_SIZE as usize) as usize;
            if size < DG_GL_HEADER_BYTES as usize || size > record.len() {
                break;
            }
            if word(record, DG_GL_OFF_OP as usize) == DG_GL_DESKTOP && size >= 36 {
                let op = word(record, DG_GL_HEADER_BYTES as usize);
                if op == DG_DESKTOP_READBACK || op == DG_DESKTOP_RETURN {
                    out.sensitive = op;
                }
            }
            offset += size;
        }
    }
    let opaque = c.memory.opaque;
    let error = unsafe { (c.validate)(opaque, snapshot.data, r.bytes, &mut out.records) };
    if error != 0 {
        return error;
    }
    let query = unsafe {
        word(
            slice::from_raw_parts(snapshot.data, r.bytes as usize),
            DG_GL_OFF_OP as usize,
        )
    } == DG_GL_QUERY;
    if query {
        let function = unsafe { word(slice::from_raw_parts(snapshot.data, r.bytes as usize), 32) };
        let required = unsafe {
            crate::gl_validation::dreamgpu_gl_query_result_bytes(function, snapshot.data.add(36))
        };
        match result_capacity(r.result_capacity) {
            Some(capacity) if capacity >= required => (),
            _ => return DG_GL_ERROR_BATCH,
        }
        if !unsafe { result_destination(c.memory, r.result_address, r.result_capacity) } {
            return DG_GL_ERROR_DMA;
        }
        // The destination word, flag included, travels with the batch.
        out.result_capacity = r.result_capacity;
    }
    unsafe { (c.diagnostics_begin)(opaque, r.bytes) };
    let mut offset = 0;
    while offset < r.bytes as usize {
        let record = unsafe { snapshot.data.add(offset) };
        unsafe { (c.diagnostic_record)(opaque, record) };
        let (op, size) = unsafe {
            let record = slice::from_raw_parts(record, (r.bytes as usize) - offset);
            (
                word(record, DG_GL_OFF_OP as usize),
                word(record, DG_GL_OFF_SIZE as usize),
            )
        };
        if op == DG_GL_DESKTOP {
            // Framing and reserved-field validation completed before this loop.
            let format = unsafe {
                u32::from_le_bytes(core::ptr::read_unaligned(record.add(64).cast::<[u8; 4]>()))
            };
            let expected = if format == 16 { 16 } else { 32 };
            if r.bpp != expected || r.busy_2d != 0 {
                return DG_GL_ERROR_DESKTOP;
            }
        }
        offset += size as usize;
    }
    let info = Enqueue {
        sequence: r.sequence,
        client: r.client,
        stream: r.stream,
        error: 0,
    };
    let error = unsafe { (c.enqueue)(opaque, snapshot.data, r.bytes, out.records, &info) };
    if error == 0 {
        snapshot.release();
    }
    error
}

/// DMA write of host bytes into guest RAM; nonzero on success.
pub type ResultWrite = unsafe extern "C" fn(*mut c_void, u64, *const u8, u32) -> u32;
const PAGE: u32 = DG_GL_RESULT_PAGE_BYTES;

/// The capacity a RESULT_CAPACITY word grants: up to DG_GL_MAX_READBACK_BYTES
/// contiguous, or with DG_GL_RESULT_PAGES up to DG_GL_MAX_BULK_RESULT_BYTES
/// through a page list (gl.h, DG_CAP2_GL_BULK_READ). None for zero or more.
pub(crate) fn result_capacity(word: u32) -> Option<u32> {
    let capacity = word & !DG_GL_RESULT_PAGES;
    let limit = if word & DG_GL_RESULT_PAGES != 0 {
        DG_GL_MAX_BULK_RESULT_BYTES
    } else {
        DG_GL_MAX_READBACK_BYTES
    };
    (1..=limit).contains(&capacity).then_some(capacity)
}

/// Visit the guest page of every DG_GL_RESULT_PAGE_BYTES of `bytes` named by
/// the page list at `list`, in order. False, before any visit, unless the list
/// is an aligned RAM page and every needed frame is nonzero writable RAM.
unsafe fn result_pages(
    m: Memory,
    list: u64,
    bytes: u32,
    mut visit: impl FnMut(u32, u64) -> bool,
) -> bool {
    let count = bytes.div_ceil(PAGE) as usize;
    let mut frames = [0u8; PAGE as usize];
    if count == 0
        || count * 4 > frames.len()
        || !list.is_multiple_of(u64::from(PAGE))
        || list.checked_add(u64::from(PAGE)).is_none()
        || unsafe { (m.ram)(m.opaque, list, PAGE, 0) } == 0
        || unsafe { (m.read)(m.opaque, list, frames.as_mut_ptr(), count as u32 * 4) } == 0
    {
        return false;
    }
    let page = |i: usize| u64::from(word(&frames, i * 4)) << 12;
    // Check every page before the first visit: no partial writes on failure.
    if (0..count).any(|i| page(i) == 0 || unsafe { (m.ram)(m.opaque, page(i), PAGE, 1) } == 0) {
        return false;
    }
    (0..count).all(|i| visit(i as u32, page(i)))
}

/// Submission check of a QUERY destination: `word` is the RESULT_CAPACITY
/// word, already within result_capacity. A contiguous range must be writable
/// RAM; a page list must name writable RAM pages for its whole capacity.
pub(crate) unsafe fn result_destination(m: Memory, address: u64, word: u32) -> bool {
    let Some(capacity) = result_capacity(word) else {
        return false;
    };
    if word & DG_GL_RESULT_PAGES != 0 {
        unsafe { result_pages(m, address, capacity, |_, _| true) }
    } else {
        address.checked_add(u64::from(capacity)).is_some()
            && unsafe { (m.ram)(m.opaque, address, capacity, 1) } != 0
    }
}

/// Write a completed result of `bytes` (at most the word's capacity) to its
/// destination, rechecked now: guest memory may have changed since submission.
/// Nothing is written outside the range or the listed pages.
pub(crate) unsafe fn result_write(
    m: Memory,
    write: ResultWrite,
    address: u64,
    word: u32,
    data: *const u8,
    bytes: u32,
) -> bool {
    if bytes == 0 || data.is_null() || result_capacity(word).is_none_or(|c| bytes > c) {
        return false;
    }
    if word & DG_GL_RESULT_PAGES == 0 {
        return address.checked_add(u64::from(bytes)).is_some()
            && unsafe { (m.ram)(m.opaque, address, bytes, 1) } != 0
            && unsafe { write(m.opaque, address, data, bytes) } != 0;
    }
    unsafe {
        result_pages(m, address, bytes, |i, page| {
            let offset = i * PAGE;
            let length = (bytes - offset).min(PAGE);
            write(m.opaque, page, data.add(offset as usize), length) != 0
        })
    }
}

/// Register-transport completion: write a QUERY result to the destination
/// its submission accepted. Nonzero on success; zero is DG_GL_ERROR_DMA.
///
/// # Safety
/// `memory` and `write` are the device's callbacks; `data` holds `bytes`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dreamgpu_device_result_write(
    memory: *const Memory,
    write: ResultWrite,
    address: u64,
    word: u32,
    data: *const u8,
    bytes: u32,
) -> u32 {
    if memory.is_null() {
        return 0;
    }
    u32::from(unsafe { result_write(*memory, write, address, word, data, bytes) })
}

#[cfg(test)]
mod tests;
