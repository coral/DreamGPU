// SPDX-License-Identifier: GPL-2.0-or-later
//! Ordered desktop ownership/coherence transitions and transport packets.
use crate::{
    gl_api::*,
    publication::{Slot, PENDING, PUBLISHED},
};
use core::ffi::c_void;
#[repr(C)]
#[derive(Default)]
pub struct State {
    pub epoch: u64,
    pub sequence: u64,
    pub width: u32,
    pub height: u32,
    pub active: u32,
    pub coherent: u32,
    pub exclusive: u32,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Ops {
    pub opaque: *mut c_void,
    pub capture: unsafe extern "C" fn(*mut c_void, *const u8, u32) -> u32,
    pub readback: unsafe extern "C" fn(*mut c_void, *const u8, *mut u8) -> u32,
    pub queue: unsafe extern "C" fn(*mut c_void, *const u8) -> u32,
    pub retained: unsafe extern "C" fn(*mut c_void, *const u8) -> u32,
    pub failed: unsafe extern "C" fn(*mut c_void),
}
fn word(r: &[u8], off: u32) -> u32 {
    u32::from_le_bytes(r[off as usize..off as usize + 4].try_into().unwrap())
}
fn qword(r: &[u8], off: u32) -> u64 {
    u64::from_le_bytes(r[off as usize..off as usize + 8].try_into().unwrap())
}
fn put(p: &mut [u8], off: u32, x: u32) {
    p[off as usize..off as usize + 4].copy_from_slice(&x.to_le_bytes());
}
fn putq(p: &mut [u8], off: u32, x: u64) {
    p[off as usize..off as usize + 8].copy_from_slice(&x.to_le_bytes());
}
unsafe fn rollback(s: *mut State) {
    unsafe {
        (*s).sequence = (*s)
            .sequence
            .checked_sub(1)
            .expect("desktop sequence ownership")
    };
}
/// # Safety
/// Slot examined under engine mutex, record is an immutable validated desktop
/// command. Published packet may have concurrent readers but no writer.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_desktop_retained(s: *const Slot, record: *const u8) -> u32 {
    let record = unsafe {
        core::slice::from_raw_parts(record, (DG_GL_HEADER_BYTES + DG_DESKTOP_BYTES) as usize)
    };
    let r = &record[DG_GL_HEADER_BYTES as usize..];
    let state = unsafe { (*s).state };
    if state != PENDING && state != PUBLISHED {
        return 0;
    }
    let p = unsafe { &(*s).packet };
    let valid = word(p, DG_TRANSPORT_OFF_CLIENT) == word(record, DG_GL_OFF_CLIENT)
        && word(p, DG_TRANSPORT_OFF_DRAWABLE) == word(record, DG_GL_OFF_DRAWABLE)
        && qword(p, DG_TRANSPORT_OFF_EPOCH) == qword(r, DG_DESKTOP_IMAGE_EPOCH)
        && unsafe { (*s).generation } == qword(r, DG_DESKTOP_IMAGE_FRAME)
        && word(p, DG_TRANSPORT_OFF_FLAGS) & DG_TRANSPORT_FLAG_RETAIN_FOR_DESKTOP != 0;
    if !valid {
        return 0;
    }
    if word(r, DG_DESKTOP_OP) == DG_DESKTOP_BLIT {
        return crate::desktop::dreamgpu_desktop_rect(
            word(r, DG_DESKTOP_SRC_X),
            word(r, DG_DESKTOP_SRC_Y),
            word(r, DG_DESKTOP_WIDTH),
            word(r, DG_DESKTOP_HEIGHT),
            word(p, DG_TRANSPORT_OFF_WIDTH),
            word(p, DG_TRANSPORT_OFF_HEIGHT),
        );
    }
    1
}
/// # Safety
/// State belongs to the render worker. The fixed-size record passed outer/desktop
/// validation; callbacks may inspect state through their opaque engine but cannot
/// mutate it/reenter. Capture/readback implement ordered drains; queue copies the
/// packet before returning. Readback may update its temporary packet in place.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_desktop_execute(
    state: *mut State,
    ops: *const Ops,
    record: *const u8,
    width: u32,
    height: u32,
) -> u32 {
    let ops = unsafe { *ops };
    let record = unsafe {
        core::slice::from_raw_parts(record, (DG_GL_HEADER_BYTES + DG_DESKTOP_BYTES) as usize)
    };
    let r = &record[DG_GL_HEADER_BYTES as usize..];
    let op = word(r, DG_DESKTOP_OP);
    let w = word(r, DG_DESKTOP_WIDTH);
    let h = word(r, DG_DESKTOP_HEIGHT);
    let detached = op == DG_DESKTOP_DISCARD;
    unsafe {
        if op == DG_DESKTOP_SEED {
            if (*state).active != 0 {
                return DG_GL_ERROR_DESKTOP;
            }
            let Some(epoch) = (*state).epoch.checked_add(1) else {
                return DG_GL_ERROR_LIMIT;
            };
            (*state).epoch = epoch;
            (*state).sequence = 0;
            (*state).width = width;
            (*state).height = height;
        } else if !detached
            && ((*state).active == 0 || (*state).width != width || (*state).height != height)
        {
            return DG_GL_ERROR_DESKTOP;
        }
        if !detached {
            let Some(sequence) = (*state).sequence.checked_add(1) else {
                return DG_GL_ERROR_LIMIT;
            };
            (*state).sequence = sequence;
        }
    }
    let wire = match op {
        DG_DESKTOP_SEED | DG_DESKTOP_PATCH | DG_DESKTOP_RETURN => {
            if op == DG_DESKTOP_RETURN && unsafe { (*state).coherent } == 0 {
                unsafe { rollback(state) };
                return DG_GL_ERROR_DESKTOP;
            }
            let subtype = if op == DG_DESKTOP_SEED {
                DG_TRANSPORT_CPU_SEED
            } else if op == DG_DESKTOP_PATCH {
                DG_TRANSPORT_CPU_PATCH
            } else {
                DG_TRANSPORT_CPU_RETURN
            };
            let error = unsafe { (ops.capture)(ops.opaque, r.as_ptr(), subtype) };
            unsafe {
                if error == 0 && op == DG_DESKTOP_SEED {
                    (*state).active = 1;
                    (*state).coherent = 1;
                }
                if error == 0 && op == DG_DESKTOP_RETURN {
                    (*state).exclusive = 0;
                    (*state).active = 0;
                    (*state).coherent = 0;
                }
                if error != 0 {
                    rollback(state);
                }
            }
            return error;
        }
        DG_DESKTOP_FILL => DG_TRANSPORT_DESKTOP_FILL,
        DG_DESKTOP_COPY => DG_TRANSPORT_DESKTOP_COPY,
        DG_DESKTOP_READBACK => DG_TRANSPORT_DESKTOP_READBACK,
        DG_DESKTOP_BLIT | DG_DESKTOP_DISCARD => {
            if unsafe { (ops.retained)(ops.opaque, record.as_ptr()) } == 0 {
                if !detached {
                    unsafe { rollback(state) }
                }
                return DG_GL_ERROR_DESKTOP;
            }
            if op == DG_DESKTOP_BLIT {
                DG_TRANSPORT_DESKTOP_BLIT
            } else {
                DG_TRANSPORT_DESKTOP_DISCARD
            }
        }
        _ => return DG_GL_ERROR_DESKTOP,
    };
    let mut p = [0u8; DG_TRANSPORT_PACKET_BYTES as usize];
    put(&mut p, DG_TRANSPORT_OFF_MAGIC, DG_TRANSPORT_MAGIC);
    put(&mut p, DG_TRANSPORT_OFF_VERSION, DG_TRANSPORT_VERSION);
    put(&mut p, DG_TRANSPORT_OFF_SIZE, DG_TRANSPORT_PACKET_BYTES);
    put(&mut p, DG_TRANSPORT_OFF_KIND, DG_TRANSPORT_KIND_DESKTOP_OP);
    for (off, x) in [
        (DG_TRANSPORT_DESKTOP_OFF_OPCODE, wire),
        (DG_TRANSPORT_DESKTOP_OFF_DST_X, word(r, DG_DESKTOP_DST_X)),
        (DG_TRANSPORT_DESKTOP_OFF_DST_Y, word(r, DG_DESKTOP_DST_Y)),
        (DG_TRANSPORT_DESKTOP_OFF_WIDTH, w),
        (DG_TRANSPORT_DESKTOP_OFF_HEIGHT, h),
    ] {
        put(&mut p, off, x);
    }
    putq(
        &mut p,
        DG_TRANSPORT_OFF_EPOCH,
        if detached {
            0
        } else {
            unsafe { (*state).epoch }
        },
    );
    putq(
        &mut p,
        DG_TRANSPORT_OFF_GENERATION,
        if detached {
            0
        } else {
            unsafe { (*state).sequence }
        },
    );
    if op == DG_DESKTOP_FILL {
        put(
            &mut p,
            DG_TRANSPORT_DESKTOP_OFF_COLOR,
            word(r, DG_DESKTOP_SRC_X),
        );
    } else if matches!(op, DG_DESKTOP_COPY | DG_DESKTOP_BLIT | DG_DESKTOP_DISCARD) {
        put(
            &mut p,
            DG_TRANSPORT_DESKTOP_OFF_SRC_X,
            word(r, DG_DESKTOP_SRC_X),
        );
        put(
            &mut p,
            DG_TRANSPORT_DESKTOP_OFF_SRC_Y,
            word(r, DG_DESKTOP_SRC_Y),
        );
    }
    if matches!(op, DG_DESKTOP_BLIT | DG_DESKTOP_DISCARD) {
        for (off, x) in [
            (DG_TRANSPORT_OFF_CLIENT, word(record, DG_GL_OFF_CLIENT)),
            (DG_TRANSPORT_OFF_DRAWABLE, word(record, DG_GL_OFF_DRAWABLE)),
            (
                DG_TRANSPORT_DESKTOP_OFF_GPU_SLOT,
                word(r, DG_DESKTOP_SLOT_OR_OFFSET),
            ),
            (DG_TRANSPORT_DESKTOP_OFF_FLAGS, word(r, DG_DESKTOP_FLAGS)),
        ] {
            put(&mut p, off, x);
        }
        putq(
            &mut p,
            DG_TRANSPORT_DESKTOP_OFF_GPU_EPOCH,
            qword(r, DG_DESKTOP_IMAGE_EPOCH),
        );
        putq(
            &mut p,
            DG_TRANSPORT_DESKTOP_OFF_GPU_FRAME,
            qword(r, DG_DESKTOP_IMAGE_FRAME),
        );
    }
    let error = if op == DG_DESKTOP_READBACK {
        let e = unsafe { (ops.readback)(ops.opaque, r.as_ptr(), p.as_mut_ptr()) };
        if e == 0 && unsafe { w == (*state).width && h == (*state).height } {
            unsafe { (*state).coherent = 1 }
        }
        e
    } else {
        let e = unsafe { (ops.queue)(ops.opaque, p.as_ptr()) };
        if !detached {
            unsafe { (*state).coherent = 0 }
        }
        e
    };
    if error != 0 && error != DG_GL_ERROR_GENERATION {
        unsafe { (ops.failed)(ops.opaque) }
    }
    error
}
#[cfg(test)]
mod tests;
