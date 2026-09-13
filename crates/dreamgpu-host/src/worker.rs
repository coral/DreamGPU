// SPDX-License-Identifier: GPL-2.0-or-later
//! Bounded output queue and CPU export/readback ownership. Callers keep QEMU
//! synchronization/OS adapters; policy and records are render-core owned.
use crate::{gl_api::*, publication::Slot};
use core::{ffi::c_void, ptr};
const OUTPUTS: usize = (DG_GL_MAX_DRAWABLES * DG_GL_EXPORT_SLOTS + 64) as usize;
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Output {
    pub slot: *mut Slot,
    pub packet: [u8; 128],
}
impl Output {
    const EMPTY: Self = Self {
        slot: ptr::null_mut(),
        packet: [0; 128],
    };
}
#[repr(C)]
pub struct OutputQueue {
    pub head: u32,
    pub len: u32,
    pub pending: u32,
    pub desktop: u32,
    pub items: [Output; OUTPUTS],
}
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_output_push(
    q: *mut OutputQueue,
    slot: *mut Slot,
    packet: *const u8,
) -> u32 {
    // Engine mutex held; no slot/image ownership is transferred through a Rust borrow.
    unsafe {
        if (*q).len as usize == OUTPUTS
            || (*q).pending as usize == OUTPUTS
            || slot.is_null() && (*q).desktop == 64
        {
            return 0;
        }
        let index = ((*q).head + (*q).len) as usize % OUTPUTS;
        let mut out = Output::EMPTY;
        out.slot = slot;
        if slot.is_null() {
            ptr::copy_nonoverlapping(packet, out.packet.as_mut_ptr(), 128);
            (*q).desktop += 1;
        }
        (*q).items[index] = out;
        (*q).len += 1;
        (*q).pending += 1;
    }
    1
}
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_output_pop(q: *mut OutputQueue, out: *mut Output) -> u32 {
    unsafe {
        if (*q).len == 0 {
            return 0;
        }
        let head = (*q).head as usize;
        out.write((*q).items[head]);
        (*q).items[head] = Output::EMPTY;
        (*q).head = ((head + 1) % OUTPUTS) as u32;
        (*q).len -= 1;
    }
    1
}
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_output_done(q: *mut OutputQueue, desktop: u32) {
    unsafe {
        (*q).pending = (*q).pending.checked_sub(1).expect("pending output credit");
        if desktop != 0 {
            (*q).desktop = (*q).desktop.checked_sub(1).expect("desktop output credit");
        }
    }
}
#[repr(C)]
pub struct CpuSlot {
    pub pixels: *mut u8,
    pub bytes: usize,
    pub fd: i32,
    pub width: u32,
    pub height: u32,
    pub stride: u32,
    pub published: u32,
    pub epoch: u64,
    pub sequence: u64,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct CpuMemory {
    pub opaque: *mut c_void,
    pub allocate: unsafe extern "C" fn(*mut c_void, usize, *mut i32) -> *mut u8,
    pub free: unsafe extern "C" fn(*mut c_void, *mut u8, usize, i32),
}
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_cpu_claim(slots: *mut CpuSlot, epoch: u64, sequence: u64) -> u32 {
    for i in 0..DG_TRANSPORT_MAX_CPU_SLOTS {
        let s = unsafe { slots.add(i as usize) };
        unsafe {
            if (*s).published == 0 {
                (*s).published = 1;
                (*s).epoch = epoch;
                (*s).sequence = sequence;
                return i;
            }
        }
    }
    u32::MAX
}
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_cpu_release(s: *mut CpuSlot, epoch: u64, sequence: u64) {
    unsafe {
        if (*s).published != 0 && (*s).epoch == epoch && (*s).sequence == sequence {
            (*s).published = 0;
        }
    }
}
/// # Safety
/// Slot was claimed after the prior consumer released it. Mapping callbacks own
/// exact OS allocations; no Rust slice asserts exclusive access to guest VRAM.
/// CPU export mappings are host-owned and have no live consumer during writes.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_cpu_storage(
    s: *mut CpuSlot,
    memory: *const CpuMemory,
    width: u32,
    height: u32,
) -> u32 {
    if width == 0 || height == 0 || width > DG_GL_MAX_DIMENSION || height > DG_GL_MAX_DIMENSION {
        return DG_GL_ERROR_LIMIT;
    }
    let stride = (width * 4 + 255) & !255;
    let bytes = ((u64::from(stride) * u64::from(height) + 65535) & !65535) as usize;
    if bytes > DG_TRANSPORT_MAX_CPU_BYTES as usize {
        return DG_GL_ERROR_LIMIT;
    }
    let m = unsafe { *memory };
    let replace = unsafe { (*s).pixels.is_null() || (*s).bytes != bytes };
    if replace {
        unsafe { dreamgpu_cpu_storage_free(s, memory) };
        let mut fd = -1;
        let pixels = unsafe { (m.allocate)(m.opaque, bytes, &mut fd) };
        unsafe {
            (*s).pixels = pixels;
            (*s).fd = fd;
            (*s).bytes = bytes;
        }
        if pixels.is_null() {
            return DG_GL_ERROR_HOST;
        }
    }
    unsafe {
        if replace || (*s).width != width || (*s).height != height || (*s).stride != stride {
            ptr::write_bytes((*s).pixels, 0, bytes);
            (*s).width = width;
            (*s).height = height;
            (*s).stride = stride;
        }
    }
    0
}
/// # Safety
/// Consumer leases and transfer callback are drained before freeing this mapping.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_cpu_storage_free(s: *mut CpuSlot, memory: *const CpuMemory) {
    let m = unsafe { *memory };
    let (pixels, bytes, fd) = unsafe { ((*s).pixels, (*s).bytes, (*s).fd) };
    unsafe {
        (*s).pixels = ptr::null_mut();
        (*s).bytes = 0;
        (*s).fd = -1;
        if !pixels.is_null() {
            (m.free)(m.opaque, pixels, bytes, fd);
        }
    }
}
fn get(p: &[u8], off: u32) -> u32 {
    u32::from_le_bytes(p[off as usize..off as usize + 4].try_into().unwrap())
}
fn getq(p: &[u8], off: u32) -> u64 {
    u64::from_le_bytes(p[off as usize..off as usize + 8].try_into().unwrap())
}
fn put(p: &mut [u8], off: u32, x: u32) {
    p[off as usize..off as usize + 4].copy_from_slice(&x.to_le_bytes());
}
fn putq(p: &mut [u8], off: u32, x: u64) {
    p[off as usize..off as usize + 8].copy_from_slice(&x.to_le_bytes());
}
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_cpu_packet(
    s: *const CpuSlot,
    index: u32,
    subtype: u32,
    r: *const u8,
    cpu_epoch: u64,
    cpu_generation: u64,
    out: *mut u8,
) {
    let r = unsafe { core::slice::from_raw_parts(r, DG_DESKTOP_BYTES as usize) };
    let mut p = [0u8; 128];
    for (off, x) in unsafe {
        [
            (DG_TRANSPORT_OFF_MAGIC, DG_TRANSPORT_MAGIC),
            (DG_TRANSPORT_OFF_VERSION, DG_TRANSPORT_VERSION),
            (DG_TRANSPORT_OFF_KIND, DG_TRANSPORT_KIND_CPU_DESKTOP),
            (DG_TRANSPORT_OFF_SIZE, 128),
            (DG_TRANSPORT_CPU_OFF_SUBTYPE, subtype),
            (DG_TRANSPORT_OFF_WIDTH, (*s).width),
            (DG_TRANSPORT_OFF_HEIGHT, (*s).height),
            (DG_TRANSPORT_OFF_STRIDE, (*s).stride),
            (DG_TRANSPORT_OFF_FOURCC, DG_TRANSPORT_FORMAT_ARGB8888),
            (DG_TRANSPORT_OFF_SLOT, DG_TRANSPORT_CPU_SLOT_BASE + index),
            (DG_TRANSPORT_CPU_OFF_DST_X, get(r, DG_DESKTOP_DST_X)),
            (DG_TRANSPORT_CPU_OFF_DST_Y, get(r, DG_DESKTOP_DST_Y)),
        ]
    } {
        put(&mut p, off, x);
    }
    unsafe {
        putq(&mut p, DG_TRANSPORT_OFF_EPOCH, (*s).epoch);
        putq(&mut p, DG_TRANSPORT_OFF_GENERATION, (*s).sequence);
        putq(&mut p, DG_TRANSPORT_CPU_OFF_ALLOCATION, (*s).bytes as u64);
    }
    if subtype == DG_TRANSPORT_CPU_RETURN {
        putq(&mut p, DG_TRANSPORT_CPU_OFF_LEGACY_EPOCH, cpu_epoch);
        putq(&mut p, DG_TRANSPORT_CPU_OFF_LEGACY_FRAME, cpu_generation);
    }
    unsafe { ptr::copy_nonoverlapping(p.as_ptr(), out, 128) };
}
#[repr(C)]
pub struct Reply {
    pub pending: u32,
    pub ready: u32,
    pub epoch: u64,
    pub sequence: u64,
    pub token: u64,
    pub packet: [u8; 128],
    pub fd: i32,
}
#[repr(C)]
pub struct ReplyResult {
    pub fd: i32,
    pub error: u32,
    pub packet: [u8; 128],
}
/// # Safety
/// All reply transitions occur under the engine mutex. A returned value2
/// transfers received_fd ownership;0 rejects it,1 ignores a cancelled reply.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_reply_receive(s: *mut Reply, packet: *const u8, fd: i32) -> u32 {
    let p = unsafe { core::slice::from_raw_parts(packet, 128) };
    let matches = unsafe {
        getq(p, DG_TRANSPORT_OFF_EPOCH) == (*s).epoch
            && getq(p, DG_TRANSPORT_OFF_GENERATION) == (*s).sequence
            && getq(p, DG_TRANSPORT_DESKTOP_OFF_TOKEN) == (*s).token
    };
    unsafe {
        if (*s).pending == 0 && matches {
            return 1;
        }
        if (*s).pending == 0 || (*s).ready != 0 || !matches {
            return 0;
        }
        ptr::copy_nonoverlapping(packet, (*s).packet.as_mut_ptr(), 128);
        (*s).fd = fd;
        (*s).ready = 1;
    }
    2
}
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_reply_begin(
    s: *mut Reply,
    epoch: u64,
    sequence: u64,
    packet: *mut u8,
) {
    unsafe {
        assert_eq!((*s).fd, -1);
        (*s).pending = 1;
        (*s).ready = 0;
        (*s).epoch = epoch;
        (*s).sequence = sequence;
        (*s).token = sequence;
        ptr::copy_nonoverlapping(
            sequence.to_le_bytes().as_ptr(),
            packet.add(DG_TRANSPORT_DESKTOP_OFF_TOKEN as usize),
            8,
        );
    }
}
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_reply_finish(
    s: *mut Reply,
    error: u32,
    cancelled: u32,
    failed: u32,
    out: *mut ReplyResult,
) {
    unsafe {
        let error = if cancelled != 0 {
            DG_GL_ERROR_GENERATION
        } else if error == 0 && ((*s).ready == 0 || failed != 0) {
            DG_GL_ERROR_TRANSPORT
        } else {
            error
        };
        out.write(ReplyResult {
            fd: (*s).fd,
            error,
            packet: (*s).packet,
        });
        (*s).fd = -1;
        (*s).pending = 0;
        (*s).ready = 0;
    }
}
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_reply_layout(
    r: *const ReplyResult,
    width: u32,
    height: u32,
    stat_ok: u32,
    stat_bytes: i64,
    bytes: *mut u64,
    stride: *mut u32,
) -> u32 {
    let r = unsafe { &*r };
    if r.error != 0 {
        return r.error;
    }
    let n = getq(&r.packet, DG_TRANSPORT_CPU_OFF_ALLOCATION);
    let pitch = get(&r.packet, DG_TRANSPORT_OFF_STRIDE);
    if get(&r.packet, 16) != 0
        || r.fd < 0
        || get(&r.packet, DG_TRANSPORT_OFF_WIDTH) != width
        || get(&r.packet, DG_TRANSPORT_OFF_HEIGHT) != height
        || u64::from(pitch) != u64::from(width) * 4
        || n != u64::from(pitch) * u64::from(height)
        || n > u64::from(DG_TRANSPORT_MAX_CPU_BYTES)
        || stat_ok == 0
        || stat_bytes < 0
        || (stat_bytes as u64) < n
    {
        return DG_GL_ERROR_DESKTOP;
    }
    unsafe {
        *bytes = n;
        *stride = pitch;
    }
    0
}
#[cfg(test)]
mod tests;
