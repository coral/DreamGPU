// SPDX-License-Identifier: GPL-2.0-or-later
//! Direct Metal textures over immutable producer-owned framebuffer mappings.
//!
//! The bounded cache retains native resources and mapping lifetime. Current
//! frames and submitted GPU work separately retain producer read leases, so
//! cached textures do not prevent a producer from reusing an idle slot.
//!
//! Integration contract: keep the frame alive for as long as its texture/view
//! can be submitted, and call `retain_until_submitted_work_done` immediately
//! after EVERY submission sampling it. Service wgpu completion callbacks even
//! when the window is idle or tracing is disabled. Never retain a cloned view or
//! bind group for later use after releasing its associated frame.

use std::{
    collections::VecDeque,
    ffi::c_void,
    ptr::NonNull,
    sync::{Arc, Mutex},
};

use crate::{FrameLease, FrameStorage, PixelFormat};

use objc2::{rc::Retained, runtime::ProtocolObject};
use objc2_metal::{
    MTLBuffer, MTLCPUCacheMode, MTLDevice, MTLPixelFormat, MTLResourceOptions, MTLStorageMode,
    MTLTextureDescriptor, MTLTextureType, MTLTextureUsage,
};

/// Ownership of this module's Metal shared allocation, not general MTLBuffer
/// access. Retaining/releasing Metal resources is thread-safe. The raw handle
/// stays private: imported pixels are protected by their producer's FrameLease.
/// GPU completion callbacks only drop owners.
#[derive(Clone)]
struct SharedBuffer {
    _raw: Retained<ProtocolObject<dyn MTLBuffer>>,
}

// SAFETY: transferring this owner only transfers a retained Metal resource;
// creation is complete before publication and destruction may occur on any
// completion-polling thread. No thread-affine encoder is stored here.
unsafe impl Send for SharedBuffer {}
// SAFETY: shared owners only retain/release this read-only resource. The
// producer writes through its own allocation only after all FrameLeases finish.
unsafe impl Sync for SharedBuffer {}

struct Slot {
    _buffer: SharedBuffer,
    _texture: wgpu::Texture,
    view: wgpu::TextureView,
}

/// A frame whose shared storage must remain immutable until its GPU use ends.
#[derive(Clone)]
pub struct SharedTextureFrame {
    slot: Arc<Slot>,
    // Pin producer READ ownership independently of the cached mapping/texture.
    _lease: FrameLease,
}

impl SharedTextureFrame {
    pub fn texture(&self) -> &wgpu::Texture {
        &self.slot._texture
    }

    pub fn view(&self) -> &wgpu::TextureView {
        &self.slot.view
    }

    /// Register immediately after submitting commands that sample this frame.
    /// `on_complete` runs after releasing this submission's slot reference.
    pub fn retain_until_submitted_work_done(
        &self,
        queue: &wgpu::Queue,
        on_complete: impl FnOnce() + Send + 'static,
    ) {
        let retained = self.clone();
        queue.on_submitted_work_done(move || {
            drop(retained);
            on_complete();
        });
    }
}

const DIRECT_CACHE_CAPACITY: usize = 6;

#[derive(Clone, Copy, PartialEq, Eq)]
struct DirectKey {
    allocation: usize,
    offset: usize,
    length: usize,
    width: u32,
    height: u32,
    stride: u32,
}

/// Cached native resources retain only allocation lifetime, never producer READ
/// ownership. A returned frame pins the full FrameLease until its last GPU use.
#[derive(Default)]
pub struct DirectTextureCache {
    device: Option<wgpu::Device>,
    entries: VecDeque<(DirectKey, Arc<Slot>)>,
}

impl DirectTextureCache {
    pub fn clear(&mut self) {
        self.entries.clear();
        self.device = None;
    }

