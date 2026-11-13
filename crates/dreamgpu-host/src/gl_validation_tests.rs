use super::*;

#[test]
fn programmable_shader_catalog_entries_are_not_host_command_capabilities() {
    // The private provider's 1D border implementation targets fixed-function
    // GL1.1. A guest bypassing the ICD cannot gain shader access just because
    // the preserved upstream enumeration contains programmable API names.
    // ARB_fragment_program and ARB_vertex_program assembly are negotiated
    // separately and accept only their two targets
    // (program::tests::record_shapes_follow_the_protocol).
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
        FEnum_glProgramParameteriARB,
        FEnum_glLinkProgramARB,
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
    // GL_MAX_GENERAL_COMBINERS_NV, asked unconditionally by Jedi Academy's
    // extension setup: NV_register_combiners is never advertised, so the
    // host answers it as drivers without the extension did (INVALID_ENUM).
    assert_eq!(
        query_validate(FEnum_glGetIntegerv, [0x854d, 0, 0]),
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
    // Past a contiguous destination, only a bulk page list (gl.h
    // DG_CAP2_GL_BULK_READ) takes the rectangle; the capacity check decides.
    assert_eq!(
        query_result_bytes(FEnum_glReadPixels, [0, 0, (129 << 16) | 128]),
        66048
    );
    assert_eq!(
        query_result_bytes(FEnum_glReadPixels, [0, 0, (1024 << 16) | 1024]),
        DG_GL_MAX_BULK_RESULT_BYTES
    );
    assert_eq!(
        query_result_bytes(FEnum_glReadPixels, [0, 0, (1024 << 16) | 1025]),
        0
    );
    // DG_CAP2_GL_BULK_READ texture reads: SHAPE is five words of the level
    // alone; UNITS counts DG_GL_TEXTURE_READ_UNIT pixels up to the bulk limit.
    let units = |n: u32| DG_GL_TEXTURE_READ_UNITS | n << 16;
    for (args, expected) in [
        (
            [GL_TEXTURE_2D, DG_GL_TEXTURE_READ_SHAPE, 0],
            (5, DG_GL_RESULT_INT),
        ),
        (
            [GL_TEXTURE_2D, DG_GL_TEXTURE_READ_SHAPE | 11, 0],
            (5, DG_GL_RESULT_INT),
        ),
        (
            [GL_TEXTURE_3D, DG_GL_TEXTURE_READ_SHAPE | 3, 0],
            (5, DG_GL_RESULT_INT),
        ),
        (
            [GL_TEXTURE_1D, DG_GL_TEXTURE_READ_SHAPE, 0],
            (5, DG_GL_RESULT_INT),
        ),
        (
            [GL_TEXTURE_RECTANGLE_ARB, DG_GL_TEXTURE_READ_SHAPE, 0],
            (5, DG_GL_RESULT_INT),
        ),
        (
            [GL_TEXTURE_RECTANGLE_ARB, DG_GL_TEXTURE_READ_SHAPE | 1, 0],
            (0, DG_GL_RESULT_INT),
        ),
        (
            [GL_TEXTURE_2D, DG_GL_TEXTURE_READ_SHAPE | 12, 0],
            (0, DG_GL_RESULT_INT),
        ),
        (
            [GL_TEXTURE_2D, DG_GL_TEXTURE_READ_SHAPE, 1],
            (0, DG_GL_RESULT_INT),
        ),
        (
            [
                GL_TEXTURE_2D,
                DG_GL_TEXTURE_READ_SHAPE | DG_GL_TEXTURE_READ_DEPTH,
                0,
            ],
            (0, DG_GL_RESULT_INT),
        ),
        (
            [GL_PROXY_TEXTURE_2D, DG_GL_TEXTURE_READ_SHAPE, 0],
            (0, DG_GL_RESULT_INT),
        ),
        ([GL_TEXTURE_2D, units(1), 0], (64, DG_GL_RESULT_INT)),
        ([GL_TEXTURE_2D, units(0), 0], (0, DG_GL_RESULT_INT)),
        (
            [GL_TEXTURE_RECTANGLE_ARB, units(12288), 0],
            (786432, DG_GL_RESULT_INT),
        ),
        (
            [GL_TEXTURE_2D, units(16384) | 11, 5],
            (1 << 20, DG_GL_RESULT_INT),
        ),
        ([GL_TEXTURE_2D, units(16385), 0], (0, DG_GL_RESULT_INT)),
        (
            [GL_TEXTURE_2D, units(4096) | DG_GL_TEXTURE_READ_FLOAT, 0],
            (1 << 20, DG_GL_RESULT_FLOAT),
        ),
        (
            [GL_TEXTURE_2D, units(4097) | DG_GL_TEXTURE_READ_FLOAT, 0],
            (0, DG_GL_RESULT_FLOAT),
        ),
        (
            [GL_TEXTURE_2D, units(16384) | DG_GL_TEXTURE_READ_DEPTH, 0],
            (1 << 20, DG_GL_RESULT_FLOAT),
        ),
        (
            [GL_TEXTURE_3D, units(1) | DG_GL_TEXTURE_READ_DEPTH, 0],
            (0, DG_GL_RESULT_FLOAT),
        ),
        ([GL_TEXTURE_2D, units(1) | 12, 0], (0, DG_GL_RESULT_INT)),
    ] {
        assert_eq!(
            query_shape(FEnum_glGetTexImage, args),
            expected,
            "{args:x?}"
        );
    }
    // Without UNITS the count stays below the contiguous limit.
    assert_eq!(
        query_shape(FEnum_glGetTexImage, [GL_TEXTURE_2D, 16385 << 16, 0]).0,
        0
    );
    for (args, expected) in [
        ([GL_TEXTURE_2D, units(1), 0], 64),
        ([GL_TEXTURE_2D, units(16384) | 3, 7], 1 << 20),
        ([GL_TEXTURE_2D, units(16385), 0], 0),
        ([GL_TEXTURE_2D, units(0), 0], 0),
        ([GL_TEXTURE_RECTANGLE_ARB, units(1), 0], 0),
        ([GL_TEXTURE_2D, units(1) | 12, 0], 0),
        ([GL_TEXTURE_2D, 16384 << 16, 0], 16384),
        ([GL_TEXTURE_2D, 16385 << 16, 0], 0),
    ] {
        assert_eq!(
            query_shape(FEnum_glGetCompressedTexImage, args).0,
            expected,
            "{args:x?}"
        );
    }
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
    // ReadBuffer(NONE) exists for framebuffer objects (EXT_framebuffer_object);
    // the window rejects it at execution as GL_INVALID_ENUM.
    assert_eq!(call_validate(FEnum_glReadBuffer, &a), 0);
    a[0] = GL_FRONT_AND_BACK;
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
        (GL_LUMINANCE, GL_DOUBLE),
        (GL_RGBA, GL_BITMAP),
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

#[test]
fn texture_scalar_payloads_keep_native_precision_and_empty_definitions() {
    for (kind, unit) in [
        (GL_BYTE, 1),
        (GL_UNSIGNED_BYTE, 1),
        (GL_SHORT, 2),
        (GL_UNSIGNED_SHORT, 2),
        (GL_INT, 4),
        (GL_UNSIGNED_INT, 4),
        (GL_FLOAT, 4),
    ] {
        for (format, components) in [
            (GL_COLOR_INDEX, 1),
            (GL_RED, 1),
            (GL_GREEN, 1),
            (GL_BLUE, 1),
            (GL_ALPHA, 1),
            (GL_LUMINANCE, 1),
            (GL_LUMINANCE_ALPHA, 2),
            (GL_RGB, 3),
            (GL_RGBA, 4),
        ] {
            let a = [GL_TEXTURE_2D, 0, GL_RGBA16, 2, 2, 0, format, kind];
            let pixels = vec![0; 4 * components * unit];
            assert_eq!(data_validate(FEnum_glTexImage2D, &a, &pixels), 0);
            assert_eq!(
                data_validate(FEnum_glTexImage2D, &a, &pixels[..pixels.len() - 1]),
                DG_GL_ERROR_TEXTURE
            );
            assert_eq!(
                data_validate(
                    FEnum_glTexSubImage2D,
                    &[GL_TEXTURE_2D, 0, 0, 0, 2, 2, format, kind],
                    &pixels
                ),
                0
            );
        }
    }
    for internal in (0x803b..=0x804d)
        .chain(0x804f..=0x805b)
        .chain([GL_R3_G3_B2])
    {
        assert_eq!(
            data_validate(
                FEnum_glTexImage2D,
                &[GL_TEXTURE_2D, 0, internal, 0, 2, 0, GL_RGBA, GL_FLOAT],
                &[]
            ),
            0
        );
        assert_eq!(
            data_validate(
                FEnum_glTexImage1D,
                &[GL_TEXTURE_1D, 0, internal, 0, 1, 0, GL_RGBA, GL_FLOAT],
                &[]
            ),
            0
        );
    }
    assert_eq!(
        data_validate(
            FEnum_glTexImage2D,
            &[GL_TEXTURE_2D, 0, GL_RGBA, 2, 0, 0, GL_RGBA, GL_FLOAT],
            &[]
        ),
        0
    );
}

#[test]
fn proxy_definitions_accept_virtual_limit_probes_but_never_payloads_or_wrong_targets() {
    for (target, function, height) in [
        (GL_PROXY_TEXTURE_1D, FEnum_glTexImage1D, 1),
        (GL_PROXY_TEXTURE_2D, FEnum_glTexImage2D, 4096),
    ] {
        let mut a = [target, 0, GL_RGBA16, 4096, height, 0, GL_RGBA, GL_FLOAT];
        assert_eq!(data_validate(function, &a, &[]), 0);
        assert_eq!(data_validate(function, &a, &[0; 4]), DG_GL_ERROR_TEXTURE);
        a[3] = u32::MAX;
        assert_eq!(data_validate(function, &a, &[]), DG_GL_ERROR_TEXTURE);
        a[3] = 1;
        a[0] = if target == GL_PROXY_TEXTURE_1D {
            GL_PROXY_TEXTURE_2D
        } else {
            GL_PROXY_TEXTURE_1D
        };
        assert_eq!(data_validate(function, &a, &[]), DG_GL_ERROR_TEXTURE);
    }
}

#[test]
fn texture_control_names_are_sized_and_hinted() {
    assert_eq!(texture_params(GL_TEXTURE_2D, GL_GENERATE_MIPMAP), 1);
    assert_eq!(texture_params(GL_TEXTURE_1D, GL_GENERATE_MIPMAP), 1);
    assert_eq!(texture_params(GL_PROXY_TEXTURE_2D, GL_GENERATE_MIPMAP), 0);
    assert_eq!(query_state_count(GL_GENERATE_MIPMAP_HINT), 1);
    let mut a = [0u32; 8];
    a[0] = GL_GENERATE_MIPMAP_HINT;
    a[1] = GL_NICEST;
    assert_eq!(call_validate(FEnum_glHint, &a), 0);
    a[1] = GL_GENERATE_MIPMAP;
    assert_eq!(call_validate(FEnum_glHint, &a), DG_GL_ERROR_UNSUPPORTED);
}

#[test]
fn s3tc_record_shapes_are_exact_and_level_rules_are_left_to_execution() {
    let image = |a: [u32; 7], n: usize| {
        let mut w = [0u32; 8];
        w[..7].copy_from_slice(&a);
        data_validate(FEnum_glCompressedTexImage2D, &w, &vec![0u8; n])
    };
    let dxt1 = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
    let dxt5 = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    // Partial blocks round up; payload is empty or exactly the image.
    assert_eq!(image([GL_TEXTURE_2D, 0, dxt1, 6, 5, 0, 32], 32), 0);
    assert_eq!(image([GL_TEXTURE_2D, 0, dxt1, 6, 5, 0, 32], 0), 0);
    assert_eq!(image([GL_TEXTURE_2D, 0, dxt5, 4, 4, 0, 16], 16), 0);
    assert_eq!(image([GL_PROXY_TEXTURE_2D, 0, dxt5, 4, 4, 0, 16], 0), 0);
    for (bad, n) in [
        ([GL_PROXY_TEXTURE_2D, 0, dxt5, 4, 4, 0, 16], 16), // proxy with payload
        ([GL_TEXTURE_2D, 0, dxt1, 6, 5, 0, 24], 24),       // wrong image size
        ([GL_TEXTURE_2D, 0, dxt1, 6, 5, 0, 32], 31),       // short payload
        ([GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, 8], 8),     // not S3TC
        ([GL_TEXTURE_1D, 0, dxt1, 4, 4, 0, 8], 8),         // 2D only
        ([GL_TEXTURE_2D, 0, dxt1, 4, 4, 1, 8], 8),         // no borders
        ([GL_TEXTURE_2D, 1, dxt1, 2048, 4, 0, 4096], 4096), // beyond level 1 limit
        ([GL_TEXTURE_2D, 12, dxt1, 1, 1, 0, 8], 8),        // beyond the last level
    ] {
        assert_eq!(image(bad, n), DG_GL_ERROR_TEXTURE, "{bad:?}");
    }
    let sub =
        |a: [u32; 8], n: usize| data_validate(FEnum_glCompressedTexSubImage2D, &a, &vec![0u8; n]);
    assert_eq!(sub([GL_TEXTURE_2D, 0, 4, 8, 6, 4, dxt1, 16], 16), 0);
    for (bad, n) in [
        ([GL_TEXTURE_2D, 0, 2, 0, 4, 4, dxt1, 8], 8), // unaligned offset
        ([GL_TEXTURE_2D, 0, 0, 0, 0, 4, dxt1, 0], 0), // empty update
        ([GL_TEXTURE_2D, 0, 0, 0, 4, 4, dxt1, 8], 0), // missing payload
        ([GL_TEXTURE_2D, 0, 0, 0, 4, 4, dxt5, 8], 8), // DXT5 block size
        ([GL_TEXTURE_2D, 0, 0, 0, 4, 4, GL_RGBA, 8], 8),
    ] {
        assert_eq!(sub(bad, n), DG_GL_ERROR_TEXTURE, "{bad:?}");
    }
    // Width 6 at x=4 is not judged here: whether it reaches the edge depends on
    // the level, so execution reports GL_INVALID_OPERATION instead.
    assert_eq!(sub([GL_TEXTURE_2D, 0, 4, 0, 6, 4, dxt1, 16], 16), 0);
    assert_eq!(
        dreamgpu_gl_function_words(FEnum_glCompressedTexImage2D),
        DG_GL_FUNCTION_INLINE_DATA | 7
    );
    assert_eq!(
        dreamgpu_gl_function_words(FEnum_glGetCompressedTexImage),
        DG_GL_FUNCTION_QUERY | 3
    );
    assert_eq!(query_state_count(GL_COMPRESSED_TEXTURE_FORMATS), 4);
    assert_eq!(query_state_count(GL_NUM_COMPRESSED_TEXTURE_FORMATS), 1);
    // TexImage2D accepts compressed internal formats for 2D only.
    let mut t = [GL_TEXTURE_2D, 0, dxt5, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE];
    assert_eq!(data_validate(FEnum_glTexImage2D, &t, &[0; 64]), 0);
    t[2] = GL_COMPRESSED_RGB;
    assert_eq!(data_validate(FEnum_glTexImage2D, &t, &[]), 0);
    let one = [GL_TEXTURE_1D, 0, dxt1, 4, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE];
    assert_eq!(
        data_validate(FEnum_glTexImage1D, &one, &[0; 16]),
        DG_GL_ERROR_TEXTURE
    );
}

#[test]
fn cube_map_targets_faces_and_proxies_have_exact_record_shapes() {
    const FACES: [u32; 6] = [
        GL_TEXTURE_CUBE_MAP_POSITIVE_X,
        GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
        GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
        GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
    ];
    // The binding target takes every 2D parameter plus WRAP_R, which no other
    // target accepts; faces and the proxy are never parameter targets.
    for pname in [
        GL_TEXTURE_MIN_FILTER,
        GL_TEXTURE_WRAP_S,
        GL_TEXTURE_MAX_LEVEL,
        GL_TEXTURE_MAX_ANISOTROPY_EXT,
        GL_GENERATE_MIPMAP,
        GL_TEXTURE_WRAP_R,
    ] {
        assert_eq!(texture_params(GL_TEXTURE_CUBE_MAP, pname), 1, "{pname:#x}");
    }
    assert_eq!(
        texture_params(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BORDER_COLOR),
        4
    );
    // TEXTURE_WRAP_R is a parameter of every binding target (GL 1.2 and
    // EXT_texture3D); the frontend gates it, never a face or a proxy.
    for target in [
        GL_TEXTURE_1D,
        GL_TEXTURE_2D,
        GL_TEXTURE_CUBE_MAP,
        GL_TEXTURE_3D,
    ] {
        assert_eq!(texture_params(target, GL_TEXTURE_WRAP_R), 1, "{target:#x}");
    }
    for target in [FACES[0], GL_PROXY_TEXTURE_CUBE_MAP, GL_PROXY_TEXTURE_3D] {
        assert_eq!(texture_params(target, GL_TEXTURE_WRAP_R), 0, "{target:#x}");
    }
    assert_eq!(texture_params(FACES[2], GL_TEXTURE_MIN_FILTER), 0);
    let mut v = [0u32; 8];
    v[..2].copy_from_slice(&[GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R]);
    assert_eq!(data_validate(FEnum_glTexParameteriv, &v, &[0; 4]), 0);
    // NORMAL_MAP/REFLECTION_MAP travel as TEXTURE_GEN_MODE values of S, T and R.
    for coord in [GL_S, GL_T, GL_R] {
        assert_eq!(texgen_params(coord, GL_TEXTURE_GEN_MODE), 1);
    }
    assert!(query_cap(GL_TEXTURE_CUBE_MAP) && query_cap(GL_TEXTURE_GEN_R));
    assert!(!query_cap(FACES[0]));

    // Every face is a 2D-shaped image: square, borderless, 2D level limits.
    let image = |target: u32, w: u32, h: u32, border: u32, n: usize| {
        let a = [target, 0, GL_RGBA8, w, h, border, GL_RGBA, GL_UNSIGNED_BYTE];
        data_validate(FEnum_glTexImage2D, &a, &vec![0; n])
    };
    for face in FACES {
        assert_eq!(image(face, 4, 4, 0, 64), 0);
        assert_eq!(image(face, 4, 4, 0, 0), 0); // allocation only
        assert_eq!(image(face, 0, 0, 0, 0), 0); // empty definition
        assert_eq!(image(face, 4, 2, 0, 32), DG_GL_ERROR_TEXTURE); // not square
        assert_eq!(image(face, 6, 6, 1, 144), DG_GL_ERROR_TEXTURE); // bordered
        let sub = [face, 0, 1, 1, 2, 3, GL_RGBA, GL_UNSIGNED_BYTE];
        assert_eq!(data_validate(FEnum_glTexSubImage2D, &sub, &[0; 24]), 0);
    }
    assert_eq!(image(2048, 4, 4, 0, 64), DG_GL_ERROR_TEXTURE);
    let mut level = [
        FACES[1],
        1,
        GL_RGBA8,
        2048,
        2048,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
    ];
    assert_eq!(
        data_validate(FEnum_glTexImage2D, &level, &[]),
        DG_GL_ERROR_TEXTURE
    );
    level[3] = 1024;
    level[4] = 1024;
    assert_eq!(data_validate(FEnum_glTexImage2D, &level, &[]), 0);
    // The binding target and the face after NEGATIVE_Z are not image targets.
    for target in [GL_TEXTURE_CUBE_MAP, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z + 1] {
        assert_eq!(
            image(target, 4, 4, 0, 64),
            DG_GL_ERROR_TEXTURE,
            "{target:#x}"
        );
    }
    let one = [FACES[0], 0, GL_RGBA8, 4, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE];
    assert_eq!(
        data_validate(FEnum_glTexImage1D, &one, &[0; 16]),
        DG_GL_ERROR_TEXTURE
    );
    // The proxy: square, borderless, compressed formats allowed, never data.
    let mut proxy = [
        GL_PROXY_TEXTURE_CUBE_MAP,
        0,
        GL_RGBA8,
        2048,
        2048,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
    ];
    assert_eq!(data_validate(FEnum_glTexImage2D, &proxy, &[]), 0);
    assert_eq!(
        data_validate(FEnum_glTexImage2D, &proxy, &[0; 4]),
        DG_GL_ERROR_TEXTURE
    );
    proxy[2] = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    assert_eq!(data_validate(FEnum_glTexImage2D, &proxy, &[]), 0);
    proxy[4] = 1024;
    assert_eq!(
        data_validate(FEnum_glTexImage2D, &proxy, &[]),
        DG_GL_ERROR_TEXTURE
    );
    proxy[4] = 2048;
    proxy[5] = 1;
    assert_eq!(
        data_validate(FEnum_glTexImage2D, &proxy, &[]),
        DG_GL_ERROR_TEXTURE
    );

    // Copies: faces like 2D, CopyTexImage2D square and borderless.
    let copy = |target: u32, w: u32, h: u32, border: u32| {
        call_validate(
            FEnum_glCopyTexImage2D,
            &[target, 0, GL_RGBA8, 0, 0, w, h, border],
        )
    };
    assert_eq!(copy(FACES[3], 8, 8, 0), 0);
    assert_eq!(copy(FACES[3], 8, 4, 0), DG_GL_ERROR_TEXTURE);
    assert_eq!(copy(FACES[3], 10, 10, 1), DG_GL_ERROR_TEXTURE);
    assert_eq!(copy(GL_TEXTURE_2D, 8, 4, 0), 0);
    assert_eq!(copy(GL_TEXTURE_CUBE_MAP, 8, 8, 0), DG_GL_ERROR_TEXTURE);
    assert_eq!(
        call_validate(FEnum_glCopyTexSubImage2D, &[FACES[4], 0, 1, 2, 0, 0, 3, 5]),
        0
    );

    // S3TC on faces: square images, aligned updates; the cube proxy too.
    let dxt1 = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
    let compressed = |a: [u32; 7], n: usize| {
        let mut w = [0u32; 8];
        w[..7].copy_from_slice(&a);
        data_validate(FEnum_glCompressedTexImage2D, &w, &vec![0u8; n])
    };
    assert_eq!(compressed([FACES[5], 0, dxt1, 8, 8, 0, 32], 32), 0);
    assert_eq!(
        compressed([GL_PROXY_TEXTURE_CUBE_MAP, 0, dxt1, 8, 8, 0, 32], 0),
        0
    );
    for (bad, n) in [
        ([FACES[5], 0, dxt1, 8, 4, 0, 16], 16),
        ([GL_PROXY_TEXTURE_CUBE_MAP, 0, dxt1, 8, 4, 0, 16], 0),
        ([GL_TEXTURE_CUBE_MAP, 0, dxt1, 8, 8, 0, 32], 32),
    ] {
        assert_eq!(compressed(bad, n), DG_GL_ERROR_TEXTURE, "{bad:?}");
    }
    let sub = [FACES[0], 0, 4, 0, 4, 4, dxt1, 8];
    assert_eq!(
        data_validate(FEnum_glCompressedTexSubImage2D, &sub, &[0; 8]),
        0
    );

    // Queries: per-face levels and reads, the proxy, parameters and state.
    for face in FACES {
        assert_eq!(
            query_shape(FEnum_glGetTexLevelParameteriv, [face, 3, GL_TEXTURE_WIDTH]),
            (1, DG_GL_RESULT_INT)
        );
        assert_eq!(query_shape(FEnum_glGetTexImage, [face, 0, 0]).0, 128);
        assert_eq!(
            query_shape(FEnum_glGetCompressedTexImage, [face, 4 << 16, 0]).0,
            4
        );
    }
    assert_eq!(
        query_shape(
            FEnum_glGetTexLevelParameterfv,
            [GL_PROXY_TEXTURE_CUBE_MAP, 0, GL_TEXTURE_HEIGHT]
        ),
        (1, DG_GL_RESULT_FLOAT)
    );
    for target in [GL_TEXTURE_CUBE_MAP, GL_PROXY_TEXTURE_CUBE_MAP] {
        assert_eq!(query_shape(FEnum_glGetTexImage, [target, 0, 0]).0, 0);
    }
    assert_eq!(
        query_shape(
            FEnum_glGetTexLevelParameteriv,
            [GL_TEXTURE_CUBE_MAP, 0, GL_TEXTURE_WIDTH]
        )
        .0,
        0
    );
    assert_eq!(
        query_shape(
            FEnum_glGetTexParameteriv,
            [GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, 0]
        ),
        (1, DG_GL_RESULT_INT)
    );
    assert_eq!(
        query_shape(
            FEnum_glGetTexParameteriv,
            [GL_TEXTURE_CUBE_MAP, GL_TEXTURE_RESIDENT, 0]
        )
        .0,
        1
    );
    assert_eq!(
        query_shape(FEnum_glGetTexParameteriv, [FACES[0], GL_TEXTURE_WRAP_S, 0]).0,
        0
    );
    for pname in [
        GL_TEXTURE_CUBE_MAP,
        GL_TEXTURE_BINDING_CUBE_MAP,
        GL_MAX_CUBE_MAP_TEXTURE_SIZE,
    ] {
        assert_eq!(query_state_count(pname), 1, "{pname:#x}");
    }
}

#[test]
fn vertex_attribute_records_draws_and_queries_have_exact_shapes() {
    let word_bytes = |a: &[u32]| a.iter().flat_map(|w| w.to_le_bytes()).collect::<Vec<u8>>();
    // VertexAttrib4fARB: index and four float words, index below 16.
    assert_eq!(function_words(FEnum_glVertexAttrib4fARB), 5);
    for (index, expected) in [(0, 0), (15, 0), (16, DG_GL_ERROR_UNSUPPORTED)] {
        let args = word_bytes(&[index, 0, 0, 0, 0x3f80_0000]);
        assert_eq!(
            unsafe { dreamgpu_gl_call_validate(FEnum_glVertexAttrib4fARB, args.as_ptr()) },
            expected
        );
    }
    // Only the ARB float form exists on the wire.
    for f in [
        FEnum_glVertexAttrib4f,
        FEnum_glVertexAttrib4fvARB,
        FEnum_glVertexAttrib4NubvARB,
        FEnum_glEnableVertexAttribArrayARB,
        FEnum_glGetVertexAttribivARB,
        FEnum_glGetVertexAttribPointervARB,
    ] {
        assert_eq!(function_words(f), u32::MAX);
    }
    // The attribute draw: five scalar words, compact only.
    assert_eq!(
        function_words(FEnum_glVertexAttribPointerARB),
        DG_GL_FUNCTION_INLINE_DATA | 5
    );
    let data = crate::arrays::raw::tests::attribute_packet();
    let mask = DG_GL_ARRAY_RAW | DG_GL_ARRAY_TEXCOORD;
    let generic = 1 | 1 << 3 | 1 << 15;
    let validate = |a: &[u32], data: &[u8]| {
        let args = word_bytes(a);
        unsafe {
            dreamgpu_gl_data_validate(
                FEnum_glVertexAttribPointerARB,
                args.as_ptr(),
                data.as_ptr(),
                data.len() as u32,
            )
        }
    };
    assert_eq!(validate(&[GL_TRIANGLES, 0, 3, mask, generic], &data), 0);
    for bad in [
        [GL_POLYGON + 1, 0, 3, mask, generic],
        [GL_TRIANGLES, 1, 3, mask, generic],
        [GL_TRIANGLES, 0, 3, mask & !DG_GL_ARRAY_RAW, generic],
        [GL_TRIANGLES, 0, 3, mask, generic & !1],
        [GL_TRIANGLES, 0, 2, mask, generic],
    ] {
        assert_eq!(validate(&bad, &data), DG_GL_ERROR_BATCH, "{bad:x?}");
    }
    // Queries: the current value of attributes 1-15, the limit and the modes.
    let query = |f: u32, a: [u32; 3]| (query_validate(f, a), query_shape(f, a));
    assert_eq!(
        query(
            FEnum_glGetVertexAttribfvARB,
            [15, GL_CURRENT_VERTEX_ATTRIB_ARB, 0]
        ),
        (0, (4, DG_GL_RESULT_FLOAT))
    );
    for a in [
        [0, GL_CURRENT_VERTEX_ATTRIB_ARB, 0],
        [16, GL_CURRENT_VERTEX_ATTRIB_ARB, 0],
        [1, GL_VERTEX_ATTRIB_ARRAY_SIZE_ARB, 0],
    ] {
        assert_eq!(
            query_validate(FEnum_glGetVertexAttribfvARB, a),
            DG_GL_ERROR_UNSUPPORTED
        );
    }
    assert_eq!(
        query_validate(
            FEnum_glGetVertexAttribfvARB,
            [1, GL_CURRENT_VERTEX_ATTRIB_ARB, 1]
        ),
        DG_GL_ERROR_BATCH
    );
    assert_eq!(
        query(FEnum_glGetIntegerv, [GL_MAX_VERTEX_ATTRIBS_ARB, 0, 0]),
        (0, (1, DG_GL_RESULT_INT))
    );
    for cap in [
        GL_VERTEX_PROGRAM_ARB,
        GL_VERTEX_PROGRAM_POINT_SIZE_ARB,
        GL_VERTEX_PROGRAM_TWO_SIDE_ARB,
    ] {
        assert_eq!(
            query(FEnum_glIsEnabled, [cap, 0, 0]),
            (0, (1, DG_GL_RESULT_BOOL))
        );
        assert_eq!(
            query(FEnum_glGetBooleanv, [cap, 0, 0]),
            (0, (1, DG_GL_RESULT_BOOL))
        );
    }
}
#[test]
fn buffer_object_records_and_the_buffer_draw_have_exact_shapes() {
    let word_bytes = |a: &[u32]| a.iter().flat_map(|w| w.to_le_bytes()).collect::<Vec<u8>>();
    let validate = |f: u32, a: &[u32], data: &[u8]| {
        let args = word_bytes(a);
        unsafe { dreamgpu_gl_data_validate(f, args.as_ptr(), data.as_ptr(), data.len() as u32) }
    };
    assert_eq!(
        function_words(FEnum_glBufferDataARB),
        DG_GL_FUNCTION_INLINE_DATA | 3
    );
    assert_eq!(
        function_words(FEnum_glBufferSubDataARB),
        DG_GL_FUNCTION_INLINE_DATA | 2
    );
    assert_eq!(
        function_words(FEnum_glDeleteBuffersARB),
        DG_GL_FUNCTION_INLINE_DATA | 1
    );
    assert_eq!(
        function_words(FEnum_glDrawRangeElements),
        DG_GL_FUNCTION_INLINE_DATA | 10
    );
    // Bindings, mapping and reads are frontend state: never records.
    for f in [
        FEnum_glBindBufferARB,
        FEnum_glGenBuffersARB,
        FEnum_glIsBufferARB,
        FEnum_glMapBufferARB,
        FEnum_glUnmapBufferARB,
        FEnum_glGetBufferSubDataARB,
        FEnum_glGetBufferParameterivARB,
        FEnum_glGetBufferPointervARB,
        FEnum_glBufferData,
    ] {
        assert_eq!(function_words(f), u32::MAX);
    }
    let data = FEnum_glBufferDataARB;
    assert_eq!(validate(data, &[1, 3, GL_STATIC_DRAW], &[1, 2, 3]), 0);
    assert_eq!(validate(data, &[1, 3, GL_DYNAMIC_READ], &[]), 0);
    for (a, payload) in [
        ([0, 3, GL_STATIC_DRAW], &[1u8, 2, 3][..]),
        ([1, 3, GL_STATIC_DRAW], &[1, 2][..]),
        ([1, 3, 0], &[][..]),
        ([1, DG_GL_MAX_BUFFER_BYTES + 1, GL_STATIC_DRAW], &[][..]),
    ] {
        assert_eq!(validate(data, &a, payload), DG_GL_ERROR_BATCH, "{a:x?}");
    }
    let sub = FEnum_glBufferSubDataARB;
    assert_eq!(validate(sub, &[1, 9], &[1, 2]), 0);
    assert_eq!(validate(sub, &[0, 9], &[1, 2]), DG_GL_ERROR_BATCH);
    assert_eq!(
        validate(sub, &[1, DG_GL_MAX_BUFFER_BYTES], &[1]),
        DG_GL_ERROR_BATCH
    );
    let delete = FEnum_glDeleteBuffersARB;
    assert_eq!(validate(delete, &[2], &word_bytes(&[4, 5])), 0);
    assert_eq!(
        validate(delete, &[3], &word_bytes(&[4, 5])),
        DG_GL_ERROR_BATCH
    );
    assert_eq!(
        validate(delete, &[DG_GL_MAX_BUFFERS + 1], &[]),
        DG_GL_ERROR_BATCH
    );
    // The buffer draw: the shared layout, inline indices within the client vertices.
    let draw = FEnum_glDrawRangeElements;
    let mask = crate::arrays::raw::tests::BUFFER_MASK;
    let inline = crate::arrays::raw::tests::buffer_packet(3, &[5, 7, 6, 5]);
    let a = [
        GL_TRIANGLE_STRIP,
        5,
        4,
        mask,
        2,
        1,
        3,
        GL_UNSIGNED_SHORT,
        0,
        0,
    ];
    assert_eq!(validate(draw, &a, &inline), 0);
    let outside = crate::arrays::raw::tests::buffer_packet(3, &[5, 8, 6, 5]);
    assert_eq!(validate(draw, &a, &outside), DG_GL_ERROR_BATCH);
    let mut bad = a;
    bad[0] = GL_POLYGON + 1;
    assert_eq!(validate(draw, &bad, &inline), DG_GL_ERROR_BATCH);
}
#[test]
fn bgra_buffer_draw_is_the_buffer_draw_with_bgra_buffer_slots() {
    let word_bytes = |a: &[u32]| a.iter().flat_map(|w| w.to_le_bytes()).collect::<Vec<u8>>();
    let validate = |f: u32, a: &[u32], data: &[u8]| {
        let args = word_bytes(a);
        unsafe { dreamgpu_gl_data_validate(f, args.as_ptr(), data.as_ptr(), data.len() as u32) }
    };
    let ext = FEnum_glDrawRangeElementsEXT;
    assert_eq!(function_words(ext), DG_GL_FUNCTION_INLINE_DATA | 10);
    let (a, data) = crate::arrays::raw::tests::bgra_packet();
    assert_eq!(validate(ext, &a, &data), 0);
    // The buffer draw keeps its admission: no BGRA descriptor.
    assert_eq!(
        validate(FEnum_glDrawRangeElements, &a, &data),
        DG_GL_ERROR_BATCH
    );
    // Every buffer draw rule holds, inline indices within the client vertices included.
    let mask = crate::arrays::raw::tests::BUFFER_MASK;
    let i = [
        GL_TRIANGLE_STRIP,
        5,
        4,
        mask,
        2,
        1,
        3,
        GL_UNSIGNED_SHORT,
        0,
        0,
    ];
    let inline = crate::arrays::raw::tests::buffer_packet(3, &[5, 7, 6, 5]);
    assert_eq!(validate(ext, &i, &inline), 0);
    let outside = crate::arrays::raw::tests::buffer_packet(3, &[5, 8, 6, 5]);
    assert_eq!(validate(ext, &i, &outside), DG_GL_ERROR_BATCH);
    let mut bad = a;
    bad[0] = GL_POLYGON + 1;
    assert_eq!(validate(ext, &bad, &data), DG_GL_ERROR_BATCH);
    assert_eq!(validate(ext, &a, &data[..184]), DG_GL_ERROR_BATCH);
}

#[test]
fn rectangle_records_have_one_borderless_uncompressed_level() {
    // ARB_texture_rectangle: TEXTURE_RECTANGLE is a binding, enable and
    // parameter target; GENERATE_MIPMAP is no rectangle parameter.
    for pname in [
        GL_TEXTURE_MIN_FILTER,
        GL_TEXTURE_MAG_FILTER,
        GL_TEXTURE_WRAP_S,
        GL_TEXTURE_WRAP_T,
        GL_TEXTURE_WRAP_R,
        GL_TEXTURE_BASE_LEVEL,
        GL_TEXTURE_MAX_LEVEL,
        GL_TEXTURE_PRIORITY,
        GL_TEXTURE_MAX_ANISOTROPY_EXT,
    ] {
        assert_eq!(
            texture_params(GL_TEXTURE_RECTANGLE_ARB, pname),
            1,
            "{pname:#x}"
        );
    }
    assert_eq!(
        texture_params(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_BORDER_COLOR),
        4
    );
    assert_eq!(
        texture_params(GL_TEXTURE_RECTANGLE_ARB, GL_GENERATE_MIPMAP),
        0
    );
    assert_eq!(texture_params(GL_TEXTURE_2D, GL_GENERATE_MIPMAP), 1);
    assert_eq!(
        texture_params(GL_PROXY_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER),
        0
    );
    assert!(query_cap(GL_TEXTURE_RECTANGLE_ARB));
    assert!(!query_cap(GL_PROXY_TEXTURE_RECTANGLE_ARB));
    for name in [
        GL_TEXTURE_BINDING_RECTANGLE_ARB,
        GL_MAX_RECTANGLE_TEXTURE_SIZE_ARB,
    ] {
        assert_eq!(query_state_count(name), 1, "{name:#x}");
    }

    // Images of any size up to the 2D level-zero limit, level zero only, no
    // border, no compressed internal format (depth images: below).
    let image = |a: [u32; 8], n: usize| data_validate(FEnum_glTexImage2D, &a, &vec![0; n]);
    let rect = GL_TEXTURE_RECTANGLE_ARB;
    let (rgba, ubyte) = (GL_RGBA, GL_UNSIGNED_BYTE);
    assert_eq!(image([rect, 0, GL_RGBA8, 3, 5, 0, rgba, ubyte], 60), 0);
    assert_eq!(image([rect, 0, GL_RGBA8, 640, 480, 0, rgba, ubyte], 0), 0);
    assert_eq!(image([rect, 0, GL_RGB5, 2048, 1, 0, rgba, ubyte], 0), 0);
    assert_eq!(image([rect, 0, GL_RGBA8, 0, 0, 0, rgba, ubyte], 0), 0);
    for (a, n) in [
        ([rect, 1, GL_RGBA8, 3, 5, 0, rgba, ubyte], 60), // level one
        ([rect, 0, GL_RGBA8, 5, 7, 1, rgba, ubyte], 140), // bordered
        ([rect, 0, GL_RGBA8, 2049, 1, 0, rgba, ubyte], 0), // beyond the limit
        (
            [
                rect,
                0,
                GL_COMPRESSED_RGB_S3TC_DXT1_EXT,
                8,
                8,
                0,
                rgba,
                ubyte,
            ],
            0,
        ),
        ([rect, 0, GL_COMPRESSED_RGBA, 8, 8, 0, rgba, ubyte], 0),
    ] {
        assert_eq!(image(a, n), DG_GL_ERROR_TEXTURE, "{a:?}");
    }
    let sub = |a: [u32; 8], n: usize| data_validate(FEnum_glTexSubImage2D, &a, &vec![0; n]);
    assert_eq!(sub([rect, 0, 637, 1, 3, 2, rgba, ubyte], 24), 0);
    assert_eq!(
        sub([rect, 1, 0, 0, 1, 1, rgba, ubyte], 4),
        DG_GL_ERROR_TEXTURE
    );
    // The proxy: level zero, borderless, uncompressed, never data.
    let proxy = GL_PROXY_TEXTURE_RECTANGLE_ARB;
    assert_eq!(
        image([proxy, 0, GL_RGBA8, 2048, 2048, 0, rgba, ubyte], 0),
        0
    );
    for (a, n) in [
        ([proxy, 0, GL_RGBA8, 4, 4, 0, rgba, ubyte], 64),
        ([proxy, 1, GL_RGBA8, 4, 4, 0, rgba, ubyte], 0),
        ([proxy, 0, GL_RGBA8, 6, 6, 1, rgba, ubyte], 0),
        (
            [
                proxy,
                0,
                GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,
                4,
                4,
                0,
                rgba,
                ubyte,
            ],
            0,
        ),
    ] {
        assert_eq!(image(a, n), DG_GL_ERROR_TEXTURE, "{a:?}");
    }
    // Rectangles are no 1D target.
    assert_eq!(
        data_validate(
            FEnum_glTexImage1D,
            &[rect, 0, GL_RGBA8, 4, 1, 0, rgba, ubyte],
            &[0; 16]
        ),
        DG_GL_ERROR_TEXTURE
    );
    // Compressed records never name a rectangle or its proxy.
    let dxt1 = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
    for target in [rect, proxy] {
        assert_eq!(
            data_validate(
                FEnum_glCompressedTexImage2D,
                &[target, 0, dxt1, 8, 8, 0, 32, 0],
                &[]
            ),
            DG_GL_ERROR_TEXTURE
        );
    }
    assert_eq!(
        data_validate(
            FEnum_glCompressedTexSubImage2D,
            &[rect, 0, 0, 0, 4, 4, dxt1, 8],
            &[0; 8]
        ),
        DG_GL_ERROR_TEXTURE
    );

    // Copies: any size, level zero, no border.
    let copy = |a: [u32; 8]| call_validate(FEnum_glCopyTexImage2D, &a);
    assert_eq!(copy([rect, 0, GL_RGBA8, 0, 0, 640, 480, 0]), 0);
    assert_eq!(
        copy([rect, 1, GL_RGBA8, 0, 0, 4, 4, 0]),
        DG_GL_ERROR_TEXTURE
    );
    assert_eq!(
        copy([rect, 0, GL_RGBA8, 0, 0, 6, 6, 1]),
        DG_GL_ERROR_TEXTURE
    );
    // A depth copy defines a depth rectangle (ARB_depth_texture).
    assert_eq!(copy([rect, 0, GL_DEPTH_COMPONENT24, 0, 0, 4, 4, 0]), 0);
    assert_eq!(
        copy([proxy, 0, GL_RGBA8, 0, 0, 4, 4, 0]),
        DG_GL_ERROR_TEXTURE
    );
    let sub_copy =
        |level: u32| call_validate(FEnum_glCopyTexSubImage2D, &[rect, level, 1, 2, 0, 0, 3, 5]);
    assert_eq!(sub_copy(0), 0);
    assert_eq!(sub_copy(1), DG_GL_ERROR_TEXTURE);

    // Queries: level-zero readback and level parameters (also of the proxy);
    // no compressed readback.
    assert_eq!(query_result_bytes(FEnum_glGetTexImage, [rect, 0, 0]), 512);
    assert_eq!(query_result_bytes(FEnum_glGetTexImage, [rect, 1, 0]), 0);
    assert_eq!(
        query_result_bytes(FEnum_glGetCompressedTexImage, [rect, 0, 0]),
        0
    );
    for target in [rect, proxy] {
        assert_eq!(
            query_result_bytes(
                FEnum_glGetTexLevelParameteriv,
                [target, 0, GL_TEXTURE_WIDTH]
            ),
            4
        );
        assert_eq!(
            query_result_bytes(
                FEnum_glGetTexLevelParameteriv,
                [target, 1, GL_TEXTURE_WIDTH]
            ),
            0
        );
    }
    assert_eq!(
        query_result_bytes(FEnum_glGetTexParameteriv, [rect, GL_TEXTURE_RESIDENT, 0]),
        4
    );
    assert_eq!(
        query_result_bytes(FEnum_glGetTexParameteriv, [rect, GL_TEXTURE_WRAP_S, 0]),
        4
    );
    assert_eq!(
        query_result_bytes(FEnum_glGetTexParameteriv, [rect, GL_GENERATE_MIPMAP, 0]),
        0
    );
    assert_eq!(query_result_bytes(FEnum_glIsEnabled, [rect, 0, 0]), 1);
}

/// ARB_depth_texture and ARB_shadow (DG_CAP2_GL_DEPTH_TEXTURE): depth images
/// of TEXTURE_1D, TEXTURE_2D and TEXTURE_RECTANGLE (and their proxies) in the
/// existing records, never of cube maps or 3D textures; the compare and depth
/// texture mode parameters on every target; the level-parameter sizes; and the
/// GetTexImage depth reads.
#[test]
fn depth_texture_records_take_1d_2d_and_rectangle_images_and_compare_parameters() {
    let rect = GL_TEXTURE_RECTANGLE_ARB;
    let (depth, uint, float) = (GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, GL_FLOAT);
    let (ds, packed) = (GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8);
    let image1 = |a: [u32; 8], n: usize| data_validate(FEnum_glTexImage1D, &a, &vec![0; n]);
    let image2 = |a: [u32; 8], n: usize| data_validate(FEnum_glTexImage2D, &a, &vec![0; n]);
    let sub1 = |a: [u32; 8], n: usize| data_validate(FEnum_glTexSubImage1D, &a, &vec![0; n]);
    let sub2 = |a: [u32; 8], n: usize| data_validate(FEnum_glTexSubImage2D, &a, &vec![0; n]);
    for format in [
        GL_DEPTH_COMPONENT,
        GL_DEPTH_COMPONENT16,
        GL_DEPTH_COMPONENT24,
        GL_DEPTH_COMPONENT32,
    ] {
        assert_eq!(
            image1([GL_TEXTURE_1D, 0, format, 8, 1, 0, depth, uint], 32),
            0
        );
        assert_eq!(
            image1([GL_TEXTURE_1D, 2, format, 2, 1, 0, depth, float], 0),
            0
        );
        assert_eq!(
            image1([GL_PROXY_TEXTURE_1D, 0, format, 8, 1, 0, depth, uint], 0),
            0
        );
        assert_eq!(
            image2([rect, 0, format, 37, 23, 0, depth, uint], 37 * 23 * 4),
            0
        );
        assert_eq!(image2([rect, 0, format, 640, 480, 0, depth, uint], 0), 0);
        let proxy = GL_PROXY_TEXTURE_RECTANGLE_ARB;
        assert_eq!(image2([proxy, 0, format, 2048, 9, 0, depth, uint], 0), 0);
        assert_eq!(
            image2([GL_TEXTURE_2D, 1, format, 4, 4, 0, depth, uint], 64),
            0
        );
        // A border, a cube face or a 3D texture never has a depth image.
        assert_eq!(
            image1([GL_TEXTURE_1D, 0, format, 10, 1, 1, depth, uint], 40),
            DG_GL_ERROR_TEXTURE
        );
        assert_eq!(
            image2([rect, 0, format, 6, 6, 1, depth, uint], 0),
            DG_GL_ERROR_TEXTURE
        );
        let face = GL_TEXTURE_CUBE_MAP_POSITIVE_X;
        assert_eq!(
            image2([face, 0, format, 4, 4, 0, depth, uint], 64),
            DG_GL_ERROR_TEXTURE
        );
        assert_eq!(
            image2(
                [GL_PROXY_TEXTURE_CUBE_MAP, 0, format, 4, 4, 0, depth, uint],
                0
            ),
            DG_GL_ERROR_TEXTURE
        );
    }
    for format in [GL_DEPTH_STENCIL, GL_DEPTH24_STENCIL8] {
        assert_eq!(
            image1([GL_TEXTURE_1D, 0, format, 8, 1, 0, ds, packed], 32),
            0
        );
        assert_eq!(image2([rect, 0, format, 3, 5, 0, ds, packed], 60), 0);
    }
    // Depth pixels, every scalar type, into the targets with depth images.
    for (kind, bytes) in [
        (GL_UNSIGNED_BYTE, 1),
        (GL_BYTE, 1),
        (GL_UNSIGNED_SHORT, 2),
        (GL_SHORT, 2),
        (GL_UNSIGNED_INT, 4),
        (GL_INT, 4),
        (GL_FLOAT, 4),
    ] {
        assert_eq!(
            sub1([GL_TEXTURE_1D, 0, 1, 0, 3, 1, depth, kind], 3 * bytes),
            0
        );
        assert_eq!(sub2([rect, 0, 1, 2, 3, 4, depth, kind], 12 * bytes), 0);
        assert_eq!(
            sub2([GL_TEXTURE_2D, 0, 1, 2, 3, 4, depth, kind], 12 * bytes),
            0
        );
        assert_eq!(
            sub2(
                [GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, 0, 0, 0, 1, 1, depth, kind],
                bytes
            ),
            DG_GL_ERROR_TEXTURE
        );
    }
    assert_eq!(sub2([rect, 0, 0, 0, 2, 2, ds, packed], 16), 0);
    assert_eq!(
        sub2([rect, 0, 0, 0, 2, 2, depth, packed], 16),
        DG_GL_ERROR_TEXTURE
    );
    // Copies define depth images of the same targets.
    assert_eq!(
        call_validate(
            FEnum_glCopyTexImage1D,
            &[GL_TEXTURE_1D, 0, GL_DEPTH_COMPONENT24, 0, 0, 16, 0, 0]
        ),
        0
    );
    assert_eq!(
        call_validate(
            FEnum_glCopyTexImage1D,
            &[GL_TEXTURE_1D, 0, GL_DEPTH_COMPONENT24, 0, 0, 18, 1, 0]
        ),
        DG_GL_ERROR_TEXTURE
    );
    for target in [GL_TEXTURE_2D, rect] {
        assert_eq!(
            call_validate(
                FEnum_glCopyTexImage2D,
                &[target, 0, GL_DEPTH_COMPONENT16, 0, 0, 37, 23, 0]
            ),
            0
        );
        assert_eq!(
            call_validate(
                FEnum_glCopyTexImage2D,
                &[target, 0, GL_DEPTH24_STENCIL8, 0, 0, 37, 23, 1]
            ),
            DG_GL_ERROR_TEXTURE
        );
    }
    assert_eq!(
        call_validate(
            FEnum_glCopyTexImage2D,
            &[
                GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
                0,
                GL_DEPTH_COMPONENT,
                0,
                0,
                8,
                8,
                0
            ]
        ),
        DG_GL_ERROR_TEXTURE
    );

    // The compare and depth texture mode parameters exist on every target,
    // with one value; their GetTexParameter answers one value.
    for target in [
        GL_TEXTURE_1D,
        GL_TEXTURE_2D,
        GL_TEXTURE_CUBE_MAP,
        GL_TEXTURE_3D,
        rect,
    ] {
        for pname in [
            GL_TEXTURE_COMPARE_MODE,
            GL_TEXTURE_COMPARE_FUNC,
            GL_DEPTH_TEXTURE_MODE,
        ] {
            assert_eq!(texture_params(target, pname), 1);
            for function in [FEnum_glGetTexParameteriv, FEnum_glGetTexParameterfv] {
                assert_eq!(query_shape(function, [target, pname, 0]).0, 1);
            }
        }
    }
    assert_eq!(
        texture_params(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FAIL_VALUE_ARB),
        0
    );
    assert_eq!(
        texture_params(GL_PROXY_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE),
        0
    );
    for pname in [GL_TEXTURE_DEPTH_SIZE, GL_TEXTURE_STENCIL_SIZE] {
        for (target, level) in [
            (GL_TEXTURE_1D, 3),
            (GL_TEXTURE_2D, 0),
            (rect, 0),
            (GL_PROXY_TEXTURE_RECTANGLE_ARB, 0),
        ] {
            assert_eq!(
                query_shape(FEnum_glGetTexLevelParameteriv, [target, level, pname]),
                (1, DG_GL_RESULT_INT)
            );
        }
    }

    // GetTexImage: DEPTH reads float depth, DEPTH with FLOAT packed words, of
    // 1D, 2D and rectangle levels only.
    let read = |target: u32, word: u32| query_shape(FEnum_glGetTexImage, [target, word, 0]);
    let d = DG_GL_TEXTURE_READ_DEPTH;
    let both = d | DG_GL_TEXTURE_READ_FLOAT;
    assert_eq!(
        read(GL_TEXTURE_2D, d | 3 | (64 << 16)),
        (64, DG_GL_RESULT_FLOAT)
    );
    assert_eq!(read(GL_TEXTURE_1D, d), (128, DG_GL_RESULT_FLOAT));
    assert_eq!(read(rect, both | (5 << 16)), (5, DG_GL_RESULT_INT));
    assert_eq!(
        read(GL_TEXTURE_2D, d | (DG_GL_MAX_READBACK_BYTES / 4) << 16),
        (DG_GL_MAX_READBACK_BYTES / 4, DG_GL_RESULT_FLOAT)
    );
    assert_eq!(
        read(GL_TEXTURE_2D, d | (DG_GL_MAX_READBACK_BYTES / 4 + 1) << 16).0,
        0
    );
    assert_eq!(read(rect, d | 1).0, 0);
    assert_eq!(read(GL_TEXTURE_2D, d | (DG_GL_MAX_TEXTURE_LEVEL + 1)).0, 0);
    assert_eq!(read(GL_TEXTURE_3D, d).0, 0);
    assert_eq!(read(GL_TEXTURE_CUBE_MAP_POSITIVE_X, both).0, 0);
    // Colour reads are unchanged; bit 13 is no level.
    assert_eq!(read(GL_TEXTURE_2D, 0), (128, DG_GL_RESULT_INT));
    assert_eq!(
        read(GL_TEXTURE_2D, DG_GL_TEXTURE_READ_FLOAT | (4 << 16)),
        (16, DG_GL_RESULT_FLOAT)
    );
    assert_eq!(read(GL_TEXTURE_2D, 0x2000).0, 0);
}

#[test]
fn rgb565_is_a_colour_internal_format_of_the_existing_records() {
    // DG_CAP2_GL_RGB565: images, proxies and copies of every colour target.
    let (rgb, short, ubyte) = (GL_RGB, GL_UNSIGNED_SHORT_5_6_5, GL_UNSIGNED_BYTE);
    let image1 = |a: [u32; 8], n: usize| data_validate(FEnum_glTexImage1D, &a, &vec![0; n]);
    let image2 = |a: [u32; 8], n: usize| data_validate(FEnum_glTexImage2D, &a, &vec![0; n]);
    let rgb565 = DG_GL_RGB565;
    assert_eq!(
        image1([GL_TEXTURE_1D, 0, rgb565, 8, 1, 0, rgb, short], 16),
        0
    );
    assert_eq!(
        image1([GL_PROXY_TEXTURE_1D, 0, rgb565, 8, 1, 0, rgb, short], 0),
        0
    );
    for target in [
        GL_TEXTURE_2D,
        GL_PROXY_TEXTURE_2D,
        GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
        GL_PROXY_TEXTURE_CUBE_MAP,
        GL_TEXTURE_RECTANGLE_ARB,
        GL_PROXY_TEXTURE_RECTANGLE_ARB,
    ] {
        let proxy = matches!(
            target,
            GL_PROXY_TEXTURE_2D | GL_PROXY_TEXTURE_CUBE_MAP | GL_PROXY_TEXTURE_RECTANGLE_ARB
        );
        let n = if proxy { 0 } else { 4 * 4 * 2 };
        assert_eq!(
            image2([target, 0, rgb565, 4, 4, 0, rgb, short], n),
            0,
            "{target:#x}"
        );
        assert_eq!(
            image2([target, 0, rgb565, 4, 4, 0, GL_RGBA, ubyte], 0),
            0,
            "{target:#x}"
        );
    }
    assert_eq!(
        call_validate(
            FEnum_glCopyTexImage1D,
            &[GL_TEXTURE_1D, 0, rgb565, 0, 0, 16, 0, 0]
        ),
        0
    );
    for target in [
        GL_TEXTURE_2D,
        GL_TEXTURE_RECTANGLE_ARB,
        GL_TEXTURE_CUBE_MAP_POSITIVE_X,
    ] {
        assert_eq!(
            call_validate(FEnum_glCopyTexImage2D, &[target, 0, rgb565, 0, 0, 8, 8, 0]),
            0,
            "{target:#x}"
        );
    }
    // The neighbouring ES formats stay unknown: RGB565 is the only one.
    for format in [rgb565 - 1, rgb565 + 1] {
        assert_eq!(
            image2([GL_TEXTURE_2D, 0, format, 4, 4, 0, GL_RGBA, ubyte], 0),
            DG_GL_ERROR_TEXTURE
        );
        assert_eq!(
            call_validate(
                FEnum_glCopyTexImage2D,
                &[GL_TEXTURE_2D, 0, format, 0, 0, 8, 8, 0]
            ),
            DG_GL_ERROR_TEXTURE
        );
    }
    let mut volume = [0u32; 10];
    volume[..9].copy_from_slice(&[GL_TEXTURE_3D, 0, rgb565, 2, 2, 2, 0, rgb, short]);
    assert_eq!(
        crate::texture::volume::validate(FEnum_glTexImage3D, &volume, &[0; 16]),
        0
    );
    volume[2] = rgb565 + 1;
    assert_eq!(
        crate::texture::volume::validate(FEnum_glTexImage3D, &volume, &[0; 16]),
        DG_GL_ERROR_TEXTURE
    );
    // A renderbuffer is charged like the other colour formats.
    assert_eq!(crate::framebuffer::renderbuffer_bytes(rgb565), 4);
    assert_eq!(crate::framebuffer::renderbuffer_bytes(rgb565 + 1), 0);
    let mut storage = [0u32; 10];
    storage[..4].copy_from_slice(&[GL_RENDERBUFFER, rgb565, 16, 16]);
    assert!(crate::framebuffer::call_shape(
        FEnum_glRenderbufferStorage,
        &storage
    ));
}

#[test]
fn gl_1_2_core_names_are_texture_parameters_and_state_queries() {
    // DG_CAP2_GL_CORE_1_2: the LOD clamps on every binding target (never a
    // face or a proxy), in scalar and vector records and as GetTexParameter names.
    for target in [
        GL_TEXTURE_1D,
        GL_TEXTURE_2D,
        GL_TEXTURE_CUBE_MAP,
        GL_TEXTURE_3D,
        GL_TEXTURE_RECTANGLE_ARB,
    ] {
        for pname in [GL_TEXTURE_MIN_LOD, GL_TEXTURE_MAX_LOD] {
            assert_eq!(texture_params(target, pname), 1, "{target:#x} {pname:#x}");
        }
    }
    for target in [
        GL_PROXY_TEXTURE_2D,
        GL_PROXY_TEXTURE_3D,
        GL_TEXTURE_CUBE_MAP_POSITIVE_X,
    ] {
        assert_eq!(texture_params(target, GL_TEXTURE_MIN_LOD), 0, "{target:#x}");
    }
    let mut v = [0u32; 8];
    v[..2].copy_from_slice(&[GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD]);
    assert_eq!(data_validate(FEnum_glTexParameterfv, &v, &[0; 4]), 0);
    // RESCALE_NORMAL, which Enable always passed natively, is a query name;
    // the draw-range hints answer the native values.
    assert!(query_cap(GL_RESCALE_NORMAL));
    assert_eq!(query_state_count(GL_RESCALE_NORMAL), 1);
    assert_eq!(query_state_count(GL_MAX_ELEMENTS_VERTICES), 1);
    assert_eq!(query_state_count(GL_MAX_ELEMENTS_INDICES), 1);
    assert_eq!(DG_CAP2_GL_CORE_1_2, 8);
}
