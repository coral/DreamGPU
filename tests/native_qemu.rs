//! Diskless QEMU guest-command → native image IPC → renderer import acceptance.

#[path = "native_qemu/cursor.rs"]
mod cursor;
#[path = "native_qemu/discard.rs"]
mod discard;
#[path = "native_qemu/evaluator.rs"]
mod evaluator;
#[path = "native_qemu/fixed.rs"]
mod fixed;
#[path = "native_qemu/pixel_image.rs"]
mod pixel_image;
#[path = "native_qemu/pixels.rs"]
mod pixels;
#[path = "native_qemu/raster.rs"]
mod raster;
#[path = "native_qemu/secondary.rs"]
mod secondary;
#[path = "native_qemu/texture_control.rs"]
mod texture_control;
#[path = "native_qemu/textures.rs"]
mod textures;

use dreamgpu::shmem::ShmemServer;
use dreamgpu::transport::*;
use dreamgpu::{GpuFrameLease, GpuHostInfo};
use std::{
    fmt::Write as _,
    io::{BufRead, BufReader},
    process::{Child, Command, Stdio},
    time::{Duration, Instant},
};
use std::{
    io::Write,
    os::unix::net::UnixStream,
    path::{Path, PathBuf},
    sync::{mpsc, Arc},
};

