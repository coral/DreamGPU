#![recursion_limit = "256"]
use dreamgpu::presentation::desktop::DesktopCanvas;
use dreamgpu::{
    desktop::{DesktopBatch, DesktopOp, DesktopRect},
    FrameLease, PixelFormat,
};
use std::sync::{mpsc, Arc};

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
            std::task::Poll::Ready(x) => return x,
            std::task::Poll::Pending => std::thread::park(),
        }
    }
}
fn pixels(w: u32, h: u32, color: [u8; 4]) -> FrameLease {
    FrameLease {
        width: w,
        height: h,
        stride: w * 4,
        pixels: Arc::new(color.repeat((w * h) as usize)),
        format: PixelFormat::Bgra8,
        generation: 1,
        damage: None,
    }
}
fn rect(x: u32, y: u32, width: u32, height: u32) -> DesktopRect {
    DesktopRect {
        x,
        y,
        width,
        height,
    }
}
fn batch(sequence: u64, operations: Vec<DesktopOp>) -> DesktopBatch {
    DesktopBatch {
        epoch: 1,
        sequence,
        operations,
    }
}
fn paint(bytes: &mut [u8], rect: DesktopRect, color: [u8; 4]) {
    for y in rect.y..rect.y + rect.height {
        for x in rect.x..rect.x + rect.width {
            let n = (y as usize * 16 + x as usize) * 4;
            bytes[n..n + 4].copy_from_slice(&color);
        }
    }
}
fn copy(bytes: &mut [u8], source: DesktopRect, x: u32, y: u32) {
    let old = bytes.to_vec();
    for row in 0..source.height {
        for col in 0..source.width {
            let a = (((source.y + row) * 16 + source.x + col) * 4) as usize;
            let b = (((y + row) * 16 + x + col) * 4) as usize;
            bytes[b..b + 4].copy_from_slice(&old[a..a + 4]);
        }
    }
}

