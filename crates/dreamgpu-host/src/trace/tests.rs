use super::*;

/// A growable sink; `fail_after` makes writes past that many bytes fail.
#[derive(Default)]
struct Sink {
    bytes: Vec<u8>,
    writes: usize,
    fail_after: Option<usize>,
}
unsafe extern "C" fn sink(opaque: *mut c_void, data: *const u8, n: usize) -> u32 {
    let s = unsafe { &mut *opaque.cast::<Sink>() };
    s.writes += 1;
    if s.fail_after.is_some_and(|limit| s.bytes.len() + n > limit) {
        return 1;
    }
    s.bytes
        .extend_from_slice(unsafe { core::slice::from_raw_parts(data, n) });
    0
}
fn trace(s: &mut Sink) -> Trace {
    Trace {
        opaque: (s as *mut Sink).cast(),
        write: Some(sink),
        ..Trace::default()
    }
}
fn text(s: &Sink) -> String {
    String::from_utf8(s.bytes.clone()).unwrap()
}
/// Lines after the two header lines.
fn body(s: &Sink) -> Vec<String> {
    text(s).lines().skip(2).map(str::to_owned).collect()
}

fn record(op: u32, flags: u32, words: &[u32]) -> Vec<u8> {
    let size = 32 + words.len() as u32 * 4;
    let mut r = Vec::new();
    for w in [op, size, 1, 2, 3, flags, 0, 7].iter().chain(words) {
        r.extend_from_slice(&w.to_le_bytes());
    }
    r
}
fn call(f: u32, words: &[u32]) -> Vec<u8> {
    let mut w = vec![f];
    w.extend_from_slice(words);
    record(DG_GL_CALL, 0, &w)
}
fn data_call(f: u32, args: &[u32], payload: &[u8]) -> Vec<u8> {
    let mut r = record(DG_GL_DATA_CALL, 0, &[f, payload.len() as u32]);
    for w in args {
        r.extend_from_slice(&w.to_le_bytes());
    }
    r.extend_from_slice(payload);
    r.resize(r.len().next_multiple_of(4), 0);
    let size = r.len() as u32;
    r[4..8].copy_from_slice(&size.to_le_bytes());
    r
}
fn present() -> Vec<u8> {
    record(DG_GL_PRESENT, 0, &[])
}
fn floats(v: &[f32]) -> Vec<u8> {
    v.iter().flat_map(|f| f.to_le_bytes()).collect()
}
/// Formats one record inside an active capture and returns its lines.
fn format_one(r: &[u8], error: u32, flags: u32) -> Vec<String> {
    let mut s = Sink::default();
    let mut t = trace(&mut s);
    assert_eq!(begin(&mut t, 2, flags, 0), 0);
    record_into(&mut t, &present(), 0);
    let before = s.bytes.len();
    record_into(&mut t, r, error);
    String::from_utf8(s.bytes[before..].to_vec())
        .unwrap()
        .lines()
        .map(str::to_owned)
        .collect()
}
fn record_into(t: &mut Trace, r: &[u8], error: u32) -> u32 {
    unsafe { dreamgpu_trace_record(t, r.as_ptr(), r.len() as u32, 9, 4, error) }
}

#[test]
fn disabled_by_default_formats_and_writes_nothing() {
    // QEMU zero-allocates its engine; an unattached or never-armed trace is idle.
    let mut s = Sink::default();
    let mut t = trace(&mut s);
    assert_eq!(Trace::default().state, IDLE);
    for r in [present(), call(FEnum_glEnable, &[GL_LIGHTING]), present()] {
        assert_eq!(record_into(&mut t, &r, 0), IDLE);
    }
    unsafe { dreamgpu_trace_end(&mut t, END_CANCELLED) };
    assert_eq!((t.state, t.records, t.bytes), (IDLE, 0, 0));
    assert_eq!(s.writes, 0);
    let mut zeroed: Trace = unsafe { core::mem::zeroed() };
    assert_eq!(record_into(&mut zeroed, &present(), 0), IDLE);
    assert_eq!(
        unsafe { dreamgpu_trace_record(core::ptr::null_mut(), core::ptr::null(), 0, 0, 0, 0) },
        IDLE
    );
}

