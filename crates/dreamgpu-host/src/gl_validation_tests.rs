use super::*;

#[test]
fn programmable_shader_catalog_entries_are_not_host_command_capabilities() {
    // The private provider's 1D border implementation targets fixed-function
    // GL1.1. A guest bypassing the ICD cannot gain shader access just because
    // the preserved upstream enumeration contains programmable API names.
    for function in [
        FEnum_glCreateShader,
        FEnum_glCreateShaderObjectARB,
        FEnum_glShaderSource,
        FEnum_glShaderSourceARB,
        FEnum_glCreateProgram,
        FEnum_glCreateProgramObjectARB,
        FEnum_glUseProgram,
        FEnum_glUseProgramObjectARB,
        FEnum_glUseProgramStages,
        FEnum_glProgramStringARB,
    ] {
        assert_eq!(function_words(function), u32::MAX);
        assert_eq!(
            unsafe {
                dreamgpu_gl_data_validate(function, core::ptr::null(), core::ptr::null(), u32::MAX)
            },
            DG_GL_ERROR_UNSUPPORTED
        );
        assert_eq!(query_validate(function, [0; 3]), DG_GL_ERROR_UNSUPPORTED);
    }
}

#[test]
fn function_classes_preserve_scalar_data_and_query_contracts() {
    for (f, n) in [
        (FEnum_glEnd, 0),
        (FEnum_glColor4f, 4),
        (FEnum_glLoadMatrixd, 32),
        (FEnum_glClearDepth, 2),
        (FEnum_glGetError, DG_GL_FUNCTION_QUERY),
        (FEnum_glGetTexImage, DG_GL_FUNCTION_QUERY | 3),
        (FEnum_glDrawElements, DG_GL_FUNCTION_INLINE_DATA | 5),
        (FEnum_glTexImage1D, DG_GL_FUNCTION_INLINE_DATA | 8),
    ] {
        assert_eq!(function_words(f), n);
    }
    assert_eq!(function_words(u32::MAX), u32::MAX);
}

#[test]
fn vector_shape_checks_cover_legacy_lighting_texture_and_double_planes() {
    for (f, a, n) in [
        (FEnum_glMaterialfv, [GL_FRONT, GL_AMBIENT], 16),
        (FEnum_glLightfv, [GL_LIGHT7, GL_SPOT_DIRECTION], 12),
        (FEnum_glTexEnvfv, [GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR], 16),
        (
            FEnum_glTexParameteriv,
            [GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER],
            4,
        ),
        (FEnum_glTexGendv, [GL_S, GL_OBJECT_PLANE], 32),
        (FEnum_glClipPlane, [GL_CLIP_PLANE5, 0], 32),
    ] {
        let mut args = [0; 8];
        args[..2].copy_from_slice(&a);
        assert_eq!(data_validate(f, &args, &vec![0; n]), 0);
        assert_eq!(data_validate(f, &args, &vec![0; n - 1]), DG_GL_ERROR_BATCH);
    }
    let mut args = [0; 8];
    args[0] = GL_LIGHT7 + 1;
    args[1] = GL_POSITION;
    assert_eq!(
        data_validate(FEnum_glLightfv, &args, &[0; 16]),
        DG_GL_ERROR_BATCH
    );
}

