//! GPU secondary-color addition after texture modulation, with preserved state.
use super::textures::{call, data_call, query, query_capacity, receive, words};
use super::*;

const SECONDARY: u32 = 0x7fe;
const CURRENT_SECONDARY: u32 = 0x8459;
const COLOR_SUM: u32 = 0x8458;
const LIGHT_CONTROL: u32 = 0x81f8;

fn submit(qemu: &mut Qemu, sequence: &mut u32, records: &[(u32, Vec<u32>)]) {
    *sequence += 1;
    qemu.batch(*sequence, records);
    qemu.await_completion(*sequence);
}

fn quad(secondary: Option<[f32; 3]>) -> Vec<(u32, Vec<u32>)> {
    let mut out = vec![call(0x01a, &[7])];
    if let Some(rgb) = secondary {
        // SecondaryColor is legal within Begin/End, including before a vertex.
        out.push(call(SECONDARY, &rgb.map(f32::to_bits)));
    }
    for (x, y) in [(-1f32, -1f32), (1., -1.), (1., 1.), (-1., 1.)] {
        out.push(call(0x9dc, &[x.to_bits(), y.to_bits()]));
    }
    out.push(call(0x216, &[]));
    out
}

fn packed(secondary: Option<[f32; 3]>) -> Vec<u8> {
    let mut bytes = Vec::new();
    for (x, y) in [(-1f32, -1f32), (1., -1.), (1., 1.), (-1., 1.)] {
        for value in [x, y, 0., 1., 1., 0., 0., 0.5, 0., 0., 1., 0., 0., 0., 1.] {
            bytes.extend_from_slice(&value.to_le_bytes());
        }
        bytes.extend_from_slice(&0u32.to_le_bytes());
        if let Some(rgb) = secondary {
            for value in rgb {
                bytes.extend_from_slice(&value.to_le_bytes());
            }
            bytes.extend_from_slice(&0u32.to_le_bytes());
        }
    }
    bytes
}

fn pixels(qemu: &mut Qemu, sequence: &mut u32, rgba: [u8; 4]) {
    let (_, result) = query(qemu, sequence, 0x7a4, &[0, 0, 8 | (8 << 16)]);
    assert_eq!(result, rgba.repeat(64));
}