#[test]
fn capture_starts_at_a_present_and_ends_after_the_requested_frames() {
    let mut s = Sink::default();
    let mut t = trace(&mut s);
    assert_eq!(begin(&mut t, 2, 0, 0), 0);
    assert_eq!(t.state, ARMED);
    let header = s.bytes.len();
    // Mid-frame records before the first boundary are not written.
    assert_eq!(
        record_into(&mut t, &call(FEnum_glEnable, &[GL_FOG]), 0),
        ARMED
    );
    assert_eq!(record_into(&mut t, &present(), 0), ACTIVE);
    assert_eq!(s.bytes.len(), header);
    assert_eq!(
        record_into(&mut t, &call(FEnum_glEnable, &[GL_LIGHTING]), 0),
        ACTIVE
    );
    assert_eq!(record_into(&mut t, &present(), 0), ACTIVE);
    assert_eq!(t.completed, 1);
    assert_eq!(
        record_into(&mut t, &call(FEnum_glDisable, &[GL_LIGHTING]), 0),
        ACTIVE
    );
    let written = s.bytes.len() as u64;
    assert_eq!(record_into(&mut t, &present(), 0), DONE);
    assert_eq!((t.reason, t.completed, t.records), (END_FRAMES, 2, 4));
    let end = s.bytes.len();
    // Later records, presents and a cancellation change nothing.
    for r in [call(FEnum_glEnable, &[GL_LIGHTING]), present()] {
        assert_eq!(record_into(&mut t, &r, 0), DONE);
    }
    unsafe { dreamgpu_trace_end(&mut t, END_CANCELLED) };
    assert_eq!((s.bytes.len(), t.records, t.reason), (end, 4, END_FRAMES));
    let lines = text(&s);
    assert!(
        lines.starts_with("# dreamgpu frame trace 1: frames=2 flags=0x0 limit=67108864\n# frame")
    );
    let present_bytes = written + "2 seq=9 stream=4 ctx=1:2:3 PRESENT - flags=0x0\n".len() as u64;
    assert_eq!(
        body(&s),
        [
            "1 seq=9 stream=4 ctx=1:2:3 CALL glEnable 0xb50=GL_LIGHTING".to_owned(),
            "1 seq=9 stream=4 ctx=1:2:3 PRESENT - flags=0x0".to_owned(),
            "2 seq=9 stream=4 ctx=1:2:3 CALL glDisable 0xb50=GL_LIGHTING".to_owned(),
            "2 seq=9 stream=4 ctx=1:2:3 PRESENT - flags=0x0".to_owned(),
            format!("# end reason=frames frames=2 records=4 bytes={present_bytes}"),
        ]
    );
    // A finished trace can be armed again with the same sink.
    assert_eq!(begin(&mut t, 1, 0, 0), 0);
    assert_eq!(t.state, ARMED);
}

#[test]
fn formats_scalar_calls_by_argument_kind() {
    let matrix = [
        1.0f32, 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0.5, 0., -5., 1.,
    ];
    let depth = 0.75f64.to_bits();
    for (r, expected) in [
        (
            call(FEnum_glTexParameteri, &[GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR]),
            "CALL glTexParameteri 0xde1=GL_TEXTURE_2D 0x2801=GL_TEXTURE_MIN_FILTER 0x2601=GL_LINEAR",
        ),
        (
            call(FEnum_glMatrixMode, &[GL_MODELVIEW]),
            "CALL glMatrixMode 0x1700=GL_MODELVIEW",
        ),
        (
            call(FEnum_glLoadMatrixf, &matrix.map(f32::to_bits)),
            "CALL glLoadMatrixf 1 0 0 0 0 1 0 0 0 0 1 0 0.5 0 -5 1",
        ),
        (
            call(FEnum_glColorMaterial, &[GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE]),
            "CALL glColorMaterial 0x408=GL_FRONT_AND_BACK 0x1602=GL_AMBIENT_AND_DIFFUSE",
        ),
        (
            call(FEnum_glClearDepth, &[depth as u32, (depth >> 32) as u32]),
            "CALL glClearDepth 0.75",
        ),
        (
            call(FEnum_glBlendFunc, &[GL_ONE, GL_ONE_MINUS_SRC_ALPHA]),
            "CALL glBlendFunc 0x1=GL_ONE 0x303=GL_ONE_MINUS_SRC_ALPHA",
        ),
        (
            call(FEnum_glBegin, &[GL_TRIANGLES]),
            "CALL glBegin 0x4=GL_TRIANGLES",
        ),
        (
            call(FEnum_glColor4f, &[1e-9f32.to_bits(), 3e9f32.to_bits(), f32::NAN.to_bits(), (-0.0f32).to_bits()]),
            "CALL glColor4f 1e-9 3e9 NaN -0",
        ),
        (
            call(FEnum_glViewport, &[0, (-2i32) as u32, 640, 480]),
            "CALL glViewport 0 -2 640 480",
        ),
        (
            call(FEnum_glClear, &[GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT]),
            "CALL glClear 0x4100",
        ),
        (
            record(DG_GL_QUERY, 0, &[FEnum_glGetIntegerv, GL_VIEWPORT, 0, 0]),
            "QUERY glGetIntegerv 0xba2=GL_VIEWPORT 0x0 0x0",
        ),
    ] {
        assert_eq!(
            format_one(&r, 0, 0),
            [format!("1 seq=9 stream=4 ctx=1:2:3 {expected}")]
        );
    }
}

