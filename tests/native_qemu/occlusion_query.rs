//! ARB_occlusion_query and NV_fence through the device: exact sample counts
//! of fully visible, partially occluded and fully occluded draws with depth
//! testing, counting without it and across several draws; the reports an
//! availability answer carries and their order; QUERY_RESULT waiting; the
//! nesting errors (BeginQuery while active, EndQuery without one, Gen and
//! Delete while active, results of the active query and of non-objects);
//! results across presents and context switches, a query spanning a present
//! and one per context while another context's query is active; display
//! lists, which compile BeginQuery/EndQuery and SetFenceNV and execute
//! deletion immediately; deletion while active; and fences that signal after
//! the commands before them. Every GL error stays a GL error: no batch in this
//! file may fail.
use super::fragment_program::{Device, start};
use super::textures::{call, data_call, receive, words};
use super::*;
use std::sync::{Arc, mpsc};

const BEGIN_QUERY: u32 = 34;
const END_QUERY: u32 = 543;
const DELETE_QUERIES: u32 = 422;
const GEN_QUERIES: u32 = 679;
const GET_QUERYIV: u32 = 1001;
const GET_QUERY_OBJECT: u32 = 1009;
const IS_QUERY: u32 = 1233;
const DELETE_FENCES: u32 = 405;
const FINISH_FENCE: u32 = 578;
const GEN_FENCES: u32 = 667;
const GET_FENCEIV: u32 = 765;
const IS_FENCE: u32 = 1214;
const SET_FENCE: u32 = 2084;
const TEST_FENCE: u32 = 2152;
const GEN_LISTS: u32 = 671;
const NEW_LIST: u32 = 1592;
const END_LIST: u32 = 539;
const CALL_LIST: u32 = 146;
const ENABLE: u32 = 0x209;
const DISABLE: u32 = 0x1c5;
const CLEAR_COLOR: u32 = 0xa3;
const CLEAR: u32 = 0x9a;
const COLOR4F: u32 = 0x0db;
const BEGIN: u32 = 0x01a;
const END: u32 = 0x216;
const VERTEX3F: u32 = 0x9ea;

const SAMPLES_PASSED: u32 = 0x8914;
const COUNTER_BITS: u32 = 0x8864;
const CURRENT_QUERY: u32 = 0x8865;
const RESULT: u32 = 0x8866;
const AVAILABLE: u32 = 0x8867;
const ALL_COMPLETED: u32 = 0x84f2;
const FENCE_STATUS: u32 = 0x84f3;
const FENCE_CONDITION: u32 = 0x84f4;
const DEPTH_TEST: u32 = 0x0b71;
const COMPILE: u32 = 0x1300;
const COMPILE_AND_EXECUTE: u32 = 0x1301;
const INVALID_OPERATION: u32 = 0x0502;
const RESULT_BOOL: u32 = 1;
const RESULT_INT: u32 = 2;
/// GetQueryObjectuiv states (gl.h).
const PENDING: u32 = 1;
const READY: u32 = 2;
const REPORTS: u32 = 63;
const QUADS: u32 = 7;

const TEAL: [u8; 4] = [0x10, 0x90, 0xd0, 0xff];

fn f(v: f32) -> u32 {
    v.to_bits()
}
/// Clear colour and depth (to 1) with depth testing enabled.
fn clear() -> Vec<(u32, Vec<u32>)> {
    vec![
        call(CLEAR_COLOR, &[f(0.), f(0.), f(0.), f(1.)]),
        call(CLEAR, &[0x4100]),
        call(ENABLE, &[DEPTH_TEST]),
    ]
}
/// A quad over clip-space x in [left, right] and the full height at depth
/// `z`: on the 8x8 drawable, edges at multiples of a quarter are pixel edges,
/// so a quad covers exactly `(right - left) * 4 * 8` samples.
fn quad(left: f32, right: f32, z: f32) -> Vec<(u32, Vec<u32>)> {
    let mut out = vec![
        call(COLOR4F, &TEAL.map(|c| f(f32::from(c) / 255.))),
        call(BEGIN, &[QUADS]),
    ];
    for (x, y) in [(left, -1.), (right, -1.), (right, 1.), (left, 1.)] {
        out.push(call(VERTEX3F, &[f(x), f(y), f(z)]));
    }
    out.push(call(END, &[]));
    out
}
fn begin(id: u32) -> (u32, Vec<u32>) {
    call(BEGIN_QUERY, &[SAMPLES_PASSED, id])
}
fn end() -> (u32, Vec<u32>) {
    call(END_QUERY, &[SAMPLES_PASSED])
}
fn delete(function: u32, names: &[u32]) -> (u32, Vec<u32>) {
    let bytes: Vec<u8> = names.iter().flat_map(|n| n.to_le_bytes()).collect();
    data_call(function, &[names.len() as u32], &bytes)
}