#[test]
fn array_bounds_reserved_fields_and_index_widths_are_checked_before_access() {
    for (index_type, index_size) in [
        (GL_UNSIGNED_BYTE, 1),
        (GL_UNSIGNED_SHORT, 2),
        (GL_UNSIGNED_INT, 4),
    ] {
        let args = [
            GL_TRIANGLES,
            3,
            index_type,
            3,
            DG_GL_ARRAY_POSITION,
            0,
            0,
            0,
        ];
        let mut bytes = vec![0; 3 * 64 + 3 * index_size];
        for i in 0..3 {
            bytes[3 * 64 + i * index_size] = i as u8;
        }
        assert_eq!(data_validate(FEnum_glDrawElements, &args, &bytes), 0);
        bytes[3 * 64 + 2 * index_size] = 3;
        assert_eq!(
            data_validate(FEnum_glDrawElements, &args, &bytes),
            DG_GL_ERROR_BATCH
        );
        bytes[3 * 64 + 2 * index_size] = 2;
        bytes[60] = 1;
        assert_eq!(
            data_validate(FEnum_glDrawElements, &args, &bytes),
            DG_GL_ERROR_BATCH
        );
    }
    let args = [
        GL_TRIANGLES,
        0,
        1,
        DG_GL_ARRAY_POSITION | DG_GL_ARRAY_SECONDARY,
        0,
        0,
        0,
        0,
    ];
    let mut bytes = [0; 80];
    assert_eq!(data_validate(FEnum_glDrawArrays, &args, &bytes), 0);
    bytes[76] = 1;
    assert_eq!(
        data_validate(FEnum_glDrawArrays, &args, &bytes),
        DG_GL_ERROR_BATCH
    );
    let invalid = [
        GL_TRIANGLES,
        u32::MAX,
        GL_UNSIGNED_INT,
        u32::MAX,
        u32::MAX,
        0,
        0,
        0,
    ];
    assert_eq!(
        data_validate(FEnum_glDrawElements, &invalid, &[]),
        DG_GL_ERROR_BATCH
    );
    let empty = [GL_TRIANGLES, 0, 0, DG_GL_ARRAY_POSITION, 0, 0, 0, 0];
    assert_eq!(data_validate(FEnum_glDrawArrays, &empty, &[]), 0);
}

#[test]
fn textures_allow_allocation_only_but_reject_bad_mips_formats_and_subimages() {
    let mut a = [
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        2,
        2,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
    ];
    assert_eq!(data_validate(FEnum_glTexImage2D, &a, &[]), 0);
    assert_eq!(data_validate(FEnum_glTexImage2D, &a, &[0; 16]), 0);
    assert_eq!(
        data_validate(FEnum_glTexImage2D, &a, &[0; 15]),
        DG_GL_ERROR_TEXTURE
    );
    a[1] = u32::MAX;
    assert_eq!(
        data_validate(FEnum_glTexImage2D, &a, &[]),
        DG_GL_ERROR_TEXTURE
    );
    a[1] = DG_GL_MAX_TEXTURE_LEVEL;
    assert_eq!(
        data_validate(FEnum_glTexImage2D, &a, &[]),
        DG_GL_ERROR_TEXTURE
    );
    a = [GL_TEXTURE_1D, 0, GL_RGB8, 2, 1, 0, GL_RGB, GL_UNSIGNED_BYTE];
    assert_eq!(data_validate(FEnum_glTexImage1D, &a, &[0; 6]), 0);
    a[4] = 2;
    assert_eq!(
        data_validate(FEnum_glTexImage1D, &a, &[0; 12]),
        DG_GL_ERROR_TEXTURE
    );
    a = [GL_TEXTURE_2D, 0, 0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE];
    assert_eq!(
        data_validate(FEnum_glTexSubImage2D, &a, &[]),
        DG_GL_ERROR_TEXTURE
    );
}

