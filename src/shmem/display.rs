// SPDX-License-Identifier: GPL-2.0-or-later
//! Protocol v5: immutable mmap epochs with page-aligned, explicitly leased planes.
use super::input::{
    DreamGpuInputEvent, InputSender, DREAMGPU_INPUT_KEY, DREAMGPU_INPUT_MOUSE_ABS,
    DREAMGPU_INPUT_MOUSE_BTN, DREAMGPU_INPUT_MOUSE_REL, DREAMGPU_INPUT_REFRESH,
    DREAMGPU_INPUT_RESET,
};
use crate::{perf, FrameAllocation, FrameLease, FramePixels, FrameStorage, PixelFormat};
use std::os::unix::io::RawFd;
use std::sync::{
    atomic::{AtomicU32, AtomicU64, Ordering},
    Arc,
};

pub const DREAMGPU_SHMEM_MAGIC: u32 = 0x454B554A;
pub const DREAMGPU_SHMEM_VERSION: u32 = 5;
pub const DREAMGPU_CURSOR_MAX_SIZE: usize = 64;
pub const DREAMGPU_CURSOR_MAX_PIXELS: usize = 4096;
const FREE: u32 = 0;
const READY: u32 = 2;
const READING: u32 = 3;
const SLOT_COUNT: usize = 3;
const ROW_ALIGNMENT: usize = 256;
// A common multiple of supported 4K, 16K and 64K host page sizes. Native
// no-copy imports can wrap each plane independently within the same mmap.
const PLANE_ALIGNMENT: usize = 65536;
const PIXEL_BASE: usize = (size_of::<ShmemHeader>() + SLOT_COUNT * size_of::<FrameMeta>())
    .next_multiple_of(PLANE_ALIGNMENT);

#[repr(C)]
pub struct ShmemHeader {
    pub magic: u32,
    pub version: u32,
    pub width: u32,
    pub height: u32,
    pub stride: u32,
    pub format: u32,
    pub frame_counter: AtomicU64,
    pub reserved: [u32; 12],
    pub slots: [AtomicU32; 3],
    pub padding: u32,
}

#[repr(C)]
struct FrameMeta {
    generation: u64,
    published_us: u64,
    input_id: u64,
    cursor_version: u32,
    cursor_x: i32,
    cursor_y: i32,
    cursor_visible: u32,
    cursor_width: u32,
    cursor_height: u32,
    cursor_hot_x: i32,
    cursor_hot_y: i32,
    cursor: [u32; DREAMGPU_CURSOR_MAX_PIXELS],
}

#[derive(Debug, Clone, Copy)]
pub struct DirtyRegion {
    pub x: u32,
    pub y: u32,
    pub width: u32,
    pub height: u32,
}
#[derive(Debug, Clone)]
pub struct CursorData {
    pub width: u16,
    pub height: u16,
    pub hot_x: u16,
    pub hot_y: u16,
    pub rgba: Vec<u32>,
}

struct Mapping {
    ptr: *mut u8,
    size: usize,
    fd: RawFd,
    layout: FrameLayout,
}