    pub fn import(
        &mut self,
        device: &wgpu::Device,
        frame: &FrameLease,
    ) -> Result<Option<SharedTextureFrame>, String> {
        let Some(storage) = frame.pixels.storage() else {
            return Ok(None);
        };
        if frame.format != PixelFormat::Bgra8 {
            return Ok(None);
        }
        if !frame.is_valid() {
            return Err("invalid direct-import frame".into());
        }
        let limit = device.limits().max_texture_dimension_2d;
        if frame.width > limit || frame.height > limit {
            return Err("direct texture dimensions exceed device limits".into());
        }
        let raw_device = {
            // SAFETY: retain the native device only; do not modify HAL state.
            let Some(hal) = (unsafe { device.as_hal::<wgpu::hal::api::Metal>() }) else {
                return Ok(None);
            };
            hal.raw_device().clone()
        };
        if !raw_device.hasUnifiedMemory() {
            return Ok(None);
        }
        let alignment =
            raw_device.minimumLinearTextureAlignmentForPixelFormat(MTLPixelFormat::BGRA8Unorm_sRGB);
        // sysconf is read-only and reports the host VM page size (16KiB on
        // current Apple Silicon, rather than assuming the protocol's 64KiB).
        let page_size = unsafe { libc::sysconf(libc::_SC_PAGESIZE) };
        if page_size <= 0 {
            return Err("cannot determine VM page size".into());
        }
        let pointer = validate_direct_storage(frame, &storage, alignment, page_size as usize)?;
        if storage.length > raw_device.maxBufferLength() {
            return Err("direct framebuffer exceeds Metal buffer size limit".into());
        }
        if self.device.as_ref() != Some(device) {
            self.clear();
            self.device = Some(device.clone());
        }
        let key = DirectKey {
            allocation: Arc::as_ptr(&storage.allocation) as *const () as usize,
            offset: storage.offset,
            length: storage.length,
            width: frame.width,
            height: frame.height,
            stride: frame.stride,
        };
        if let Some(index) = self
            .entries
            .iter()
            .position(|(candidate, _)| *candidate == key)
        {
            let entry = self.entries.remove(index).unwrap();
            let slot = entry.1.clone();
            self.entries.push_back(entry);
            return Ok(Some(SharedTextureFrame {
                slot,
                _lease: frame.clone(),
            }));
        }

        // Metal retains this block until its MTLBuffer is deallocated. Holding
        // only a wgpu drop callback is insufficient: another native texture/view
        // may still retain the MTLBuffer after that callback has run.
        let allocation = Mutex::new(Some(storage.allocation.clone()));
        let deallocator = block2::RcBlock::new(move |_: NonNull<c_void>, _: usize| {
            drop(allocation.lock().unwrap_or_else(|e| e.into_inner()).take());
        });
        // SAFETY: the unsafe FrameAllocation contract guarantees one live VM
        // mapping. Validation checks the page-aligned subrange and pixel pointer.
        // The sendable deallocator owns the allocation until native release;
        // frame and GPU callback clones separately prevent producer overwrites.
        let buffer = unsafe {
            raw_device.newBufferWithBytesNoCopy_length_options_deallocator(
                pointer,
                storage.length,
                MTLResourceOptions::StorageModeShared,
                Some(&deallocator),
            )
        }
        .ok_or("Metal cannot wrap this framebuffer mapping")?;
        let native_desc = MTLTextureDescriptor::new();
        native_desc.setTextureType(MTLTextureType::Type2D);
        native_desc.setPixelFormat(MTLPixelFormat::BGRA8Unorm_sRGB);
        // SAFETY: valid nonzero dimensions checked against device limits.
        unsafe {
            native_desc.setWidth(frame.width as usize);
            native_desc.setHeight(frame.height as usize);
            native_desc.setDepth(1);
            native_desc.setMipmapLevelCount(1);
            native_desc.setArrayLength(1);
            native_desc.setSampleCount(1);
        }
        native_desc.setStorageMode(MTLStorageMode::Shared);
        native_desc.setCpuCacheMode(MTLCPUCacheMode::DefaultCache);
        native_desc.setUsage(MTLTextureUsage::ShaderRead);
        let native_texture = buffer
            .newTextureWithDescriptor_offset_bytesPerRow(&native_desc, 0, frame.stride as usize)
            .ok_or("Metal cannot create a texture over this framebuffer mapping")?;
        let desc = wgpu::TextureDescriptor {
            label: Some("DreamGPU direct mmap framebuffer"),
            size: wgpu::Extent3d {
                width: frame.width,
                height: frame.height,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Bgra8UnormSrgb,
            usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_SRC,
            view_formats: &[],
        };
        // SAFETY: texture matches desc and belongs to this device. Pixels are
        // initialized and protected by the FrameLease. Native buffer deallocation
        // retains mapping lifetime even after cache/lease/resource eviction.
        let texture = unsafe {
            let hal_texture = wgpu::hal::metal::Device::texture_from_raw(
                native_texture,
                desc.format,
                MTLTextureType::Type2D,
                1,
                1,
                desc.size.into(),
                None,
            );
            device.create_texture_from_hal::<wgpu::hal::api::Metal>(
                hal_texture,
                &desc,
                wgpu::TextureUses::RESOURCE,
            )
        };
        let view = texture.create_view(&Default::default());
        let slot = Arc::new(Slot {
            _buffer: SharedBuffer { _raw: buffer },
            _texture: texture,
            view,
        });
        if self.entries.len() == DIRECT_CACHE_CAPACITY {
            self.entries.pop_front();
        }
        self.entries.push_back((key, slot.clone()));
        Ok(Some(SharedTextureFrame {
            slot,
            _lease: frame.clone(),
        }))
    }
}

fn validate_direct_storage(
    frame: &FrameLease,
    storage: &FrameStorage,
    row_alignment: usize,
    page_size: usize,
) -> Result<NonNull<c_void>, String> {
    let base = storage.allocation.base_ptr();
    let allocation_len = storage.allocation.len();
    let end = storage
        .offset
        .checked_add(storage.length)
        .ok_or("direct storage range overflow")?;
    let address = (base as usize)
        .checked_add(storage.offset)
        .ok_or("direct storage address overflow")?;
    let image_len = (frame.stride as usize)
        .checked_mul(frame.height as usize)
        .ok_or("direct texture image size overflow")?;
    if page_size == 0
        || row_alignment == 0
        || base.is_null()
        || !(base as usize).is_multiple_of(page_size)
        || !allocation_len.is_multiple_of(page_size)
        || !storage.offset.is_multiple_of(page_size)
        || storage.length == 0
        || !storage.length.is_multiple_of(page_size)
        || end > allocation_len
        || end > isize::MAX as usize
        || address % page_size != 0
        || !(frame.stride as usize).is_multiple_of(row_alignment)
        || image_len > storage.length
        || frame.bytes().as_ptr() as usize != address
    {
        return Err(
            "direct texture storage has invalid bounds, alignment, or pixel identity".into(),
        );
    }
    // SAFETY: the in-bounds offset was checked; retain pointer provenance from
    // FrameAllocation rather than reconstructing a pointer from its address.
    NonNull::new(unsafe { base.add(storage.offset) }.cast())
        .ok_or_else(|| "null direct texture pointer".into())
}

#[cfg(test)]
mod tests {
    use super::{validate_direct_storage, DirectTextureCache};
    use crate::{FrameAllocation, FrameLease, FramePixels, FrameStorage, PixelFormat};
    use objc2_metal::MTLBuffer;
    use std::sync::{
        atomic::{AtomicBool, AtomicUsize, Ordering},
        Arc,
    };

