#![cfg(target_os = "linux")]

use std::ffi::{c_char, c_void, CStr, CString};
use std::os::fd::{AsFd, FromRawFd, OwnedFd};
use std::sync::{
    atomic::{AtomicUsize, Ordering},
    Arc,
};

use dreamgpu::presentation::linux_gpu_texture::{self, DmaBufLayout};

fn block_on<T>(future: impl std::future::Future<Output = T>) -> T {
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
            std::task::Poll::Ready(value) => return value,
            std::task::Poll::Pending => std::thread::park(),
        }
    }
}

#[repr(C)]
struct NativeImage {
    width: u32,
    height: u32,
    stride: u32,
    fourcc: u32,
    modifier: u64,
    fd: i32,
    ready: i32,
    owner: *mut c_void,
}

struct ProducerLease {
    _allocation: OwnedFd,
    released: Arc<AtomicUsize>,
}
impl Drop for ProducerLease {
    fn drop(&mut self) {
        self.released.fetch_add(1, Ordering::SeqCst);
    }
}

struct CanvasImage {
    fd: OwnedFd,
    layout: DmaBufLayout,
    uuid: [u8; 16],
    released: Arc<AtomicUsize>,
}
// SAFETY: this second consumer starts only after the initial native fence and
// import have completed. The C producer is never written again and remains alive
// until the canvas submission completes. The owner pins its DMA-BUF FD.
unsafe impl dreamgpu::GpuImage for CanvasImage {
    fn handle(&self) -> dreamgpu::GpuImageHandle<'_> {
        dreamgpu::GpuImageHandle::DmaBuf {
            fd: self.fd.as_fd(),
            fourcc: self.layout.fourcc,
            modifier: self.layout.modifier,
            stride: self.layout.stride,
            offset: self.layout.offset,
            device_uuid: self.uuid,
            ready_fence: None,
        }
    }
}
impl Drop for CanvasImage {
    fn drop(&mut self) {
        self.released.fetch_add(1, Ordering::SeqCst);
    }
}