#[derive(Clone, Copy, Default)]
struct FrameLayout {
    pixel_bytes: usize,
    plane_bytes: usize,
    total_bytes: usize,
}
impl FrameLayout {
    fn new(width: u32, height: u32, stride: u32) -> Option<Self> {
        if width == 0
            || height == 0
            || !(stride as usize).is_multiple_of(ROW_ALIGNMENT)
            || (stride as usize) < (width as usize).checked_mul(4)?
        {
            return None;
        }
        let pixel_bytes = (stride as usize).checked_mul(height as usize)?;
        let plane_bytes = pixel_bytes.checked_add(PLANE_ALIGNMENT - 1)? & !(PLANE_ALIGNMENT - 1);
        let total_bytes = plane_bytes
            .checked_mul(SLOT_COUNT)?
            .checked_add(PIXEL_BASE)?;
        if total_bytes > isize::MAX as usize {
            return None;
        }
        Some(Self {
            pixel_bytes,
            plane_bytes,
            total_bytes,
        })
    }
}
// Immutable geometry and slot ownership atomics form the cross-process contract.
// Slot contents are accessed only after claiming READING and remain immutable
// until the last Arc<QemuFrame> releases that slot.
unsafe impl Send for Mapping {}
unsafe impl Sync for Mapping {}
// SAFETY: one immutable-size MAP_SHARED mapping owns this entire contiguous
// span. Its final Arc drop unmaps it; native geometry changes allocate new fds
// instead of resizing this one. FrameStorage only clones allocation ownership;
// QemuFrame separately controls which pixel plane is immutable for readers.
unsafe impl FrameAllocation for Mapping {
    fn base_ptr(&self) -> *mut u8 {
        self.ptr
    }
    fn len(&self) -> usize {
        self.size
    }
}
impl Mapping {
    fn header(&self) -> &ShmemHeader {
        unsafe { &*self.ptr.cast() }
    }
    fn pixel_bytes(&self) -> usize {
        self.layout.pixel_bytes
    }
    fn plane_offset(&self, slot: usize) -> usize {
        debug_assert!(slot < SLOT_COUNT);
        PIXEL_BASE + slot * self.layout.plane_bytes
    }
    fn pixels(&self, slot: usize) -> *mut u8 {
        // FrameLayout and from_fd validated every plane against the mapping.
        unsafe { self.ptr.add(self.plane_offset(slot)) }
    }
    fn meta(&self, slot: usize) -> *const FrameMeta {
        unsafe {
            self.ptr
                .add(size_of::<ShmemHeader>() + slot * size_of::<FrameMeta>())
                .cast()
        }
    }
}
impl Drop for Mapping {
    fn drop(&mut self) {
        unsafe {
            libc::munmap(self.ptr.cast(), self.size);
            libc::close(self.fd);
        }
    }
}

/// Holding this object prevents QEMU from writing the slot, even across resize.
pub struct QemuFrame {
    mapping: Arc<Mapping>,
    slot: usize,
}
impl QemuFrame {
    fn meta(&self) -> &FrameMeta {
        unsafe { &*self.mapping.meta(self.slot) }
    }
    fn lease(self: &Arc<Self>) -> FrameLease {
        let h = self.mapping.header();
        FrameLease {
            pixels: self.clone(),
            width: h.width,
            height: h.height,
            stride: h.stride,
            format: PixelFormat::Bgra8,
            generation: self.meta().generation,
            damage: None,
        }
    }
}
impl FramePixels for QemuFrame {
    fn bytes(&self) -> &[u8] {
        unsafe {
            std::slice::from_raw_parts(self.mapping.pixels(self.slot), self.mapping.pixel_bytes())
        }
    }
    fn storage(&self) -> Option<FrameStorage> {
        Some(FrameStorage {
            allocation: self.mapping.clone(),
            offset: self.mapping.plane_offset(self.slot),
            length: self.mapping.layout.plane_bytes,
        })
    }
}
impl Drop for QemuFrame {
    fn drop(&mut self) {
        self.mapping.header().slots[self.slot].store(FREE, Ordering::Release);
    }
}