#[test]
fn query_types_sizes_padding_and_large_readback_caps_are_exact() {
    assert_eq!(
        query_shape(FEnum_glGetDoublev, [GL_MODELVIEW_MATRIX, 0, 0]),
        (16, DG_GL_RESULT_DOUBLE)
    );
    assert_eq!(
        query_result_bytes(FEnum_glGetDoublev, [GL_MODELVIEW_MATRIX, 0, 0]),
        128
    );
    assert_eq!(query_result_bytes(FEnum_glGetString, [GL_VENDOR, 0, 0]), 9);
    assert_eq!(
        query_result_bytes(FEnum_glGetString, [GL_EXTENSIONS, 0, 0]),
        1
    );
    assert_eq!(
        query_validate(FEnum_glGetString, [GL_VERSION, 0, 0]),
        DG_GL_ERROR_UNSUPPORTED
    );
    assert_eq!(
        query_validate(FEnum_glGetError, [1, 0, 0]),
        DG_GL_ERROR_BATCH
    );
    assert_eq!(
        query_validate(FEnum_glColor4f, [0; 3]),
        DG_GL_ERROR_UNSUPPORTED
    );
    assert_eq!(
        query_result_bytes(FEnum_glGetTexImage, [GL_TEXTURE_2D, 0, 0]),
        512
    );
    assert_eq!(
        query_result_bytes(FEnum_glGetTexImage, [GL_TEXTURE_2D, 16384 << 16, 0]),
        65536
    );
    assert_eq!(
        query_result_bytes(FEnum_glGetTexImage, [GL_TEXTURE_2D, 16385 << 16, 0]),
        0
    );
    assert_eq!(
        query_result_bytes(FEnum_glReadPixels, [0, 0, (128 << 16) | 128]),
        65536
    );
    assert_eq!(
        query_result_bytes(FEnum_glReadPixels, [0, 0, (129 << 16) | 128]),
        0
    );
    assert_eq!(
        query_result_bytes(FEnum_glGetMaterialfv, [GL_FRONT_AND_BACK, GL_DIFFUSE, 0]),
        0
    );
    assert_eq!(
        query_result_bytes(FEnum_glGetTexGenfv, [GL_Q, GL_EYE_PLANE, 0]),
        16
    );
}

#[test]
fn scalar_buffer_selection_hints_copy_texture_and_null_abi() {
    let mut a = [0; 8];
    a[0] = GL_NONE;
    assert_eq!(call_validate(FEnum_glDrawBuffer, &a), 0);
    assert_eq!(
        call_validate(FEnum_glReadBuffer, &a),
        DG_GL_ERROR_UNSUPPORTED
    );
    a[0] = GL_BACK_RIGHT;
    assert_eq!(
        call_validate(FEnum_glDrawBuffer, &a),
        DG_GL_ERROR_UNSUPPORTED
    );
    a[0] = GL_FOG_HINT;
    a[1] = GL_NICEST;
    assert_eq!(call_validate(FEnum_glHint, &a), 0);
    a[1] = u32::MAX;
    assert_eq!(call_validate(FEnum_glHint, &a), DG_GL_ERROR_UNSUPPORTED);
    a = [GL_TEXTURE_2D, 0, GL_RGBA8, 0, 0, 0, 0, 0];
    assert_eq!(call_validate(FEnum_glCopyTexImage2D, &a), 0);
    a[2] = 4;
    assert_eq!(
        call_validate(FEnum_glCopyTexImage2D, &a),
        DG_GL_ERROR_TEXTURE
    );
    unsafe {
        assert_eq!(
            dreamgpu_gl_query_validate(FEnum_glGetError, core::ptr::null()),
            DG_GL_ERROR_BATCH
        );
        assert_eq!(
            dreamgpu_gl_data_validate(u32::MAX, core::ptr::null(), core::ptr::null(), 0),
            DG_GL_ERROR_UNSUPPORTED
        );
        assert_eq!(dreamgpu_gl_call_validate(u32::MAX, core::ptr::null()), 0);
        assert_eq!(dreamgpu_gl_call_validate(FEnum_glEnd, core::ptr::null()), 0);
    }
}

#[test]
fn raster_metadata_and_typed_state_sizes() {
    assert_eq!(function_words(FEnum_glRasterPos4d), 8);
    // Other catalogue enums are not wire implementations; adapters canonicalize.
    assert_eq!(function_words(FEnum_glRasterPos4dv), u32::MAX);
    for (state, count) in [
        (GL_CURRENT_RASTER_POSITION, 4),
        (GL_CURRENT_RASTER_COLOR, 4),
        (GL_CURRENT_RASTER_TEXTURE_COORDS, 4),
        (GL_CURRENT_RASTER_INDEX, 1),
        (GL_CURRENT_RASTER_POSITION_VALID, 1),
        (GL_CURRENT_RASTER_DISTANCE, 1),
    ] {
        for (function, size) in [
            (FEnum_glGetBooleanv, 1),
            (FEnum_glGetIntegerv, 4),
            (FEnum_glGetFloatv, 4),
            (FEnum_glGetDoublev, 8),
        ] {
            assert_eq!(query_validate(function, [state, 0, 0]), 0);
            assert_eq!(query_result_bytes(function, [state, 0, 0]), count * size);
        }
    }
}