struct Qemu {
    child: Child,
    commands: UnixStream,
    replies: BufReader<UnixStream>,
}
impl Qemu {
    fn start(directory: &Path, gpu_socket: &Path) -> Self {
        Self::start_with_display(directory, gpu_socket, None)
    }
    fn start_with_display(
        directory: &Path,
        gpu_socket: &Path,
        display_socket: Option<&Path>,
    ) -> Self {
        let display = display_socket
            .map(|path| format!("dreamgpu-shmem,socket={}", path.display()))
            .unwrap_or_else(|| "none".into());
        let binary = std::env::var_os("DREAMGPU_QEMU_PATH")
            .map(PathBuf::from)
            .unwrap_or_else(|| {
                PathBuf::from(env!("CARGO_MANIFEST_DIR"))
                    .join("target/qemu-build/qemu-system-x86_64")
            });
        assert!(
            binary.is_file(),
            "Build DreamGPU QEMU or set DREAMGPU_QEMU_PATH: {}",
            binary.display()
        );
        // Frozen binaries may live outside QEMU's build directory. Bind their
        // firmware explicitly instead of relying on executable-relative lookup.
        let firmware = std::env::var_os("DREAMGPU_FIRMWARE_DIR")
            .map(PathBuf::from)
            .unwrap_or_else(|| {
                PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("vendor/qemu/pc-bios")
            });
        assert!(
            firmware.join("bios-256k.bin").is_file(),
            "QEMU firmware unavailable: {}",
            firmware.display()
        );
        let qtest_path = directory.join("qtest.sock");
        let child = Command::new(binary)
            .arg("-L")
            .arg(&firmware)
            .args([
                "-machine",
                "pc",
                "-accel",
                "qtest",
                "-m",
                "64M",
                "-nodefaults",
                "-vga",
                "none",
                "-device",
                &format!("dreamgpu,addr=04.0,gpu-socket={}", gpu_socket.display()),
                "-qtest",
                &format!("unix:{},server=on,wait=off", qtest_path.display()),
                "-qtest-log",
                "/dev/null",
                "-display",
                &display,
                "-monitor",
                "none",
                "-serial",
                "none",
            ])
            .stdin(Stdio::null())
            .stdout(Stdio::null())
            .stderr(Stdio::inherit())
            .spawn()
            .unwrap();
        let deadline = Instant::now() + Duration::from_secs(10);
        let commands = loop {
            match UnixStream::connect(&qtest_path) {
                Ok(stream) => break stream,
                Err(error) if Instant::now() < deadline => {
                    let _ = error;
                    std::thread::sleep(Duration::from_millis(10));
                }
                Err(error) => {
                    let mut child = child;
                    let _ = child.kill();
                    let _ = child.wait();
                    panic!("QEMU qtest unavailable: {error}");
                }
            }
        };
        commands
            .set_read_timeout(Some(Duration::from_secs(10)))
            .unwrap();
        let replies = BufReader::new(commands.try_clone().unwrap());
        let mut qemu = Self {
            child,
            commands,
            replies,
        };
        // Device at 00:04.0, BAR0 VRAM and BAR2 registers. No firmware/guest disks.
        for (offset, value) in [(0x10, 0xe0000000u32), (0x18, 0xf0000000), (0x04, 0x0007)] {
            qemu.command(&format!("outl 0xcf8 {:#x}", 0x80002000u32 + offset));
            qemu.command(&format!("outl 0xcfc {value:#x}"));
        }
        assert_eq!(qemu.read(0x1000), 0x47524a51);
        qemu
    }
    fn command(&mut self, command: &str) -> String {
        writeln!(self.commands, "{command}").unwrap();
        loop {
            let mut reply = String::new();
            assert_ne!(
                self.replies.read_line(&mut reply).unwrap(),
                0,
                "qtest EOF for {command}"
            );
            if reply.starts_with("IRQ ") {
                continue;
            }
            assert!(reply.starts_with("OK"), "{command}: {reply}");
            return reply.trim().to_owned();
        }
    }
    fn read(&mut self, register: u32) -> u32 {
        let response = self.command(&format!("readl {:#x}", 0xf0000000u32 + register));
        u32::from_str_radix(
            response
                .split_whitespace()
                .nth(1)
                .unwrap()
                .trim_start_matches("0x"),
            16,
        )
        .unwrap()
    }
    fn write(&mut self, register: u32, value: u32) {
        self.command(&format!(
            "writel {:#x} {value:#x}",
            0xf0000000u32 + register
        ));
    }
    fn batch(&mut self, sequence: u32, records: &[(u32, Vec<u32>)]) {
        self.batch_for(sequence, 1, 1, 1, 0, records);
    }
    fn batch_for(
        &mut self,
        sequence: u32,
        client: u32,
        context: u32,
        drawable: u32,
        flags: u32,
        records: &[(u32, Vec<u32>)],
    ) {
        let generation = self.read(0x103c);
        let mut bytes = Vec::new();
        for (opcode, arguments) in records {
            for word in [
                *opcode,
                32 + arguments.len() as u32 * 4,
                client,
                context,
                drawable,
                if *opcode == 7 { flags } else { 0 },
                0,
                generation,
            ]
            .iter()
            .chain(arguments)
            {
                bytes.extend_from_slice(&word.to_le_bytes());
            }
        }
        let mut hexadecimal = String::from("0x");
        for byte in &bytes {
            write!(hexadecimal, "{byte:02x}").unwrap();
        }
        self.command(&format!("write 0x100000 {} {hexadecimal}", bytes.len()));
        self.write(0x1104, 0x100000);
        self.write(0x1108, 0);
        self.write(0x110c, bytes.len() as u32);
        self.write(0x1110, sequence);
        self.write(0x1114, generation);
        self.write(0x1118, 1);
    }

    fn await_completion(&mut self, sequence: u32) {
        let deadline = Instant::now() + Duration::from_secs(5);
        loop {
            let status = self.read(0x111c);
            assert_eq!(
                status & 4,
                0,
                "QEMU GL batch {sequence} failed: status={status}, error={}",
                self.read(0x1124)
            );
            if self.read(0x1120) == sequence {
                return;
            }
            assert!(
                Instant::now() < deadline,
                "QEMU GL batch {sequence} did not complete"
            );
            // Bounded acceptance-test polling, never part of a production frame loop.
            std::thread::sleep(Duration::from_millis(5));
        }
    }
}
impl Drop for Qemu {
    fn drop(&mut self) {
        let _ = self.child.kill();
        let _ = self.child.wait();
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
            std::task::Poll::Ready(value) => return value,
            std::task::Poll::Pending => std::thread::park(),
        }
    }
}