#[test]
#[ignore = "requires a native GPU; deliberately runs without a window or presentation loop"]
fn ordered_mixed_desktop_preserves_pixels_and_services_offscreen_reads() {
    let instance = wgpu::Instance::new(wgpu::InstanceDescriptor::new_without_display_handle());
    let adapter = block_on(instance.request_adapter(&Default::default())).unwrap();
    let (device, queue) = block_on(adapter.request_device(&Default::default())).unwrap();
    let mut canvas = DesktopCanvas::default();
    let base = [9, 17, 29, 255];
    let green = [35, 201, 47, 255];
    let red = [25, 38, 211, 255];
    let (a, ra) = mpsc::sync_channel(1);
    let (b, rb) = mpsc::sync_channel(1);
    canvas
        .apply(
            &device,
            &queue,
            batch(
                1,
                vec![
                    DesktopOp::Seed(pixels(16, 8, base)),
                    DesktopOp::Fill {
                        rect: rect(1, 1, 10, 4),
                        bgra: green,
                    },
                    DesktopOp::Patch {
                        x: 3,
                        y: 2,
                        pixels: pixels(4, 3, red),
                    },
                    DesktopOp::Copy {
                        source: rect(1, 1, 10, 4),
                        x: 3,
                        y: 2,
                    },
                    DesktopOp::Readback {
                        rect: rect(0, 0, 16, 8),
                        reply: a.into(),
                    },
                    DesktopOp::Fill {
                        rect: rect(0, 0, 16, 8),
                        bgra: base,
                    },
                    DesktopOp::Readback {
                        rect: rect(2, 1, 7, 5),
                        reply: b.into(),
                    },
                ],
            ),
        )
        .unwrap();
    device.poll(wgpu::PollType::wait_indefinitely()).unwrap();
    let mut expected = base.repeat(16 * 8);
    paint(&mut expected, rect(1, 1, 10, 4), green);
    paint(&mut expected, rect(3, 2, 4, 3), red);
    copy(&mut expected, rect(1, 1, 10, 4), 3, 2);
    assert_eq!(ra.recv().unwrap().unwrap().bytes(), expected);
    assert_eq!(rb.recv().unwrap().unwrap().bytes(), base.repeat(7 * 5));
    assert!(!canvas.has_pending_completions());
    assert!(canvas.is_mixed());

    // A bad trailing operation cannot leave its valid prefix on the canvas.
    assert!(canvas
        .apply(
            &device,
            &queue,
            batch(
                2,
                vec![
                    DesktopOp::Fill {
                        rect: rect(0, 0, 16, 8),
                        bgra: red
                    },
                    DesktopOp::Copy {
                        source: rect(u32::MAX, 0, 2, 1),
                        x: 0,
                        y: 0
                    },
                ]
            )
        )
        .is_err());
    assert!(canvas
        .apply(
            &device,
            &queue,
            batch(
                3,
                vec![DesktopOp::Fill {
                    rect: rect(0, 0, 1, 1),
                    bgra: red
                }]
            )
        )
        .is_err());
    let (a, ra) = mpsc::sync_channel(1);
    canvas
        .apply(
            &device,
            &queue,
            batch(
                2,
                vec![DesktopOp::Readback {
                    rect: rect(0, 0, 16, 8),
                    reply: a.into(),
                }],
            ),
        )
        .unwrap();
    device.poll(wgpu::PollType::wait_indefinitely()).unwrap();
    assert_eq!(ra.recv().unwrap().unwrap().bytes(), base.repeat(16 * 8));

    // Returning CPU authority is explicit, and stale mixed operations are rejected.
    canvas
        .apply(
            &device,
            &queue,
            batch(3, vec![DesktopOp::ReturnCpu(pixels(16, 8, base))]),
        )
        .unwrap();
    assert!(!canvas.is_mixed());
    assert!(canvas
        .apply(
            &device,
            &queue,
            batch(
                4,
                vec![DesktopOp::Fill {
                    rect: rect(0, 0, 1, 1),
                    bgra: red
                }]
            )
        )
        .is_err());
    assert!(canvas
        .apply(
            &device,
            &queue,
            batch(1, vec![DesktopOp::Seed(pixels(16, 8, red))])
        )
        .is_err());
    canvas
        .apply(
            &device,
            &queue,
            DesktopBatch {
                epoch: 2,
                sequence: 1,
                operations: vec![DesktopOp::Seed(pixels(8, 4, green))],
            },
        )
        .unwrap();
    assert_eq!(canvas.texture().unwrap().width(), 8);
    device.poll(wgpu::PollType::wait_indefinitely()).unwrap();
    assert!(!canvas.has_pending_completions());
    // Reset discards the canvas without waiting for earlier offscreen reads.
    let (reply, received) = mpsc::sync_channel(1);
    canvas
        .apply(
            &device,
            &queue,
            DesktopBatch {
                epoch: 2,
                sequence: 2,
                operations: vec![DesktopOp::Readback {
                    rect: rect(0, 0, 8, 4),
                    reply: reply.into(),
                }],
            },
        )
        .unwrap();
    assert!(canvas
        .apply(
            &device,
            &queue,
            DesktopBatch {
                epoch: 3,
                sequence: 1,
                operations: vec![DesktopOp::Reset, DesktopOp::Barrier]
            }
        )
        .is_err());
    canvas
        .apply(
            &device,
            &queue,
            DesktopBatch {
                epoch: 3,
                sequence: 1,
                operations: vec![DesktopOp::Reset],
            },
        )
        .unwrap();
    assert!(!canvas.is_mixed());
    assert!(canvas.texture().is_none());
    device.poll(wgpu::PollType::wait_indefinitely()).unwrap();
    assert_eq!(
        received.recv().unwrap().unwrap().bytes(),
        green.repeat(8 * 4)
    );
    assert!(!canvas.has_pending_completions());
}