trait Queries {
    fn int_for(&mut self, context: u32, function: u32, args: &[u32]) -> Vec<u32>;
    fn bool_for(&mut self, context: u32, function: u32, name: u32) -> bool;
    /// GetQueryObjectuiv words (state, result, reports) of `context`.
    fn object_for(&mut self, context: u32, id: u32, pname: u32, reports: u32) -> Vec<u32>;
    /// The waited result of a query of `context`, which raises no error.
    fn result_for(&mut self, context: u32, id: u32) -> u32;
    fn result(&mut self, id: u32) -> u32;
    fn names(&mut self, function: u32, count: u32) -> Vec<u32>;
    /// Exactly `code` and then no further error.
    fn raised_for(&mut self, context: u32, code: u32, label: &str);
}
impl Queries for Device {
    fn int_for(&mut self, context: u32, function: u32, args: &[u32]) -> Vec<u32> {
        let (kind, bytes) = self.query_for(context, function, args);
        assert_eq!(kind, RESULT_INT);
        words(&bytes)
    }
    fn bool_for(&mut self, context: u32, function: u32, name: u32) -> bool {
        let (kind, bytes) = self.query_for(context, function, &[name]);
        assert_eq!((kind, bytes.len()), (RESULT_BOOL, 1));
        assert!(bytes[0] <= 1);
        bytes[0] == 1
    }
    fn object_for(&mut self, context: u32, id: u32, pname: u32, reports: u32) -> Vec<u32> {
        let out = self.int_for(context, GET_QUERY_OBJECT, &[id, pname, reports]);
        assert!(
            out == [0] || (out.len().is_multiple_of(2) && out.len() <= 2 + 2 * reports as usize),
            "{out:?}"
        );
        out
    }
    fn result_for(&mut self, context: u32, id: u32) -> u32 {
        let out = self.object_for(context, id, RESULT, 0);
        assert_eq!(out.len(), 2, "query {id}: {out:?}");
        assert_eq!(out[0], READY);
        assert_eq!(self.error_for(context), 0);
        out[1]
    }
    fn result(&mut self, id: u32) -> u32 {
        self.result_for(1, id)
    }
    fn names(&mut self, function: u32, count: u32) -> Vec<u32> {
        let names = self.int_for(1, function, &[count]);
        assert_eq!(names.len(), count as usize);
        names
    }
    fn raised_for(&mut self, context: u32, code: u32, label: &str) {
        assert_eq!(self.error_for(context), code, "{label}");
        assert_eq!(self.error_for(context), 0, "{label}: one error");
    }
}

