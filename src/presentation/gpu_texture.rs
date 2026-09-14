// SPDX-License-Identifier: GPL-2.0-or-later
//! Metal import of completed GPU-produced IOSurfaces. No CPU upload or mapping.

use std::{ffi::c_void, ptr::NonNull, sync::Arc};

use crate::{GpuFrameLease, GpuImageHandle, PixelFormat};
use objc2::{Encoding, RefEncode, msg_send, rc::Retained, runtime::ProtocolObject};
use objc2_metal::{
    MTLPixelFormat, MTLStorageMode, MTLTexture, MTLTextureDescriptor, MTLTextureType,
    MTLTextureUsage,
};

#[link(name = "IOSurface", kind = "framework")]
unsafe extern "C" {
    fn IOSurfaceLookupFromMachPort(port: u32) -> *mut c_void;
    fn IOSurfaceGetWidth(surface: *mut c_void) -> usize;
    fn IOSurfaceGetHeight(surface: *mut c_void) -> usize;
    fn IOSurfaceGetBytesPerRow(surface: *mut c_void) -> usize;
    fn IOSurfaceGetBytesPerElement(surface: *mut c_void) -> usize;
    fn IOSurfaceGetPixelFormat(surface: *mut c_void) -> u32;
    fn IOSurfaceGetPlaneCount(surface: *mut c_void) -> usize;
    fn IOSurfaceGetAllocSize(surface: *mut c_void) -> usize;
}

#[link(name = "CoreFoundation", kind = "framework")]
unsafe extern "C" {
    fn CFRelease(value: *const c_void);
}

struct Surface(NonNull<c_void>);

// Preserve the IOSurfaceRef Objective-C method encoding when calling Metal.
#[repr(C)]
struct IOSurfaceOpaque {
    _private: [u8; 0],
}
unsafe impl RefEncode for IOSurfaceOpaque {
    const ENCODING_REF: Encoding = Encoding::Pointer(&Encoding::Struct("__IOSurface", &[]));
}

// SAFETY: this owns only a retained, immutable IOSurface. Its producer is protected
// separately by GpuFrameLease; IOSurface retain/release is thread-safe.
unsafe impl Send for Surface {}
unsafe impl Sync for Surface {}

impl Drop for Surface {
    fn drop(&mut self) {
        // SAFETY: IOSurfaceLookupFromMachPort returned this retained CF object.
        unsafe {
            CFRelease(self.0.as_ptr());
        }
    }
}

/// An imported texture keeps its producer lease through resource and queue lifetimes.
#[derive(Clone)]
pub struct GpuTextureFrame {
    texture: wgpu::Texture,
    view: wgpu::TextureView,
    _lease: GpuFrameLease,
}

impl GpuTextureFrame {
    pub fn texture(&self) -> &wgpu::Texture {
        &self.texture
    }
    pub fn view(&self) -> &wgpu::TextureView {
        &self.view
    }

