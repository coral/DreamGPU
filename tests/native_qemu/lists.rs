// SPDX-License-Identifier: GPL-2.0-or-later
//! Logical display lists replay through actual resource ownership and native GL.
use super::textures::{call, data_call, query, query_capacity, words};
use super::*;
const NEW: u32 = 1592;
const END: u32 = 539;
const CALL: u32 = 146;
const GEN: u32 = 671;
const IS: u32 = 1219;
const DELETE: u32 = 409;
fn submit(q: &mut Qemu, s: &mut u32, r: &[(u32, Vec<u32>)]) {
    *s += 1;
    q.batch(*s, r);
    q.await_completion(*s);
}
fn get(q: &mut Qemu, s: &mut u32, f: u32, a: &[u32]) -> Vec<u32> {
    words(&query(q, s, f, a).1)
}
fn new(q: &mut Qemu, s: &mut u32, n: u32, mode: u32) {
    assert_eq!(get(q, s, NEW, &[n, mode, 0]), [0]);
}
fn end(q: &mut Qemu, s: &mut u32) {
    assert_eq!(get(q, s, END, &[0; 3]), [0]);
}
fn clear(q: &mut Qemu, s: &mut u32, color: [f32; 4]) {
    submit(
        q,
        s,
        &[call(163, &color.map(f32::to_bits)), call(154, &[0x4000])],
    );
}
fn pixels(q: &mut Qemu, s: &mut u32) -> Vec<u8> {
    query_capacity(q, s, 1956, &[0, 0, 32 | (32 << 16)], 4096).1
}
fn solid(q: &mut Qemu, s: &mut u32, color: [u8; 4]) {
    let p = pixels(q, s);
    assert!(
        p.as_chunks::<4>().0.iter().all(|v| *v == color),
        "unexpected framebuffer {:?}",
        &p[..16]
    );
}
fn quad() -> Vec<(u32, Vec<u32>)> {
    let mut r = vec![call(26, &[7])];
    for (x, y) in [(-1f32, -1f32), (1., -1.), (1., 1.), (-1., 1.)] {
        r.push(call(2524, &[x.to_bits(), y.to_bits()]));
    }
    r.push(call(534, &[]));
    r
}
fn raster(x: f64, y: f64) -> (u32, Vec<u32>) {
    call(
        1941,
        &[x / 16. - 1., y / 16. - 1., 0., 1.]
            .into_iter()
            .flat_map(|v| {
                let b = v.to_bits();
                [b as u32, (b >> 32) as u32]
            })
            .collect::<Vec<_>>(),
    )
}
#[test]
#[ignore = "requires native GPU; display list compilation and owned replay"]
fn qemu_display_lists_compile_pixels_resources_and_sharing() {
    let (_, _, host) = native_gpu();
    let dir = std::env::temp_dir().join(format!("dglst-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&dir).unwrap();
    let socket = dir.join("g.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let mut q = Qemu::start(&dir, &socket);
    let mut s = 0;
    submit(
        &mut q,
        &mut s,
        &[
            (1, vec![0]),
            (3, vec![32, 32]),
            (5, vec![]),
            call(453, &[0x0bd0]),
        ],
    );
    clear(&mut q, &mut s, [0., 0., 1., 1.]);
    assert_eq!(get(&mut q, &mut s, GEN, &[2, 0, 0]), [0, 1]);
    assert_eq!(query(&mut q, &mut s, IS, &[1, 0, 0]).1, [1]);
    new(&mut q, &mut s, 1, 0x1300);
    submit(
        &mut q,
        &mut s,
        &[call(219, &[1f32.to_bits(), 0, 0, 1f32.to_bits()])],
    );
    submit(&mut q, &mut s, &quad());
    assert_eq!(get(&mut q, &mut s, 809, &[0x0b33]), [1]);
    assert_eq!(get(&mut q, &mut s, 809, &[0x0b30]), [0x1300]);
    end(&mut q, &mut s);
    solid(&mut q, &mut s, [0, 0, 255, 255]);
    assert_eq!(get(&mut q, &mut s, 809, &[0x0b33]), [0]);
    submit(&mut q, &mut s, &[call(CALL, &[1])]);
    solid(&mut q, &mut s, [255, 0, 0, 255]);
    // Dynamic list references resolve the latest definition, rather than expanding
    // its contents while the caller list is compiled.
    new(&mut q, &mut s, 2, 0x1300);
    submit(&mut q, &mut s, &[call(CALL, &[1])]);
    end(&mut q, &mut s);
    new(&mut q, &mut s, 1, 0x1301);
    submit(
        &mut q,
        &mut s,
        &[call(219, &[0, 1f32.to_bits(), 0, 1f32.to_bits()])],
    );
    submit(&mut q, &mut s, &quad());
    end(&mut q, &mut s);
    solid(&mut q, &mut s, [0, 255, 0, 255]);
    clear(&mut q, &mut s, [0., 0., 1., 1.]);
    submit(&mut q, &mut s, &[call(CALL, &[2])]);
    solid(&mut q, &mut s, [0, 255, 0, 255]);
    // Lists may consist only of vertices and be invoked inside a caller Begin.
    new(&mut q, &mut s, 3, 0x1300);
    let vertices = quad();
    submit(&mut q, &mut s, &vertices[1..5]);
    end(&mut q, &mut s);
    submit(
        &mut q,
        &mut s,
        &[
            call(219, &[1f32.to_bits(), 0, 0, 1f32.to_bits()]),
            call(26, &[7]),
            call(CALL, &[3]),
            call(534, &[]),
        ],
    );
    solid(&mut q, &mut s, [255, 0, 0, 255]);
    // Errors in GL_COMPILE are deferred until each list execution.
    new(&mut q, &mut s, 4, 0x1300);
    submit(&mut q, &mut s, &[call(521, &[u32::MAX])]);
    assert_eq!(get(&mut q, &mut s, 764, &[]), [0]);
    submit(&mut q, &mut s, &[call(4095, &[0x501])]);
    assert_eq!(get(&mut q, &mut s, 764, &[]), [0]);
    end(&mut q, &mut s);
    submit(&mut q, &mut s, &[call(CALL, &[4])]);
    let first = get(&mut q, &mut s, 764, &[])[0];
    let second = get(&mut q, &mut s, 764, &[])[0];
    assert!(first != second && [0x500, 0x501].contains(&first) && [0x500, 0x501].contains(&second));
    assert_eq!(get(&mut q, &mut s, 764, &[]), [0]);
    // ListBase is native state; CallLists captures that base once for the array.
    new(&mut q, &mut s, 11, 0x1300);
    submit(&mut q, &mut s, &[call(1274, &[50]), call(CALL, &[1])]);
    end(&mut q, &mut s);
    new(&mut q, &mut s, 12, 0x1300);
    submit(
        &mut q,
        &mut s,
        &[call(219, &[1f32.to_bits(), 0, 0, 1f32.to_bits()])],
    );
    submit(&mut q, &mut s, &quad());
    end(&mut q, &mut s);
    submit(
        &mut q,
        &mut s,
        &[
            call(1274, &[10]),
            data_call(
                147,
                &[2],
                &[1u32, 2]
                    .into_iter()
                    .flat_map(u32::to_le_bytes)
                    .collect::<Vec<_>>(),
            ),
        ],
    );
    solid(&mut q, &mut s, [255, 0, 0, 255]);
    assert_eq!(get(&mut q, &mut s, 809, &[0x0b32]), [50]);
    // Texture image bytes are captured at compile, while the logical bind and
    // allocation occur at replay. Deletion/recreation must not use stale GL names.
    new(&mut q, &mut s, 5, 0x1300);
    let mut tex = [255u8, 0, 255, 255];
    submit(
        &mut q,
        &mut s,
        &[
            call(78, &[0x0de1, 77]),
            data_call(2260, &[0x0de1, 0, 0x1908, 1, 1, 0, 0x1908, 0x1401], &tex),
        ],
    );
    tex.fill(0);
    end(&mut q, &mut s);
    assert_eq!(get(&mut q, &mut s, 809, &[0x8069]), [0]);
    submit(&mut q, &mut s, &[call(CALL, &[5])]);
    assert_eq!(get(&mut q, &mut s, 809, &[0x8069]), [77]);
    assert_eq!(
        query(&mut q, &mut s, 1046, &[0x0de1, 0, 0x1000]).1,
        1u32.to_le_bytes()
    );
    submit(
        &mut q,
        &mut s,
        &[data_call(432, &[1], &77u32.to_le_bytes()), call(CALL, &[5])],
    );
    assert_eq!(get(&mut q, &mut s, 809, &[0x8069]), [77]);
    assert_eq!(
        &query(&mut q, &mut s, 0x414, &[0x0de1, 0, 0]).1[..4],
        &[255, 0, 255, 255]
    );
    // Canonical array payloads retain client data, including the final current
    // color. Replaying does not dereference any guest pointer.
    new(&mut q, &mut s, 7, 0x1300);
    let mut array = Vec::new();
    for (x, y) in [(-1f32, -1f32), (1., -1.), (1., 1.), (-1., 1.)] {
        for v in [x, y, 0., 1., 0., 1., 1., 1., 0., 0., 1., 0., 0., 0., 1.] {
            array.extend(v.to_le_bytes());
        }
        array.extend(0u32.to_le_bytes());
    }
    submit(&mut q, &mut s, &[data_call(0x1d5, &[7, 0, 4, 3], &array)]);
    array.fill(0);
    end(&mut q, &mut s);
    submit(&mut q, &mut s, &[call(CALL, &[7])]);
    solid(&mut q, &mut s, [0, 255, 255, 255]);
    // Bitmap's fractional raster movement occurs once per replay, never while
    // compiling or per transport fragment.
    new(&mut q, &mut s, 8, 0x1300);
    let mut bitmap = Vec::new();
    for v in [0f32, 0., 1.25, 0.5] {
        bitmap.extend(v.to_le_bytes());
    }
    bitmap.push(0x80);
    submit(
        &mut q,
        &mut s,
        &[data_call(103, &[1, 1, 0x1900, 0x1a00, 1, 0, 3, 2], &bitmap)],
    );
    end(&mut q, &mut s);
    submit(
        &mut q,
        &mut s,
        &[raster(8., 8.), call(CALL, &[8]), call(CALL, &[8])],
    );
    let raster_value = words(&query(&mut q, &mut s, 773, &[0x0b07]).1)
        .into_iter()
        .map(f32::from_bits)
        .collect::<Vec<_>>();
    assert_eq!(&raster_value[..2], &[10.5, 9.]);
    // Replaying an immutable DrawPixels twice creates fresh private image
    // transactions without consuming or rewinding external stream IDs.
    new(&mut q, &mut s, 6, 0x1300);
    submit(
        &mut q,
        &mut s,
        &[
            raster(0., 0.),
            data_call(
                498,
                &[32, 32, 0x1908, 0x1401, 4096, 0, 3, 1],
                &[255, 255, 0, 255].repeat(1024),
            ),
        ],
    );
    end(&mut q, &mut s);
    for _ in 0..2 {
        clear(&mut q, &mut s, [0., 0., 1., 1.]);
        submit(&mut q, &mut s, &[call(CALL, &[6])]);
        solid(&mut q, &mut s, [255, 255, 0, 255]);
    }
    submit(
        &mut q,
        &mut s,
        &[
            raster(0., 0.),
            data_call(
                498,
                &[1, 1, 0x1908, 0x1401, 4, 0, 3, 1],
                &[0, 255, 255, 255],
            ),
        ],
    );
    assert_eq!(get(&mut q, &mut s, DELETE, &[4, 1, 0]), [0]);
    assert_eq!(query(&mut q, &mut s, IS, &[4, 0, 0]).1, [0]);
    submit(&mut q, &mut s, &[call(CALL, &[4])]);
    // Shared contexts retain the same namespace after the creator is destroyed.
    s += 1;
    q.batch_for(
        s,
        1,
        2,
        1,
        0,
        &[(1, vec![1]), (5, vec![]), call(CALL, &[1])],
    );
    q.await_completion(s);
    s += 1;
    q.batch_for(s, 1, 1, 1, 0, &[(2, vec![])]);
    q.await_completion(s);
    s += 1;
    q.batch_for(s, 1, 2, 1, 0, &[call(CALL, &[1])]);
    q.await_completion(s);
    // Retire the final context; all list nodes, texture references and allocations
    // must be released before reset can complete.
    s += 1;
    q.batch_for(s, 1, 2, 1, 0, &[(2, vec![]), (4, vec![])]);
    q.await_completion(s);
    q.write(0x1108, 2);
    assert!(server.take_error().is_none());
    drop(q);
    drop(server);
    let _ = std::fs::remove_dir_all(dir);
}