#[test]
#[ignore = "requires native host GPU and occlusion query QEMU producer; diskless acceptance"]
fn qemu_occlusion_queries_count_exact_samples_and_report_in_order() {
    let (mut d, _server, _directory) = start("occlusion");
    assert_ne!(d.qemu.read(0x1008) & 0x800_0000, 0);
    for (function, words) in [
        (BEGIN_QUERY, 2),
        (END_QUERY, 1),
        (DELETE_QUERIES, 0x8000_0001),
        (GEN_QUERIES, 0x4000_0001),
        (GET_QUERY_OBJECT, 0x4000_0003),
        (SET_FENCE, 2),
        (GET_FENCEIV, 0x4000_0002),
    ] {
        d.qemu.write(0x1130, function);
        assert_eq!(d.qemu.read(0x1134), words, "function words of {function}");
    }
    assert_eq!(
        d.int_for(1, GET_QUERYIV, &[SAMPLES_PASSED, COUNTER_BITS]),
        [32]
    );
    assert_eq!(
        d.int_for(1, GET_QUERYIV, &[SAMPLES_PASSED, CURRENT_QUERY]),
        [0]
    );
    let q = d.names(GEN_QUERIES, 5);
    assert!(q.iter().all(|&n| n != 0 && !d.bool_for(1, IS_QUERY, n)));
    let mut sorted = q.clone();
    sorted.sort_unstable();
    sorted.dedup();
    assert_eq!(sorted.len(), 5);

    // Fully visible: the whole 8x8 drawable at the far plane's half.
    let mut records = clear();
    records.push(begin(q[0]));
    records.extend(quad(-1., 1., 0.));
    records.push(end());
    // Partially occluded: a near quad over the left half, then a farther one
    // over columns 2-7, whose columns 2-3 fail the depth test.
    records.extend(clear());
    records.extend(quad(-1., 0., -0.5));
    records.push(begin(q[1]));
    records.extend(quad(-0.5, 1., 0.5));
    records.push(end());
    // Fully occluded: columns 0-1 behind the near quad.
    records.push(begin(q[2]));
    records.extend(quad(-1., -0.5, 0.5));
    records.push(end());
    // Without depth testing every sample passes, accumulated over draws.
    records.push(call(DISABLE, &[DEPTH_TEST]));
    records.push(begin(q[3]));
    for _ in 0..3 {
        records.extend(quad(-1., 1., 0.9));
    }
    records.push(end());
    d.submit(&records);
    assert_eq!(d.error(), 0);
    assert!(d.bool_for(1, IS_QUERY, q[0]) && d.bool_for(1, IS_QUERY, q[3]));
    assert!(!d.bool_for(1, IS_QUERY, q[4]));

    // Poll the newest; every report is an earlier query with its exact count,
    // each reported once, oldest first.
    let expected = [(q[0], 64), (q[1], 32), (q[2], 0), (q[3], 192)];
    let mut reported = Vec::new();
    let deadline = Instant::now() + Duration::from_secs(5);
    loop {
        let out = d.object_for(1, q[3], AVAILABLE, REPORTS);
        assert!(out[0] == PENDING || out[0] == READY, "{out:?}");
        for pair in out[2..].chunks(2) {
            reported.push((pair[0], pair[1]));
        }
        if out[0] == READY {
            assert_eq!(out[1], 192);
            break;
        }
        assert_eq!(out[1], 0);
        assert!(Instant::now() < deadline, "query never became available");
    }
    assert_eq!(reported, expected[..reported.len()]);
    // An answer with the newest available has every earlier one reported or
    // reportable now.
    let rest = d.object_for(1, q[3], AVAILABLE, REPORTS);
    assert_eq!(rest[..2], [READY, 192]);
    for pair in rest[2..].chunks(2) {
        reported.push((pair[0], pair[1]));
    }
    assert_eq!(reported, expected[..3]);
    assert_eq!(d.object_for(1, q[3], AVAILABLE, REPORTS), [READY, 192]);
    // Reported queries still answer themselves, exactly.
    for (id, samples) in expected {
        assert_eq!(d.result(id), samples, "query {id}");
        assert_eq!(d.object_for(1, id, AVAILABLE, 0), [READY, samples]);
    }
    assert_eq!(d.error(), 0);

    // QUERY_RESULT waits for a query ended in the same batch as its draws.
    let mut records = clear();
    records.push(begin(q[0]));
    records.extend(quad(-1., 0., 0.));
    records.push(end());
    d.submit(&records);
    assert_eq!(d.object_for(1, q[0], RESULT, REPORTS), [READY, 32]);

    // Nesting: a second BeginQuery, of another or the same name, fails and
    // the active query keeps counting; q[1] keeps its last result.
    let mut records = clear();
    records.push(begin(q[0]));
    records.push(begin(q[1]));
    records.extend(quad(-1., 1., 0.));
    records.push(begin(q[0]));
    d.submit(&records);
    d.raised_for(1, INVALID_OPERATION, "BeginQuery while active");
    assert_eq!(
        d.int_for(1, GET_QUERYIV, &[SAMPLES_PASSED, CURRENT_QUERY]),
        [q[0]]
    );
    // The active query has no result, Gen and Delete fail and change nothing.
    assert_eq!(d.object_for(1, q[0], AVAILABLE, REPORTS), [0]);
    d.raised_for(1, INVALID_OPERATION, "result of the active query");
    assert_eq!(d.object_for(1, q[0], RESULT, 0), [0]);
    d.raised_for(1, INVALID_OPERATION, "waiting for the active query");
    assert_eq!(d.names(GEN_QUERIES, 2), [0, 0]);
    d.raised_for(1, INVALID_OPERATION, "GenQueries while active");
    // Deletion while active, of the active query itself and of another.
    d.submit(&[delete(DELETE_QUERIES, &[q[0], q[1]])]);
    d.raised_for(1, INVALID_OPERATION, "DeleteQueries while active");
    assert!(d.bool_for(1, IS_QUERY, q[0]) && d.bool_for(1, IS_QUERY, q[1]));
    // Nearer than the full quad: columns 0-1 pass again.
    let mut records = quad(-1., -0.5, -0.5);
    records.push(end());
    records.push(end());
    d.submit(&records);
    d.raised_for(1, INVALID_OPERATION, "EndQuery without an active query");
    assert_eq!(d.result(q[0]), 64 + 16);
    assert_eq!(d.result(q[1]), 32);
    d.submit(&[begin(0)]);
    d.raised_for(1, INVALID_OPERATION, "BeginQuery of zero");
    assert_eq!(
        d.int_for(1, GET_QUERYIV, &[SAMPLES_PASSED, CURRENT_QUERY]),
        [0]
    );
    // Names that are not query objects have no result.
    for id in [q[4], 0, 0x7fff_0000] {
        assert_eq!(d.object_for(1, id, AVAILABLE, REPORTS), [0]);
        d.raised_for(1, INVALID_OPERATION, "result of a non-object");
    }
    // Any nonzero name becomes an object at its first BeginQuery.
    let mut records = clear();
    records.push(begin(0x7fff_0001));
    records.extend(quad(0.5, 1., 0.));
    records.push(end());
    d.submit(&records);
    assert_eq!(d.result(0x7fff_0001), 16);
    assert!(d.bool_for(1, IS_QUERY, 0x7fff_0001));

    // Deletion: objects and reserved names; the names then have no result.
    d.submit(&[delete(
        DELETE_QUERIES,
        &[q[0], 0, q[4], 0x7fff_0001, 0x1234_5678],
    )]);
    assert_eq!(d.error(), 0);
    for id in [q[0], q[4], 0x7fff_0001] {
        assert!(!d.bool_for(1, IS_QUERY, id));
    }
    assert_eq!(d.object_for(1, q[0], RESULT, 0), [0]);
    d.raised_for(1, INVALID_OPERATION, "result of a deleted query");
    assert_eq!(d.result(q[2]), 0);
    // The context still draws.
    d.submit(&[
        call(DISABLE, &[DEPTH_TEST]),
        call(CLEAR_COLOR, &[f(0.), f(0.), f(0.), f(1.)]),
        call(CLEAR, &[0x4000]),
    ]);
    d.submit(&quad(-1., 1., 0.));
    d.expect(TEAL, "drawing after the query errors");
    assert_eq!(d.error(), 0);
}

