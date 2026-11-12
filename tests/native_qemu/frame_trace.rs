//! Diagnostic frame trace through the device: QOM diagnostic-trace arms a
//! capture that starts at the next presentation, writes the records the
//! render worker executes for the requested frames and ends by itself.
use super::textures::{call, data_call};
use super::*;

const ENABLE: u32 = 521;
const DISABLE: u32 = 453;
const MATRIX_MODE: u32 = 1353;
const LIGHTFV: u32 = 1258;
const DRAW_ARRAYS: u32 = 469;
const CLEAR: u32 = 154;
const NO_EXPORT: u32 = 8;

fn device() -> &'static str {
    "/machine/peripheral-anon/device[0]"
}
/// The QMP reply holding the status JSON string, without spaces.
fn trace_status(qemu: &Qemu) -> String {
    qemu.qmp(
        "qom-get",
        &format!(
            "{{\"path\":\"{}\",\"property\":\"diagnostic-trace\"}}",
            device()
        ),
    )
    .replace(' ', "")
}
fn trace_set(qemu: &Qemu, value: &str) -> String {
    qemu.qmp(
        "qom-set",
        &format!(
            "{{\"path\":\"{}\",\"property\":\"diagnostic-trace\",\"value\":\"{value}\"}}",
            device()
        ),
    )
}
fn frame(qemu: &mut Qemu, sequence: &mut u32, records: &[(u32, Vec<u32>)]) {
    *sequence += 1;
    let mut records = records.to_vec();
    records.push((7, vec![]));
    qemu.batch_for(*sequence, 1, 1, 1, NO_EXPORT, &records);
    qemu.await_completion(*sequence);
}
fn floats(values: &[f32]) -> Vec<u8> {
    values.iter().flat_map(|v| v.to_le_bytes()).collect()
}
/// Lines without their batch sequence and stream fields.
fn lines(path: &Path) -> Vec<String> {
    std::fs::read_to_string(path)
        .unwrap()
        .lines()
        .map(|line| {
            let fields: Vec<&str> = line.split(' ').collect();
            if fields.len() > 3 && fields[1].starts_with("seq=") {
                [&fields[..1], &fields[3..]].concat().join(" ")
            } else {
                line.to_owned()
            }
        })
        .collect()
}

