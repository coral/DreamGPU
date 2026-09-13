//! Real retained-image retirement with CPU-owned and returned desktops.
use super::*;

fn detached_completion(
    qemu: &mut Qemu,
    sequence: u32,
    server: &GpuServer,
    device: &wgpu::Device,
    ready: &mpsc::Receiver<()>,
) {
    let deadline = Instant::now() + Duration::from_secs(10);
    loop {
        assert!(
            server.take_desktop_update().is_none(),
            "detached discard produced a desktop batch"
        );
        assert!(
            !server.desktop_active(),
            "invisible present took primary ownership"
        );
        device.poll(wgpu::PollType::Poll).unwrap();
        if let Some(error) = server.take_error() {
            panic!("GPU transport: {error}");
        }
        let status = qemu.read(0x111c);
        assert_eq!(status & 4, 0, "GL batch failed: {}", qemu.read(0x1124));
        if qemu.read(0x1120) == sequence {
            return;
        }
        assert!(
            Instant::now() < deadline,
            "detached completion {sequence} timed out"
        );
        let _ = ready.recv_timeout(Duration::from_millis(2));
    }
}
fn discard(qemu: &mut Qemu) -> Vec<u32> {
    let mut record = desktop_command(8, 0, 0, 0, 0, qemu.read(0x1140), 0);
    for (word, register) in [(12, 0x1144), (13, 0x1148), (14, 0x114c), (15, 0x1150)] {
        record[word] = qemu.read(register);
    }
    record
}

#[test]
#[ignore = "requires bundled QEMU and native GPU; diskless detached retained-image acceptance"]
fn qemu_detached_discard_recycles_slots_before_seed_and_after_cpu_return() {
    let (device, queue, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!(
        "jrd-{}",
        &uuid::Uuid::new_v4().simple().to_string()[..16]
    ));
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
    qemu.batch(1, &[(1, vec![0]), (3, vec![16, 16]), (5, vec![])]);
    qemu.await_completion(1);
    let mut sequence = 1;
    // More than the three producer slots: missing detached release deadlocks
    // this sequence even though no renderer canvas has ever been created.
    for _ in 0..8 {
        sequence += 1;
        qemu.batch_for(
            sequence,
            1,
            1,
            1,
            2,
            &[
                (6, vec![0x0a3, 1f32.to_bits(), 0, 0, 1f32.to_bits()]),
                (6, vec![0x09a, 0x4000]),
                (7, vec![]),
            ],
        );
        detached_completion(&mut qemu, sequence, &server, &device, &ready);
        let record = discard(&mut qemu);
        sequence += 1;
        qemu.batch(sequence, &[(9, record)]);
        detached_completion(&mut qemu, sequence, &server, &device, &ready);
    }
    // Hold an immutable GL image across a complete GPU-primary ownership
    // interval, then discard after CPU RETURN without resurrecting the canvas.
    sequence += 1;
    qemu.batch_for(sequence, 1, 1, 1, 2, &[(7, vec![])]);
    detached_completion(&mut qemu, sequence, &server, &device, &ready);
    let retained = discard(&mut qemu);
    let mut canvas = dreamgpu::presentation::desktop::DesktopCanvas::default();
    sequence += 1;
    qemu.batch(sequence, &[(9, desktop_command(1, 0, 0, 64, 32, 0, 256))]);
    drive_desktop(
        &mut qemu,
        sequence,
        1,
        true,
        &server,
        &mut canvas,
        &device,
        &queue,
        &ready,
    );
    sequence += 1;
    qemu.batch(sequence, &[(9, desktop_command(7, 0, 0, 64, 32, 0, 256))]);
    drive_desktop(
        &mut qemu,
        sequence,
        2,
        true,
        &server,
        &mut canvas,
        &device,
        &queue,
        &ready,
    );
    sequence += 1;
    qemu.batch(sequence, &[(9, desktop_command(3, 0, 0, 64, 32, 0, 256))]);
    drive_desktop(
        &mut qemu,
        sequence,
        3,
        false,
        &server,
        &mut canvas,
        &device,
        &queue,
        &ready,
    );
    sequence += 1;
    qemu.batch(sequence, &[(9, retained)]);
    detached_completion(&mut qemu, sequence, &server, &device, &ready);
    assert_eq!(
        canvas.generation(),
        3,
        "discard advanced the desktop sequence"
    );
    assert!(!canvas.is_mixed());
    sequence += 1;
    qemu.batch(sequence, &[(8, vec![])]);
    detached_completion(&mut qemu, sequence, &server, &device, &ready);
    drop(qemu);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}