    struct TestMapping {
        pointer: std::ptr::NonNull<u8>,
        length: usize,
        leases: AtomicUsize,
        dropped: Arc<AtomicBool>,
    }
    // SAFETY: a single initialized mmap region lives until the last allocation
    // owner drops. Test writes occur only before creating immutable pixel leases.
    unsafe impl Send for TestMapping {}
    unsafe impl Sync for TestMapping {}
    unsafe impl FrameAllocation for TestMapping {
        fn base_ptr(&self) -> *mut u8 {
            self.pointer.as_ptr()
        }
        fn len(&self) -> usize {
            self.length
        }
    }
    impl TestMapping {
        fn new(length: usize) -> Arc<Self> {
            // SAFETY: anonymous mmap owns a new initialized, page-aligned VM region.
            let pointer = unsafe {
                libc::mmap(
                    std::ptr::null_mut(),
                    length,
                    libc::PROT_READ | libc::PROT_WRITE,
                    libc::MAP_ANON | libc::MAP_SHARED,
                    -1,
                    0,
                )
            };
            assert_ne!(pointer, libc::MAP_FAILED);
            Arc::new(Self {
                pointer: std::ptr::NonNull::new(pointer.cast()).unwrap(),
                length,
                leases: AtomicUsize::new(0),
                dropped: Arc::new(AtomicBool::new(false)),
            })
        }
        fn write(&self, offset: usize, bytes: &[u8]) {
            assert_eq!(
                self.leases.load(Ordering::Acquire),
                0,
                "producer cannot overwrite leased pixels"
            );
            assert!(offset + bytes.len() <= self.length);
            // SAFETY: test is single-threaded and no immutable pixel lease exists.
            unsafe {
                std::ptr::copy_nonoverlapping(
                    bytes.as_ptr(),
                    self.pointer.as_ptr().add(offset),
                    bytes.len(),
                );
            }
        }
        fn frame(self: &Arc<Self>, width: u32, height: u32, generation: u64) -> FrameLease {
            self.leases.fetch_add(1, Ordering::Relaxed);
            FrameLease {
                pixels: Arc::new(TestPixels {
                    allocation: self.clone(),
                    offset: 65536,
                    length: 65536,
                    bytes_len: 256 * height as usize,
                }),
                width,
                height,
                stride: 256,
                format: PixelFormat::Bgra8,
                generation,
                damage: None,
            }
        }
    }
    impl Drop for TestMapping {
        fn drop(&mut self) {
            assert_eq!(self.leases.load(Ordering::Acquire), 0);
            // SAFETY: final allocation owner releases precisely its original mmap.
            assert_eq!(
                unsafe { libc::munmap(self.pointer.as_ptr().cast(), self.length) },
                0
            );
            self.dropped.store(true, Ordering::Release);
        }
    }
    struct TestPixels {
        allocation: Arc<TestMapping>,
        offset: usize,
        length: usize,
        bytes_len: usize,
    }
    impl FramePixels for TestPixels {
        fn bytes(&self) -> &[u8] {
            // SAFETY: this pixel owner prevents TestMapping::write until dropped.
            unsafe {
                std::slice::from_raw_parts(
                    self.allocation.pointer.as_ptr().add(self.offset),
                    self.bytes_len,
                )
            }
        }
        fn storage(&self) -> Option<FrameStorage> {
            Some(FrameStorage {
                allocation: self.allocation.clone(),
                offset: self.offset,
                length: self.length,
            })
        }
    }
    impl Drop for TestPixels {
        fn drop(&mut self) {
            self.allocation.leases.fetch_sub(1, Ordering::Release);
        }
    }