#[test]
fn formats_payloads_lifecycle_presents_and_errors() {
    let pixels: Vec<u8> = (0..40).collect();
    let program = b"!!ARBfp1.0\nMOV result.color, \"x\";\nEND";
    for (r, error, expected) in [
        (
            data_call(
                FEnum_glLightfv,
                &[GL_LIGHT0, GL_DIFFUSE],
                &floats(&[1., 0.5, 0.25, 1.]),
            ),
            0,
            "DATA_CALL glLightfv 0x4000=GL_LIGHT0 0x1201=GL_DIFFUSE data=16 [1 0.5 0.25 1]",
        ),
        (
            data_call(
                FEnum_glLightModelfv,
                &[GL_LIGHT_MODEL_AMBIENT],
                &floats(&[0.2, 0.2, 0.2, 1.]),
            ),
            0,
            "DATA_CALL glLightModelfv 0xb53=GL_LIGHT_MODEL_AMBIENT data=16 [0.2 0.2 0.2 1]",
        ),
        (
            data_call(FEnum_glDeleteTextures, &[2], &[5, 0, 0, 0, 9, 0, 0, 0]),
            0,
            "DATA_CALL glDeleteTextures 2 data=8 [5 9]",
        ),
        (
            data_call(
                FEnum_glTexSubImage2D,
                &[GL_TEXTURE_2D, 0, 0, 0, 10, 1, GL_RGBA, GL_UNSIGNED_BYTE],
                &pixels,
            ),
            0,
            "DATA_CALL glTexSubImage2D 0xde1=GL_TEXTURE_2D 0 0 0 10 1 0x1908=GL_RGBA \
             0x1401=GL_UNSIGNED_BYTE data=40 \
             hex=000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f..",
        ),
        (
            data_call(
                FEnum_glProgramStringARB,
                &[
                    GL_FRAGMENT_PROGRAM_ARB,
                    GL_PROGRAM_FORMAT_ASCII_ARB,
                    program.len() as u32,
                ],
                program,
            ),
            0,
            "DATA_CALL glProgramStringARB 0x8804=GL_FRAGMENT_PROGRAM_ARB \
             0x8875=GL_PROGRAM_FORMAT_ASCII_ARB 37 data=37 \
             text=\"!!ARBfp1.0\\x0aMOV result.color, \\\"x\\\";\\x0aEND\"",
        ),
        (call(FEnum_glEnd, &[]), 4, "CALL glEnd err=4"),
        (call(2000, &[]), 0, "CALL fn2000"),
        (record(DG_GL_MAKE_CURRENT, 0, &[]), 0, "MAKE_CURRENT -"),
        (
            record(DG_GL_CREATE_DRAWABLE, 0, &[640, 480]),
            0,
            "CREATE_DRAWABLE - 640x480",
        ),
        (
            record(
                DG_GL_PRESENT,
                DG_GL_PRESENT_EXCLUSIVE | DG_GL_PRESENT_BOUNDED,
                &[800, 600],
            ),
            0,
            "PRESENT - flags=0x11,exclusive,bounded 800x600",
        ),
    ] {
        assert_eq!(
            format_one(&r, error, 0),
            [format!("1 seq=9 stream=4 ctx=1:2:3 {expected}")]
        );
    }
}