fn assert_gpu_image(
    device: &wgpu::Device,
    queue: &wgpu::Queue,
    frame: &GpuFrameLease,
    expected: impl Fn(usize, usize) -> [u8; 4],
) {
    let pixels = dreamgpu::presentation::gpu_diagnostic::begin_readback(device, queue, frame)
        .unwrap()
        .finish()
        .unwrap();
    for y in 0..frame.height as usize {
        for x in 0..frame.width as usize {
            let offset = (y * frame.width as usize + x) * 4;
            assert_eq!(&pixels[offset..offset + 4], &expected(x, y));
        }
    }
}

fn native_gpu() -> (wgpu::Device, wgpu::Queue, GpuHostInfo) {
    #[cfg(target_os = "macos")]
    let backends = wgpu::Backends::METAL;
    #[cfg(target_os = "linux")]
    let backends = wgpu::Backends::VULKAN;
    let instance = wgpu::Instance::new(wgpu::InstanceDescriptor {
        backends,
        ..wgpu::InstanceDescriptor::new_without_display_handle()
    });
    let adapter = block_on(instance.request_adapter(&Default::default())).unwrap();
    #[cfg(target_os = "macos")]
    let (device, queue) = block_on(adapter.request_device(&Default::default())).unwrap();
    #[cfg(target_os = "linux")]
    let (device, queue) =
        dreamgpu::presentation::linux_gpu_texture::create_device(&adapter, &Default::default())
            .unwrap()
            .unwrap();
    #[cfg(target_os = "macos")]
    let host = GpuHostInfo::default();
    #[cfg(target_os = "linux")]
    let host = GpuHostInfo {
        device_uuid: dreamgpu::presentation::linux_gpu_texture::device_uuid(&device).unwrap(),
        render_node: Some(dreamgpu::presentation::linux_gpu_texture::render_node(&device).unwrap()),
    };
    (device, queue, host)
}

#[test]
#[ignore = "requires bundled QEMU GL producer and native host GPU; diskless acceptance"]
fn qemu_gl_drawable_crosses_native_ipc_and_releases_export_slots() {
    let (device, queue, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!("dg-qgpu-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("gpu.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let (wake, ready) = mpsc::sync_channel(1);
    server.set_callback(Arc::new(move |_| {
        let _ = wake.try_send(());
    }));
    let mut qemu = Qemu::start(&directory, &socket);
    qemu.batch(
        1,
        &[
            (1, vec![0]),
            (3, vec![32, 16]),
            (5, vec![]),
            (6, vec![0x0a3, 1f32.to_bits(), 0, 0, 1f32.to_bits()]),
            (6, vec![0x09a, 0x4000]),
            (7, vec![]),
        ],
    );
    let mut previous_generation = None;
    for sequence in 1..=7 {
        if sequence != 1 {
            // Swap exchanges FRONT/BACK; redraw the next back buffer rather
            // than relying on contents from the preceding presentation.
            qemu.batch(sequence, &[(6, vec![0x09a, 0x4000]), (7, vec![])]);
        }
        qemu.await_completion(sequence);
        ready
            .recv_timeout(Duration::from_secs(10))
            .unwrap_or_else(|error| {
                panic!(
                    "No GPU frame: {error}; status={} error={} transport={:?}",
                    qemu.read(0x111c),
                    qemu.read(0x1124),
                    server.take_error()
                )
            });
        if let Some(error) = server.take_error() {
            panic!("GPU transport: {error}");
        }
        assert!(
            !server.desktop_active(),
            "normal GL drawable must not replace the desktop"
        );
        assert!(server.take_frame().is_none());
        let mut frames = server.take_drawables();
        assert_eq!(frames.len(), 1);
        let drawable = frames.pop().unwrap();
        assert_eq!((drawable.client, drawable.drawable), (1, 1));
        let frame = drawable.frame;
        assert_eq!((frame.width, frame.height), (32, 16));
        if let Some(previous) = previous_generation {
            assert!(frame.generation > previous);
        }
        previous_generation = Some(frame.generation);
        assert_gpu_image(&device, &queue, &frame, |_, _| [0, 0, 255, 255]);
        drop(frame);
        device.poll(wgpu::PollType::wait_indefinitely()).unwrap();
    }
    // Seven successful presents exceed the three export slots, proving completion
    // releases reach QEMU instead of leaving producer slots permanently occupied.
    drop(qemu);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}

fn desktop_command(
    op: u32,
    x: u32,
    y: u32,
    width: u32,
    height: u32,
    offset: u32,
    stride: u32,
) -> Vec<u32> {
    let mut words = vec![0; 16];
    words[0] = op;
    words[2] = x;
    words[3] = y;
    words[4] = width;
    words[5] = height;
    words[10] = offset;
    words[11] = stride;
    words
}

#[expect(
    clippy::too_many_arguments,
    reason = "Drives the native desktop oracle with its independent transport and rendering resources."
)]
fn drive_desktop(
    qemu: &mut Qemu,
    gl_sequence: u32,
    desktop_sequence: u64,
    expected_mixed: bool,
    server: &GpuServer,
    canvas: &mut dreamgpu::presentation::desktop::DesktopCanvas,
    device: &wgpu::Device,
    queue: &wgpu::Queue,
    wake: &mpsc::Receiver<()>,
) {
    let deadline = Instant::now() + Duration::from_secs(10);
    loop {
        while let Some(batch) = server.take_desktop_update() {
            canvas.apply(device, queue, batch).unwrap();
        }
        device.poll(wgpu::PollType::Poll).unwrap();
        if let Some(error) = server.take_error() {
            panic!("GPU transport: {error}");
        }
        let status = qemu.read(0x111c);
        assert_eq!(status & 4, 0, "GL batch failed: {}", qemu.read(0x1124));
        if qemu.read(0x1120) == gl_sequence
            && canvas.generation() == desktop_sequence
            && canvas.is_mixed() == expected_mixed
        {
            return;
        }
        assert!(
            Instant::now() < deadline,
            "desktop/GL completion timeout at {desktop_sequence}/{gl_sequence}"
        );
        let _ = wake.recv_timeout(Duration::from_millis(2));
    }
}