    #[test]
    fn direct_storage_rejects_misaligned_truncated_and_unrelated_pixels() {
        let allocation = TestMapping::new(131072);
        let frame = allocation.frame(17, 9, 1);
        let page = unsafe { libc::sysconf(libc::_SC_PAGESIZE) } as usize;
        let mut storage = frame.pixels.storage().unwrap();
        assert!(validate_direct_storage(&frame, &storage, 256, page).is_ok());
        storage.offset += 1;
        assert!(validate_direct_storage(&frame, &storage, 256, page).is_err());
        storage.offset -= 1;
        storage.length -= 1;
        assert!(validate_direct_storage(&frame, &storage, 256, page).is_err());
        storage.length = 131072;
        assert!(validate_direct_storage(&frame, &storage, 256, page).is_err());
        storage.length = 65536;
        storage.offset = 0;
        assert!(validate_direct_storage(&frame, &storage, 256, page).is_err());
        storage.offset = usize::MAX;
        assert!(validate_direct_storage(&frame, &storage, 256, page).is_err());
    }

    fn block_on<T>(future: impl std::future::Future<Output = T>) -> T {
        struct WakeThread(std::thread::Thread);
        impl std::task::Wake for WakeThread {
            fn wake(self: Arc<Self>) {
                self.0.unpark();
            }
        }
        let waker = Arc::new(WakeThread(std::thread::current())).into();
        let mut context = std::task::Context::from_waker(&waker);
        let mut future = std::pin::pin!(future);
        loop {
            match future.as_mut().poll(&mut context) {
                std::task::Poll::Ready(value) => return value,
                std::task::Poll::Pending => std::thread::park(),
            }
        }
    }