    /// Register after every submission using the image; service device completion polls.
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

/// Import an immutable IOSurface after the producer's GL fence has completed.
/// The image owner retains its Mach send right until the final consumer release.
pub fn import_iosurface(
    device: &wgpu::Device,
    frame: &GpuFrameLease,
) -> Result<GpuTextureFrame, String> {
    frame.validate().map_err(|e| e.to_string())?;
    let GpuImageHandle::IoSurface { mach_port } = frame.image.handle() else {
        return Err("Metal GPU import requires an IOSurface handle".into());
    };
    if frame.format != PixelFormat::Bgra8 {
        return Err("IOSurface GPU import currently requires BGRA8".into());
    }
    let limits = device.limits();
    if frame.width > limits.max_texture_dimension_2d
        || frame.height > limits.max_texture_dimension_2d
    {
        return Err("IOSurface dimensions exceed the Metal device limit".into());
    }
    // SAFETY: a nonzero Mach port is looked up by the kernel; a stale/wrong right
    // returns null. The GpuImage owner retains the send right during this call.
    let surface = Arc::new(Surface(
        NonNull::new(unsafe { IOSurfaceLookupFromMachPort(mach_port) })
            .ok_or("Cannot resolve IOSurface Mach send right")?,
    ));
    let pointer = surface.0.as_ptr();
    // SAFETY: surface retains the live IOSurface returned by lookup.
    unsafe {
        let stride = IOSurfaceGetBytesPerRow(pointer);
        if IOSurfaceGetWidth(pointer) != frame.width as usize
            || IOSurfaceGetHeight(pointer) != frame.height as usize
            || IOSurfaceGetPlaneCount(pointer) != 0
            || IOSurfaceGetBytesPerElement(pointer) != 4
            || IOSurfaceGetPixelFormat(pointer) != u32::from_be_bytes(*b"BGRA")
            || stride < frame.width as usize * 4
            || stride
                .checked_mul(frame.height as usize)
                .is_none_or(|size| size > IOSurfaceGetAllocSize(pointer))
        {
            return Err("IOSurface geometry or BGRA layout does not match the GPU frame".into());
        }
    }
    let raw_device = {
        // SAFETY: retain the Metal device without changing HAL state.
        let hal = unsafe { device.as_hal::<wgpu::hal::api::Metal>() }
            .ok_or("IOSurface import requires a Metal wgpu device")?;
        hal.raw_device().clone()
    };
    let native_desc = MTLTextureDescriptor::new();
    native_desc.setTextureType(MTLTextureType::Type2D);
    native_desc.setPixelFormat(MTLPixelFormat::BGRA8Unorm);
    native_desc.setStorageMode(MTLStorageMode::Shared);
    native_desc.setUsage(MTLTextureUsage::ShaderRead);
    // SAFETY: dimensions and layout were checked against the backing and limits.
    unsafe {
        native_desc.setWidth(frame.width as usize);
        native_desc.setHeight(frame.height as usize);
        native_desc.setDepth(1);
        native_desc.setMipmapLevelCount(1);
        native_desc.setArrayLength(1);
        native_desc.setSampleCount(1);
    }
    // SAFETY: the selector takes MTLTextureDescriptor*, IOSurfaceRef and NSUInteger.
    // Both objects are retained and plane 0 names the validated packed BGRA image.
    let native_texture: Option<Retained<ProtocolObject<dyn MTLTexture>>> = unsafe {
        msg_send![&*raw_device, newTextureWithDescriptor: &*native_desc, iosurface: pointer.cast::<IOSurfaceOpaque>(), plane: 0usize]
    };
    let native_texture = native_texture.ok_or("Metal cannot create an IOSurface texture")?;
    let desc = wgpu::TextureDescriptor {
        label: Some("DreamGPU GPU IOSurface"),
        size: wgpu::Extent3d {
            width: frame.width,
            height: frame.height,
            depth_or_array_layers: 1,
        },
        mip_level_count: 1,
        sample_count: 1,
        dimension: wgpu::TextureDimension::D2,
        format: wgpu::TextureFormat::Bgra8Unorm,
        usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_SRC,
        view_formats: &[],
    };
    let resource_lease = frame.clone();
    // SAFETY: the texture matches desc and its backing pixels were completed by
    // the producer. Both native resource lifetime and submissions retain owners.
    let texture = unsafe {
        let hal_texture = wgpu::hal::metal::Device::texture_from_raw(
            native_texture,
            desc.format,
            MTLTextureType::Type2D,
            1,
            1,
            desc.size.into(),
            Some(Box::new(move || {
                drop(resource_lease);
                drop(surface);
            })),
        );
        device.create_texture_from_hal::<wgpu::hal::api::Metal>(
            hal_texture,
            &desc,
            wgpu::TextureUses::RESOURCE,
        )
    };
    let view = texture.create_view(&Default::default());
    Ok(GpuTextureFrame {
        texture,
        view,
        _lease: frame.clone(),
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::GpuImage;
    use std::sync::atomic::{AtomicBool, Ordering};

    unsafe extern "C" {
        static mach_task_self_: u32;
        fn mach_port_deallocate(task: u32, name: u32) -> i32;
    }

    #[link(name = "CoreFoundation", kind = "framework")]
    unsafe extern "C" {
        fn CFNumberCreate(
            allocator: *const c_void,
            kind: isize,
            value: *const c_void,
        ) -> *mut c_void;
        fn CFDictionaryCreate(
            allocator: *const c_void,
            keys: *const *const c_void,
            values: *const *const c_void,
            count: isize,
            key_callbacks: *const c_void,
            value_callbacks: *const c_void,
        ) -> *mut c_void;
        static kCFTypeDictionaryKeyCallBacks: u8;
        static kCFTypeDictionaryValueCallBacks: u8;
    }
    #[link(name = "IOSurface", kind = "framework")]
    unsafe extern "C" {
        fn IOSurfaceCreate(properties: *const c_void) -> *mut c_void;
        fn IOSurfaceCreateMachPort(surface: *mut c_void) -> u32;
        static kIOSurfaceWidth: *const c_void;
        static kIOSurfaceHeight: *const c_void;
        static kIOSurfaceBytesPerElement: *const c_void;
        static kIOSurfacePixelFormat: *const c_void;
    }
    #[link(name = "OpenGL", kind = "framework")]
    unsafe extern "C" {
        fn CGLChoosePixelFormat(
            attributes: *const i32,
            pixel_format: *mut *mut c_void,
            count: *mut i32,
        ) -> i32;
        fn CGLDestroyPixelFormat(pixel_format: *mut c_void) -> i32;
        fn CGLCreateContext(
            pixel_format: *mut c_void,
            share: *mut c_void,
            context: *mut *mut c_void,
        ) -> i32;
        fn CGLDestroyContext(context: *mut c_void) -> i32;
        fn CGLSetCurrentContext(context: *mut c_void) -> i32;
        fn CGLGetCurrentContext() -> *mut c_void;
        fn CGLTexImageIOSurface2D(
            context: *mut c_void,
            target: u32,
            internal_format: u32,
            width: i32,
            height: i32,
            format: u32,
            kind: u32,
            surface: *mut c_void,
            plane: u32,
        ) -> i32;
        fn glGenTextures(count: i32, textures: *mut u32);
        fn glDeleteTextures(count: i32, textures: *const u32);
        fn glBindTexture(target: u32, texture: u32);
        fn glGenFramebuffersEXT(count: i32, buffers: *mut u32);
        fn glDeleteFramebuffersEXT(count: i32, buffers: *const u32);
        fn glBindFramebufferEXT(target: u32, buffer: u32);
        fn glFramebufferTexture2DEXT(
            target: u32,
            attachment: u32,
            texture_target: u32,
            texture: u32,
            level: i32,
        );
        fn glCheckFramebufferStatusEXT(target: u32) -> u32;
        fn glClearColor(red: f32, green: f32, blue: f32, alpha: f32);
        fn glClear(mask: u32);
        fn glEnable(capability: u32);
        fn glScissor(x: i32, y: i32, width: i32, height: i32);
        fn glFlush();
        fn glFenceSync(condition: u32, flags: u32) -> *mut c_void;
        fn glClientWaitSync(sync: *mut c_void, flags: u32, timeout: u64) -> u32;
        fn glDeleteSync(sync: *mut c_void);
        fn glGetError() -> u32;
    }

    struct TestImage {
        port: u32,
        released: Arc<AtomicBool>,
    }
    // SAFETY: the test completes CGL work before publication and never writes again.
    // This owner retains its IOSurface send right until every image lease is gone.
    unsafe impl GpuImage for TestImage {
        fn handle(&self) -> GpuImageHandle<'_> {
            GpuImageHandle::IoSurface {
                mach_port: self.port,
            }
        }
    }
    impl Drop for TestImage {
        fn drop(&mut self) {
            unsafe {
                mach_port_deallocate(mach_task_self_, self.port);
            }
            self.released.store(true, Ordering::Release);
        }
    }

    struct Context {
        raw: *mut c_void,
        previous: *mut c_void,
        texture: u32,
        framebuffer: u32,
    }
    impl Drop for Context {
        fn drop(&mut self) {
            unsafe {
                CGLSetCurrentContext(self.raw);
                glDeleteFramebuffersEXT(1, &self.framebuffer);
                glDeleteTextures(1, &self.texture);
                CGLSetCurrentContext(self.previous);
                CGLDestroyContext(self.raw);
            }
        }
    }

    fn block_on<F: std::future::Future>(future: F) -> F::Output {
        struct Wake(std::thread::Thread);
        impl std::task::Wake for Wake {
            fn wake(self: Arc<Self>) {
                self.0.unpark();
            }
        }
        let waker = std::task::Waker::from(Arc::new(Wake(std::thread::current())));
        let mut context = std::task::Context::from_waker(&waker);
        let mut future = std::pin::pin!(future);
        loop {
            match future.as_mut().poll(&mut context) {
                std::task::Poll::Ready(result) => return result,
                std::task::Poll::Pending => std::thread::park(),
            }
        }
    }

    fn render_surface(width: u32, height: u32, released: Arc<AtomicBool>) -> GpuFrameLease {
        unsafe {
            let keys = [
                kIOSurfaceWidth,
                kIOSurfaceHeight,
                kIOSurfaceBytesPerElement,
                kIOSurfacePixelFormat,
            ];
            let values = [
                i64::from(width),
                i64::from(height),
                4,
                i64::from(u32::from_be_bytes(*b"BGRA")),
            ];
            let numbers: Vec<_> = values
                .iter()
                .map(|value| CFNumberCreate(std::ptr::null(), 4, (value as *const i64).cast()))
                .collect();
            assert!(numbers.iter().all(|p| !p.is_null()));
            let pointers: Vec<_> = numbers.iter().map(|p| *p as *const c_void).collect();
            let properties = CFDictionaryCreate(
                std::ptr::null(),
                keys.as_ptr(),
                pointers.as_ptr(),
                4,
                (&raw const kCFTypeDictionaryKeyCallBacks).cast(),
                (&raw const kCFTypeDictionaryValueCallBacks).cast(),
            );
            assert!(!properties.is_null());
            let surface =
                Surface(NonNull::new(IOSurfaceCreate(properties)).expect("IOSurfaceCreate"));
            CFRelease(properties);
            for number in numbers {
                CFRelease(number);
            }

            let mut format = std::ptr::null_mut();
            let mut count = 0;
            // kCGLPFAAccelerated, kCGLPFAOpenGLProfile, legacy compatibility profile.
            assert_eq!(
                CGLChoosePixelFormat([73, 99, 0x1000, 0].as_ptr(), &mut format, &mut count),
                0
            );
            let mut raw = std::ptr::null_mut();
            assert_eq!(CGLCreateContext(format, std::ptr::null_mut(), &mut raw), 0);
            CGLDestroyPixelFormat(format);
            let mut context = Context {
                raw,
                previous: CGLGetCurrentContext(),
                texture: 0,
                framebuffer: 0,
            };
            assert_eq!(CGLSetCurrentContext(raw), 0);
            glGenTextures(1, &mut context.texture);
            glBindTexture(0x84F5, context.texture); // GL_TEXTURE_RECTANGLE
            assert_eq!(
                CGLTexImageIOSurface2D(
                    raw,
                    0x84F5,
                    0x8058,
                    width as i32,
                    height as i32,
                    0x80E1,
                    0x8367,
                    surface.0.as_ptr(),
                    0
                ),
                0
            );
            glGenFramebuffersEXT(1, &mut context.framebuffer);
            glBindFramebufferEXT(0x8D40, context.framebuffer);
            glFramebufferTexture2DEXT(0x8D40, 0x8CE0, 0x84F5, context.texture, 0);
            assert_eq!(glCheckFramebufferStatusEXT(0x8D40), 0x8CD5);
            glClearColor(1.0, 0.0, 0.0, 1.0);
            glClear(0x4000);
            // Write the first physical rows blue. Canonical image row 0 is top-left;
            // a game exporter performs its GL-coordinate flip in the preceding blit.
            glEnable(0x0C11);
            glScissor(0, 0, width as i32, height as i32 / 2);
            glClearColor(0.0, 0.0, 1.0, 1.0);
            glClear(0x4000);
            assert_eq!(glGetError(), 0);
            let fence = glFenceSync(0x9117, 0);
            assert!(!fence.is_null());
            glFlush();
            let completion = glClientWaitSync(fence, 0, 5_000_000_000);
            glDeleteSync(fence);
            assert!(
                matches!(completion, 0x911A | 0x911C),
                "GL completion: {completion:#x}"
            );
            let port = IOSurfaceCreateMachPort(surface.0.as_ptr());
            assert_ne!(port, 0);
            // No texture upload, IOSurfaceLock or CPU mapping was used to produce pixels.
            GpuFrameLease {
                image: Arc::new(TestImage { port, released }),
                width,
                height,
                format: PixelFormat::Bgra8,
                epoch: 1,
                generation: 1,
            }
        }
    }

    #[test]
    #[ignore = "requires macOS CGL and Metal; run outside performance captures"]
    fn cgl_iosurface_metal_pixels_and_consumer_lifetime() {
        let instance = wgpu::Instance::new(wgpu::InstanceDescriptor {
            backends: wgpu::Backends::METAL,
            ..wgpu::InstanceDescriptor::new_without_display_handle()
        });
        let adapter = block_on(instance.request_adapter(&Default::default())).unwrap();
        let (device, queue) = block_on(adapter.request_device(&Default::default())).unwrap();
        let released = Arc::new(AtomicBool::new(false));
        let source = render_surface(16, 8, released.clone());
        let imported = import_iosurface(&device, &source).unwrap();
        let mut wrong = source.clone();
        wrong.width += 1;
        assert!(import_iosurface(&device, &wrong).is_err());
        drop(wrong);
        let readback = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("IOSurface test readback oracle"),
            size: 256 * 8,
            usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });
        let mut encoder = device.create_command_encoder(&Default::default());
        let desktop = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("IOSurface compositor copy oracle"),
            size: imported.texture().size(),
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Bgra8UnormSrgb,
            usage: wgpu::TextureUsages::COPY_DST | wgpu::TextureUsages::COPY_SRC,
            view_formats: &[],
        });
        encoder.copy_texture_to_texture(
            imported.texture().as_image_copy(),
            desktop.as_image_copy(),
            desktop.size(),
        );
        encoder.copy_texture_to_buffer(
            desktop.as_image_copy(),
            wgpu::TexelCopyBufferInfo {
                buffer: &readback,
                layout: wgpu::TexelCopyBufferLayout {
                    offset: 0,
                    bytes_per_row: Some(256),
                    rows_per_image: Some(8),
                },
            },
            imported.texture().size(),
        );
        queue.submit([encoder.finish()]);
        imported.retain_until_submitted_work_done(&queue, || {});
        // The HAL-owned lease also protects separately cloned texture/view owners.
        let retained_view = imported.view().clone();
        drop(source);
        drop(imported);
        assert!(!released.load(Ordering::Acquire));
        let (sender, receiver) = std::sync::mpsc::sync_channel(1);
        readback
            .slice(..)
            .map_async(wgpu::MapMode::Read, move |result| {
                sender.send(result).unwrap()
            });
        device.poll(wgpu::PollType::wait_indefinitely()).unwrap();
        receiver.recv().unwrap().unwrap();
        let mapped = readback.slice(..).get_mapped_range().unwrap();
        for y in 0..8usize {
            for x in 0..16usize {
                let expected = if y < 4 {
                    [255, 0, 0, 255]
                } else {
                    [0, 0, 255, 255]
                };
                assert_eq!(&mapped[y * 256 + x * 4..y * 256 + x * 4 + 4], &expected);
            }
        }
        drop(mapped);
        readback.unmap();
        assert!(
            !released.load(Ordering::Acquire),
            "a retained view must pin producer ownership"
        );
        drop(retained_view);
        device.poll(wgpu::PollType::wait_indefinitely()).unwrap();
        assert!(
            released.load(Ordering::Acquire),
            "producer slot should be released after all consumers"
        );
    }
    #[test]
    #[ignore = "requires macOS CGL and Metal"]
    fn cgl_drawable_composites_in_a_window_with_ordered_cpu_occlusion() {
        use crate::{
            FrameLease,
            desktop::{DesktopBatch, DesktopOp, DesktopRect},
        };
        let instance = wgpu::Instance::new(wgpu::InstanceDescriptor {
            backends: wgpu::Backends::METAL,
            ..wgpu::InstanceDescriptor::new_without_display_handle()
        });
        let adapter = block_on(instance.request_adapter(&Default::default())).unwrap();
        let (device, queue) = block_on(adapter.request_device(&Default::default())).unwrap();
        let released = Arc::new(AtomicBool::new(false));
        let image = render_surface(16, 8, released.clone());
        let bg = [41, 79, 113, 255];
        let seed = FrameLease {
            pixels: Arc::new(bg.repeat(32 * 16)),
            width: 32,
            height: 16,
            stride: 128,
            format: PixelFormat::Bgra8,
            generation: 1,
            damage: None,
        };
        let (reply, received) = std::sync::mpsc::sync_channel(1);
        let mut canvas = crate::presentation::desktop::DesktopCanvas::default();
        canvas
            .apply(
                &device,
                &queue,
                DesktopBatch {
                    epoch: 1,
                    sequence: 1,
                    operations: vec![
                        DesktopOp::Seed(seed),
                        DesktopOp::Blit {
                            image,
                            source: DesktopRect {
                                x: 2,
                                y: 1,
                                width: 12,
                                height: 6,
                            },
                            x: 7,
                            y: 5,
                        },
                        DesktopOp::Fill {
                            rect: DesktopRect {
                                x: 10,
                                y: 4,
                                width: 2,
                                height: 8,
                            },
                            bgra: [91, 52, 23, 255],
                        },
                        DesktopOp::Readback {
                            rect: DesktopRect {
                                x: 0,
                                y: 0,
                                width: 32,
                                height: 16,
                            },
                            reply: reply.into(),
                        },
                    ],
                },
            )
            .unwrap();
        device.poll(wgpu::PollType::wait_indefinitely()).unwrap();
        let pixels = received.recv().unwrap().unwrap();
        for y in 0..16usize {
            for x in 0..32usize {
                let expected = if (10..12).contains(&x) && (4..12).contains(&y) {
                    [91, 52, 23, 255]
                } else if (7..19).contains(&x) && (5..11).contains(&y) {
                    if y < 8 {
                        [255, 0, 0, 255]
                    } else {
                        [0, 0, 255, 255]
                    }
                } else {
                    bg
                };
                assert_eq!(
                    &pixels.bytes()[(y * 32 + x) * 4..(y * 32 + x + 1) * 4],
                    &expected,
                    "pixel {x},{y}"
                );
            }
        }
        assert!(released.load(Ordering::Acquire));
        assert!(!canvas.has_pending_completions());
    }
}
