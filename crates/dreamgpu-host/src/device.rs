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
    pub enqueue: unsafe extern "C" fn(*mut c_void, *mut u8, u32, u32) -> u32,
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
unsafe fn gl_submit(c: GlCallbacks, r: GlRequest, out: &mut GlResult) -> u32 {
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
        if r.result_capacity < required || r.result_capacity > DG_GL_MAX_READBACK_BYTES {
            return DG_GL_ERROR_BATCH;
        }
        if r.result_capacity == 0
            || r.result_address
                .checked_add(r.result_capacity as u64)
                .is_none()
            || unsafe { (c.memory.ram)(opaque, r.result_address, r.result_capacity, 1) } == 0
        {
            return DG_GL_ERROR_DMA;
        }
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
        if op == DG_GL_DESKTOP && (r.bpp != 32 || r.busy_2d != 0) {
            return DG_GL_ERROR_DESKTOP;
        }
        offset += size as usize;
    }
    let error = unsafe { (c.enqueue)(opaque, snapshot.data, r.bytes, out.records) };
    if error == 0 {
        snapshot.release();
    }
    error
}

#[cfg(test)]
mod tests;