    /// Exercise the same filtered, sRGB-decoding sampling used by the margin
    /// pass, then read back RGBA output. This deliberately does not copy the
    /// source texture, so this checks shader sampling rather than just a blit.
    fn sampled_pixels(
        device: &wgpu::Device,
        queue: &wgpu::Queue,
        source: &wgpu::TextureView,
        width: u32,
        height: u32,
    ) -> Vec<u8> {
        let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("shared texture regression shader"),
            source: wgpu::ShaderSource::Wgsl(
                r#"
                @group(0) @binding(0) var image: texture_2d<f32>;
                @group(0) @binding(1) var image_sampler: sampler;
                @vertex fn vs(@builtin(vertex_index) index: u32)
                    -> @builtin(position) vec4<f32> {
                    let vertices = array<vec2<f32>, 3>(
                        vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
                    return vec4(vertices[index], 0.0, 1.0);
                }
                @fragment fn fs(@builtin(position) position: vec4<f32>)
                    -> @location(0) vec4<f32> {
                    let uv = (position.xy + vec2(0.25, 0.125))
                        / vec2<f32>(textureDimensions(image));
                    return textureSampleLevel(image, image_sampler, uv, 0.0);
                }
            "#
                .into(),
            ),
        });
        let pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("shared texture regression pipeline"),
            layout: None,
            vertex: wgpu::VertexState {
                module: &shader,
                entry_point: Some("vs"),
                compilation_options: Default::default(),
                buffers: &[],
            },
            fragment: Some(wgpu::FragmentState {
                module: &shader,
                entry_point: Some("fs"),
                compilation_options: Default::default(),
                targets: &[Some(wgpu::ColorTargetState {
                    format: wgpu::TextureFormat::Rgba8Unorm,
                    blend: None,
                    write_mask: wgpu::ColorWrites::ALL,
                })],
            }),
            primitive: Default::default(),
            depth_stencil: None,
            multisample: Default::default(),
            multiview_mask: None,
            cache: None,
        });
        let sampler = device.create_sampler(&wgpu::SamplerDescriptor {
            mag_filter: wgpu::FilterMode::Linear,
            min_filter: wgpu::FilterMode::Linear,
            ..Default::default()
        });
        let bindings = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: None,
            layout: &pipeline.get_bind_group_layout(0),
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::TextureView(source),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::Sampler(&sampler),
                },
            ],
        });
        let output = device.create_texture(&wgpu::TextureDescriptor {
            label: None,
            size: wgpu::Extent3d {
                width,
                height,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba8Unorm,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::COPY_SRC,
            view_formats: &[],
        });
        let row_pitch = (width * 4).div_ceil(wgpu::COPY_BYTES_PER_ROW_ALIGNMENT)
            * wgpu::COPY_BYTES_PER_ROW_ALIGNMENT;
        let readback = device.create_buffer(&wgpu::BufferDescriptor {
            label: None,
            size: u64::from(row_pitch) * u64::from(height),
            usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });
        let output_view = output.create_view(&Default::default());
        let mut encoder = device.create_command_encoder(&Default::default());
        {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: None,
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &output_view,
                    depth_slice: None,
                    resolve_target: None,
                    ops: wgpu::Operations {
                        load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
                        store: wgpu::StoreOp::Store,
                    },
                })],
                ..Default::default()
            });
            pass.set_pipeline(&pipeline);
            pass.set_bind_group(0, &bindings, &[]);
            pass.draw(0..3, 0..1);
        }
        encoder.copy_texture_to_buffer(
            output.as_image_copy(),
            wgpu::TexelCopyBufferInfo {
                buffer: &readback,
                layout: wgpu::TexelCopyBufferLayout {
                    offset: 0,
                    bytes_per_row: Some(row_pitch),
                    rows_per_image: Some(height),
                },
            },
            output.size(),
        );
        queue.submit([encoder.finish()]);
        let (sender, receiver) = std::sync::mpsc::sync_channel(1);
        readback
            .slice(..)
            .map_async(wgpu::MapMode::Read, move |result| {
                sender.send(result).unwrap();
            });
        device.poll(wgpu::PollType::wait_indefinitely()).unwrap();
        receiver.recv().unwrap().unwrap();
        let mapped = readback.slice(..).get_mapped_range().unwrap();
        let pixels = mapped
            .chunks_exact(row_pitch as usize)
            .flat_map(|row| row[..width as usize * 4].iter().copied())
            .collect();
        drop(mapped);
        readback.unmap();
        pixels
    }

    #[test]
    #[ignore = "requires an Apple Silicon Metal device; run outside performance captures"]
    fn metal_direct_mmap_reuses_resources_without_retaining_producer_slots() {
        let instance = wgpu::Instance::new(wgpu::InstanceDescriptor {
            backends: wgpu::Backends::METAL,
            ..wgpu::InstanceDescriptor::new_without_display_handle()
        });
        let adapter = block_on(instance.request_adapter(&Default::default())).unwrap();
        let (device, queue) = block_on(adapter.request_device(&Default::default())).unwrap();
        let (width, height) = (17, 9);
        let allocation = TestMapping::new(131072);
        let mut bytes = vec![0xcc; 256 * height as usize];
        for y in 0..height as usize {
            for x in 0..width as usize {
                bytes[y * 256 + x * 4..y * 256 + x * 4 + 4].copy_from_slice(&[
                    (x * 13 + y * 7) as u8,
                    (x * 3 + y * 29) as u8,
                    (x * 11 + y * 5) as u8,
                    255,
                ]);
            }
        }
        allocation.write(65536, &bytes);
        let source = allocation.frame(width, height, 1);
        let mut cache = DirectTextureCache::default();
        let first = cache.import(&device, &source).unwrap().unwrap();
        // The Metal buffer points at the original mmap plane: no renderer copy.
        assert_eq!(
            first.slot._buffer._raw.contents().as_ptr() as usize,
            allocation.pointer.as_ptr() as usize + 65536
        );
        let first_resource = first.slot.clone();
        // Staging exists only as an independent pixel-correctness oracle here.
        let ordinary = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("direct mmap reference upload"),
            size: wgpu::Extent3d {
                width,
                height,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Bgra8UnormSrgb,
            usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
            view_formats: &[],
        });
        queue.write_texture(
            ordinary.as_image_copy(),
            &bytes,
            wgpu::TexelCopyBufferLayout {
                offset: 0,
                bytes_per_row: Some(256),
                rows_per_image: Some(height),
            },
            ordinary.size(),
        );
        let expected = sampled_pixels(
            &device,
            &queue,
            &ordinary.create_view(&Default::default()),
            width,
            height,
        );
        assert_eq!(
            sampled_pixels(&device, &queue, first.view(), width, height),
            expected
        );

        // Region-only compatibility writes preserve the imported image in a
        // private texture. Verify COPY_SRC support and filtered/sRGB equivalence.
        let mut encoder = device.create_command_encoder(&Default::default());
        encoder.copy_texture_to_texture(
            first.texture().as_image_copy(),
            ordinary.as_image_copy(),
            ordinary.size(),
        );
        queue.submit([encoder.finish()]);
        first.retain_until_submitted_work_done(&queue, || {});
        assert_eq!(
            sampled_pixels(
                &device,
                &queue,
                &ordinary.create_view(&Default::default()),
                width,
                height
            ),
            expected
        );

        queue.submit([]);
        first.retain_until_submitted_work_done(&queue, || {});
        drop(first);
        drop(source);
        assert_eq!(
            allocation.leases.load(Ordering::Acquire),
            1,
            "GPU completion pins READ ownership"
        );
        device.poll(wgpu::PollType::wait_indefinitely()).unwrap();
        assert_eq!(
            allocation.leases.load(Ordering::Acquire),
            0,
            "cache must not pin READ ownership"
        );

        allocation.write(65536, &vec![255; bytes.len()]);
        let source = allocation.frame(width, height, 2);
        let second = cache.import(&device, &source).unwrap().unwrap();
        assert!(
            Arc::ptr_eq(&first_resource, &second.slot),
            "same mapping/plane reuses native texture"
        );
        assert_eq!(
            sampled_pixels(&device, &queue, second.view(), width, height),
            vec![255; width as usize * height as usize * 4]
        );

        // Changing geometry creates a new view of the same plane. Cache eviction
        // must not invalidate a retained frame or keep its producer lease forever.
        let smaller_source = allocation.frame(16, 8, 3);
        let smaller = cache.import(&device, &smaller_source).unwrap().unwrap();
        assert!(!Arc::ptr_eq(&smaller.slot, &second.slot));
        for _ in 0..super::DIRECT_CACHE_CAPACITY + 2 {
            let other = TestMapping::new(131072);
            let other_source = other.frame(width, height, 1);
            let imported = cache.import(&device, &other_source).unwrap().unwrap();
            drop(imported);
            drop(other_source);
            assert_eq!(other.leases.load(Ordering::Acquire), 0);
            assert!(cache.entries.len() <= super::DIRECT_CACHE_CAPACITY);
        }
        assert_eq!(
            sampled_pixels(&device, &queue, second.view(), width, height),
            vec![255; width as usize * height as usize * 4]
        );

        let native_buffer = second.slot._buffer.clone();
        let dropped = allocation.dropped.clone();
        cache.clear();
        drop(first_resource);
        drop(second);
        drop(source);
        drop(smaller);
        drop(smaller_source);
        drop(allocation);
        drop(ordinary);
        queue.submit([]);
        device.poll(wgpu::PollType::wait_indefinitely()).unwrap();
        drop(queue);
        drop(device);
        assert!(
            !dropped.load(Ordering::Acquire),
            "native MTLBuffer alias must keep mmap alive"
        );
        drop(native_buffer);
        assert!(
            dropped.load(Ordering::Acquire),
            "native deallocator releases mmap exactly after final native owner"
        );
    }
}