/// Indexed client draw (ARB_vertex_buffer_object buffer draw with no buffer
/// slots): FLOAT*3 positions, UNSIGNED_BYTE*4 colours, FLOAT*3 normals.
fn buffer_draw() -> Vec<u8> {
    let mut data = vec![0u8; 128];
    for (slot, descriptor) in [
        (0, GL_FLOAT | (3 << 16)),
        (1, GL_UNSIGNED_BYTE | (4 << 16)),
        (2, GL_FLOAT | (3 << 16)),
    ] {
        data[slot * 4..slot * 4 + 4].copy_from_slice(&u32::to_le_bytes(descriptor));
    }
    data.extend(floats(&[0., 0., 0., 1., 0., 0., 0., 1., 0.]));
    data.extend([255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 128]);
    data.extend(floats(&[0., 0., 1., 0., 0., -1., 0.5, 0.5, 0.]));
    data.extend([12, 0, 10, 0, 11, 0]);
    data.resize(data.len().next_multiple_of(4), 0);
    let mask = DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION | DG_GL_ARRAY_COLOR | DG_GL_ARRAY_NORMAL;
    let a = [GL_TRIANGLES, 10, 3, mask, 0, 0, 3, GL_UNSIGNED_SHORT, 0, 0];
    // The record must be one the engine admits, indices within the vertices.
    assert!(raw::layout_buffer_draw(FEnum_glDrawRangeElements, &a, &data, true).is_some());
    data_call(FEnum_glDrawRangeElements, &a, &data)
}

#[test]
fn draws_show_their_layout_and_optionally_every_client_vertex() {
    let r = buffer_draw();
    let summary = "1 seq=9 stream=4 ctx=1:2:3 DATA_CALL glDrawRangeElements mode=0x4=GL_TRIANGLES \
                   base=10 count=3 mask=0x80000007 generic=0x0 buffered=0x0 vertices=3 \
                   type=0x1403=GL_UNSIGNED_SHORT element=0 offset=0 data=220 \
                   layout=pos:FLOAT*3,col:UNSIGNED_BYTE*4,nrm:FLOAT*3 \
                   hex=0614030001140400061403000000000000000000000000000000000000000000..";
    assert_eq!(format_one(&r, 0, 0), [summary]);
    assert_eq!(
        format_one(&r, 0, VERTICES),
        [
            summary,
            "  v10 pos=(0 0 0) col=(255 0 0 255) nrm=(0 0 1)",
            "  v11 pos=(1 0 0) col=(0 255 0 255) nrm=(0 0 -1)",
            "  v12 pos=(0 1 0) col=(0 0 255 128) nrm=(0.5 0.5 0)",
            "  idx0: 12 10 11",
        ]
    );
    // The original fixed vertex: position and normal of one float vertex.
    let mut vertex = floats(&[1., 2., 3., 1., 0., 0., 0., 0., 0., 1., 0.]);
    vertex.resize(64, 0);
    let fixed = data_call(
        FEnum_glDrawArrays,
        &[GL_POINTS, 0, 1, DG_GL_ARRAY_POSITION | DG_GL_ARRAY_NORMAL],
        &vertex,
    );
    let lines = format_one(&fixed, 0, VERTICES);
    assert!(
        lines[0].starts_with(
            "1 seq=9 stream=4 ctx=1:2:3 DATA_CALL glDrawArrays mode=0x0=GL_POINTS first=0 \
             count=1 mask=0x5 data=64 layout=pos:FLOAT*4,nrm:FLOAT*3 hex=0000803f"
        ),
        "{}",
        lines[0]
    );
    assert_eq!(lines[1..], ["  v0 pos=(1 2 3 1) nrm=(0 1 0)"]);
}

