use super::*;
fn put(r: &mut [u8], off: usize, x: u32) {
    r[off..off + 4].copy_from_slice(&x.to_le_bytes());
}
fn record(op: u32, bytes: usize) -> Vec<u8> {
    let mut r = vec![0; bytes];
    put(&mut r, DG_GL_OFF_OP as usize, op);
    put(&mut r, DG_GL_OFF_SIZE as usize, bytes as u32);
    put(&mut r, DG_GL_OFF_CLIENT as usize, 1);
    put(&mut r, DG_GL_OFF_GENERATION as usize, 7);
    r
}
fn check(r: &[u8]) -> (u32, u32) {
    let mut n = 99;
    let e = unsafe {
        dreamgpu_batch_validate(
            r.as_ptr(),
            r.len(),
            7,
            640,
            480,
            16 * 1024 * 1024,
            &mut n,
            None,
            core::ptr::null_mut(),
        )
    };
    (e, n)
}
#[test]
fn framing_rejects_each_truncation_reserved_flag_and_generation_mismatch() {
    let mut r = record(DG_GL_CREATE_DRAWABLE, 40);
    put(&mut r, 32, 640);
    put(&mut r, 36, 480);
    assert_eq!(check(&r), (0, 1));
    for size in 0..r.len() {
        assert_ne!(check(&r[..size]).0, 0);
    }
    for (off, value, error) in [
        (DG_GL_OFF_CLIENT, 0, DG_GL_ERROR_BATCH),
        (DG_GL_OFF_RESERVED, 1, DG_GL_ERROR_BATCH),
        (DG_GL_OFF_FLAGS, 1, DG_GL_ERROR_BATCH),
        (DG_GL_OFF_GENERATION, 8, DG_GL_ERROR_GENERATION),
        (32, 0, DG_GL_ERROR_DRAWABLE),
        (36, u32::MAX, DG_GL_ERROR_DRAWABLE),
    ] {
        let mut bad = r.clone();
        put(&mut bad, off as usize, value);
        assert_eq!(check(&bad), (error, 0));
    }
}
#[test]
fn present_flags_and_solo_query_shape_are_enforced_before_payload_access() {
    for flags in 0..64 {
        let bounded = flags & DG_GL_PRESENT_BOUNDED != 0;
        let mut r = record(DG_GL_PRESENT, if bounded { 40 } else { 32 });
        put(&mut r, DG_GL_OFF_FLAGS as usize, flags);
        if bounded {
            put(&mut r, 32, 640);
            put(&mut r, 36, 480);
        }
        let allowed = flags & !31 == 0
            && !(flags & DG_GL_PRESENT_EXCLUSIVE != 0 && flags & DG_GL_PRESENT_RETAIN != 0)
            && !(flags & DG_GL_PRESENT_NO_EXPORT != 0
                && (flags & !DG_GL_PRESENT_BOUNDED) != DG_GL_PRESENT_NO_EXPORT);
        assert_eq!(check(&r).0 == 0, allowed);
    }
    let mut q = record(DG_GL_QUERY, DG_GL_QUERY_BYTES as usize);
    put(&mut q, 32, FEnum_glGetError);
    assert_eq!(check(&q), (0, 1));
    q.extend(record(DG_GL_CLOSE_CLIENT, 32));
    assert_eq!(check(&q), (DG_GL_ERROR_BATCH, 0));
}
#[test]
fn inline_payload_alignment_padding_and_function_kind_are_checked() {
    let mut r = record(DG_GL_DATA_CALL, (DG_GL_DATA_ARGS + 4 + 4) as usize);
    put(&mut r, DG_GL_DATA_FUNCTION as usize, FEnum_glDeleteTextures);
    put(&mut r, DG_GL_DATA_BYTES as usize, 4);
    put(&mut r, DG_GL_DATA_ARGS as usize, 1);
    put(&mut r, (DG_GL_DATA_ARGS + 4) as usize, 7);
    assert_eq!(check(&r), (0, 1));
    let mut wrong = r.clone();
    put(&mut wrong, DG_GL_DATA_FUNCTION as usize, FEnum_glGetError);
    assert_eq!(check(&wrong), (DG_GL_ERROR_UNSUPPORTED, 0));
    let mut bad = r.clone();
    put(&mut bad, DG_GL_DATA_BYTES as usize, u32::MAX);
    assert_eq!(check(&bad), (DG_GL_ERROR_BATCH, 0));
}
