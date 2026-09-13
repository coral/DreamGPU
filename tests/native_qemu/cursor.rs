//! Real producer/consumer cursor acceptance without firmware, disk or a GL context.
use super::*;
use dreamgpu::NativeCursor;

fn await_cursor(
    cpu: &mut crate::ShmemServer,
    predicate: impl Fn(&NativeCursor) -> bool,
) -> NativeCursor {
    let deadline = Instant::now() + Duration::from_secs(5);
    loop {
        cpu.poll();
        if let Some(error) = cpu.take_cursor_error() {
            panic!("cursor transport: {error}");
        }
        if let Some(state) = cpu.native_cursor() {
            if predicate(&state) {
                return state;
            }
        }
        assert!(Instant::now() < deadline, "cursor update did not arrive");
        std::thread::sleep(Duration::from_millis(2));
    }
}

#[test]
#[ignore = "requires bundled QEMU cursor producer; diskless real transport acceptance"]
fn qemu_native_cursor_moves_without_publishing_desktop_frames() {
    let directory = std::env::temp_dir().join(format!("jrc-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("cpu.sock");
    let mut cpu = crate::ShmemServer::new(socket.to_str().unwrap()).unwrap();
    let mut qemu = Qemu::start_with_display(
        &directory,
        &directory.join("unused-gpu.sock"),
        Some(&socket),
    );
    assert_ne!(qemu.read(0x1008) & 0x20, 0, "native cursor capability");
    for (index, value) in [(1, 64), (2, 32), (3, 32), (4, 0x41)] {
        qemu.command(&format!(
            "writew {:#x} {value:#x}",
            0xf0000500u32 + index * 2
        ));
    }
    qemu.command("outb 0x3c0 0x20");
    qemu.command("memset 0xe0000000 8192 0x30");
    let deadline = Instant::now() + Duration::from_secs(5);
    loop {
        cpu.poll();
        if let Some(display) = cpu.display_mut() {
            if let Some(frame) = display.acquire_frame() {
                if (frame.width, frame.height) == (64, 32) {
                    break;
                }
                display.discard_current();
            }
        }
        assert!(Instant::now() < deadline, "initial CPU frame missing");
        std::thread::sleep(Duration::from_millis(2));
    }
    let pairs = [
        [0xffffffu32, 0],
        [0xffffff, 0xffffff],
        [0, 0x123456],
        [0xaabbcc, 0x778899],
    ];
    let mut hex = String::from("0x");
    for byte in pairs.iter().flatten().flat_map(|x| x.to_le_bytes()) {
        write!(hex, "{byte:02x}").unwrap();
    }
    qemu.command(&format!("write 0x200000 32 {hex}"));
    for (reg, value) in [
        (0x1084, 0x200000),
        (0x1088, 0),
        (0x108c, 32),
        (0x1090, 2),
        (0x1094, 2),
        (0x1098, 1),
        (0x109c, 1),
        (0x10a0, 2),
        (0x10a4, 10),
        (0x10a8, 12),
        (0x10ac, 3),
        (0x10b0, 1),
        (0x10b4, 1),
    ] {
        qemu.write(reg, value);
    }
    let initial = await_cursor(&mut cpu, |state| {
        state.enabled && state.visible && state.shape.is_some()
    });
    let shape = initial.shape.unwrap();
    assert_eq!(shape.pixels, pairs);
    assert_eq!((initial.x, initial.y), (10, 12));
    qemu.command("memset 0x200000 32 0x00");
    // Let the initial VBE mode transition and cursor channel attachment settle
    // before measuring cursor-only movement against a stable display generation.
    qemu.command("clock_step 100000000");
    std::thread::sleep(Duration::from_millis(100));
    cpu.poll();
    let display = cpu.display_mut().unwrap();
    let epoch = display.epoch();
    let generation = display.frame_counter();
    for i in 1..=1000u32 {
        qemu.write(0x10a4, i);
        qemu.write(0x10a8, 1000 - i);
        qemu.write(0x10b0, i + 1);
        qemu.write(0x10b4, 2);
    }
    let moved = await_cursor(&mut cpu, |state| (state.x, state.y) == (1000, 0));
    assert!(
        Arc::ptr_eq(&shape, moved.shape.as_ref().unwrap()),
        "moves retain one owned shape"
    );
    assert_eq!(
        shape.pixels, pairs,
        "guest DMA overwrite cannot mutate accepted cursor"
    );
    // Advance the display clock so delayed dirty publication would be observable.
    qemu.command("clock_step 100000000");
    std::thread::sleep(Duration::from_millis(100));
    cpu.poll();
    let display = cpu.display_mut().unwrap();
    let after_epoch = display.epoch();
    let after = display.acquire_frame().unwrap();
    assert_eq!(
        (after_epoch, after.generation),
        (epoch, generation),
        "cursor-only input must not dirty/publish the desktop"
    );
    assert!(after
        .bytes()
        .as_chunks::<4>()
        .0
        .iter()
        .all(|p| p[0..3] == [0x30; 3]));
    qemu.write(0x10ac, 2);
    qemu.write(0x10b0, 1002);
    qemu.write(0x10b4, 2);
    let hidden = await_cursor(&mut cpu, |state| state.enabled && !state.visible);
    assert!(Arc::ptr_eq(&shape, hidden.shape.as_ref().unwrap()));
    drop(qemu);
    drop(cpu);
    std::fs::remove_dir_all(directory).unwrap();
}