#[cfg(target_os = "macos")]
#[test]
#[ignore = "requires unified-memory Metal"]
fn shared_cpu_patch_uses_gpu_copy_and_releases_credit_before_cached_mapping() {
    use std::sync::atomic::{AtomicBool, Ordering};
    struct Mapping {
        data: *mut u8,
        dropped: Arc<AtomicBool>,
    }
    unsafe impl Send for Mapping {}
    unsafe impl Sync for Mapping {}
    // SAFETY: an owned stable mmap, initialized before publication and immutable thereafter.
    unsafe impl dreamgpu::FrameAllocation for Mapping {
        fn base_ptr(&self) -> *mut u8 {
            self.data
        }
        fn len(&self) -> usize {
            65536
        }
    }
    impl Drop for Mapping {
        fn drop(&mut self) {
            unsafe {
                libc::munmap(self.data.cast(), 65536);
            }
            self.dropped.store(true, Ordering::Release);
        }
    }
    struct ReadLease {
        mapping: Arc<Mapping>,
        released: Arc<AtomicBool>,
    }
    impl dreamgpu::FramePixels for ReadLease {
        fn bytes(&self) -> &[u8] {
            unsafe { std::slice::from_raw_parts(self.mapping.data, 65536) }
        }
        fn storage(&self) -> Option<dreamgpu::FrameStorage> {
            Some(dreamgpu::FrameStorage {
                allocation: self.mapping.clone(),
                offset: 0,
                length: 65536,
            })
        }
    }
    impl Drop for ReadLease {
        fn drop(&mut self) {
            self.released.store(true, Ordering::Release);
        }
    }
    let instance = wgpu::Instance::new(wgpu::InstanceDescriptor {
        backends: wgpu::Backends::METAL,
        ..wgpu::InstanceDescriptor::new_without_display_handle()
    });
    let adapter = block_on(instance.request_adapter(&Default::default())).unwrap();
    let (device, queue) = block_on(adapter.request_device(&Default::default())).unwrap();
    let data = unsafe {
        libc::mmap(
            std::ptr::null_mut(),
            65536,
            libc::PROT_READ | libc::PROT_WRITE,
            libc::MAP_PRIVATE | libc::MAP_ANON,
            -1,
            0,
        )
    };
    assert_ne!(data, libc::MAP_FAILED);
    let color = [67, 109, 173, 255];
    for y in 0..8 {
        for x in 0..16 {
            unsafe {
                std::ptr::copy_nonoverlapping(
                    color.as_ptr(),
                    data.cast::<u8>().add(y * 256 + x * 4),
                    4,
                );
            }
        }
    }
    let mapping_dropped = Arc::new(AtomicBool::new(false));
    let released = Arc::new(AtomicBool::new(false));
    let lease = FrameLease {
        pixels: Arc::new(ReadLease {
            mapping: Arc::new(Mapping {
                data: data.cast(),
                dropped: mapping_dropped.clone(),
            }),
            released: released.clone(),
        }),
        width: 16,
        height: 8,
        stride: 256,
        format: PixelFormat::Bgra8,
        generation: 1,
        damage: None,
    };
    let (tx, rx) = mpsc::sync_channel(1);
    let mut canvas = DesktopCanvas::default();
    canvas
        .apply(
            &device,
            &queue,
            batch(
                1,
                vec![
                    DesktopOp::Seed(lease),
                    DesktopOp::Readback {
                        rect: rect(0, 0, 16, 8),
                        reply: tx.into(),
                    },
                ],
            ),
        )
        .unwrap();
    device.poll(wgpu::PollType::wait_indefinitely()).unwrap();
    assert_eq!(rx.recv().unwrap().unwrap().bytes(), color.repeat(16 * 8));
    assert!(
        released.load(Ordering::Acquire),
        "producer slot credit must be released after the GPU copy"
    );
    assert!(
        !mapping_dropped.load(Ordering::Acquire),
        "native texture cache retains only mapping ownership"
    );
    drop(canvas);
    device.poll(wgpu::PollType::wait_indefinitely()).unwrap();
    assert!(mapping_dropped.load(Ordering::Acquire));
}