#[test]
fn pixel_maps_validate_types_dimensions_and_exact_payload_bytes() {
    for (function, size) in [
        (FEnum_glPixelMapfv, 4),
        (FEnum_glPixelMapuiv, 4),
        (FEnum_glPixelMapusv, 2),
    ] {
        assert_eq!(function_words(function), DG_GL_FUNCTION_INLINE_DATA | 2);
        let mut a = [0; 8];
        a[0] = GL_PIXEL_MAP_R_TO_R;
        a[1] = 3;
        assert_eq!(data_validate(function, &a, &vec![0; 3 * size]), 0);
        for bytes in [0, 3 * size - 1, 3 * size + 1] {
            assert_eq!(
                data_validate(function, &a, &vec![0; bytes]),
                DG_GL_ERROR_BATCH
            );
        }
        for count in [0, 257, u32::MAX] {
            a[1] = count;
            assert_eq!(data_validate(function, &a, &[]), DG_GL_ERROR_BATCH);
        }
        a[0] = GL_PIXEL_MAP_S_TO_S;
        a[1] = 3;
        assert_eq!(
            data_validate(function, &a, &vec![0; 3 * size]),
            DG_GL_ERROR_BATCH
        );
        a[0] = u32::MAX;
        assert_eq!(
            data_validate(function, &a, &vec![0; 3 * size]),
            DG_GL_ERROR_BATCH
        );
    }
    for function in [
        FEnum_glGetPixelMapfv,
        FEnum_glGetPixelMapuiv,
        FEnum_glGetPixelMapusv,
    ] {
        assert_eq!(
            query_result_bytes(function, [GL_PIXEL_MAP_R_TO_R, 256, 0]),
            1024
        );
        assert_eq!(
            query_result_bytes(function, [GL_PIXEL_MAP_R_TO_R, 257, 0]),
            0
        );
        assert_eq!(query_result_bytes(function, [GL_PIXEL_MAP_R_TO_R, 2, 1]), 0);
    }
}
#[test]
fn fixed_state_and_extended_arrays_reject_malformed_payloads() {
    let a = [
        GL_TRIANGLES,
        0,
        1,
        DG_GL_ARRAY_POSITION | DG_GL_ARRAY_EDGE | DG_GL_ARRAY_INDEX,
        0,
        0,
        0,
        0,
    ];
    let mut p = [0u8; 96];
    assert_eq!(data_validate(FEnum_glDrawArrays, &a, &p), 0);
    for index in [60, 76, 89, 95] {
        p[index] = 1;
        assert_ne!(data_validate(FEnum_glDrawArrays, &a, &p), 0);
        p[index] = 0;
    }
    p[88] = 2;
    assert_ne!(data_validate(FEnum_glDrawArrays, &a, &p), 0);
    p[88] = 1;
    assert_eq!(data_validate(FEnum_glDrawArrays, &a, &p), 0);
    for n in [0, 64, 80, 95] {
        assert_ne!(data_validate(FEnum_glDrawArrays, &a, &p[..n]), 0);
    }
    assert_eq!(data_validate(FEnum_glPolygonStipple, &[0; 8], &[0; 128]), 0);
    assert_ne!(data_validate(FEnum_glPolygonStipple, &[0; 8], &[0; 127]), 0);
    assert_eq!(query_result_bytes(FEnum_glGetPolygonStipple, [0; 3]), 128);
    assert_ne!(query_validate(FEnum_glGetPolygonStipple, [1, 0, 0]), 0);
    assert_eq!(function_words(FEnum_glIndexd), 2);
    assert_eq!(query_state_count(GL_ACCUM_CLEAR_VALUE), 4);
}