pub struct ShmemDisplay {
    mapping: Arc<Mapping>,
    current: Option<Arc<QemuFrame>>,
    /// Packed scratch used only by compatibility APIs that omit stride.
    compatibility_pixels: Vec<u8>,
    last_frame: u64,
    last_notified_frame: u64,
    last_cursor_version: u32,
    input: Option<InputSender>,
}
impl ShmemDisplay {
    /// Immutable producer epoch stored in the v5 header's reserved extension.
    pub fn epoch(&self) -> u64 {
        let reserved = &self.mapping.header().reserved;
        u64::from(reserved[0]) | (u64::from(reserved[1]) << 32)
    }
    /// Snapshot of producer lease states for acceptance diagnostics.
    pub fn slot_states(&self) -> [u32; 3] {
        self.mapping
            .header()
            .slots
            .each_ref()
            .map(|slot| slot.load(Ordering::Acquire))
    }
    /// Takes ownership of fd, including on validation failure.
    /// # Safety
    /// fd must describe a shared-memory object, with its actual fstat size.
    pub unsafe fn from_fd(fd: RawFd, size: usize) -> Option<Self> {
        if size < PIXEL_BASE || size > isize::MAX as usize || !size.is_multiple_of(PLANE_ALIGNMENT)
        {
            libc::close(fd);
            return None;
        }
        let ptr = libc::mmap(
            std::ptr::null_mut(),
            size,
            libc::PROT_READ | libc::PROT_WRITE,
            libc::MAP_SHARED,
            fd,
            0,
        );
        if ptr == libc::MAP_FAILED {
            libc::close(fd);
            return None;
        }
        let mut mapping = Mapping {
            ptr: ptr.cast(),
            size,
            fd,
            layout: FrameLayout::default(),
        };
        let h = mapping.header();
        let layout = FrameLayout::new(h.width, h.height, h.stride);
        // Accept only 32-bit little-endian BGRA-compatible surfaces. The current
        // guest display devices use PIXMAN_x8r8g8b8 / PIXMAN_a8r8g8b8.
        if h.magic != DREAMGPU_SHMEM_MAGIC
            || h.version != DREAMGPU_SHMEM_VERSION
            || layout.is_none_or(|layout| layout.total_bytes != size)
            || !matches!(h.format, 0x20020888 | 0x20028888)
        {
            tracing::error!(
                "invalid QEMU v5 display epoch: version={} {}x{} stride={} format={:#x}",
                h.version,
                h.width,
                h.height,
                h.stride,
                h.format
            );
            return None;
        }
        mapping.layout = layout?;
        Some(Self {
            mapping: Arc::new(mapping),
            current: None,
            compatibility_pixels: Vec::new(),
            last_frame: 0,
            last_notified_frame: 0,
            last_cursor_version: 0,
            input: None,
        })
    }
    pub(super) fn attach_input(&mut self, input: InputSender) {
        self.input = Some(input);
    }
    pub fn is_valid(&self) -> bool {
        true
    }
    pub fn has_new_frame(&self) -> bool {
        self.frame_counter() != self.last_notified_frame
    }
    pub fn take_frame_changed(&mut self) -> bool {
        let generation = self.frame_counter();
        let changed = generation != self.last_notified_frame;
        self.last_notified_frame = generation;
        changed
    }
    pub fn frame_counter(&self) -> u64 {
        self.mapping.header().frame_counter.load(Ordering::Acquire)
    }
    pub fn dimensions(&self) -> (u32, u32) {
        let h = self.mapping.header();
        (h.width, h.height)
    }
    pub fn dimensions_checked(&self) -> Option<(u32, u32)> {
        Some(self.dimensions())
    }
    pub fn stride(&self) -> u32 {
        self.mapping.header().stride
    }
    pub fn dirty_region(&self) -> DirtyRegion {
        let (width, height) = self.dimensions();
        DirtyRegion {
            x: 0,
            y: 0,
            width,
            height,
        }
    }