#[test]
#[ignore = "requires a native GPU; explicit diagnostic readback without a window"]
fn diagnostic_capture_reads_composed_canvas_without_returning_cpu_authority() {
    let instance = wgpu::Instance::new(wgpu::InstanceDescriptor::new_without_display_handle());
    let adapter = block_on(instance.request_adapter(&Default::default())).unwrap();
    let (device, queue) = block_on(adapter.request_device(&Default::default())).unwrap();
    let mut canvas = DesktopCanvas::default();
    let cpu = [11, 23, 47, 255];
    let gpu = [29, 61, 193, 255];
    canvas
        .apply(
            &device,
            &queue,
            batch(
                1,
                vec![
                    DesktopOp::Seed(pixels(13, 7, cpu)),
                    DesktopOp::Fill {
                        rect: rect(3, 2, 7, 3),
                        bgra: gpu,
                    },
                ],
            ),
        )
        .unwrap();
    let pending = dreamgpu::presentation::gpu_diagnostic::begin_texture_readback(
        &device,
        &queue,
        canvas.texture().unwrap(),
    )
    .unwrap();
    assert_eq!(pending.dimensions(), (13, 7));
    let captured = std::thread::spawn(move || pending.finish().unwrap())
        .join()
        .unwrap();
    for y in 0..7 {
        for x in 0..13 {
            let i = (y * 13 + x) * 4;
            assert_eq!(
                &captured[i..i + 4],
                if (3..10).contains(&x) && (2..5).contains(&y) {
                    &gpu
                } else {
                    &cpu
                }
            );
        }
    }
    assert!(canvas.is_mixed());
    // A diagnostic consumes no guest sequence and does not issue ReturnCpu.
    canvas
        .apply(
            &device,
            &queue,
            batch(
                2,
                vec![DesktopOp::Fill {
                    rect: rect(0, 0, 1, 1),
                    bgra: gpu,
                }],
            ),
        )
        .unwrap();
    assert!(canvas.is_mixed());
}