#[test]
#[ignore = "requires native host GPU and occlusion query QEMU producer; diskless acceptance"]
fn qemu_occlusion_queries_survive_presents_context_switches_and_lists() {
    let (mut d, server, _directory) = start("occlusion-lifetime");
    let (wake, ready) = mpsc::sync_channel(1);
    server.set_callback(Arc::new(move |_| {
        let _ = wake.try_send(());
    }));
    let q = d.names(GEN_QUERIES, 8);

    // A result read after the present that follows its EndQuery.
    let mut records = clear();
    records.push(begin(q[0]));
    records.extend(quad(-1., 0., 0.));
    records.push(end());
    records.push((7, vec![]));
    d.submit(&records);
    drop(receive(&server, &ready, 1, 1));
    assert_eq!(d.result(q[0]), 32);
    // A query spanning a present counts both buffers' draws.
    let mut records = clear();
    records.push(begin(q[1]));
    records.extend(quad(-1., 0., 0.));
    records.push((7, vec![]));
    d.submit(&records);
    drop(receive(&server, &ready, 1, 1));
    let mut records = clear();
    records.extend(quad(0., 0.5, 0.));
    records.push(end());
    d.submit(&records);
    assert_eq!(d.result(q[1]), 32 + 16);
    assert_eq!(d.error(), 0);

    // A context sharing context 1's objects still has its own queries.
    d.submit_for(2, &[(1, vec![1]), (3, vec![8, 8]), (5, vec![])]);
    assert!(!d.bool_for(2, IS_QUERY, q[0]));
    assert_eq!(
        d.int_for(2, GET_QUERYIV, &[SAMPLES_PASSED, CURRENT_QUERY]),
        [0]
    );
    assert_eq!(d.object_for(2, q[0], AVAILABLE, REPORTS), [0]);
    d.raised_for(2, INVALID_OPERATION, "another context's query");
    // Context 1's query stays active while context 2 begins, counts and ends
    // its own query of the same name.
    let mut records = clear();
    records.push(begin(q[2]));
    records.extend(quad(-1., 0., 0.));
    d.submit(&records);
    let mut records = clear();
    records.push(begin(q[2]));
    records.extend(quad(-1., 1., 0.));
    records.push(end());
    d.submit_for(2, &records);
    assert_eq!(d.error_for(2), 0);
    assert_eq!(
        d.int_for(1, GET_QUERYIV, &[SAMPLES_PASSED, CURRENT_QUERY]),
        [q[2]]
    );
    let mut records = quad(0.5, 1., 0.);
    records.push(end());
    d.submit(&records);
    assert_eq!(d.result(q[2]), 32 + 16);
    assert_eq!(d.result_for(2, q[2]), 64);
    // Pending results survive switching away and back.
    let mut records = clear();
    records.push(begin(q[3]));
    records.extend(quad(-1., -0.5, 0.));
    records.push(end());
    d.submit(&records);
    d.submit_for(2, &quad(-1., 1., 0.));
    assert_eq!(d.result(q[3]), 16);
    assert_eq!(d.error_for(2), 0);

    // Display lists compile BeginQuery and EndQuery: nothing executes, the
    // name stays a non-object, until the list is called.
    let list = words(&d.query(GEN_LISTS, &[2]).1)[1];
    assert_eq!(d.word(NEW_LIST, &[list, COMPILE]), 0);
    let mut records = vec![begin(q[4])];
    records.extend(quad(-1., 0.5, 0.));
    records.push(end());
    d.submit(&records);
    assert_eq!(d.word(END_LIST, &[]), 0);
    assert!(!d.bool_for(1, IS_QUERY, q[4]));
    let mut records = clear();
    records.push(call(CALL_LIST, &[list]));
    d.submit(&records);
    assert_eq!(d.result(q[4]), 48);
    // COMPILE_AND_EXECUTE runs it at once; deletion while compiling executes
    // immediately and is not replayed.
    assert_eq!(d.word(NEW_LIST, &[list + 1, COMPILE_AND_EXECUTE]), 0);
    let mut records = clear();
    records.push(begin(q[5]));
    records.extend(quad(-1., -0.5, 0.));
    records.push(end());
    d.submit(&records);
    assert_eq!(d.result(q[5]), 16);
    d.submit(&[delete(DELETE_QUERIES, &[q[5]])]);
    assert!(!d.bool_for(1, IS_QUERY, q[5]));
    assert_eq!(d.word(END_LIST, &[]), 0);
    d.submit(&[call(CALL_LIST, &[list + 1])]);
    assert_eq!(d.result(q[5]), 16, "the replayed list recreated the query");
    // Calling the list again restarts its query.
    let mut records = clear();
    records.push(call(CALL_LIST, &[list]));
    d.submit(&records);
    assert_eq!(d.result(q[4]), 48);

    // Deletion while active fails; after EndQuery it succeeds.
    let mut records = clear();
    records.push(begin(q[6]));
    records.push(delete(DELETE_QUERIES, &[q[6]]));
    records.extend(quad(-1., 1., 0.));
    records.push(end());
    d.submit(&records);
    d.raised_for(1, INVALID_OPERATION, "deleting the active query");
    assert_eq!(d.result(q[6]), 64);
    d.submit(&[delete(DELETE_QUERIES, &[q[6]])]);
    assert!(!d.bool_for(1, IS_QUERY, q[6]));
    assert_eq!(d.object_for(1, q[6], RESULT, 0), [0]);
    d.raised_for(1, INVALID_OPERATION, "result of the deleted query");

    // Destroying a context with an active query releases it cleanly.
    let mut records = clear();
    records.push(begin(q[7]));
    records.extend(quad(-1., 1., 0.));
    d.submit_for(2, &records);
    d.submit_for(2, &[(2, vec![])]);
    d.submit(&[
        call(DISABLE, &[DEPTH_TEST]),
        call(CLEAR_COLOR, &[f(0.), f(0.), f(0.), f(1.)]),
        call(CLEAR, &[0x4000]),
    ]);
    d.submit(&quad(-1., 1., 0.));
    d.expect(TEAL, "drawing after context 2's release");
    assert_eq!(d.error(), 0);
}