#[test]
#[ignore = "requires native host GPU and secondary-color QEMU producer; diskless acceptance"]
fn qemu_secondary_color_modulates_then_adds_preserving_alpha_and_context_state() {
    let (device, queue, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!("dg-sec-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("g.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let (wake, ready) = mpsc::sync_channel(1);
    server.set_callback(Arc::new(move |_| {
        let _ = wake.try_send(());
    }));
    let mut qemu = Qemu::start(&directory, &socket);
    let mut sequence = 0;
    qemu.write(0x1130, SECONDARY);
    assert_eq!(qemu.read(0x1134), 3);
    submit(
        &mut qemu,
        &mut sequence,
        &[(1, vec![0]), (3, vec![8, 8]), (5, vec![])],
    );
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x305, &[CURRENT_SECONDARY]).1),
        [0; 4]
    );
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x329, &[COLOR_SUM]).1),
        [0]
    );
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x329, &[LIGHT_CONTROL]).1),
        [0x81f9]
    );
    // White primary * green texture, then blue secondary. Alpha stays128.
    let mut records = vec![
        call(0x04e, &[0x0de1, 17]),
        call(0x8df, &[0x0de1, 0x2801, 0x2600]),
        call(0x8df, &[0x0de1, 0x2800, 0x2600]),
        data_call(
            0x8d4,
            &[0x0de1, 0, 0x1908, 1, 1, 0, 0x1908, 0x1401],
            &[0, 255, 0, 128],
        ),
        call(0x209, &[0x0de1]),
        call(0x209, &[COLOR_SUM]),
        call(0x0db, &[1f32.to_bits(); 4]),
    ];
    records.extend(quad(Some([0., 0., 1.])));
    submit(&mut qemu, &mut sequence, &records);
    pixels(&mut qemu, &mut sequence, [0, 255, 255, 128]);
    let mut records = vec![call(0x1c5, &[COLOR_SUM])];
    records.extend(quad(None));
    submit(&mut qemu, &mut sequence, &records);
    pixels(&mut qemu, &mut sequence, [0, 255, 0, 128]);
    // Typed CURRENT_SECONDARY_COLOR returns four components, with alpha0
    // even on Apple's legacy GL whose raw query incorrectly reports alpha1.
    for (function, size) in [(0x305, 4), (0x329, 4), (0x2fb, 8), (0x2cb, 1)] {
        let (_, value) = query_capacity(
            &mut qemu,
            &mut sequence,
            function,
            &[CURRENT_SECONDARY],
            4 * size,
        );
        assert_eq!(value.len(), (4 * size) as usize);
        assert!(value[3 * size as usize..].iter().all(|byte| *byte == 0));
    }
    // Array-secondary blue differs from current-secondary green. Each array
    // draw must restore current state; the old64-byte wire shape still works.
    submit(
        &mut qemu,
        &mut sequence,
        &[
            call(0x1c5, &[0x0de1]),
            call(0x209, &[COLOR_SUM]),
            call(SECONDARY, &[0, 1f32.to_bits(), 0]),
            data_call(0x1d5, &[7, 0, 4, 19], &packed(Some([0., 0., 1.]))),
        ],
    );
    pixels(&mut qemu, &mut sequence, [255, 0, 255, 128]);
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x305, &[CURRENT_SECONDARY]).1),
        [0, 1f32.to_bits(), 0, 0]
    );
    submit(
        &mut qemu,
        &mut sequence,
        &[data_call(0x1d5, &[7, 0, 4, 3], &packed(None))],
    );
    pixels(&mut qemu, &mut sequence, [255, 255, 0, 128]);
    for (ty, size) in [(0x1401, 1), (0x1403, 2), (0x1405, 4)] {
        let mut data = packed(Some([0., 0., 1.]));
        for index in [0u32, 1, 2, 0, 2, 3] {
            data.extend_from_slice(&index.to_le_bytes()[..size]);
        }
        submit(
            &mut qemu,
            &mut sequence,
            &[data_call(0x1e6, &[4, 6, ty, 4, 19], &data)],
        );
        pixels(&mut qemu, &mut sequence, [255, 0, 255, 128]);
    }
    // Server attribute stack restores current secondary, enable and lighting
    // model; a distinct context starts from defaults and does not alias them.
    submit(
        &mut qemu,
        &mut sequence,
        &[
            call(0x775, &[1 | 0x40 | 0x2000]),
            call(SECONDARY, &[1f32.to_bits(), 0, 0]),
            call(0x1c5, &[COLOR_SUM]),
            call(0x4ed, &[LIGHT_CONTROL, (0x81fa as f32).to_bits()]),
        ],
    );
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x329, &[LIGHT_CONTROL]).1),
        [0x81fa]
    );
    submit(&mut qemu, &mut sequence, &[call(0x6af, &[])]);
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x305, &[CURRENT_SECONDARY]).1),
        [0, 1f32.to_bits(), 0, 0]
    );
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x329, &[COLOR_SUM]).1),
        [1]
    );
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x329, &[LIGHT_CONTROL]).1),
        [0x81f9]
    );
    submit(
        &mut qemu,
        &mut sequence,
        &[
            call(0x775, &[0x80]),
            call(0x1c5, &[COLOR_SUM]),
            call(0x6af, &[]),
        ],
    );
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x329, &[COLOR_SUM]).1),
        [1]
    );
    // Separate specular is the dependency of EXT_secondary_color: a black
    // texture modulates primary lighting, while separate white specular is
    // added afterward even with the explicit COLOR_SUM switch disabled.
    let floats = |v: [f32; 4]| v.into_iter().flat_map(f32::to_le_bytes).collect::<Vec<_>>();
    let mut lit = vec![
        call(0x775, &[1 | 0x40 | 0x2000 | 0x40000]),
        call(0x04e, &[0x0de1, 18]),
        call(0x8df, &[0x0de1, 0x2801, 0x2600]),
        call(0x8df, &[0x0de1, 0x2800, 0x2600]),
        data_call(
            0x8d4,
            &[0x0de1, 0, 0x1908, 1, 1, 0, 0x1908, 0x1401],
            &[0, 0, 0, 255],
        ),
        call(0x209, &[0x0de1]),
        call(0x209, &[0x0b50]),
        call(0x209, &[0x4000]),
        call(0x1c5, &[COLOR_SUM]),
        call(0x63e, &[0, 0, 1f32.to_bits()]),
        data_call(0x4ee, &[0x0b53], &floats([0.; 4])),
        data_call(0x537, &[0x0408, 0x1200], &floats([0.; 4])),
        data_call(0x537, &[0x0408, 0x1201], &floats([0., 0., 0., 1.])),
        data_call(0x537, &[0x0408, 0x1202], &floats([1.; 4])),
        call(0x4ed, &[LIGHT_CONTROL, (0x81fa as f32).to_bits()]),
    ];
    lit.extend(quad(None));
    submit(&mut qemu, &mut sequence, &lit);
    pixels(&mut qemu, &mut sequence, [255; 4]);
    let mut single = vec![call(0x4ed, &[LIGHT_CONTROL, (0x81f9 as f32).to_bits()])];
    single.extend(quad(None));
    submit(&mut qemu, &mut sequence, &single);
    pixels(&mut qemu, &mut sequence, [0, 0, 0, 255]);
    submit(&mut qemu, &mut sequence, &[call(0x6af, &[])]);
    sequence += 1;
    let mut records = vec![
        (1, vec![0]),
        (3, vec![8, 8]),
        (5, vec![]),
        call(0x209, &[COLOR_SUM]),
        call(0x0db, &[1f32.to_bits(), 0, 0, 1f32.to_bits()]),
    ];
    records.extend(quad(None));
    records.push((7, vec![]));
    qemu.batch_for(sequence, 1, 2, 2, 0, &records);
    qemu.await_completion(sequence);
    assert_gpu_image(&device, &queue, &receive(&server, &ready, 1, 2), |_, _| {
        [0, 0, 255, 255]
    });
    assert_eq!(
        words(&query(&mut qemu, &mut sequence, 0x305, &[CURRENT_SECONDARY]).1),
        [0, 1f32.to_bits(), 0, 0]
    );
    assert_eq!(words(&query(&mut qemu, &mut sequence, 0x2fc, &[]).1), [0]);
    // Secondary padding is never interpreted as data, even after all valid
    // pixels. The rejected immutable packet must not execute.
    let mut bad = packed(Some([0., 0., 1.]));
    bad[76] = 1;
    sequence += 1;
    qemu.batch(sequence, &[data_call(0x1d5, &[7, 0, 4, 19], &bad)]);
    let deadline = Instant::now() + Duration::from_secs(5);
    while qemu.read(0x111c) & 4 == 0 {
        assert!(Instant::now() < deadline);
        std::thread::sleep(Duration::from_millis(1));
    }
    assert_eq!(qemu.read(0x1124), 1);
    drop(qemu);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}