#[test]
fn one_dimensional_border_upload_admission_checks_full_bytes_and_interior_limit() {
    let mut a = [
        GL_TEXTURE_1D,
        0,
        GL_RGBA16,
        2050,
        1,
        1,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
    ];
    assert_eq!(data_validate(FEnum_glTexImage1D, &a, &[]), 0);
    assert_eq!(data_validate(FEnum_glTexImage1D, &a, &[0; 8200]), 0);
    assert_eq!(
        data_validate(FEnum_glTexImage1D, &a, &[0; 8192]),
        DG_GL_ERROR_TEXTURE
    );
    a[5] = 0;
    assert_eq!(
        data_validate(FEnum_glTexImage1D, &a, &[]),
        DG_GL_ERROR_TEXTURE
    );
    a[5] = 2;
    assert_eq!(
        data_validate(FEnum_glTexImage1D, &a, &[]),
        DG_GL_ERROR_TEXTURE
    );
    a[5] = 1;
    a[3] = 1;
    assert_eq!(
        data_validate(FEnum_glTexImage1D, &a, &[]),
        DG_GL_ERROR_TEXTURE
    );
    a[3] = 6;
    a[1] = 10;
    assert_eq!(
        data_validate(FEnum_glTexImage1D, &a, &[]),
        DG_GL_ERROR_TEXTURE
    );
    a[1] = 0;
    a[2] = 0x804e; // reserved hole between sized internal formats
    assert_eq!(
        data_validate(FEnum_glTexImage1D, &a, &[]),
        DG_GL_ERROR_TEXTURE
    );
    let sub = [
        GL_TEXTURE_1D,
        0,
        u32::MAX,
        0,
        6,
        1,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
    ];
    assert_eq!(data_validate(FEnum_glTexSubImage1D, &sub, &[0; 24]), 0);
    assert_eq!(
        data_validate(FEnum_glTexSubImage1D, &sub, &[0; 23]),
        DG_GL_ERROR_TEXTURE
    );
}

#[test]
fn packed_texture_words_require_exact_pairs_and_complete_payloads() {
    for (format, kind) in [
        (GL_RGB, GL_UNSIGNED_SHORT_5_6_5),
        (GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4),
        (GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1),
        (GL_BGRA, GL_UNSIGNED_SHORT_4_4_4_4_REV),
        (GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV),
    ] {
        let mut a = [GL_TEXTURE_2D, 0, GL_RGBA8, 3, 2, 0, format, kind];
        assert_eq!(data_validate(FEnum_glTexImage2D, &a, &[0; 12]), 0);
        assert_eq!(data_validate(FEnum_glTexImage2D, &a, &[]), 0);
        for bytes in [10, 11, 13, 24] {
            assert_eq!(
                data_validate(FEnum_glTexImage2D, &a, &vec![0; bytes]),
                DG_GL_ERROR_TEXTURE
            );
        }
        a = [GL_TEXTURE_2D, 0, 1, 2, 3, 2, format, kind];
        assert_eq!(data_validate(FEnum_glTexSubImage2D, &a, &[0; 12]), 0);
        assert_eq!(
            data_validate(FEnum_glTexSubImage2D, &a, &[]),
            DG_GL_ERROR_TEXTURE
        );
        a = [GL_TEXTURE_1D, 0, GL_RGBA8, 3, 1, 0, format, kind];
        assert_eq!(data_validate(FEnum_glTexImage1D, &a, &[0; 6]), 0);
    }
    for (format, kind) in [
        (GL_RGBA, GL_UNSIGNED_SHORT_5_6_5),
        (GL_RGB, GL_UNSIGNED_SHORT_4_4_4_4),
        (GL_ALPHA, GL_UNSIGNED_SHORT_5_5_5_1),
        (GL_LUMINANCE, GL_UNSIGNED_SHORT),
        (GL_RGBA, GL_UNSIGNED_INT),
    ] {
        let a = [GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, format, kind];
        assert_eq!(
            data_validate(FEnum_glTexImage2D, &a, &[]),
            DG_GL_ERROR_TEXTURE
        );
        assert_eq!(
            data_validate(FEnum_glTexImage2D, &a, &[0; 2]),
            DG_GL_ERROR_TEXTURE
        );
    }
}