    fn acquire_latest(&mut self) -> bool {
        let mut latest: Option<Arc<QemuFrame>> = None;
        // Claim before inspecting metadata: a READY slot can be reclaimed by
        // QEMU at any time. Looking at generation before CAS would race writes.
        for slot in 0..3 {
            if self.mapping.header().slots[slot]
                .compare_exchange(READY, READING, Ordering::Acquire, Ordering::Relaxed)
                .is_ok()
            {
                let frame = Arc::new(QemuFrame {
                    mapping: self.mapping.clone(),
                    slot,
                });
                if frame.meta().generation > self.last_frame
                    && latest
                        .as_ref()
                        .is_none_or(|old| frame.meta().generation > old.meta().generation)
                {
                    latest = Some(frame);
                }
            }
        }
        if let Some(frame) = latest {
            self.last_frame = frame.meta().generation;
            perf::event("frame.claimed", self.last_frame, frame.bytes().len() as u64);
            self.current = Some(frame);
            true
        } else {
            false
        }
    }
    pub fn prepare_frame(&mut self) {
        self.acquire_latest();
    }
    pub fn acquire_frame(&mut self) -> Option<FrameLease> {
        self.acquire_latest();
        Some(self.current.as_ref()?.lease())
    }
    /// A coherence gate rejected this cached image. Preserve the generation
    /// watermark while releasing our read lease so a newer frame can publish.
    pub fn discard_current(&mut self) {
        self.current = None;
    }
    pub fn get_framebuffer_with_dimensions(&mut self) -> Option<(&[u8], u32, u32)> {
        if !self.acquire_latest() {
            return None;
        }
        let (w, h) = self.dimensions();
        Some((self.current_packed_pixels()?, w, h))
    }
    pub fn get_framebuffer(&mut self) -> Option<&[u8]> {
        self.get_framebuffer_with_dimensions().map(|(b, _, _)| b)
    }
    /// Packed current image without claiming another frame. Requires mutable
    /// access only for the reusable compatibility scratch buffer; acquire_frame
    /// retains the native padded stride and never packs.
    pub fn get_framebuffer_now(&mut self) -> &[u8] {
        self.current_packed_pixels().unwrap_or(&[])
    }
    fn current_packed_pixels(&mut self) -> Option<&[u8]> {
        let frame = self.current.as_ref()?;
        let header = frame.mapping.header();
        // Geometry and these products were validated when importing the epoch.
        let row = header.width as usize * 4;
        let length = row * header.height as usize;
        if header.stride as usize == row {
            return Some(&frame.bytes()[..length]);
        }
        self.compatibility_pixels.resize(length, 0);
        for (y, destination) in self.compatibility_pixels.chunks_exact_mut(row).enumerate() {
            let start = y * header.stride as usize;
            destination.copy_from_slice(&frame.bytes()[start..start + row]);
        }
        Some(&self.compatibility_pixels)
    }
    pub fn has_new_cursor(&self) -> bool {
        self.current
            .as_ref()
            .is_some_and(|f| f.meta().cursor_version != self.last_cursor_version)
    }
    pub fn get_cursor(&mut self) -> Option<CursorData> {
        let frame = self.current.as_ref()?;
        let m = frame.meta();
        if m.cursor_version == self.last_cursor_version {
            return None;
        }
        self.last_cursor_version = m.cursor_version;
        let w = m.cursor_width.min(DREAMGPU_CURSOR_MAX_SIZE as u32) as usize;
        let h = m.cursor_height.min(DREAMGPU_CURSOR_MAX_SIZE as u32) as usize;
        let mut rgba = Vec::with_capacity(w * h);
        for y in 0..h {
            rgba.extend_from_slice(&m.cursor[y * 64..y * 64 + w]);
        }
        Some(CursorData {
            width: w as u16,
            height: h as u16,
            hot_x: m.cursor_hot_x.max(0) as u16,
            hot_y: m.cursor_hot_y.max(0) as u16,
            rgba,
        })
    }
    pub fn cursor_state(&self) -> (i32, i32, bool) {
        self.current.as_ref().map_or((0, 0, false), |f| {
            let m = f.meta();
            (m.cursor_x, m.cursor_y, m.cursor_visible != 0)
        })
    }
    pub fn send_mouse_rel(&self, x: i32, y: i32) {
        self.send(DREAMGPU_INPUT_MOUSE_REL, 0, 0, x, y);
    }
    pub fn send_mouse_abs(&self, x: i32, y: i32) {
        self.send(DREAMGPU_INPUT_MOUSE_ABS, 0, 0, x, y);
    }
    pub fn send_mouse_btn(&self, button: u8, pressed: bool) {
        self.send(DREAMGPU_INPUT_MOUSE_BTN, button, pressed as u8, 0, 0);
    }
    pub fn send_key(&self, code: u32, pressed: bool) {
        self.send(DREAMGPU_INPUT_KEY, 0, pressed as u8, code as i32, 0);
    }
    pub fn refresh(&self) {
        self.send(DREAMGPU_INPUT_REFRESH, 0, 0, 0, 0);
    }
    pub fn release_all_and_wait(&self) -> std::io::Result<()> {
        self.input
            .as_ref()
            .ok_or(std::io::ErrorKind::NotConnected)?
            .reset_and_wait(std::time::Duration::from_secs(2))
    }
    pub fn release_all(&self) {
        self.send(DREAMGPU_INPUT_RESET, 0, 0, 0, 0);
    }
    fn send(&self, event_type: u8, button: u8, pressed: u8, x: i32, y: i32) {
        if let Some(input) = &self.input {
            input.send(DreamGpuInputEvent {
                event_type,
                button,
                pressed,
                reserved: 0,
                x,
                y,
                padding: 0,
                id: perf::input_id(),
            });
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn display(width: u32, height: u32) -> ShmemDisplay {
        use std::os::fd::IntoRawFd;
        let path = std::env::temp_dir().join(format!(
            "dreamgpu-frame-test-{}-{}",
            std::process::id(),
            uuid::Uuid::new_v4()
        ));
        let f = std::fs::OpenOptions::new()
            .read(true)
            .write(true)
            .create_new(true)
            .open(&path)
            .unwrap();
        std::fs::remove_file(path).unwrap();
        let stride = (width * 4).next_multiple_of(ROW_ALIGNMENT as u32);
        let size = FrameLayout::new(width, height, stride).unwrap().total_bytes;
        f.set_len(size as u64).unwrap();
        let fd = f.into_raw_fd();
        unsafe {
            let ptr = libc::mmap(
                std::ptr::null_mut(),
                size,
                libc::PROT_READ | libc::PROT_WRITE,
                libc::MAP_SHARED,
                fd,
                0,
            );
            assert_ne!(ptr, libc::MAP_FAILED);
            ptr.cast::<ShmemHeader>().write(ShmemHeader {
                magic: DREAMGPU_SHMEM_MAGIC,
                version: DREAMGPU_SHMEM_VERSION,
                width,
                height,
                stride,
                format: 0x20020888,
                frame_counter: AtomicU64::new(0),
                reserved: [0; 12],
                slots: std::array::from_fn(|_| AtomicU32::new(FREE)),
                padding: 0,
            });
            libc::munmap(ptr, size);
            ShmemDisplay::from_fd(fd, size).unwrap()
        }
    }
    fn publish(mapping: &Mapping, slot: usize, generation: u64, value: u8) -> bool {
        if mapping.header().slots[slot]
            .compare_exchange(FREE, 1, Ordering::Acquire, Ordering::Relaxed)
            .is_err()
        {
            return false;
        }
        unsafe {
            let m = mapping.meta(slot).cast_mut();
            std::ptr::write_bytes(m.cast::<u8>(), 0, size_of::<FrameMeta>());
            (*m).generation = generation;
            std::ptr::write_bytes(mapping.pixels(slot), value, mapping.pixel_bytes());
        }
        mapping.header().slots[slot].store(READY, Ordering::Release);
        mapping
            .header()
            .frame_counter
            .store(generation, Ordering::Release);
        true
    }
    #[test]
    fn rejecting_cached_frame_releases_only_our_lease_and_keeps_generation_watermark() {
        let mut d = display(2, 1);
        assert!(publish(&d.mapping, 0, 10, 10));
        let external = d.acquire_frame().unwrap();
        d.discard_current();
        assert_eq!(d.mapping.header().slots[0].load(Ordering::Acquire), READING);
        drop(external);
        assert_eq!(d.mapping.header().slots[0].load(Ordering::Acquire), FREE);
        assert!(publish(&d.mapping, 0, 9, 9));
        assert!(
            d.acquire_frame().is_none(),
            "rejecting a frame must not accept an older publication"
        );
        assert!(publish(&d.mapping, 0, 11, 11));
        assert_eq!(d.acquire_frame().unwrap().generation, 11);
    }

    #[test]
    fn leased_pixels_survive_resize_and_display_drop() {
        let mut d = display(3, 1); // padded row and page with a nonaligned width
        assert!(publish(&d.mapping, 0, 1, 42));
        let lease = d.acquire_frame().unwrap();
        let map = d.mapping.clone();
        assert!(!publish(&map, 0, 2, 99));
        let replacement = display(4, 4);
        drop(d);
        drop(replacement);
        assert_eq!(lease.stride, 256);
        assert_eq!(lease.bytes(), &[42; 256]);
        assert_eq!(lease.packed_copy().unwrap(), &[42; 12]);
        assert_eq!(map.header().slots[0].load(Ordering::Acquire), READING);
        drop(lease);
        assert!(publish(&map, 0, 2, 99));
    }
    #[test]
    fn skipped_frames_choose_newest_complete_snapshot() {
        let mut d = display(3, 3);
        assert!(publish(&d.mapping, 0, 1, 1));
        assert!(publish(&d.mapping, 1, 3, 3));
        assert!(publish(&d.mapping, 2, 2, 2));
        let frame = d.acquire_frame().unwrap();
        assert_eq!(frame.generation, 3);
        assert!(frame.bytes().iter().all(|&b| b == 3));
        assert_eq!(d.mapping.header().slots[0].load(Ordering::Acquire), FREE);
        assert_eq!(d.mapping.header().slots[2].load(Ordering::Acquire), FREE);
        assert_eq!(d.acquire_frame().unwrap().generation, 3);
        assert!(d.take_frame_changed());
        assert!(!d.take_frame_changed());
    }
    #[test]
    fn concurrent_publication_never_modifies_leased_pixels() {
        let mut display = display(33, 17);
        let mapping = display.mapping.clone();
        let done = Arc::new(std::sync::atomic::AtomicBool::new(false));
        let finished = done.clone();
        let writer = std::thread::spawn(move || {
            for generation in 1..=1000 {
                loop {
                    if (0..3).any(|slot| publish(&mapping, slot, generation, generation as u8)) {
                        break;
                    }
                    std::thread::yield_now();
                }
            }
            finished.store(true, Ordering::Release);
        });
        let mut checked = 0;
        while !done.load(Ordering::Acquire) {
            if let Some(frame) = display.acquire_frame() {
                // Give the producer opportunities to write other slots while this
                // lease remains alive, then check every pixel for torn snapshots.
                std::thread::yield_now();
                assert!(frame.bytes().iter().all(|&b| b == frame.generation as u8));
                checked += 1;
            }
        }
        writer.join().unwrap();
        assert_eq!(display.acquire_frame().unwrap().generation, 1000);
        assert!(checked > 0);
    }
    #[test]
    fn protocol_layout() {
        assert_eq!(size_of::<ShmemHeader>(), 96);
        assert_eq!(size_of::<FrameMeta>(), 16440);
        assert_eq!(std::mem::offset_of!(ShmemHeader, slots), 80);
        assert_eq!(PIXEL_BASE, 65536);
        let layout = FrameLayout::new(641, 479, 2816).unwrap();
        assert_eq!(layout.pixel_bytes, 1_348_864);
        assert_eq!(layout.plane_bytes, 1_376_256);
        assert_eq!(layout.total_bytes, 4_194_304);
    }

    #[test]
    fn allocation_owner_outlives_lease_without_pinning_slot() {
        let mut display = display(17, 9);
        assert!(publish(&display.mapping, 1, 7, 0x42));
        let frame = display.acquire_frame().unwrap();
        let storage = frame.pixels.storage().unwrap();
        assert_eq!(frame.stride, 256);
        assert_eq!(storage.offset, PIXEL_BASE + PLANE_ALIGNMENT);
        assert_eq!(storage.length, PLANE_ALIGNMENT);
        assert_eq!(storage.allocation.len(), PIXEL_BASE + 3 * PLANE_ALIGNMENT);
        assert_eq!(storage.offset % PLANE_ALIGNMENT, 0);
        assert_eq!(storage.length % PLANE_ALIGNMENT, 0);
        assert_eq!(
            unsafe { storage.allocation.base_ptr().add(storage.offset) } as *const u8,
            frame.bytes().as_ptr(),
        );
        let page_size = unsafe { libc::sysconf(libc::_SC_PAGESIZE) } as usize;
        assert_eq!(storage.allocation.base_ptr() as usize % page_size, 0);
        assert_eq!(storage.length % page_size, 0);
        // The remainder of a plane is initialized and remains available to
        // import even though FramePixels exposes only the image's padded rows.
        let padding = unsafe {
            std::slice::from_raw_parts(
                storage
                    .allocation
                    .base_ptr()
                    .add(storage.offset + frame.bytes().len()),
                storage.length - frame.bytes().len(),
            )
        };
        assert!(padding.iter().all(|&byte| byte == 0));
        drop(display);
        drop(frame);
        // The allocation owner keeps the mmap alive but did not keep READING.
        let header = unsafe { &*storage.allocation.base_ptr().cast::<ShmemHeader>() };
        assert_eq!(header.slots[1].load(Ordering::Acquire), FREE);
    }

    #[test]
    fn padded_rows_pack_without_including_native_alignment_bytes() {
        let mut display = display(17, 9);
        assert!(publish(&display.mapping, 0, 1, 0xcc));
        // This test producer owns READY exclusively before acquiring a lease.
        unsafe {
            for y in 0..9 {
                std::ptr::write_bytes(display.mapping.pixels(0).add(y * 256), y as u8, 17 * 4);
            }
        }
        let frame = display.acquire_frame().unwrap();
        let packed = frame.packed_copy().unwrap();
        for (y, row) in packed.as_chunks::<{ 17 * 4 }>().0.iter().enumerate() {
            assert!(row.iter().all(|&byte| byte == y as u8));
        }
        assert_eq!(frame.pixels.storage().unwrap().length, 65536);
    }

    #[test]
    fn invalid_geometry_and_overflow_are_rejected() {
        assert!(FrameLayout::new(0, 1, 256).is_none());
        assert!(FrameLayout::new(1, 0, 256).is_none());
        assert!(FrameLayout::new(65, 1, 256).is_none());
        assert!(FrameLayout::new(1, 1, 260).is_none());
        assert!(FrameLayout::new(u32::MAX, 1, u32::MAX - 255).is_none());
        assert!(FrameLayout::new(1, u32::MAX, u32::MAX - 255).is_none());
    }

    #[test]
    fn legacy_getters_pack_multiple_padded_rows() {
        let mut display = display(3, 3);
        assert!(publish(&display.mapping, 0, 1, 0xcc));
        // No consumer has acquired the test producer's READY slot yet.
        for y in 0..3 {
            unsafe {
                std::ptr::write_bytes(display.mapping.pixels(0).add(y * 256), y as u8 + 1, 12);
            }
        }
        let expected = [vec![1; 12], vec![2; 12], vec![3; 12]].concat();
        let (pixels, width, height) = display.get_framebuffer_with_dimensions().unwrap();
        assert_eq!((width, height), (3, 3));
        assert_eq!(pixels, expected);
        let capacity = display.compatibility_pixels.capacity();
        assert_eq!(display.get_framebuffer_now(), expected);
        assert_eq!(display.compatibility_pixels.capacity(), capacity);
        let frame = display.acquire_frame().unwrap();
        assert_eq!(frame.stride, 256);
        assert_eq!(frame.bytes().len(), 768);
        assert_eq!(frame.packed_copy().unwrap(), expected);
    }

    #[test]
    fn packed_legacy_getter_borrows_without_scratch_copy() {
        let mut display = display(64, 2);
        assert!(publish(&display.mapping, 0, 1, 7));
        let pointer = display.mapping.pixels(0);
        let (pixels, _, _) = display.get_framebuffer_with_dimensions().unwrap();
        assert_eq!(pixels.as_ptr(), pointer as *const u8);
        assert_eq!(pixels, &[7; 512]);
        assert!(display.compatibility_pixels.is_empty());
    }
}