#[test]
fn byte_limit_bounds_the_file_and_ends_the_capture() {
    let mut s = Sink::default();
    let mut t = trace(&mut s);
    assert_eq!(begin(&mut t, MAX_FRAMES, VERTICES, MIN_LIMIT), 0);
    record_into(&mut t, &present(), 0);
    let draw = buffer_draw();
    let mut records = 0;
    while record_into(&mut t, &draw, 0) == ACTIVE {
        records += 1;
        assert!(records < 100);
    }
    assert_eq!((t.state, t.reason), (DONE, END_LIMIT));
    assert_eq!(t.bytes, s.bytes.len() as u64);
    assert!(s.bytes.len() as u64 <= MIN_LIMIT + TRAILER);
    let all = text(&s);
    let last = all.lines().last().unwrap();
    assert_eq!(
        last,
        format!(
            "# end reason=limit frames=0 records={} bytes={MIN_LIMIT}",
            records + 1
        )
    );
}

#[test]
fn failed_writes_and_cancellation_end_the_capture() {
    let mut s = Sink {
        fail_after: Some(300),
        ..Sink::default()
    };
    let mut t = trace(&mut s);
    assert_eq!(begin(&mut t, 5, 0, 0), 0);
    record_into(&mut t, &present(), 0);
    let lighting = call(FEnum_glEnable, &[GL_LIGHTING]);
    while record_into(&mut t, &lighting, 0) == ACTIVE {}
    assert_eq!((t.state, t.reason), (DONE, END_WRITE));
    assert!(s.bytes.len() <= 300);

    let mut s = Sink {
        fail_after: Some(10),
        ..Sink::default()
    };
    let mut t = trace(&mut s);
    assert_eq!(begin(&mut t, 1, 0, 0), 2);
    assert_eq!((t.state, t.reason), (DONE, END_WRITE));

    let mut s = Sink::default();
    let mut t = trace(&mut s);
    assert_eq!(begin(&mut t, 3, 0, 0), 0);
    record_into(&mut t, &present(), 0);
    record_into(&mut t, &lighting, 0);
    let written = s.bytes.len();
    unsafe { dreamgpu_trace_end(&mut t, END_CANCELLED) };
    assert_eq!((t.state, t.reason), (DONE, END_CANCELLED));
    assert_eq!(
        body(&s),
        [
            "1 seq=9 stream=4 ctx=1:2:3 CALL glEnable 0xb50=GL_LIGHTING".to_owned(),
            format!("# end reason=cancelled frames=0 records=1 bytes={written}"),
        ]
    );
}

#[test]
fn begin_rejects_invalid_requests_and_a_running_capture() {
    let mut s = Sink::default();
    let mut t = trace(&mut s);
    for (frames, flags, limit) in [
        (0, 0, 0),
        (MAX_FRAMES + 1, 0, 0),
        (1, 2, 0),
        (1, 0, MIN_LIMIT - 1),
        (1, 0, MAX_LIMIT + 1),
    ] {
        assert_eq!(begin(&mut t, frames, flags, limit), 1);
        assert_eq!(t.state, IDLE);
    }
    assert_eq!(s.writes, 0);
    assert_eq!(
        unsafe { dreamgpu_trace_begin(core::ptr::null_mut(), 1, 0, 0) },
        1
    );
    assert_eq!(begin(&mut t, MAX_FRAMES, VERTICES, MAX_LIMIT), 0);
    assert_eq!(begin(&mut t, 1, 0, 0), 1);
    record_into(&mut t, &present(), 0);
    assert_eq!(begin(&mut t, 1, 0, 0), 1);
}

#[test]
fn every_protocol_function_is_named() {
    for f in 0..=DG_GL_RECORD_ERROR {
        if function_words(f) != u32::MAX {
            assert!(names::function(f).is_some(), "function {f}");
        }
    }
    assert_eq!(
        names::function(FEnum_glDrawRangeElements),
        Some("glDrawRangeElements")
    );
    assert_eq!(names::enumerant(GL_TEXTURE0), Some("GL_TEXTURE0"));
}

#[test]
fn trace_layout_matches_the_c_header() {
    let pointer = core::mem::size_of::<usize>();
    assert_eq!(core::mem::offset_of!(Trace, limit), 24);
    assert_eq!(core::mem::offset_of!(Trace, records), 40);
    assert_eq!(core::mem::offset_of!(Trace, opaque), 48);
    assert_eq!(core::mem::size_of::<Trace>(), 48 + 2 * pointer);
}
