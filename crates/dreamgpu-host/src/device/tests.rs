use super::*;
use std::{
    alloc::{alloc, dealloc, Layout},
    vec,
    vec::Vec,
};
#[derive(Default)]
struct Host {
    source: Vec<u8>,
    allocations: usize,
    frees: usize,
    reads: usize,
    deny_alloc: bool,
    deny_read: bool,
    deny_ram: bool,
    validation_error: u32,
    enqueue_error: u32,
    queued: *mut u8,
    begin: usize,
    records: usize,
}
unsafe extern "C" fn allocate(p: *mut c_void, bytes: u32) -> *mut u8 {
    let h = unsafe { &mut *p.cast::<Host>() };
    if h.deny_alloc {
        return ptr::null_mut();
    }
    h.allocations += 1;
    unsafe { alloc(Layout::from_size_align(bytes as usize, 8).unwrap()) }
}
unsafe extern "C" fn free(p: *mut c_void, data: *mut u8) {
    let h = unsafe { &mut *p.cast::<Host>() };
    h.frees += 1;
    unsafe { dealloc(data, Layout::from_size_align(h.source.len(), 8).unwrap()) };
}
unsafe extern "C" fn read(p: *mut c_void, _: u64, data: *mut u8, bytes: u32) -> u32 {
    let h = unsafe { &mut *p.cast::<Host>() };
    h.reads += 1;
    if h.deny_read {
        return 0;
    }
    assert_eq!(bytes as usize, h.source.len());
    unsafe { ptr::copy_nonoverlapping(h.source.as_ptr(), data, bytes as usize) };
    1
}
unsafe extern "C" fn ram(p: *mut c_void, _: u64, _: u32, _: u32) -> u32 {
    (!unsafe { &*p.cast::<Host>() }.deny_ram) as u32
}
unsafe extern "C" fn validate(p: *mut c_void, _: *const u8, _: u32, records: *mut u32) -> u32 {
    unsafe { records.write(1) };
    unsafe { &*p.cast::<Host>() }.validation_error
}
unsafe extern "C" fn begin(p: *mut c_void, _: u32) {
    unsafe { &mut *p.cast::<Host>() }.begin += 1;
}
unsafe extern "C" fn record(p: *mut c_void, _: *const u8) {
    unsafe { &mut *p.cast::<Host>() }.records += 1;
}
unsafe extern "C" fn enqueue(p: *mut c_void, data: *mut u8, _: u32, _: u32) -> u32 {
    let h = unsafe { &mut *p.cast::<Host>() };
    if h.enqueue_error == 0 {
        h.queued = data;
    }
    h.enqueue_error
}
fn memory(h: &mut Host) -> Memory {
    Memory {
        opaque: (h as *mut Host).cast(),
        allocate,
        free,
        read,
        ram,
    }
}
fn callbacks(h: &mut Host) -> GlCallbacks {
    GlCallbacks {
        memory: memory(h),
        validate,
        diagnostics_begin: begin,
        diagnostic_record: record,
        enqueue,
    }
}
fn packet(op: u32, size: usize) -> Vec<u8> {
    let mut data = vec![0; size];
    data[..4].copy_from_slice(&op.to_le_bytes());
    data[4..8].copy_from_slice(&(size as u32).to_le_bytes());
    data
}
fn submit(h: &mut Host, r: GlRequest) -> (u32, GlResult) {
    let mut out = GlResult::default();
    let error = unsafe { dreamgpu_device_gl_submit(&callbacks(h), &r, &mut out) };
    (error, out)
}
#[test]
fn snapshot_failure_and_queue_ownership() {
    for stage in 0..6 {
        let mut h = Host {
            source: packet(DG_GL_CALL, 32),
            ..Host::default()
        };
        let r = GlRequest {
            bytes: 32,
            bpp: 32,
            ..GlRequest::default()
        };
        let expected = match stage {
            0 => {
                h.deny_alloc = true;
                DG_GL_ERROR_DMA
            }
            1 => {
                h.deny_read = true;
                DG_GL_ERROR_DMA
            }
            2 => {
                h.validation_error = DG_GL_ERROR_BATCH;
                DG_GL_ERROR_BATCH
            }
            3 => {
                h.enqueue_error = DG_GL_ERROR_TRANSPORT;
                DG_GL_ERROR_TRANSPORT
            }
            4 => {
                h.enqueue_error = DG_GL_ERROR_LIMIT;
                DG_GL_ERROR_LIMIT
            }
            _ => 0,
        };
        let (error, _) = submit(&mut h, r);
        assert_eq!(error, expected);
        if stage == 5 {
            assert!(!h.queued.is_null());
            assert_eq!(h.frees, 0);
            let pointer = h.queued;
            unsafe { free((&mut h as *mut Host).cast(), pointer) };
        }
        assert_eq!(h.allocations, h.frees);
    }
}
#[test]
fn envelope_checks_precede_dma_and_sensitive_fault_survives_rejection() {
    let mut h = Host {
        source: packet(DG_GL_DESKTOP, 36),
        ..Host::default()
    };
    h.source[32..36].copy_from_slice(&DG_DESKTOP_READBACK.to_le_bytes());
    let mut r = GlRequest {
        bytes: 36,
        bpp: 32,
        ..GlRequest::default()
    };
    r.generation = 1;
    assert_eq!(submit(&mut h, r).0, DG_GL_ERROR_GENERATION);
    r.bytes = 3;
    assert_eq!(submit(&mut h, r).0, DG_GL_ERROR_BATCH);
    r.bytes = 36;
    r.generation = 0;
    r.address = u64::MAX;
    assert_eq!(submit(&mut h, r).0, DG_GL_ERROR_DMA);
    assert_eq!(h.allocations, 0);
    r.address = 0;
    h.validation_error = DG_GL_ERROR_BATCH;
    let (error, out) = submit(&mut h, r);
    assert_eq!(
        (error, out.sensitive),
        (DG_GL_ERROR_BATCH, DG_DESKTOP_READBACK)
    );
    assert_eq!(h.begin, 0);
    h.validation_error = 0;
    r.busy_2d = 1;
    assert_eq!(submit(&mut h, r).0, DG_GL_ERROR_DESKTOP);
    assert_eq!((h.begin, h.records), (1, 1));
    assert_eq!(h.allocations, h.frees);
}
#[test]
fn cursor_update_is_transactional_and_move_allocates_nothing() {
    let mut h = Host {
        source: vec![0; 8],
        ..Host::default()
    };
    let mut pixels = vec![0x55; (DG_CURSOR_MAX_DIMENSION * DG_CURSOR_MAX_DIMENSION * 8) as usize];
    let mut r = CursorRequest {
        operation: DG_CURSOR_SHAPE,
        bytes: 8,
        width: 1,
        height: 1,
        format: DG_CURSOR_ARGB_PREMULTIPLIED,
        ..CursorRequest::default()
    };
    h.source[0] = 1; // Non-premultiplied transparent pixel.
    assert_eq!(
        unsafe { dreamgpu_device_cursor(&memory(&mut h), &r, pixels.as_mut_ptr()) },
        DG_CURSOR_ERROR_SHAPE
    );
    assert!(pixels.iter().all(|v| *v == 0x55));
    h.source[0] = 0;
    assert_eq!(
        unsafe { dreamgpu_device_cursor(&memory(&mut h), &r, pixels.as_mut_ptr()) },
        0
    );
    assert!(pixels.iter().all(|v| *v == 0));
    let allocations = h.allocations;
    r.operation = DG_CURSOR_MOVE;
    r.bytes = u32::MAX;
    assert_eq!(
        unsafe { dreamgpu_device_cursor(&memory(&mut h), &r, pixels.as_mut_ptr()) },
        0
    );
    assert_eq!(h.allocations, allocations);
    assert_eq!(h.allocations, h.frees);
    r.flags = !DG_CURSOR_FLAGS_MASK;
    assert_eq!(
        unsafe { dreamgpu_device_cursor(&memory(&mut h), &r, pixels.as_mut_ptr()) },
        DG_CURSOR_ERROR_FLAGS
    );
}
#[test]
fn two_d_capture_and_restore_bound_progress_before_access() {
    let mut command = vec![0u8; 40];
    for (offset, value) in [(0, 1u32), (4, 4), (20, 8), (24, 2), (28, 2)] {
        command[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
    }
    let mut h = Host {
        source: command,
        ..Host::default()
    };
    let mut output = [0u8; 2560];
    let mut work = 0;
    assert_eq!(
        unsafe {
            dreamgpu_device_2d_capture(&memory(&mut h), 0, 65, output.as_mut_ptr(), 4096, &mut work)
        },
        DG_ERROR_BATCH_COUNT
    );
    assert_eq!(h.reads, 0);
    assert_eq!(
        unsafe {
            dreamgpu_device_2d_capture(&memory(&mut h), 0, 1, output.as_mut_ptr(), 4096, &mut work)
        },
        0
    );
    let pixels = [0u8; 32768];
    let mut r = Restore {
        cursor: CursorRequest::default(),
        cursor_status: 0,
        gl_status: 0,
        status: DG_STATUS_BUSY,
        count: 1,
        command: 0,
        row: 1,
        column: 4,
        vram: 4096,
    };
    assert_eq!(
        unsafe { dreamgpu_device_restore(&r, pixels.as_ptr(), output.as_ptr(), &mut work) },
        1
    );
    r.column = 3;
    assert_eq!(
        unsafe { dreamgpu_device_restore(&r, pixels.as_ptr(), output.as_ptr(), &mut work) },
        0
    );
    r.column = 4;
    r.row = 2;
    assert_eq!(
        unsafe { dreamgpu_device_restore(&r, pixels.as_ptr(), output.as_ptr(), &mut work) },
        0
    );
    r.row = 0;
    r.command = u32::MAX;
    assert_eq!(
        unsafe { dreamgpu_device_restore(&r, pixels.as_ptr(), output.as_ptr(), &mut work) },
        0
    );
    r.status = 0;
    r.cursor.width = 1;
    r.cursor.height = 0;
    assert_eq!(
        unsafe { dreamgpu_device_restore(&r, pixels.as_ptr(), output.as_ptr(), &mut work) },
        0
    );
}

#[test]
fn query_result_lease_checked_before_enqueue() {
    let mut h = Host {
        source: packet(DG_GL_QUERY, 48),
        ..Host::default()
    };
    h.source[32..36].copy_from_slice(&FEnum_glGetError.to_le_bytes());
    let mut r = GlRequest {
        bytes: 48,
        bpp: 32,
        result_capacity: 3,
        ..GlRequest::default()
    };
    assert_eq!(submit(&mut h, r).0, DG_GL_ERROR_BATCH);
    r.result_capacity = DG_GL_MAX_READBACK_BYTES + 1;
    assert_eq!(submit(&mut h, r).0, DG_GL_ERROR_BATCH);
    r.result_capacity = 4;
    r.result_address = u64::MAX;
    assert_eq!(submit(&mut h, r).0, DG_GL_ERROR_DMA);
    r.result_address = 0;
    h.deny_ram = true;
    assert_eq!(submit(&mut h, r).0, DG_GL_ERROR_DMA);
    assert!(h.queued.is_null());
    assert_eq!(h.begin, 0);
    h.deny_ram = false;
    let (error, output) = submit(&mut h, r);
    assert_eq!((error, output.result_capacity), (0, 4));
    let queued = h.queued;
    unsafe { free((&mut h as *mut Host).cast(), queued) };
    assert_eq!(h.allocations, h.frees);
}