#[test]
#[ignore = "requires native host GPU and the QEMU GL producer; diskless acceptance"]
fn qemu_frame_trace_writes_executed_records_of_the_next_frames() {
    let (_device, _queue, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!("dg-ft-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("g.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let mut qemu = Qemu::start(&directory, &socket);
    let path = directory.join("frames.trace");
    let request = format!("{},2,vertices", path.display());
    // Disabled by default; there is no engine before the first GL batch.
    assert!(trace_status(&qemu).contains(r#"\"state\":\"idle\""#));
    assert!(trace_set(&qemu, &request).contains("\"error\""));
    assert!(!path.exists());

    let mut sequence = 1;
    qemu.batch(sequence, &[(1, vec![0]), (3, vec![8, 8]), (5, vec![])]);
    qemu.await_completion(sequence);
    assert!(trace_status(&qemu).contains(r#"\"state\":\"idle\""#));
    assert!(trace_set(&qemu, "relative,2").contains("\"error\""));
    assert!(trace_set(&qemu, &request).contains("\"return\""));
    assert!(trace_status(&qemu).contains(r#"\"state\":\"armed\""#));
    // One capture at a time, and never over an existing file.
    assert!(trace_set(&qemu, &request).contains("\"error\""));

    // The armed capture starts after this batch's presentation.
    frame(&mut qemu, &mut sequence, &[call(ENABLE, &[0x0b50])]);
    let mut vertices = Vec::new();
    for (position, color) in [
        ([0f32, 0., 0., 1.], [1f32, 0., 0., 1.]),
        ([1., 0., 0., 1.], [0., 1., 0., 1.]),
        ([0., 1., 0., 1.], [0., 0., 1., 1.]),
    ] {
        let mut vertex = floats(&position);
        vertex.extend(floats(&color));
        vertex.extend(floats(&[0., 0., 1., 0., 0., 0., 0.]));
        vertex.extend([0; 4]);
        vertices.extend(vertex);
    }
    frame(
        &mut qemu,
        &mut sequence,
        &[
            call(MATRIX_MODE, &[0x1700]),
            data_call(LIGHTFV, &[0x4000, 0x1201], &floats(&[1., 0.5, 0.25, 1.])),
            data_call(DRAW_ARRAYS, &[4, 0, 3, 7], &vertices),
        ],
    );
    frame(&mut qemu, &mut sequence, &[call(CLEAR, &[0x4000])]);
    // Outside the window: written by no one.
    frame(&mut qemu, &mut sequence, &[call(DISABLE, &[0x0b50])]);
    let status = trace_status(&qemu);
    for field in [
        r#"\"state\":\"done\""#,
        r#"\"reason\":\"frames\""#,
        r#"\"completed\":2"#,
        r#"\"records\":6"#,
    ] {
        assert!(status.contains(field), "{status}");
    }
    let traced = lines(&path);
    assert_eq!(
        traced[0],
        "# dreamgpu frame trace 1: frames=2 flags=0x1 limit=67108864"
    );
    let draw = "1 ctx=1:1:1 DATA_CALL glDrawArrays mode=0x4=GL_TRIANGLES first=0 count=3 \
                mask=0x7 data=192 layout=pos:FLOAT*4,col:FLOAT*4,nrm:FLOAT*3 \
                hex=000000000000000000000000";
    assert!(traced[4].starts_with(draw), "{}", traced[4]);
    let bytes = std::fs::metadata(&path).unwrap().len();
    let end = traced.last().unwrap();
    assert!(
        end.starts_with("# end reason=frames frames=2 records=6 bytes="),
        "{end}"
    );
    let end_bytes: u64 = end.rsplit('=').next().unwrap().parse().unwrap();
    assert_eq!(end_bytes + end.len() as u64 + 1, bytes);
    assert_eq!(
        [&traced[2..4], &traced[5..traced.len() - 1]].concat(),
        [
            "1 ctx=1:1:1 CALL glMatrixMode 0x1700=GL_MODELVIEW",
            "1 ctx=1:1:1 DATA_CALL glLightfv 0x4000=GL_LIGHT0 0x1201=GL_DIFFUSE data=16 \
             [1 0.5 0.25 1]",
            "  v0 pos=(0 0 0 1) col=(1 0 0 1) nrm=(0 0 1)",
            "  v1 pos=(1 0 0 1) col=(0 1 0 1) nrm=(0 0 1)",
            "  v2 pos=(0 1 0 1) col=(0 0 1 1) nrm=(0 0 1)",
            "1 ctx=1:1:1 PRESENT - flags=0x8,no-export",
            "2 ctx=1:1:1 CALL glClear 0x4000",
            "2 ctx=1:1:1 PRESENT - flags=0x8,no-export",
        ]
    );

    // A capture stopped before it starts still closes its file with an end line.
    let stopped = directory.join("stopped.trace");
    assert!(trace_set(&qemu, &format!("{},5", stopped.display())).contains("\"return\""));
    assert!(trace_set(&qemu, "stop").contains("\"return\""));
    assert!(trace_status(&qemu).contains(r#"\"reason\":\"cancelled\""#));
    let text = std::fs::read_to_string(&stopped).unwrap();
    let header = text
        .lines()
        .take(2)
        .map(|line| line.len() + 1)
        .sum::<usize>();
    assert!(text.starts_with("# dreamgpu frame trace 1: frames=5 flags=0x0"));
    assert!(
        text.ends_with(&format!(
            "# end reason=cancelled frames=0 records=0 bytes={header}\n"
        )),
        "{text}"
    );
    if let Some(error) = server.take_error() {
        panic!("GPU transport: {error}");
    }
    drop(qemu);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}