#[test]
#[ignore = "requires a native GPU; verifies cursor RGB algebra and clipping"]
fn native_cursor_preserves_encoded_rgb_inversion_alpha_and_desktop_pixels() {
    use dreamgpu::presentation::{
        gpu_diagnostic::begin_texture_readback, native_cursor::NativeCursorComposer,
    };
    use dreamgpu::{NativeCursorFormat, NativeCursorShape};
    let instance = wgpu::Instance::new(wgpu::InstanceDescriptor::new_without_display_handle());
    let adapter = block_on(instance.request_adapter(&Default::default())).unwrap();
    let (device, queue) = block_on(adapter.request_device(&Default::default())).unwrap();
    let source: Vec<u8> = (0..512usize)
        .flat_map(|i| {
            [
                (i % 256) as u8,
                ((i * 17) % 256) as u8,
                ((i * 113) % 256) as u8,
                255,
            ]
        })
        .collect();
    let mut canvas = DesktopCanvas::default();
    canvas
        .apply(
            &device,
            &queue,
            batch(
                1,
                vec![DesktopOp::Seed(FrameLease {
                    width: 64,
                    height: 8,
                    stride: 256,
                    pixels: Arc::new(source.clone()),
                    format: PixelFormat::Bgra8,
                    generation: 1,
                    damage: None,
                })],
            ),
        )
        .unwrap();
    let mut shape = NativeCursorShape {
        width: 64,
        height: 8,
        hot_x: 0,
        hot_y: 0,
        format: NativeCursorFormat::AndXor,
        pixels: (0..512)
            .map(|i| match i % 4 {
                0 => [0xffffff, 0],
                1 => [0xffffff, 0xffffff],
                2 => [0, 0],
                _ => [0x35a5e9, 0xd17248],
            })
            .collect(),
    };
    let cursor = NativeCursorComposer::new(&device, &shape).unwrap();
    let mut encoder = device.create_command_encoder(&Default::default());
    cursor.compose(
        &device,
        &queue,
        &mut encoder,
        canvas.texture().unwrap(),
        0,
        0,
    );
    queue.submit([encoder.finish()]);
    let actual = begin_texture_readback(&device, &queue, cursor.texture())
        .unwrap()
        .finish()
        .unwrap();
    for (i, &pixel) in actual.as_chunks::<4>().0.iter().enumerate() {
        let [and, xor] = shape.pixels[i];
        if i % 4 == 0 {
            assert_eq!(pixel, [0, 0, 0, 0]);
            continue;
        }
        let p = &source[i * 4..i * 4 + 4];
        let rgb = (u32::from(p[2]) << 16) | (u32::from(p[1]) << 8) | u32::from(p[0]);
        let expected = (rgb & and) ^ xor;
        assert_eq!(
            pixel,
            [
                expected as u8,
                (expected >> 8) as u8,
                (expected >> 16) as u8,
                255
            ],
            "pixel {i}"
        );
    }
    shape.format = NativeCursorFormat::PremultipliedArgb;
    shape.pixels = (0..512)
        .map(|i| match i % 3 {
            0 => [0, 0],
            1 => [0x80402010, 0],
            _ => [0xff7391a7, 0],
        })
        .collect();
    let cursor = NativeCursorComposer::new(&device, &shape).unwrap();
    let mut encoder = device.create_command_encoder(&Default::default());
    cursor.compose(
        &device,
        &queue,
        &mut encoder,
        canvas.texture().unwrap(),
        -1,
        1,
    );
    queue.submit([encoder.finish()]);
    let actual = begin_texture_readback(&device, &queue, cursor.texture())
        .unwrap()
        .finish()
        .unwrap();
    for (i, &pixel) in actual.as_chunks::<4>().0.iter().enumerate() {
        let x = i % 64;
        let y = i / 64;
        let argb = shape.pixels[i][0];
        let alpha = argb >> 24;
        if x == 0 || y == 7 || alpha == 0 {
            assert_eq!(pixel, [0, 0, 0, 0]);
            continue;
        }
        let source = &source[((y + 1) * 64 + x - 1) * 4..];
        let mut expected = [0u8; 4];
        for c in 0..3 {
            expected[c] = (((argb >> (c * 8)) & 255)
                + (u32::from(source[c]) * (255 - alpha) + 127) / 255)
                .min(255) as u8;
        }
        expected[3] = 255;
        assert_eq!(pixel, expected, "alpha pixel {i}");
    }
    let snapshot = cursor
        .snapshot(&device, &queue, canvas.texture().unwrap(), -1, 1)
        .unwrap();
    let captured = begin_texture_readback(&device, &queue, &snapshot)
        .unwrap()
        .finish()
        .unwrap();
    let mut expected = source.clone();
    for y in 1..8usize {
        for x in 0..63usize {
            let argb = shape.pixels[(y - 1) * 64 + x + 1][0];
            let alpha = argb >> 24;
            let at = (y * 64 + x) * 4;
            for c in 0..3 {
                expected[at + c] = (((argb >> (c * 8)) & 255)
                    + (u32::from(source[at + c]) * (255 - alpha) + 127) / 255)
                    .min(255) as u8;
            }
        }
    }
    assert_eq!(
        captured, expected,
        "diagnostic snapshot preserves transparent pixels and clipped edges"
    );
    assert!(canvas.is_mixed());
    assert_eq!(
        begin_texture_readback(&device, &queue, canvas.texture().unwrap())
            .unwrap()
            .finish()
            .unwrap(),
        source
    );
}