#[test]
#[ignore = "requires bundled mixed-desktop QEMU producer and native host GPU; diskless acceptance"]
fn qemu_mixed_desktop_preserves_cpu_occlusion_and_reads_back_to_guest_vram() {
    let (device, queue, host) = native_gpu();
    let directory =
        std::env::temp_dir().join(format!("dg-mixed-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("gpu.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let (wake, ready) = mpsc::sync_channel(1);
    server.set_callback(Arc::new(move |_| {
        let _ = wake.try_send(());
    }));
    let cpu_socket = directory.join("cpu.sock");
    let mut cpu = crate::ShmemServer::new(cpu_socket.to_str().unwrap()).unwrap();
    let mut qemu = Qemu::start_with_display(&directory, &socket, Some(&cpu_socket));
    for (index, value) in [(1, 64), (2, 32), (3, 32), (4, 0x41)] {
        qemu.command(&format!(
            "writew {:#x} {value:#x}",
            0xf0000500u32 + index * 2
        ));
    }
    qemu.command("outb 0x3c0 0x20");
    qemu.command("memset 0xe0000000 8192 0x30");
    let (old_cpu_epoch, old_cpu_frame) = wait_cpu_frame(&mut cpu, None);
    let mut canvas = dreamgpu::presentation::desktop::DesktopCanvas::default();
    qemu.batch(1, &[(9, desktop_command(1, 0, 0, 64, 32, 0, 256))]);
    drive_desktop(
        &mut qemu,
        1,
        1,
        true,
        &server,
        &mut canvas,
        &device,
        &queue,
        &ready,
    );
    assert!(server.desktop_active());
    // GL uses a bottom-left origin: blue upper half must become the top half
    // after the producer's GPU flip, with the CPU desktop visible around it.
    qemu.batch_for(
        2,
        1,
        1,
        1,
        2,
        &[
            (1, vec![0]),
            (3, vec![32, 16]),
            (5, vec![]),
            (6, vec![0x0a3, 1f32.to_bits(), 0, 0, 1f32.to_bits()]),
            (6, vec![0x09a, 0x4000]),
            (6, vec![0x209, 0x0c11]),
            (6, vec![0x7f0, 0, 8, 32, 8]),
            (6, vec![0x0a3, 0, 0, 1f32.to_bits(), 1f32.to_bits()]),
            (6, vec![0x09a, 0x4000]),
            (7, vec![]),
        ],
    );
    qemu.await_completion(2);
    let mut blit = desktop_command(6, 16, 8, 32, 16, qemu.read(0x1140), 0);
    blit[1] = 1;
    blit[12] = qemu.read(0x1144);
    blit[13] = qemu.read(0x1148);
    blit[14] = qemu.read(0x114c);
    blit[15] = qemu.read(0x1150);
    qemu.batch(3, &[(9, blit)]);
    drive_desktop(
        &mut qemu,
        3,
        2,
        true,
        &server,
        &mut canvas,
        &device,
        &queue,
        &ready,
    );
    // CPU window damage occludes part of the accelerated drawable, then a real
    // guest read returns the combined image to its original VRAM allocation.
    qemu.command("memset 0xe0020000 32 0x77");
    qemu.batch(
        4,
        &[
            (9, desktop_command(2, 17, 9, 4, 2, 0x20000, 16)),
            (9, desktop_command(7, 0, 0, 64, 32, 0, 256)),
        ],
    );
    drive_desktop(
        &mut qemu,
        4,
        4,
        true,
        &server,
        &mut canvas,
        &device,
        &queue,
        &ready,
    );
    let reply = qemu.command("read 0xe0000000 8192");
    let hex = reply
        .split_whitespace()
        .nth(1)
        .unwrap()
        .trim_start_matches("0x");
    let pixels: Vec<u8> = (0..hex.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&hex[i..i + 2], 16).unwrap())
        .collect();
    for y in 0..32usize {
        for x in 0..64usize {
            let expected = if (17..21).contains(&x) && (9..11).contains(&y) {
                [0x77; 4]
            } else if (16..48).contains(&x) && (8..16).contains(&y) {
                [255, 0, 0, 255]
            } else if (16..48).contains(&x) && (16..24).contains(&y) {
                [0, 0, 255, 255]
            } else {
                [0x30; 4]
            };
            assert_eq!(
                &pixels[(y * 64 + x) * 4..(y * 64 + x + 1) * 4],
                expected,
                "pixel {x},{y}"
            );
        }
    }
    qemu.batch(5, &[(9, desktop_command(3, 0, 0, 64, 32, 0, 256))]);
    drive_desktop(
        &mut qemu,
        5,
        5,
        false,
        &server,
        &mut canvas,
        &device,
        &queue,
        &ready,
    );
    assert!(!canvas.is_mixed());
    assert!(
        !server.permits_cpu_frame(old_cpu_epoch, old_cpu_frame.generation),
        "ReturnCpu must reject an older live CPU slot"
    );
    // The deliberately stale test lease has served its purpose. Keeping both
    // historical frames plus ShmemDisplay's newest frame would pin all3 slots
    // during reset and correctly prevent the producer publishing its anchor.
    drop(old_cpu_frame);
    let (coherent_epoch, coherent) = wait_cpu_frame(&mut cpu, Some(&server));
    let cpu_pixels = coherent.packed_copy().unwrap();
    for (actual, expected) in cpu_pixels
        .as_chunks::<4>()
        .0
        .iter()
        .zip(pixels.as_chunks::<4>().0)
    {
        assert_eq!(
            &actual[..3],
            &expected[..3],
            "legacy CPU display must include prior GPU writes"
        );
    }
    qemu.write(0x1038, 1);
    drive_desktop(
        &mut qemu,
        0,
        1,
        false,
        &server,
        &mut canvas,
        &device,
        &queue,
        &ready,
    );
    assert!(!canvas.is_mixed() && canvas.texture().is_none());
    assert!(
        !server.permits_cpu_frame(coherent_epoch, coherent.generation),
        "reset requires its own fresh CPU anchor"
    );
    drop(wait_cpu_frame(&mut cpu, Some(&server)));
    drop(coherent);
    qemu.batch(6, &[(9, desktop_command(1, 0, 0, 64, 32, 0, 256))]);
    drive_desktop(
        &mut qemu,
        6,
        1,
        true,
        &server,
        &mut canvas,
        &device,
        &queue,
        &ready,
    );
    qemu.write(0x1038, 1);
    drive_desktop(
        &mut qemu,
        0,
        1,
        false,
        &server,
        &mut canvas,
        &device,
        &queue,
        &ready,
    );
    assert!(
        canvas.texture().is_none(),
        "reset of an active mixed canvas must discard its allocation"
    );
    drop(wait_cpu_frame(&mut cpu, Some(&server)));
    qemu.batch(7, &[(9, desktop_command(1, 0, 0, 64, 32, 0, 256))]);
    drive_desktop(
        &mut qemu,
        7,
        1,
        true,
        &server,
        &mut canvas,
        &device,
        &queue,
        &ready,
    );
    drop(canvas);
    device.poll(wgpu::PollType::wait_indefinitely()).unwrap();
    drop(qemu);
    drop(server);
    drop(cpu);
    std::fs::remove_dir_all(directory).unwrap();
}

#[test]
#[ignore = "requires native GPU; ordered deferred exports and CPU desktop barriers"]
fn qemu_queued_desktop_packets_preserve_export_and_cpu_patch_order() {
    let (device, queue, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!("dg-o-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("gpu.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let (wake, ready) = mpsc::sync_channel(1);
    server.set_callback(Arc::new(move |_| {
        let _ = wake.try_send(());
    }));
    let cpu_socket = directory.join("cpu.sock");
    let mut cpu = crate::ShmemServer::new(cpu_socket.to_str().unwrap()).unwrap();
    let mut qemu = Qemu::start_with_display(&directory, &socket, Some(&cpu_socket));
    for (index, value) in [(1, 64), (2, 32), (3, 32), (4, 0x41)] {
        qemu.command(&format!(
            "writew {:#x} {value:#x}",
            0xf0000500u32 + index * 2
        ));
    }
    qemu.command("outb 0x3c0 0x20");
    qemu.command("memset 0xe0000000 8192 0x30");
    drop(wait_cpu_frame(&mut cpu, None));
    let mut canvas = dreamgpu::presentation::desktop::DesktopCanvas::default();
    qemu.batch(1, &[(9, desktop_command(1, 0, 0, 64, 32, 0, 256))]);
    drive_desktop(
        &mut qemu,
        1,
        1,
        true,
        &server,
        &mut canvas,
        &device,
        &queue,
        &ready,
    );

    qemu.batch(2, &[(1, vec![0]), (3, vec![16, 16]), (5, vec![])]);
    qemu.await_completion(2);
    let mut blits = Vec::new();
    // Capture two different immutable exports without driving the consumer's
    // canvas between them. Batch completion need not wait for either GPU fence.
    for (sequence, color, x, y) in [
        (3, [1f32.to_bits(), 0, 0, 1f32.to_bits()], 0, 0),
        (4, [0, 1f32.to_bits(), 0, 1f32.to_bits()], 8, 8),
    ] {
        let mut clear = vec![0x0a3];
        clear.extend(color);
        qemu.batch_for(
            sequence,
            1,
            1,
            1,
            2,
            &[(6, clear), (6, vec![0x09a, 0x4000]), (7, vec![])],
        );
        qemu.await_completion(sequence);
        let mut blit = desktop_command(6, x, y, 16, 16, qemu.read(0x1140), 0);
        blit[1] = 1; // Final reference releases the matching immutable export.
        for (word, register) in [(12, 0x1144), (13, 0x1148), (14, 0x114c), (15, 0x1150)] {
            blit[word] = qemu.read(register);
        }
        blits.push(blit);
    }
    let mut fill = desktop_command(4, 0, 0, 4, 4, 0, 0);
    fill[6] = u32::from_le_bytes([255, 0, 0, 255]);
    let copy = desktop_command(5, 32, 16, 8, 8, 0, 0);
    qemu.command("memset 0xe0020000 16 0x77");
    // A producer that lets synchronous PATCH/READBACK overtake queued fixed
    // packets either breaks the desktop sequence or returns incorrect pixels.
    // COPY must also observe the preceding FILL and first BLIT, not the seed.
    qemu.batch(
        5,
        &[
            (9, blits.remove(0)),
            (9, fill),
            (9, copy),
            (9, blits.remove(0)),
            (9, desktop_command(2, 9, 9, 2, 2, 0x20000, 8)),
            (9, desktop_command(7, 0, 0, 64, 32, 0, 256)),
        ],
    );
    drive_desktop(
        &mut qemu,
        5,
        7,
        true,
        &server,
        &mut canvas,
        &device,
        &queue,
        &ready,
    );
    let reply = qemu.command("read 0xe0000000 8192");
    let hex = reply
        .split_whitespace()
        .nth(1)
        .unwrap()
        .trim_start_matches("0x");
    let pixels: Vec<u8> = (0..hex.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&hex[i..i + 2], 16).unwrap())
        .collect();
    for y in 0..32usize {
        for x in 0..64usize {
            let expected = if (9..11).contains(&x) && (9..11).contains(&y) {
                [0x77; 4]
            } else if (8..24).contains(&x) && (8..24).contains(&y) {
                [0, 255, 0, 255]
            } else if (32..40).contains(&x) && (16..24).contains(&y) {
                if x < 36 && y < 20 {
                    [255, 0, 0, 255]
                } else {
                    [0, 0, 255, 255]
                }
            } else if x < 4 && y < 4 {
                [255, 0, 0, 255]
            } else if x < 16 && y < 16 {
                [0, 0, 255, 255]
            } else {
                [0x30; 4]
            };
            assert_eq!(
                &pixels[(y * 64 + x) * 4..(y * 64 + x + 1) * 4],
                expected,
                "ordered desktop pixel {x},{y}"
            );
        }
    }
    qemu.batch(6, &[(9, desktop_command(3, 0, 0, 64, 32, 0, 256))]);
    drive_desktop(
        &mut qemu,
        6,
        8,
        false,
        &server,
        &mut canvas,
        &device,
        &queue,
        &ready,
    );
    qemu.batch(7, &[(8, vec![])]);
    drive_lifecycle(&mut qemu, 7, &server, &device, &ready);
    assert!(server.retained_drawables().is_empty());
    drop(canvas);
    device.poll(wgpu::PollType::wait_indefinitely()).unwrap();
    drop(qemu);
    drop(server);
    drop(cpu);
    std::fs::remove_dir_all(directory).unwrap();
}

fn wait_cpu_frame(
    cpu: &mut crate::ShmemServer,
    gate: Option<&GpuServer>,
) -> (u64, dreamgpu::FrameLease) {
    let deadline = Instant::now() + Duration::from_secs(5);
    let mut last = None;
    loop {
        cpu.poll();
        if let Some(display) = cpu.display_mut() {
            display.refresh();
            let epoch = display.epoch();
            if let Some(frame) = display.acquire_frame() {
                last = Some((
                    epoch,
                    frame.generation,
                    frame.width,
                    frame.height,
                    display.slot_states(),
                    display.frame_counter(),
                ));
                if frame.width == 64
                    && frame.height == 32
                    && gate.is_none_or(|gate| gate.permits_cpu_frame(epoch, frame.generation))
                {
                    return (epoch, frame);
                }
                display.discard_current();
            }
        }
        assert!(
            Instant::now() < deadline,
            "fresh coherent legacy CPU frame did not arrive; last={last:?}, anchor={:?}",
            gate.map(|gate| gate.legacy_anchor())
        );
        std::thread::sleep(Duration::from_millis(2));
    }
}

#[test]
#[ignore = "requires bundled QEMU GL producer and native host GPU; diskless lifecycle acceptance"]
fn qemu_drawable_context_switching_and_lifecycle_preserve_export_ownership() {
    let (device, queue, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!("dg-life-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("gpu.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let (wake, ready) = mpsc::sync_channel(1);
    server.set_callback(Arc::new(move |_| {
        let _ = wake.try_send(());
    }));
    let mut qemu = Qemu::start(&directory, &socket);
    let mut sequence = 0;
    for client in 1..=2 {
        sequence += 1;
        let (r, g) = if client == 1 {
            (1f32.to_bits(), 0)
        } else {
            (0, 1f32.to_bits())
        };
        qemu.batch_for(
            sequence,
            client,
            1,
            1,
            0,
            &[
                (1, vec![0]),
                (3, vec![32, 16]),
                (5, vec![]),
                (6, vec![0x0a3, r, g, 0, 1f32.to_bits()]),
                (6, vec![0x09a, 0x4000]),
            ],
        );
        qemu.await_completion(sequence);
        sequence += 1;
        let white = if client == 1 { 0 } else { 1f32.to_bits() };
        qemu.batch_for(
            sequence,
            client,
            2,
            1,
            2,
            &[
                (1, vec![1]),
                (5, vec![]),
                (6, vec![0x209, 0x0c11]),
                (6, vec![0x7f0, 0, 8, 32, 8]),
                (6, vec![0x0a3, white, white, 1f32.to_bits(), 1f32.to_bits()]),
                (6, vec![0x09a, 0x4000]),
                (7, vec![]),
            ],
        );
        qemu.await_completion(sequence);
        let deadline = Instant::now() + Duration::from_secs(5);
        let frame = loop {
            if let Some(frame) = server
                .retained_drawables()
                .into_iter()
                .find(|image| image.client == client)
                .map(|image| image.frame.clone())
            {
                break frame;
            }
            assert!(Instant::now() < deadline);
            let _ = ready.recv_timeout(Duration::from_millis(2));
        };
        assert_gpu_image(&device, &queue, &frame, |_, y| match (client, y < 8) {
            (1, true) => [255, 0, 0, 255],
            (1, false) => [0, 0, 255, 255],
            (_, true) => [255; 4],
            (_, false) => [0, 255, 0, 255],
        });
        drop(frame);
    }
    assert_eq!(server.retained_drawables().len(), 2);
    // Allocating a second drawable advances the producer's global resource
    // epoch; destruction must still use the first drawable's exact last export.
    sequence += 1;
    qemu.batch_for(sequence, 1, 2, 2, 0, &[(3, vec![32, 16])]);
    qemu.await_completion(sequence);
    sequence += 1;
    qemu.batch_for(sequence, 1, 2, 1, 0, &[(4, vec![])]);
    drive_lifecycle(&mut qemu, sequence, &server, &device, &ready);
    assert_eq!(server.retained_drawables().len(), 1);
    // Closing also destroys drawable2, which has never exported a frame.
    sequence += 1;
    qemu.batch_for(sequence, 1, 2, 2, 0, &[(8, vec![])]);
    drive_lifecycle(&mut qemu, sequence, &server, &device, &ready);
    assert_eq!(
        server
            .retained_drawables()
            .into_iter()
            .next()
            .unwrap()
            .client,
        2
    );
    sequence += 1;
    qemu.batch_for(sequence, 2, 2, 1, 0, &[(8, vec![])]);
    drive_lifecycle(&mut qemu, sequence, &server, &device, &ready);
    assert!(server.retained_drawables().is_empty());
    drop(qemu);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}

fn drive_lifecycle(
    qemu: &mut Qemu,
    sequence: u32,
    server: &GpuServer,
    device: &wgpu::Device,
    wake: &mpsc::Receiver<()>,
) {
    let deadline = Instant::now() + Duration::from_secs(5);
    loop {
        assert!(
            server.take_desktop_update().is_none(),
            "resource retirement must not invent a desktop frame"
        );
        device.poll(wgpu::PollType::Poll).unwrap();
        if let Some(error) = server.take_error() {
            panic!("GPU transport: {error}");
        }
        assert_eq!(
            qemu.read(0x111c) & 4,
            0,
            "GL lifecycle failed: {}",
            qemu.read(0x1124)
        );
        if qemu.read(0x1120) == sequence {
            return;
        }
        assert!(
            Instant::now() < deadline,
            "GL lifecycle did not release its export slots"
        );
        let _ = wake.recv_timeout(Duration::from_millis(2));
    }
}

#[path = "native_qemu/selection.rs"]
mod selection;

#[path = "native_qemu/lists.rs"]
mod lists;