#[test]
#[ignore = "requires a real DRM GPU, EGL/GL/GBM development libraries and C compiler"]
fn egl_dma_buf_and_native_fence_reach_vulkan_without_cpu_upload() {
    let instance = wgpu::Instance::new(wgpu::InstanceDescriptor {
        backends: wgpu::Backends::VULKAN,
        ..wgpu::InstanceDescriptor::new_without_display_handle()
    });
    let adapter = block_on(instance.request_adapter(&wgpu::RequestAdapterOptions {
        power_preference: wgpu::PowerPreference::HighPerformance,
        ..Default::default()
    }))
    .unwrap();
    let (device, queue) = linux_gpu_texture::create_device(&adapter, &Default::default())
        .unwrap()
        .expect("hardware must support DMA-BUF plus foreign queue/fence import");
    let render_node = CString::new(linux_gpu_texture::render_node(&device).unwrap()).unwrap();

    let folder = std::env::temp_dir().join(format!("dg-egl-interop-{}", std::process::id()));
    std::fs::create_dir_all(&folder).unwrap();
    let library_path = folder.join("producer.so");
    let source =
        std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/egl-dmabuf-producer.c");
    let compile = std::process::Command::new("cc")
        .args(["-shared", "-fPIC", "-O2", "-Wall", "-Wextra", "-Werror"])
        .arg(source)
        .arg("-o")
        .arg(&library_path)
        .args(["-lEGL", "-lGL", "-lgbm"])
        .output()
        .unwrap();
    assert!(
        compile.status.success(),
        "{}",
        String::from_utf8_lossy(&compile.stderr)
    );
    let library = unsafe { libloading::Library::new(library_path) }.unwrap();
    let create =
        unsafe {
            library.get::<unsafe extern "C" fn(*mut NativeImage, *const c_char,
        *mut c_char, u32) -> i32>(b"dreamgpu_test_image_create\0")
        }
        .unwrap();
    let destroy = unsafe {
        library.get::<unsafe extern "C" fn(*mut NativeImage)>(b"dreamgpu_test_image_destroy\0")
    }
    .unwrap();
    let mut image = NativeImage {
        width: 0,
        height: 0,
        stride: 0,
        fourcc: 0,
        modifier: 0,
        fd: -1,
        ready: -1,
        owner: std::ptr::null_mut(),
    };
    let mut diagnostic = [0 as c_char; 512];
    assert_eq!(
        unsafe {
            create(
                &mut image,
                render_node.as_ptr(),
                diagnostic.as_mut_ptr(),
                512,
            )
        },
        1,
        "{}",
        unsafe { CStr::from_ptr(diagnostic.as_ptr()) }.to_string_lossy()
    );
    struct Cleanup<'a> {
        image: &'a mut NativeImage,
        destroy: unsafe extern "C" fn(*mut NativeImage),
    }
    impl Drop for Cleanup<'_> {
        fn drop(&mut self) {
            unsafe { (self.destroy)(self.image) };
        }
    }
    let cleanup = Cleanup {
        image: &mut image,
        destroy: *destroy,
    };
    let allocation = unsafe { OwnedFd::from_raw_fd(cleanup.image.fd) };
    cleanup.image.fd = -1;
    let canvas_allocation = allocation.try_clone().unwrap();
    let fence = unsafe { OwnedFd::from_raw_fd(cleanup.image.ready) };
    cleanup.image.ready = -1;
    let released = Arc::new(AtomicUsize::new(0));
    let lease = Arc::new(ProducerLease {
        _allocation: allocation.try_clone().unwrap(),
        released: released.clone(),
    });
    let layout = DmaBufLayout {
        width: cleanup.image.width,
        height: cleanup.image.height,
        fourcc: cleanup.image.fourcc,
        modifier: cleanup.image.modifier,
        stride: cleanup.image.stride,
        offset: 0,
    };
    // The C helper made a GL-rendered image on this renderer's exact DRM node,
    // with a real native fence. Its objects and allocation stay alive until the
    // final queue completion. There is no other producer or queue submitter.
    let texture = unsafe {
        linux_gpu_texture::copy_frame(&device, &queue, allocation, Some(fence), layout, lease)
    }
    .unwrap();
    // Readback is a test oracle only; the production path ends at the GPU texture.
    let readback = device.create_buffer(&wgpu::BufferDescriptor {
        label: Some("EGL/Vulkan pixel oracle"),
        size: 256 * 16,
        usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
        mapped_at_creation: false,
    });
    let mut encoder = device.create_command_encoder(&Default::default());
    encoder.copy_texture_to_buffer(
        texture.as_image_copy(),
        wgpu::TexelCopyBufferInfo {
            buffer: &readback,
            layout: wgpu::TexelCopyBufferLayout {
                offset: 0,
                bytes_per_row: Some(256),
                rows_per_image: Some(16),
            },
        },
        texture.size(),
    );
    queue.submit([encoder.finish()]);
    let (tx, rx) = std::sync::mpsc::channel();
    readback
        .slice(..)
        .map_async(wgpu::MapMode::Read, move |result| {
            tx.send(result).unwrap();
        });
    device
        .poll(wgpu::PollType::Wait {
            submission_index: None,
            timeout: Some(std::time::Duration::from_secs(10)),
        })
        .unwrap();
    rx.recv_timeout(std::time::Duration::from_secs(1))
        .unwrap()
        .unwrap();
    let bytes = readback.slice(..).get_mapped_range().unwrap();
    for y in 0..16 {
        for x in 0..32 {
            let expected: &[u8] = if y < 8 {
                &[0, 0, 255, 255]
            } else {
                &[255, 0, 0, 255]
            };
            assert_eq!(
                &bytes[y * 256 + x * 4..y * 256 + x * 4 + 4],
                expected,
                "GL -> DMA-BUF -> Vulkan pixel ({x}, {y})"
            );
        }
    }
    drop(bytes);
    readback.unmap();
    assert_eq!(
        released.load(Ordering::SeqCst),
        1,
        "producer slot released after completion"
    );
    // Reuse the completed immutable drawable in a clipped window. Ordinary CPU
    // desktop pixels around it and subsequent occlusion must survive exactly.
    use dreamgpu::{
        desktop::{DesktopBatch, DesktopOp, DesktopRect},
        FrameLease, GpuFrameLease, PixelFormat,
    };
    let canvas_released = Arc::new(AtomicUsize::new(0));
    let image = GpuFrameLease {
        image: Arc::new(CanvasImage {
            fd: canvas_allocation,
            layout,
            uuid: linux_gpu_texture::device_uuid(&device).unwrap(),
            released: canvas_released.clone(),
        }),
        width: 32,
        height: 16,
        format: PixelFormat::Bgra8,
        epoch: 1,
        generation: 1,
    };
    let bg = [41, 79, 113, 255];
    let seed = FrameLease {
        pixels: Arc::new(bg.repeat(48 * 32)),
        width: 48,
        height: 32,
        stride: 48 * 4,
        format: PixelFormat::Bgra8,
        generation: 1,
        damage: None,
    };
    let (reply, received) = std::sync::mpsc::sync_channel(1);
    let mut canvas = dreamgpu::presentation::desktop::DesktopCanvas::default();
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
                            x: 4,
                            y: 2,
                            width: 24,
                            height: 12,
                        },
                        x: 10,
                        y: 10,
                    },
                    DesktopOp::Fill {
                        rect: DesktopRect {
                            x: 15,
                            y: 8,
                            width: 3,
                            height: 20,
                        },
                        bgra: [91, 52, 23, 255],
                    },
                    DesktopOp::Readback {
                        rect: DesktopRect {
                            x: 0,
                            y: 0,
                            width: 48,
                            height: 32,
                        },
                        reply: reply.into(),
                    },
                ],
            },
        )
        .unwrap();
    device.poll(wgpu::PollType::wait_indefinitely()).unwrap();
    let pixels = received.recv().unwrap().unwrap();
    for y in 0..32usize {
        for x in 0..48usize {
            let expected = if (15..18).contains(&x) && (8..28).contains(&y) {
                [91, 52, 23, 255]
            } else if (10..34).contains(&x) && (10..22).contains(&y) {
                if y < 16 {
                    [0, 0, 255, 255]
                } else {
                    [255, 0, 0, 255]
                }
            } else {
                bg
            };
            assert_eq!(
                &pixels.bytes()[(y * 48 + x) * 4..(y * 48 + x + 1) * 4],
                &expected,
                "window pixel {x},{y}"
            );
        }
    }
    assert_eq!(canvas_released.load(Ordering::SeqCst), 1);
    assert!(!canvas.has_pending_completions());
    drop(cleanup);
}
