// SPDX-License-Identifier: GPL-2.0-or-later
//! Producer export leases, publication ordering and exact frame identity packets.
//! QEMU supplies the mutex/condition waits and native send; this module owns the
//! transition policy. Raw field accesses avoid borrowing slots whose packet/image
//! may be read concurrently while an already-published send is in progress.
use crate::{gl::Drawable, gl_api::*};
use core::ffi::c_void;
#[cfg(test)]
use core::ptr;
pub const FREE: u32 = 0;
pub const RENDERING: u32 = 1;
pub const PENDING: u32 = 2;
pub const PUBLISHED: u32 = 3;
const COUNT: usize = (DG_GL_MAX_DRAWABLES * DG_GL_EXPORT_SLOTS) as usize;
#[repr(C)]
pub struct Slot {
    pub image: *mut c_void,
    pub state: u32,
    pub sending: u32,
    pub release_pending: u32,
    pub generation: u64,
    pub packet: [u8; DG_TRANSPORT_PACKET_BYTES as usize],
}
#[cfg(test)]
impl Slot {
    const EMPTY: Self = Self {
        image: ptr::null_mut(),
        state: FREE,
        sending: 0,
        release_pending: 0,
        generation: 0,
        packet: [0; DG_TRANSPORT_PACKET_BYTES as usize],
    };
}
#[repr(C)]
pub struct ImageMetadata {
    pub stride: u32,
    pub offset: u32,
    pub modifier: u64,
    pub ready_fence: u32,
    pub uuid: [u8; 16],
}
fn word(p: &[u8], offset: u32) -> u64 {
    u64::from_le_bytes(p[offset as usize..offset as usize + 8].try_into().unwrap())
}
fn put32(p: &mut [u8], off: u32, v: u32) {
    p[off as usize..off as usize + 4].copy_from_slice(&v.to_le_bytes());
}
fn put64(p: &mut [u8], off: u32, v: u64) {
    p[off as usize..off as usize + 8].copy_from_slice(&v.to_le_bytes());
}
/// # Safety
/// Slots is COUNT live records; engine mutex held. Only state fields are read;
/// published packets/native images can concurrently be used by the send adapter.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_slot_claim(slots: *mut Slot, first: u32) -> u32 {
    if first as usize >= COUNT || !first.is_multiple_of(DG_GL_EXPORT_SLOTS) {
        return u32::MAX;
    }
    for i in first..first + DG_GL_EXPORT_SLOTS {
        let s = unsafe { slots.add(i as usize) };
        if unsafe { (*s).state } == FREE {
            unsafe {
                (*s).state = RENDERING;
                (*s).release_pending = 0
            };
            return i;
        }
    }
    u32::MAX
}
/// # Safety
/// Engine mutex held. Epoch/sequence come from a validated consumer record.
/// Early release records credit without recycling an image still being sent.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_slot_release(s: *mut Slot, epoch: u64, generation: u64) {
    unsafe {
        if (*s).state == PUBLISHED
            && word(&(*s).packet, DG_TRANSPORT_OFF_EPOCH) == epoch
            && (*s).generation == generation
        {
            if (*s).sending != 0 {
                (*s).release_pending = 1;
            } else {
                (*s).state = FREE;
            }
        }
    }
}
/// # Safety
/// Engine mutex held after completion wait. Publish before unlocking for send so
/// an immediate consumer release cannot be lost. No packet fields change here.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_slot_publish(s: *mut Slot, ready: u32, unavailable: u32) -> u32 {
    unsafe {
        assert_eq!((*s).state, PENDING);
        (*s).state = PUBLISHED;
        (*s).sending = 1;
    }
    u32::from(ready != 0 && unavailable == 0)
}
/// # Safety
/// Engine mutex held after synchronous send returns. Drawable remains live until
/// output drain finishes. Only publication fields change; no whole-record copy.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_slot_sent(
    s: *mut Slot,
    d: *mut Drawable,
    sent: u32,
    publish: u32,
    ready: u32,
) -> u32 {
    unsafe {
        assert_eq!((*s).state, PUBLISHED);
        assert_eq!((*s).sending, 1);
        (*s).sending = 0;
        if sent == 0 || (*s).release_pending != 0 {
            (*s).state = FREE;
        }
        if sent != 0 {
            (*d).published_epoch = word(&(*s).packet, DG_TRANSPORT_OFF_EPOCH);
            (*d).published_generation = (*s).generation;
        }
    }
    u32::from(sent == 0 && (publish != 0 || ready == 0))
}
/// # Safety
/// Engine mutex held, no pending/sending operation owns this slot. Native image
/// destruction is separate; aborting a render retains the reusable allocation.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_slot_abort(s: *mut Slot) {
    unsafe {
        assert_eq!((*s).sending, 0);
        (*s).state = FREE;
        (*s).release_pending = 0;
    }
}
/// # Safety
/// Render worker owns this RENDERING slot; consumer ignores it. Metadata is a
/// disjoint immutable native-image snapshot. The caller publishes PENDING under
/// the engine lock after recording last_present and queueing its output job.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_slot_prepare(
    s: *mut Slot,
    d: *mut Drawable,
    index: u32,
    flags: u32,
    metadata: *const ImageMetadata,
) -> u32 {
    if index as usize >= COUNT {
        return DG_GL_ERROR_DRAWABLE;
    }
    let m = unsafe { &*metadata };
    let generation = match unsafe { (*d).generation }.checked_add(1) {
        Some(g) => g,
        None => return DG_GL_ERROR_LIMIT,
    };
    unsafe { assert_eq!((*s).state, RENDERING) };
    let mut p = [0u8; DG_TRANSPORT_PACKET_BYTES as usize];
    put32(&mut p, DG_TRANSPORT_OFF_MAGIC, DG_TRANSPORT_MAGIC);
    put32(&mut p, DG_TRANSPORT_OFF_VERSION, DG_TRANSPORT_VERSION);
    put32(&mut p, DG_TRANSPORT_OFF_SIZE, DG_TRANSPORT_PACKET_BYTES);
    put32(
        &mut p,
        DG_TRANSPORT_OFF_KIND,
        if flags & DG_GL_PRESENT_EXCLUSIVE != 0 {
            DG_TRANSPORT_KIND_FRAME
        } else {
            DG_TRANSPORT_KIND_DRAWABLE
        },
    );
    let mut wire_flags = DG_TRANSPORT_FLAG_TOP_LEFT;
    if flags & DG_GL_PRESENT_RETAIN != 0 {
        wire_flags |= DG_TRANSPORT_FLAG_RETAIN_FOR_DESKTOP;
    }
    if m.ready_fence != 0 {
        wire_flags |= DG_TRANSPORT_FLAG_READY_FENCE;
    }
    put32(&mut p, DG_TRANSPORT_OFF_FLAGS, wire_flags);
    for (off, v) in unsafe {
        [
            (DG_TRANSPORT_OFF_WIDTH, (*d).width),
            (DG_TRANSPORT_OFF_HEIGHT, (*d).height),
            (DG_TRANSPORT_OFF_CLIENT, (*d).client),
            (DG_TRANSPORT_OFF_DRAWABLE, (*d).id),
        ]
    } {
        put32(&mut p, off, v);
    }
    put32(&mut p, DG_TRANSPORT_OFF_STRIDE, m.stride);
    put32(&mut p, DG_TRANSPORT_OFF_OFFSET, m.offset);
    put32(
        &mut p,
        DG_TRANSPORT_OFF_FOURCC,
        DG_TRANSPORT_FORMAT_ARGB8888,
    );
    put32(&mut p, DG_TRANSPORT_OFF_SLOT, index);
    put64(&mut p, DG_TRANSPORT_OFF_EPOCH, unsafe { (*d).epoch });
    put64(&mut p, DG_TRANSPORT_OFF_GENERATION, generation);
    put64(&mut p, DG_TRANSPORT_OFF_MODIFIER, m.modifier);
    p[DG_TRANSPORT_OFF_DEVICE_UUID as usize..DG_TRANSPORT_OFF_DEVICE_UUID as usize + 16]
        .copy_from_slice(&m.uuid);
    unsafe {
        (*s).packet = p;
        (*s).generation = generation;
        (*d).generation = generation;
    }
    0
}
/// # Safety
/// Engine mutex held after packet construction; ownership moves to output queue.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_slot_pending(s: *mut Slot) {
    unsafe {
        assert_eq!((*s).state, RENDERING);
        (*s).state = PENDING;
    }
}
#[cfg(test)]
mod tests;