#[test]
#[ignore = "requires native host GPU and occlusion query QEMU producer; diskless acceptance"]
fn qemu_nv_fences_signal_after_the_commands_before_them() {
    let (mut d, _server, _directory) = start("fences");
    let fences = d.names(GEN_FENCES, 3);
    assert!(
        fences
            .iter()
            .all(|&n| n != 0 && !d.bool_for(1, IS_FENCE, n))
    );
    // Not a fence: INVALID_OPERATION and a single zero word.
    assert_eq!(d.int_for(1, TEST_FENCE, &[fences[0]]), [0]);
    d.raised_for(1, INVALID_OPERATION, "TestFenceNV of an unset name");
    assert_eq!(d.int_for(1, FINISH_FENCE, &[0x55]), [0]);
    d.raised_for(1, INVALID_OPERATION, "FinishFenceNV of an unknown name");
    assert_eq!(d.int_for(1, GET_FENCEIV, &[fences[0], FENCE_STATUS]), [0]);
    d.raised_for(1, INVALID_OPERATION, "GetFenceivNV of an unset name");
    d.submit(&[call(SET_FENCE, &[0, ALL_COMPLETED])]);
    d.raised_for(1, INVALID_OPERATION, "SetFenceNV of zero");

    // A fence after drawing signals in finite time and then stays signalled.
    let mut records = clear();
    for _ in 0..16 {
        records.extend(quad(-1., 1., 0.));
    }
    records.push(call(SET_FENCE, &[fences[0], ALL_COMPLETED]));
    d.submit(&records);
    assert!(d.bool_for(1, IS_FENCE, fences[0]));
    let deadline = Instant::now() + Duration::from_secs(5);
    while d.int_for(1, TEST_FENCE, &[fences[0]]) != [1, 1] {
        assert!(Instant::now() < deadline, "fence never signalled");
    }
    assert_eq!(d.int_for(1, TEST_FENCE, &[fences[0]]), [1, 1]);
    assert_eq!(
        d.int_for(1, GET_FENCEIV, &[fences[0], FENCE_STATUS]),
        [1, 1]
    );
    assert_eq!(
        d.int_for(1, GET_FENCEIV, &[fences[0], FENCE_CONDITION]),
        [1, ALL_COMPLETED]
    );
    assert_eq!(d.error(), 0);
    // FinishFenceNV waits: the pixels drawn before it are complete.
    let mut records = clear();
    records.extend(quad(-1., 1., 0.));
    records.push(call(SET_FENCE, &[fences[0], ALL_COMPLETED]));
    d.submit(&records);
    assert_eq!(d.int_for(1, FINISH_FENCE, &[fences[0]]), [1, 1]);
    assert_eq!(d.int_for(1, TEST_FENCE, &[fences[0]]), [1, 1]);
    d.expect(TEAL, "drawn before the finished fence");

    // Display lists compile SetFenceNV.
    let list = words(&d.query(GEN_LISTS, &[1]).1)[1];
    assert_eq!(d.word(NEW_LIST, &[list, COMPILE]), 0);
    d.submit(&[call(SET_FENCE, &[fences[1], ALL_COMPLETED])]);
    d.submit(&[delete(DELETE_FENCES, &[fences[0]])]);
    assert_eq!(d.word(END_LIST, &[]), 0);
    assert!(!d.bool_for(1, IS_FENCE, fences[1]));
    assert!(
        !d.bool_for(1, IS_FENCE, fences[0]),
        "deletion executed at once"
    );
    d.submit(&[call(CALL_LIST, &[list])]);
    assert!(d.bool_for(1, IS_FENCE, fences[1]));
    assert_eq!(d.int_for(1, FINISH_FENCE, &[fences[1]]), [1, 1]);
    assert_eq!(d.error(), 0);

    // Fences belong to one context, even between sharing contexts.
    d.submit_for(2, &[(1, vec![1]), (3, vec![8, 8]), (5, vec![])]);
    assert!(!d.bool_for(2, IS_FENCE, fences[1]));
    d.submit_for(2, &[call(SET_FENCE, &[fences[2], ALL_COMPLETED])]);
    assert_eq!(d.int_for(2, FINISH_FENCE, &[fences[2]]), [1, 1]);
    assert!(!d.bool_for(1, IS_FENCE, fences[2]));
    assert_eq!(d.error_for(2), 0);
    // Deleting a pending fence, and an unknown name, is quiet.
    let mut records = quad(-1., 1., 0.);
    records.push(call(SET_FENCE, &[fences[2], ALL_COMPLETED]));
    records.push(delete(DELETE_FENCES, &[fences[2], 0, 0x4444]));
    d.submit(&records);
    assert!(!d.bool_for(1, IS_FENCE, fences[2]));
    assert_eq!(d.error(), 0);
    d.expect(TEAL, "drawing after the fences");
    assert_eq!(d.error(), 0);
}
