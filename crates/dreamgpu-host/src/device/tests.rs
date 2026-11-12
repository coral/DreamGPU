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
unsafe extern "C" fn enqueue(
    p: *mut c_void,
    data: *mut u8,
    _: u32,
    _: u32,
    _: *const Enqueue,
) -> u32 {
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
fn desktop_wire_format_must_match_actual_vbe_before_enqueue() {
    for (format, bpp, expected) in [
        (0u32, 32, 0),
        (16, 16, 0),
        (0, 16, DG_GL_ERROR_DESKTOP),
        (16, 32, DG_GL_ERROR_DESKTOP),
        (0, 8, DG_GL_ERROR_DESKTOP),
    ] {
        let mut h = Host {
            source: packet(DG_GL_DESKTOP, 96),
            ..Host::default()
        };
        h.source[64..68].copy_from_slice(&format.to_le_bytes());
        let r = GlRequest {
            bytes: 96,
            bpp,
            ..GlRequest::default()
        };
        assert_eq!(submit(&mut h, r).0, expected);
        if expected == 0 {
            assert!(!h.queued.is_null());
            let queued = h.queued;
            unsafe { free((&mut h as *mut Host).cast(), queued) };
        } else {
            assert!(h.queued.is_null());
        }
        assert_eq!(h.allocations, h.frees);
    }
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

/// Guest RAM for result destinations: RAM below `limit`, page-granular
/// denial, and a log of every DMA write.
struct Guest {
    ram: Vec<u8>,
    denied: Vec<u64>,
    writes: Vec<(u64, u32)>,
    allocations: std::collections::HashMap<usize, Layout>,
}
impl Guest {
    fn new() -> Box<Self> {
        Box::new(Self {
            ram: vec![0xcc; 0x100_0000],
            denied: Vec::new(),
            writes: Vec::new(),
            allocations: std::collections::HashMap::new(),
        })
    }
    fn set(&mut self, address: u64, value: u32) {
        let a = address as usize;
        self.ram[a..a + 4].copy_from_slice(&value.to_le_bytes());
    }
    fn memory(&mut self) -> Memory {
        Memory {
            opaque: (self as *mut Guest).cast(),
            allocate: guest_allocate,
            free: guest_free,
            read: guest_read,
            ram: guest_ram,
        }
    }
}
fn guest(o: *mut c_void) -> &'static mut Guest {
    unsafe { &mut *o.cast::<Guest>() }
}
unsafe extern "C" fn guest_allocate(o: *mut c_void, bytes: u32) -> *mut u8 {
    let layout = Layout::from_size_align(bytes.max(1) as usize, 8).unwrap();
    let p = unsafe { alloc(layout) };
    guest(o).allocations.insert(p as usize, layout);
    p
}
unsafe extern "C" fn guest_free(o: *mut c_void, p: *mut u8) {
    let layout = guest(o).allocations.remove(&(p as usize)).unwrap();
    unsafe { dealloc(p, layout) };
}
unsafe extern "C" fn guest_read(o: *mut c_void, address: u64, data: *mut u8, bytes: u32) -> u32 {
    let g = guest(o);
    let Some(end) = address.checked_add(u64::from(bytes)) else {
        return 0;
    };
    if end > g.ram.len() as u64 {
        return 0;
    }
    unsafe { ptr::copy_nonoverlapping(g.ram.as_ptr().add(address as usize), data, bytes as usize) };
    1
}
unsafe extern "C" fn guest_ram(o: *mut c_void, address: u64, bytes: u32, _: u32) -> u32 {
    let g = guest(o);
    let Some(end) = address.checked_add(u64::from(bytes)) else {
        return 0;
    };
    let denied = g
        .denied
        .iter()
        .any(|page| *page < end && address < page + u64::from(PAGE));
    u32::from(end <= g.ram.len() as u64 && !denied)
}
unsafe extern "C" fn guest_write(o: *mut c_void, address: u64, data: *const u8, bytes: u32) -> u32 {
    let g = guest(o);
    let end = address as usize + bytes as usize;
    assert!(end <= g.ram.len(), "write outside guest RAM");
    g.writes.push((address, bytes));
    unsafe {
        ptr::copy_nonoverlapping(
            data,
            g.ram.as_mut_ptr().add(address as usize),
            bytes as usize,
        )
    };
    1
}
const LIST: u64 = 0x10000;
/// Frames of a scattered destination: descending, every other page.
fn scattered(g: &mut Guest, pages: u32) -> Vec<u64> {
    (0..pages)
        .map(|i| {
            let page = 0xff_e000 - u64::from(i) * 0x2000;
            g.set(LIST + u64::from(i) * 4, (page >> 12) as u32);
            page
        })
        .collect()
}

#[test]
fn result_words_grant_contiguous_or_page_list_capacity() {
    for (word, expected) in [
        (0, None),
        (1, Some(1)),
        (DG_GL_MAX_READBACK_BYTES, Some(DG_GL_MAX_READBACK_BYTES)),
        (DG_GL_MAX_READBACK_BYTES + 1, None),
        (DG_GL_MAX_BULK_RESULT_BYTES, None),
        (DG_GL_RESULT_PAGES, None),
        (DG_GL_RESULT_PAGES | 1, Some(1)),
        (
            DG_GL_RESULT_PAGES | DG_GL_MAX_BULK_RESULT_BYTES,
            Some(DG_GL_MAX_BULK_RESULT_BYTES),
        ),
        (DG_GL_RESULT_PAGES | (DG_GL_MAX_BULK_RESULT_BYTES + 1), None),
        (u32::MAX, None),
    ] {
        assert_eq!(result_capacity(word), expected, "{word:#x}");
    }
}

#[test]
fn page_list_destinations_are_checked_before_any_write() {
    let mut g = Guest::new();
    let capacity = 3 * PAGE + 100;
    let word = DG_GL_RESULT_PAGES | capacity;
    let pages = scattered(&mut g, 4);
    // Frames past the capacity are never read as destinations.
    g.set(LIST + 16, 0);
    let m = g.memory();
    assert!(unsafe { result_destination(m, LIST, word) });
    // Unaligned, overflowing or non-RAM lists.
    assert!(!unsafe { result_destination(m, LIST + 4, word) });
    assert!(!unsafe { result_destination(m, !0xfff, word) });
    assert!(!unsafe { result_destination(m, 0x100_0000, word) });
    // A zero frame, a frame past RAM and a non-RAM page are rejected.
    for (index, frame) in [(0, 0), (3, 0x1_0000), (2, 0xffff_ffff)] {
        let saved = g.ram[(LIST + index * 4) as usize..][..4].to_vec();
        g.set(LIST + index * 4, frame);
        assert!(
            !unsafe { result_destination(m, LIST, word) },
            "{index} {frame:#x}"
        );
        g.ram[(LIST + index * 4) as usize..][..4].copy_from_slice(&saved);
    }
    g.denied.push(pages[1]);
    assert!(!unsafe { result_destination(m, LIST, word) });
    g.denied.clear();
    g.denied.push(LIST);
    assert!(!unsafe { result_destination(m, LIST, word) });
    g.denied.clear();
    // Capacity words outside their limits name no destination.
    for word in [
        DG_GL_RESULT_PAGES,
        DG_GL_RESULT_PAGES | (DG_GL_MAX_BULK_RESULT_BYTES + 1),
        DG_GL_MAX_READBACK_BYTES + 1,
    ] {
        assert!(!unsafe { result_destination(m, LIST, word) });
    }
    // A contiguous destination is one writable RAM range.
    assert!(unsafe { result_destination(m, 0x20000, 512) });
    assert!(!unsafe { result_destination(m, 0xff_ff00, 512) });
    assert!(g.writes.is_empty());
}

#[test]
fn page_list_results_scatter_exactly_into_listed_pages() {
    let mut g = Guest::new();
    let pages = scattered(&mut g, 1024);
    let m = g.memory();
    let data: Vec<u8> = (0..DG_GL_MAX_BULK_RESULT_BYTES)
        .map(|i| (i * 7 + i / 4096) as u8)
        .collect();
    for bytes in [
        1,
        PAGE - 4,
        PAGE,
        PAGE + 4,
        DG_GL_MAX_READBACK_BYTES + 4,
        3 * 1024 * 1024,
        DG_GL_MAX_BULK_RESULT_BYTES,
    ] {
        g.ram.fill(0xcc);
        for (i, page) in pages.iter().enumerate() {
            g.set(LIST + i as u64 * 4, (page >> 12) as u32);
        }
        g.writes.clear();
        let word = DG_GL_RESULT_PAGES
            | bytes
                .next_multiple_of(PAGE)
                .min(DG_GL_MAX_BULK_RESULT_BYTES);
        assert!(unsafe { result_write(m, guest_write, LIST, word, data.as_ptr(), bytes) });
        let used = bytes.div_ceil(PAGE) as usize;
        assert_eq!(g.writes.len(), used);
        for (i, page) in pages.iter().enumerate() {
            let start = i * PAGE as usize;
            let slice = &g.ram[*page as usize..*page as usize + PAGE as usize];
            if i < used {
                let n = (bytes as usize - start).min(PAGE as usize);
                assert_eq!(g.writes[i], (*page, n as u32));
                assert_eq!(&slice[..n], &data[start..start + n], "{bytes} page {i}");
                assert!(slice[n..].iter().all(|b| *b == 0xcc));
            } else {
                assert!(slice.iter().all(|b| *b == 0xcc));
            }
        }
        // Only listed pages and the list itself differ from the fill.
        let mut touched = g.ram.clone();
        for page in &pages[..used] {
            touched[*page as usize..*page as usize + PAGE as usize].fill(0xcc);
        }
        touched[LIST as usize..LIST as usize + 4096].fill(0xcc);
        assert!(touched.iter().all(|b| *b == 0xcc));
    }
    // More bytes than the capacity, or a page lost since submission: no write.
    g.writes.clear();
    let word = DG_GL_RESULT_PAGES | (2 * PAGE);
    assert!(!unsafe { result_write(m, guest_write, LIST, word, data.as_ptr(), 2 * PAGE + 1) });
    g.denied.push(pages[1]);
    assert!(!unsafe { result_write(m, guest_write, LIST, word, data.as_ptr(), 2 * PAGE) });
    g.denied.clear();
    g.set(LIST + 4, 0);
    assert!(!unsafe { result_write(m, guest_write, LIST, word, data.as_ptr(), 2 * PAGE) });
    assert!(!unsafe { result_write(m, guest_write, LIST, 0, data.as_ptr(), 4) });
    assert!(g.writes.is_empty());
    // Contiguous results keep their old checks.
    assert!(unsafe { result_write(m, guest_write, 0x20000, 512, data.as_ptr(), 512) });
    assert!(!unsafe { result_write(m, guest_write, 0x20000, 512, data.as_ptr(), 513) });
    assert!(!unsafe { result_write(m, guest_write, 0xff_ff00, 512, data.as_ptr(), 512) });
    assert_eq!(g.writes, [(0x20000, 512)]);
    assert_eq!(&g.ram[0x20000..0x20200], &data[..512]);
    assert_eq!(
        unsafe { dreamgpu_device_result_write(&m, guest_write, 0x20000, 512, data.as_ptr(), 4) },
        1
    );
    assert_eq!(
        unsafe {
            dreamgpu_device_result_write(ptr::null(), guest_write, 0x20000, 512, data.as_ptr(), 4)
        },
        0
    );
}

#[test]
fn bulk_query_destinations_are_checked_before_enqueue() {
    let mut g = Guest::new();
    scattered(&mut g, 1024);
    // A 1024x768 RGBA8 GetTexImage in DG_GL_TEXTURE_READ_UNIT pixels at 0x1000.
    let mut query = packet(DG_GL_QUERY, 48);
    let units = 1024 * 768 / DG_GL_TEXTURE_READ_UNIT;
    for (i, word) in [
        FEnum_glGetTexImage,
        GL_TEXTURE_2D,
        DG_GL_TEXTURE_READ_UNITS | units << 16,
        0,
    ]
    .into_iter()
    .enumerate()
    {
        query[32 + i * 4..36 + i * 4].copy_from_slice(&word.to_le_bytes());
    }
    g.ram[0x1000..0x1030].copy_from_slice(&query);
    let required = 1024 * 768 * 4;
    let mut r = GlRequest {
        address: 0x1000,
        result_address: LIST,
        bytes: 48,
        bpp: 32,
        result_capacity: DG_GL_RESULT_PAGES | required,
        ..GlRequest::default()
    };
    let c = GlCallbacks {
        memory: g.memory(),
        validate: guest_validate,
        diagnostics_begin: guest_begin,
        diagnostic_record: guest_record,
        enqueue: guest_enqueue,
    };
    let submit = |r: &GlRequest| {
        let mut out = GlResult::default();
        (
            unsafe { dreamgpu_device_gl_submit(&c, r, &mut out) },
            out.result_capacity,
        )
    };
    // The contiguous limit and the bulk limit, and a capacity below the read.
    r.result_capacity = required;
    assert_eq!(submit(&r), (DG_GL_ERROR_BATCH, 0));
    r.result_capacity = DG_GL_RESULT_PAGES | (DG_GL_MAX_BULK_RESULT_BYTES + 4096);
    assert_eq!(submit(&r), (DG_GL_ERROR_BATCH, 0));
    r.result_capacity = DG_GL_RESULT_PAGES | (required - 4);
    assert_eq!(submit(&r), (DG_GL_ERROR_BATCH, 0));
    // A list that cannot be written into: nothing executes.
    r.result_capacity = DG_GL_RESULT_PAGES | required;
    g.set(LIST + 100 * 4, 0);
    assert_eq!(submit(&r), (DG_GL_ERROR_DMA, 0));
    g.set(LIST + 100 * 4, 0x123);
    r.result_address = LIST + 8;
    assert_eq!(submit(&r), (DG_GL_ERROR_DMA, 0));
    r.result_address = LIST;
    assert_eq!(submit(&r), (0, DG_GL_RESULT_PAGES | required));
    assert!(g.writes.is_empty());
    assert_eq!(g.allocations.len(), 1);
    let queued = *g.allocations.keys().next().unwrap() as *mut u8;
    unsafe { guest_free((&mut *g as *mut Guest).cast(), queued) };
}
unsafe extern "C" fn guest_validate(
    _: *mut c_void,
    _: *const u8,
    _: u32,
    records: *mut u32,
) -> u32 {
    unsafe { records.write(1) };
    0
}
unsafe extern "C" fn guest_begin(_: *mut c_void, _: u32) {}
unsafe extern "C" fn guest_record(_: *mut c_void, _: *const u8) {}
unsafe extern "C" fn guest_enqueue(
    _: *mut c_void,
    _: *mut u8,
    _: u32,
    _: u32,
    _: *const Enqueue,
) -> u32 {
    0
}
