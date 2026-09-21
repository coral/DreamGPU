// SPDX-License-Identifier: GPL-2.0-or-later
//! Pure GL payload validation and result sizing. No platform calls or guest pointers retained.
//! Function IDs and limits come from generated bindings to the preserved native ABI.
#![allow(non_upper_case_globals)]
use crate::gl_api::*;

pub(crate) fn light_params(light: u32, pname: u32) -> u32 {
    if !(GL_LIGHT0..=GL_LIGHT7).contains(&light) {
        return 0;
    }
    match pname {
        GL_AMBIENT | GL_DIFFUSE | GL_SPECULAR | GL_POSITION => 4,
        GL_SPOT_DIRECTION => 3,
        GL_SPOT_EXPONENT
        | GL_SPOT_CUTOFF
        | GL_CONSTANT_ATTENUATION
        | GL_LINEAR_ATTENUATION
        | GL_QUADRATIC_ATTENUATION => 1,
        _ => 0,
    }
}

pub(crate) fn material_params(face: u32, pname: u32) -> u32 {
    if !matches!(face, GL_FRONT | GL_BACK | GL_FRONT_AND_BACK) {
        return 0;
    }
    match pname {
        GL_AMBIENT | GL_DIFFUSE | GL_SPECULAR | GL_EMISSION | GL_AMBIENT_AND_DIFFUSE => 4,
        GL_COLOR_INDEXES => 3,
        GL_SHININESS => 1,
        _ => 0,
    }
}

pub(crate) fn fog_params(pname: u32) -> u32 {
    match pname {
        GL_FOG_COLOR => 4,
        GL_FOG_MODE | GL_FOG_DENSITY | GL_FOG_START | GL_FOG_END | GL_FOG_INDEX => 1,
        _ => 0,
    }
}

pub(crate) fn light_model_params(pname: u32) -> u32 {
    match pname {
        GL_LIGHT_MODEL_AMBIENT => 4,
        GL_LIGHT_MODEL_LOCAL_VIEWER | GL_LIGHT_MODEL_TWO_SIDE | GL_LIGHT_MODEL_COLOR_CONTROL => 1,
        _ => 0,
    }
}

pub(crate) fn texgen_params(coord: u32, pname: u32) -> u32 {
    if !matches!(coord, GL_S | GL_T | GL_R | GL_Q) {
        return 0;
    }
    match pname {
        GL_TEXTURE_GEN_MODE => 1,
        GL_OBJECT_PLANE | GL_EYE_PLANE => 4,
        _ => 0,
    }
}

pub(crate) fn texture_params(target: u32, pname: u32) -> u32 {
    if !matches!(target, GL_TEXTURE_1D | GL_TEXTURE_2D) {
        return 0;
    }
    match pname {
        GL_TEXTURE_BORDER_COLOR => 4,
        GL_TEXTURE_MIN_FILTER
        | GL_TEXTURE_MAG_FILTER
        | GL_TEXTURE_WRAP_S
        | GL_TEXTURE_WRAP_T
        | GL_TEXTURE_BASE_LEVEL
        | GL_TEXTURE_MAX_LEVEL
        | GL_TEXTURE_PRIORITY => 1,
        _ => 0,
    }
}

pub(crate) fn texture_env_params(target: u32, pname: u32) -> u32 {
    if target != GL_TEXTURE_ENV {
        return 0;
    }
    match pname {
        GL_TEXTURE_ENV_COLOR => 4,
        GL_TEXTURE_ENV_MODE | GL_COMBINE_RGB | GL_COMBINE_ALPHA | GL_RGB_SCALE | GL_ALPHA_SCALE
        | GL_SOURCE0_RGB | GL_SOURCE1_RGB | GL_SOURCE2_RGB | GL_SOURCE0_ALPHA
        | GL_SOURCE1_ALPHA | GL_SOURCE2_ALPHA | GL_OPERAND0_RGB | GL_OPERAND1_RGB
        | GL_OPERAND2_RGB | GL_OPERAND0_ALPHA | GL_OPERAND1_ALPHA | GL_OPERAND2_ALPHA => 1,
        _ => 0,
    }
}

pub(crate) fn texture_components(format: u32) -> u32 {
    match format {
        GL_COLOR_INDEX | GL_RED | GL_GREEN | GL_BLUE | GL_ALPHA | GL_LUMINANCE => 1,
        GL_LUMINANCE_ALPHA => 2,
        GL_RGB | GL_BGR => 3,
        GL_RGBA | GL_BGRA => 4,
        _ => 0,
    }
}

pub(crate) fn texture_pixel_bytes(format: u32, kind: u32) -> u32 {
    match kind {
        GL_BYTE | GL_UNSIGNED_BYTE => texture_components(format),
        GL_SHORT | GL_UNSIGNED_SHORT => 2 * texture_components(format),
        GL_INT | GL_UNSIGNED_INT | GL_FLOAT => 4 * texture_components(format),
        GL_UNSIGNED_SHORT_5_6_5 if format == GL_RGB => 2,
        GL_UNSIGNED_SHORT_4_4_4_4
        | GL_UNSIGNED_SHORT_5_5_5_1
        | GL_UNSIGNED_SHORT_4_4_4_4_REV
        | GL_UNSIGNED_SHORT_1_5_5_5_REV
            if matches!(format, GL_RGBA | GL_BGRA) =>
        {
            2
        }
        _ => 0,
    }
}

pub(crate) fn index_bytes(kind: u32) -> u32 {
    match kind {
        GL_UNSIGNED_BYTE => 1,
        GL_UNSIGNED_SHORT => 2,
        GL_UNSIGNED_INT => 4,
        _ => 0,
    }
}

pub(crate) fn query_state_count(pname: u32) -> u32 {
    if query_cap(pname) || crate::pixels::state_name(pname) {
        return 1;
    }
    match pname {
        GL_MODELVIEW_MATRIX | GL_PROJECTION_MATRIX | GL_TEXTURE_MATRIX => 16,
        GL_CURRENT_RASTER_COLOR
        | GL_CURRENT_RASTER_TEXTURE_COORDS
        | GL_CURRENT_RASTER_POSITION
        | GL_CURRENT_COLOR
        | GL_CURRENT_SECONDARY_COLOR
        | GL_CURRENT_TEXTURE_COORDS
        | GL_VIEWPORT
        | GL_SCISSOR_BOX
        | GL_MAP2_GRID_DOMAIN
        | GL_ACCUM_CLEAR_VALUE
        | GL_COLOR_CLEAR_VALUE
        | GL_COLOR_WRITEMASK
        | GL_FOG_COLOR
        | GL_LIGHT_MODEL_AMBIENT => 4,
        GL_CURRENT_NORMAL => 3,
        GL_MAP1_GRID_DOMAIN
        | GL_MAP2_GRID_SEGMENTS
        | GL_DEPTH_RANGE
        | GL_MAX_VIEWPORT_DIMS
        | GL_POLYGON_MODE
        | GL_POINT_SIZE_RANGE
        | GL_ALIASED_POINT_SIZE_RANGE
        | GL_ALIASED_LINE_WIDTH_RANGE
        | GL_LINE_WIDTH_RANGE => 2,
        GL_MAX_EVAL_ORDER
        | GL_MAP1_GRID_SEGMENTS
        | GL_CURRENT_INDEX
        | GL_EDGE_FLAG
        | GL_INDEX_CLEAR_VALUE
        | GL_INDEX_WRITEMASK
        | GL_LOGIC_OP_MODE
        | GL_ACCUM_RED_BITS
        | GL_ACCUM_GREEN_BITS
        | GL_ACCUM_BLUE_BITS
        | GL_ACCUM_ALPHA_BITS
        | GL_CURRENT_RASTER_INDEX
        | GL_CURRENT_RASTER_POSITION_VALID
        | GL_CURRENT_RASTER_DISTANCE
        | GL_PACK_SWAP_BYTES
        | GL_PACK_LSB_FIRST
        | GL_PACK_ROW_LENGTH
        | GL_PACK_SKIP_ROWS
        | GL_PACK_SKIP_PIXELS
        | GL_PACK_ALIGNMENT
        | GL_UNPACK_SWAP_BYTES
        | GL_UNPACK_LSB_FIRST
        | GL_UNPACK_ROW_LENGTH
        | GL_UNPACK_SKIP_ROWS
        | GL_UNPACK_SKIP_PIXELS
        | GL_UNPACK_ALIGNMENT
        | GL_TEXTURE_BINDING_1D
        | GL_TEXTURE_BINDING_2D
        | GL_ATTRIB_STACK_DEPTH
        | GL_MAX_ATTRIB_STACK_DEPTH
        | GL_MATRIX_MODE
        | GL_MODELVIEW_STACK_DEPTH
        | GL_PROJECTION_STACK_DEPTH
        | GL_TEXTURE_STACK_DEPTH
        | GL_MAX_MODELVIEW_STACK_DEPTH
        | GL_MAX_PROJECTION_STACK_DEPTH
        | GL_MAX_TEXTURE_STACK_DEPTH
        | GL_MAX_TEXTURE_SIZE
        | GL_BLEND_SRC
        | GL_BLEND_DST
        | GL_DEPTH_FUNC
        | GL_DEPTH_WRITEMASK
        | GL_DEPTH_CLEAR_VALUE
        | GL_STENCIL_CLEAR_VALUE
        | GL_STENCIL_FUNC
        | GL_STENCIL_VALUE_MASK
        | GL_STENCIL_REF
        | GL_STENCIL_FAIL
        | GL_STENCIL_PASS_DEPTH_FAIL
        | GL_STENCIL_PASS_DEPTH_PASS
        | GL_STENCIL_WRITEMASK
        | GL_FOG_MODE
        | GL_FOG_DENSITY
        | GL_FOG_START
        | GL_FOG_END
        | GL_FOG_INDEX
        | GL_LIGHT_MODEL_LOCAL_VIEWER
        | GL_LIGHT_MODEL_TWO_SIDE
        | GL_LIGHT_MODEL_COLOR_CONTROL
        | GL_COLOR_MATERIAL_FACE
        | GL_COLOR_MATERIAL_PARAMETER
        | GL_MAX_LIGHTS
        | GL_MAX_CLIP_PLANES
        | GL_POLYGON_OFFSET_FACTOR
        | GL_POLYGON_OFFSET_UNITS
        | GL_POINT_SIZE
        | GL_POINT_SIZE_GRANULARITY
        | GL_LINE_WIDTH
        | GL_LINE_WIDTH_GRANULARITY
        | GL_LINE_STIPPLE_PATTERN
        | GL_LINE_STIPPLE_REPEAT
        | GL_ALPHA_TEST_FUNC
        | GL_ALPHA_TEST_REF
        | GL_SHADE_MODEL
        | GL_FRONT_FACE
        | GL_CULL_FACE_MODE
        | GL_SUBPIXEL_BITS
        | GL_RED_BITS
        | GL_GREEN_BITS
        | GL_BLUE_BITS
        | GL_ALPHA_BITS
        | GL_DEPTH_BITS
        | GL_STENCIL_BITS
        | GL_RGBA_MODE
        | GL_INDEX_MODE
        | GL_DOUBLEBUFFER
        | GL_DRAW_BUFFER
        | GL_READ_BUFFER
        | GL_PERSPECTIVE_CORRECTION_HINT
        | GL_POINT_SMOOTH_HINT
        | GL_LINE_SMOOTH_HINT
        | GL_POLYGON_SMOOTH_HINT
        | GL_FOG_HINT
        | GL_STEREO
        | GL_AUX_BUFFERS
        | GL_LIST_BASE
        | GL_LIST_INDEX
        | GL_LIST_MODE
        | GL_MAX_LIST_NESTING
        | GL_RENDER_MODE
        | GL_NAME_STACK_DEPTH
        | GL_MAX_NAME_STACK_DEPTH
        | GL_SELECTION_BUFFER_SIZE
        | GL_FEEDBACK_BUFFER_SIZE
        | GL_FEEDBACK_BUFFER_TYPE => 1,
        _ => 0,
    }
}

pub(crate) fn function_words(function: u32) -> u32 {
    match function {
        FEnum_glNewList | FEnum_glEndList | FEnum_glGenLists | FEnum_glIsList
        | FEnum_glDeleteLists => DG_GL_FUNCTION_QUERY | 3,
        FEnum_glCallList | FEnum_glListBase | DG_GL_RECORD_ERROR => 1,
        FEnum_glCallLists => DG_GL_FUNCTION_INLINE_DATA | 1,
        FEnum_glSelectBuffer | FEnum_glFeedbackBuffer | FEnum_glRenderMode => {
            DG_GL_FUNCTION_QUERY | 3
        }
        FEnum_glInitNames | FEnum_glPopName => 0,
        FEnum_glLoadName | FEnum_glPushName | FEnum_glPassThrough => 1,
        FEnum_glMap1d | FEnum_glMap1f => DG_GL_FUNCTION_INLINE_DATA | 2,
        FEnum_glMap2d | FEnum_glMap2f => DG_GL_FUNCTION_INLINE_DATA | 3,
        FEnum_glGetMapdv | FEnum_glGetMapfv | FEnum_glGetMapiv => DG_GL_FUNCTION_QUERY | 3,
        FEnum_glMapGrid1d | FEnum_glEvalMesh2 => 5,
        FEnum_glMapGrid2d => 10,
        FEnum_glEvalCoord1d | FEnum_glEvalPoint2 => 2,
        FEnum_glEvalCoord2d => 4,
        FEnum_glEvalPoint1 => 1,
        FEnum_glEvalMesh1 => 3,
        FEnum_glEdgeFlag | FEnum_glClearIndex | FEnum_glIndexMask | FEnum_glLogicOp => 1,
        FEnum_glIndexd | FEnum_glAccum => 2,
        FEnum_glClearAccum => 4,
        FEnum_glPolygonStipple => DG_GL_FUNCTION_INLINE_DATA,
        FEnum_glGetPolygonStipple => DG_GL_FUNCTION_QUERY,
        FEnum_glBitmap | FEnum_glDrawPixels => DG_GL_FUNCTION_INLINE_DATA | 8,
        FEnum_glPrioritizeTextures => DG_GL_FUNCTION_INLINE_DATA | 1,
        FEnum_glAreTexturesResident => DG_GL_FUNCTION_QUERY | 3,
        FEnum_glCopyTexImage1D => 7,
        FEnum_glCopyTexSubImage1D => 6,
        FEnum_glCopyPixels => 5,
        FEnum_glGetPixelMapfv | FEnum_glGetPixelMapuiv | FEnum_glGetPixelMapusv => {
            DG_GL_FUNCTION_QUERY | 2
        }
        FEnum_glPixelMapfv | FEnum_glPixelMapuiv | FEnum_glPixelMapusv => {
            DG_GL_FUNCTION_INLINE_DATA | 2
        }
        FEnum_glPixelZoom | FEnum_glPixelTransferf | FEnum_glPixelTransferi => 2,
        FEnum_glGetError => DG_GL_FUNCTION_QUERY,
        FEnum_glGetBooleanv | FEnum_glGetIntegerv | FEnum_glGetFloatv | FEnum_glGetDoublev
        | FEnum_glGetString | FEnum_glIsEnabled | FEnum_glIsTexture | FEnum_glGetClipPlane => {
            DG_GL_FUNCTION_QUERY | 1
        }
        FEnum_glGetTexParameterfv
        | FEnum_glGetTexParameteriv
        | FEnum_glGetTexEnvfv
        | FEnum_glGetTexEnviv
        | FEnum_glGetLightfv
        | FEnum_glGetLightiv
        | FEnum_glGetMaterialfv
        | FEnum_glGetMaterialiv
        | FEnum_glGetTexGenfv
        | FEnum_glGetTexGeniv
        | FEnum_glGetTexGendv => DG_GL_FUNCTION_QUERY | 2,
        FEnum_glGetTexLevelParameterfv
        | FEnum_glGetTexLevelParameteriv
        | FEnum_glGetTexImage
        | FEnum_glReadPixels => DG_GL_FUNCTION_QUERY | 3,
        FEnum_glDrawArrays => DG_GL_FUNCTION_INLINE_DATA | 4,
        FEnum_glDrawElements => DG_GL_FUNCTION_INLINE_DATA | 5,
        FEnum_glTexImage1D | FEnum_glTexSubImage1D | FEnum_glTexImage2D | FEnum_glTexSubImage2D => {
            DG_GL_FUNCTION_INLINE_DATA | 8
        }
        FEnum_glDeleteTextures | FEnum_glFogfv | FEnum_glLightModelfv | FEnum_glClipPlane => {
            DG_GL_FUNCTION_INLINE_DATA | 1
        }
        FEnum_glLightfv
        | FEnum_glMaterialfv
        | FEnum_glTexGenfv
        | FEnum_glTexGendv
        | FEnum_glTexParameterfv
        | FEnum_glTexParameteriv
        | FEnum_glTexEnvfv
        | FEnum_glTexEnviv => DG_GL_FUNCTION_INLINE_DATA | 2,
        FEnum_glBindTexture => 2,
        FEnum_glTexParameteri | FEnum_glTexParameterf | FEnum_glTexEnvi | FEnum_glTexEnvf => 3,
        FEnum_glEnd | FEnum_glFlush | FEnum_glFinish | FEnum_glLoadIdentity
        | FEnum_glPushMatrix | FEnum_glPopAttrib | FEnum_glPopMatrix => 0,
        FEnum_glBegin | FEnum_glPushAttrib | FEnum_glClear | FEnum_glEnable | FEnum_glDisable
        | FEnum_glMatrixMode | FEnum_glDepthFunc | FEnum_glDepthMask | FEnum_glClearStencil
        | FEnum_glStencilMask | FEnum_glCullFace | FEnum_glFrontFace | FEnum_glLineWidth
        | FEnum_glPointSize | FEnum_glShadeModel | FEnum_glDrawBuffer | FEnum_glReadBuffer => 1,
        FEnum_glBlendFunc
        | FEnum_glVertex2f
        | FEnum_glTexCoord2f
        | FEnum_glClearDepth
        | FEnum_glAlphaFunc
        | FEnum_glPolygonMode
        | FEnum_glPolygonOffset
        | FEnum_glLineStipple
        | FEnum_glColorMaterial
        | FEnum_glFogf
        | FEnum_glLightModelf
        | FEnum_glHint => 2,
        FEnum_glVertex3f
        | FEnum_glColor3f
        | FEnum_glSecondaryColor3f
        | FEnum_glTranslatef
        | FEnum_glScalef
        | FEnum_glStencilFunc
        | FEnum_glStencilOp
        | FEnum_glNormal3f
        | FEnum_glLightf
        | FEnum_glMaterialf
        | FEnum_glTexGenf => 3,
        FEnum_glClearColor | FEnum_glColor4f | FEnum_glViewport | FEnum_glScissor
        | FEnum_glRotatef | FEnum_glDepthRange | FEnum_glColorMask | FEnum_glVertex4f
        | FEnum_glTexCoord4f => 4,
        FEnum_glRasterPos4d | FEnum_glCopyTexImage2D | FEnum_glCopyTexSubImage2D => 8,
        FEnum_glOrtho | FEnum_glFrustum => 12,
        FEnum_glLoadMatrixf | FEnum_glMultMatrixf => 16,
        FEnum_glLoadMatrixd | FEnum_glMultMatrixd => 32,
        _ => u32::MAX,
    }
}

fn copy_1d_internal_format(format: u32) -> bool {
    matches!(format,GL_ALPHA|GL_LUMINANCE|GL_LUMINANCE_ALPHA|GL_INTENSITY|GL_RGB|GL_RGBA|GL_R3_G3_B2|0x803b..=0x8048|0x804a..=0x804d|0x804f..=0x805b)
}
fn texture_internal_format(format: u32) -> bool {
    (1..=4).contains(&format) || copy_1d_internal_format(format)
}

fn buffer_selection(mode: u32, draw: bool) -> bool {
    match mode {
        GL_FRONT | GL_FRONT_LEFT | GL_BACK | GL_BACK_LEFT | GL_LEFT => true,
        GL_NONE | GL_FRONT_AND_BACK => draw,
        _ => false,
    }
}

pub(crate) fn query_cap(pname: u32) -> bool {
    if crate::evaluator::components(pname).is_some()
        || pname == GL_AUTO_NORMAL
        || (GL_LIGHT0..=GL_LIGHT7).contains(&pname)
        || (GL_CLIP_PLANE0..=GL_CLIP_PLANE5).contains(&pname)
    {
        return true;
    }
    matches!(
        pname,
        GL_COLOR_LOGIC_OP
            | GL_INDEX_LOGIC_OP
            | GL_BLEND
            | GL_DEPTH_TEST
            | GL_STENCIL_TEST
            | GL_SCISSOR_TEST
            | GL_TEXTURE_1D
            | GL_TEXTURE_2D
            | GL_CULL_FACE
            | GL_LIGHTING
            | GL_FOG
            | GL_ALPHA_TEST
            | GL_DITHER
            | GL_NORMALIZE
            | GL_POLYGON_OFFSET_FILL
            | GL_POLYGON_OFFSET_LINE
            | GL_POLYGON_OFFSET_POINT
            | GL_POLYGON_SMOOTH
            | GL_POLYGON_STIPPLE
            | GL_LINE_SMOOTH
            | GL_LINE_STIPPLE
            | GL_POINT_SMOOTH
            | GL_COLOR_MATERIAL
            | GL_COLOR_SUM
            | GL_TEXTURE_GEN_S
            | GL_TEXTURE_GEN_T
            | GL_TEXTURE_GEN_R
            | GL_TEXTURE_GEN_Q
    )
}

fn vector_function(function: u32) -> bool {
    matches!(
        function,
        FEnum_glTexParameterfv
            | FEnum_glTexParameteriv
            | FEnum_glTexEnvfv
            | FEnum_glTexEnviv
            | FEnum_glLightfv
            | FEnum_glMaterialfv
            | FEnum_glFogfv
            | FEnum_glLightModelfv
            | FEnum_glTexGenfv
            | FEnum_glTexGendv
            | FEnum_glClipPlane
    )
}
fn vector_bytes(function: u32, a: &[u32; 8]) -> u32 {
    match function {
        FEnum_glTexParameterfv | FEnum_glTexParameteriv => texture_params(a[0], a[1]) * 4,
        FEnum_glTexEnvfv | FEnum_glTexEnviv => texture_env_params(a[0], a[1]) * 4,
        FEnum_glLightfv => light_params(a[0], a[1]) * 4,
        FEnum_glMaterialfv => material_params(a[0], a[1]) * 4,
        FEnum_glFogfv => fog_params(a[0]) * 4,
        FEnum_glLightModelfv => light_model_params(a[0]) * 4,
        FEnum_glTexGenfv => texgen_params(a[0], a[1]) * 4,
        FEnum_glTexGendv => texgen_params(a[0], a[1]) * 8,
        FEnum_glClipPlane if (GL_CLIP_PLANE0..=GL_CLIP_PLANE5).contains(&a[0]) => 32,
        _ => 0,
    }
}
fn hint_target(target: u32) -> bool {
    matches!(
        target,
        GL_PERSPECTIVE_CORRECTION_HINT
            | GL_POINT_SMOOTH_HINT
            | GL_LINE_SMOOTH_HINT
            | GL_POLYGON_SMOOTH_HINT
            | GL_FOG_HINT
    )
}
fn call_validate(function: u32, a: &[u32; 8]) -> u32 {
    if matches!(function, FEnum_glDrawBuffer | FEnum_glReadBuffer) {
        return if buffer_selection(a[0], function == FEnum_glDrawBuffer) {
            0
        } else {
            DG_GL_ERROR_UNSUPPORTED
        };
    }
    if function == FEnum_glHint {
        return if hint_target(a[0]) && matches!(a[1], GL_DONT_CARE | GL_FASTEST | GL_NICEST) {
            0
        } else {
            DG_GL_ERROR_UNSUPPORTED
        };
    }
    if matches!(function, FEnum_glCopyTexImage1D | FEnum_glCopyTexSubImage1D) {
        let image = function == FEnum_glCopyTexImage1D;
        let width = a[5];
        if a[0] != GL_TEXTURE_1D
            || a[1] > DG_GL_MAX_TEXTURE_LEVEL
            || width > DG_GL_MAX_TEXTURE_DIMENSION + 2
        {
            return DG_GL_ERROR_TEXTURE;
        }
        if image
            && (a[6] > 1
                || width < 2 * a[6]
                || width - 2 * a[6] > (DG_GL_MAX_TEXTURE_DIMENSION >> a[1])
                || a[2] <= 4
                || !copy_1d_internal_format(a[2]))
        {
            return DG_GL_ERROR_TEXTURE;
        }
        return 0;
    }
    let image = function == FEnum_glCopyTexImage2D;
    if !image && function != FEnum_glCopyTexSubImage2D {
        return 0;
    }
    let (wi, hi) = if image { (5, 6) } else { (6, 7) };
    if a[0] != GL_TEXTURE_2D
        || a[1] > DG_GL_MAX_TEXTURE_LEVEL
        || a[wi] > DG_GL_MAX_TEXTURE_DIMENSION + 2
        || a[hi] > DG_GL_MAX_TEXTURE_DIMENSION + 2
        || (image
            && (a[7] > 1
                || a[wi] < 2 * a[7]
                || a[hi] < 2 * a[7]
                || a[2] <= 4
                || !texture_internal_format(a[2])
                || a[wi] - 2 * a[7] > (DG_GL_MAX_TEXTURE_DIMENSION >> a[1])
                || a[hi] - 2 * a[7] > (DG_GL_MAX_TEXTURE_DIMENSION >> a[1])))
    {
        DG_GL_ERROR_TEXTURE
    } else {
        0
    }
}
fn validate_arrays(function: u32, a: &[u32; 8], data: &[u8]) -> u32 {
    let elements = function == FEnum_glDrawElements;
    let vertices = a[if elements { 3 } else { 2 }];
    let attributes = a[if elements { 4 } else { 3 }];
    if attributes & DG_GL_ARRAY_RAW != 0 {
        return if !elements
            && a[0] <= GL_POLYGON
            && a[1] == 0
            && crate::arrays::raw::layout(attributes, vertices, data).is_some()
        {
            0
        } else {
            DG_GL_ERROR_BATCH
        };
    }
    let indices = if elements { a[1] } else { 0 };
    let index_size = if elements { index_bytes(a[2]) } else { 0 };
    let stride = if attributes & (DG_GL_ARRAY_INDEX | DG_GL_ARRAY_EDGE) != 0 {
        DG_GL_VERTEX_EXTENDED_BYTES
    } else if attributes & DG_GL_ARRAY_SECONDARY != 0 {
        DG_GL_VERTEX_SECONDARY_BYTES
    } else {
        DG_GL_VERTEX_BYTES
    };
    let vertex_bytes = u64::from(vertices) * u64::from(stride);
    if a[0] > GL_POLYGON
        || vertices > DG_GL_MAX_VERTICES
        || indices > DG_GL_MAX_INDICES
        || (elements && index_size == 0)
        || (!elements && a[1] != 0)
        || attributes & !DG_GL_ARRAY_MASK != 0
        || attributes & DG_GL_ARRAY_POSITION == 0
        || vertex_bytes + u64::from(indices) * u64::from(index_size) != data.len() as u64
    {
        return DG_GL_ERROR_BATCH;
    }
    for vertex in data[..vertex_bytes as usize].chunks_exact(stride as usize) {
        if word(vertex, DG_GL_VERTEX_RESERVED as usize) != 0
            || (stride >= DG_GL_VERTEX_SECONDARY_BYTES
                && word(vertex, DG_GL_VERTEX_SECONDARY_PAD as usize) != 0)
            || (stride == DG_GL_VERTEX_EXTENDED_BYTES
                && (vertex[DG_GL_VERTEX_EDGE as usize] > 1
                    || vertex[DG_GL_VERTEX_EDGE as usize + 1..]
                        .iter()
                        .any(|&v| v != 0)))
        {
            return DG_GL_ERROR_BATCH;
        }
    }
    if indices != 0 {
        for index in data[vertex_bytes as usize..].chunks_exact(index_size as usize) {
            let value = match index_size {
                1 => u32::from(index[0]),
                2 => u32::from(u16::from_le_bytes(index.try_into().unwrap())),
                _ => u32::from_le_bytes(index.try_into().unwrap()),
            };
            if value >= vertices {
                return DG_GL_ERROR_BATCH;
            }
        }
    }
    0
}
fn data_validate(function: u32, a: &[u32; 8], data: &[u8]) -> u32 {
    if function == FEnum_glCallLists {
        return if a[0] <= DG_GL_MAX_CAPTURE_VALUES && data.len() == a[0] as usize * 4 {
            0
        } else {
            DG_GL_ERROR_BATCH
        };
    }

    if crate::evaluator::map_function(function) {
        return crate::evaluator::validate(function, a, data);
    }
    if function == FEnum_glPolygonStipple {
        return if data.len() == 128 {
            0
        } else {
            DG_GL_ERROR_BATCH
        };
    }
    if crate::pixel_image::image_function(function) {
        return crate::pixel_image::validate(function, a, data);
    }
    if let Some(size) = crate::pixels::map_input(function) {
        return if crate::pixels::map_count(a[0], a[1]) && data.len() == a[1] as usize * size {
            0
        } else {
            DG_GL_ERROR_BATCH
        };
    }
    if vector_function(function) {
        let required = vector_bytes(function, a);
        return if required != 0 && data.len() == required as usize {
            0
        } else {
            DG_GL_ERROR_BATCH
        };
    }
    if matches!(function, FEnum_glDrawArrays | FEnum_glDrawElements) {
        return validate_arrays(function, a, data);
    }
    if function == FEnum_glPrioritizeTextures {
        return if a[0] <= DG_GL_MAX_TEXTURES && data.len() as u64 == u64::from(a[0]) * 8 {
            0
        } else {
            DG_GL_ERROR_BATCH
        };
    }
    if function == FEnum_glDeleteTextures {
        return if a[0] <= DG_GL_MAX_TEXTURES && data.len() as u64 == u64::from(a[0]) * 4 {
            0
        } else {
            DG_GL_ERROR_BATCH
        };
    }
    let image = matches!(function, FEnum_glTexImage2D | FEnum_glTexImage1D);
    let one = matches!(function, FEnum_glTexImage1D | FEnum_glTexSubImage1D);
    let w = a[if image { 3 } else { 4 }];
    let h = a[if image { 4 } else { 5 }];
    let pixel_bytes = texture_pixel_bytes(a[6], a[7]);
    if image
        && a[0]
            == if one {
                GL_PROXY_TEXTURE_1D
            } else {
                GL_PROXY_TEXTURE_2D
            }
    {
        return if a[1] <= DG_GL_MAX_TEXTURE_LEVEL
            && a[5] <= 1
            && w <= i32::MAX as u32
            && h <= i32::MAX as u32
            && (!one || h == 1)
            && pixel_bytes != 0
            && texture_internal_format(a[2])
            && data.is_empty()
        {
            0
        } else {
            DG_GL_ERROR_TEXTURE
        };
    }
    let allocate_only = image && data.is_empty();
    if a[0] != (if one { GL_TEXTURE_1D } else { GL_TEXTURE_2D })
        || (one && (h != 1 || (!image && a[3] != 0)))
        || a[1] > DG_GL_MAX_TEXTURE_LEVEL
        || (!image && (w == 0 || h == 0))
        || w > DG_GL_MAX_TEXTURE_DIMENSION + 2
        || h > DG_GL_MAX_TEXTURE_DIMENSION + 2
        || pixel_bytes == 0
        || (!allocate_only
            && u64::from(w) * u64::from(h) * u64::from(pixel_bytes) != data.len() as u64)
        || (image
            && (a[5] > 1
                || w < 2 * a[5]
                || (!one && h < 2 * a[5])
                || !(if one {
                    (1..=4).contains(&a[2]) || copy_1d_internal_format(a[2])
                } else {
                    texture_internal_format(a[2])
                })
                || w - 2 * a[5] > (DG_GL_MAX_TEXTURE_DIMENSION >> a[1])
                || (!one && h - 2 * a[5] > (DG_GL_MAX_TEXTURE_DIMENSION >> a[1]))))
    {
        DG_GL_ERROR_TEXTURE
    } else {
        0
    }
}
fn word(bytes: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes(bytes[offset..offset + 4].try_into().unwrap())
}

fn query_string(name: u32) -> Option<&'static [u8]> {
    match name {
        GL_VENDOR => Some(b"DreamGPU\0"),
        GL_RENDERER => Some(b"DreamGPU (native host OpenGL)\0"),
        GL_EXTENSIONS => Some(b"\0"),
        _ => None,
    }
}
fn query_shape(function: u32, a: [u32; 3]) -> (u32, u32) {
    let [a, b, d] = a;
    let mut kind = DG_GL_RESULT_INT;
    let count = match function {
        FEnum_glNewList | FEnum_glEndList | FEnum_glGenLists | FEnum_glDeleteLists
        | FEnum_glIsList => {
            if function == FEnum_glIsList {
                kind = DG_GL_RESULT_BOOL;
            }
            crate::lists::query_shape(function, [a, b, d])
        }
        FEnum_glSelectBuffer | FEnum_glFeedbackBuffer | FEnum_glRenderMode => {
            crate::selection::shape(function, [a, b, d])
        }
        FEnum_glGetMapdv | FEnum_glGetMapfv | FEnum_glGetMapiv => {
            kind = match function {
                FEnum_glGetMapdv => DG_GL_RESULT_DOUBLE,
                FEnum_glGetMapfv => DG_GL_RESULT_FLOAT,
                _ => DG_GL_RESULT_INT,
            };
            if crate::evaluator::query_count(a, b, d) {
                d
            } else {
                0
            }
        }
        FEnum_glGetPolygonStipple => {
            if a == 0 && b == 0 && d == 0 {
                32
            } else {
                0
            }
        }
        FEnum_glGetPixelMapfv | FEnum_glGetPixelMapuiv | FEnum_glGetPixelMapusv => {
            kind = if function == FEnum_glGetPixelMapfv {
                DG_GL_RESULT_FLOAT
            } else {
                DG_GL_RESULT_INT
            };
            if d == 0 && crate::pixels::map_count(a, b) {
                b
            } else {
                0
            }
        }
        FEnum_glGetTexImage => {
            let wide = b & DG_GL_TEXTURE_READ_FLOAT != 0;
            let pixel_words = if wide { 4 } else { 1 };
            let n = b >> DG_GL_TEXTURE_READ_COUNT_SHIFT;
            let n = if n != 0 {
                n
            } else {
                DG_GL_MAX_RESULT_BYTES / 4
            };
            if wide {
                kind = DG_GL_RESULT_FLOAT;
            }
            if matches!(a, GL_TEXTURE_1D | GL_TEXTURE_2D)
                && b & DG_GL_TEXTURE_READ_LEVEL_MASK <= DG_GL_MAX_TEXTURE_LEVEL
                && n <= DG_GL_MAX_READBACK_BYTES / (4 * pixel_words)
                && d < (DG_GL_MAX_TEXTURE_DIMENSION + 2) * (DG_GL_MAX_TEXTURE_DIMENSION + 2)
            {
                n * pixel_words
            } else {
                0
            }
        }
        FEnum_glReadPixels => {
            let (w, h) = (d & 0xffff, d >> 16);
            let mode = a & !DG_GL_READ_X_MASK;
            if matches!(mode, DG_GL_READ_DEPTH | DG_GL_READ_RGBA_FLOAT) {
                kind = DG_GL_RESULT_FLOAT;
            }
            if matches!(
                mode,
                0 | DG_GL_READ_DEPTH | DG_GL_READ_STENCIL | DG_GL_READ_RGBA_FLOAT
            ) && a & DG_GL_READ_X_MASK < DG_GL_MAX_DIMENSION
                && b < DG_GL_MAX_DIMENSION
                && w != 0
                && h != 0
                && w <= DG_GL_READ_PIXELS_MAX
                    / h
                    / if mode == DG_GL_READ_RGBA_FLOAT { 4 } else { 1 }
            {
                w * h * if mode == DG_GL_READ_RGBA_FLOAT { 4 } else { 1 }
            } else {
                0
            }
        }
        FEnum_glAreTexturesResident => {
            kind = DG_GL_RESULT_BOOL;
            5
        }
        FEnum_glGetError => u32::from(a == 0 && b == 0 && d == 0),
        FEnum_glIsTexture => {
            kind = DG_GL_RESULT_BOOL;
            u32::from(b == 0 && d == 0)
        }
        FEnum_glIsEnabled => {
            kind = DG_GL_RESULT_BOOL;
            u32::from(b == 0 && d == 0 && query_cap(a))
        }
        FEnum_glGetString => {
            kind = DG_GL_RESULT_STRING;
            if b == 0 && d == 0 {
                query_string(a).map_or(0, |s| s.len() as u32)
            } else {
                0
            }
        }
        FEnum_glGetBooleanv | FEnum_glGetFloatv | FEnum_glGetDoublev | FEnum_glGetIntegerv => {
            kind = match function {
                FEnum_glGetBooleanv => DG_GL_RESULT_BOOL,
                FEnum_glGetFloatv => DG_GL_RESULT_FLOAT,
                FEnum_glGetDoublev => DG_GL_RESULT_DOUBLE,
                _ => DG_GL_RESULT_INT,
            };
            if b == 0 && d == 0 {
                query_state_count(a)
            } else {
                0
            }
        }
        FEnum_glGetTexParameterfv | FEnum_glGetTexParameteriv => {
            kind = if function == FEnum_glGetTexParameterfv {
                DG_GL_RESULT_FLOAT
            } else {
                DG_GL_RESULT_INT
            };
            if d == 0 {
                if matches!(a, GL_TEXTURE_1D | GL_TEXTURE_2D) && b == GL_TEXTURE_RESIDENT {
                    1
                } else {
                    texture_params(a, b)
                }
            } else {
                0
            }
        }
        FEnum_glGetTexLevelParameterfv | FEnum_glGetTexLevelParameteriv => {
            kind = if function == FEnum_glGetTexLevelParameterfv {
                DG_GL_RESULT_FLOAT
            } else {
                DG_GL_RESULT_INT
            };
            u32::from(
                matches!(
                    a,
                    GL_TEXTURE_1D | GL_TEXTURE_2D | GL_PROXY_TEXTURE_1D | GL_PROXY_TEXTURE_2D
                ) && b <= DG_GL_MAX_TEXTURE_LEVEL
                    && matches!(
                        d,
                        GL_TEXTURE_WIDTH
                            | GL_TEXTURE_HEIGHT
                            | GL_TEXTURE_INTERNAL_FORMAT
                            | GL_TEXTURE_BORDER
                            | GL_TEXTURE_RED_SIZE
                            | GL_TEXTURE_GREEN_SIZE
                            | GL_TEXTURE_BLUE_SIZE
                            | GL_TEXTURE_ALPHA_SIZE
                            | GL_TEXTURE_LUMINANCE_SIZE
                            | GL_TEXTURE_INTENSITY_SIZE
                    ),
            )
        }
        FEnum_glGetTexEnvfv | FEnum_glGetTexEnviv => {
            kind = if function == FEnum_glGetTexEnvfv {
                DG_GL_RESULT_FLOAT
            } else {
                DG_GL_RESULT_INT
            };
            if d == 0 {
                texture_env_params(a, b)
            } else {
                0
            }
        }
        FEnum_glGetLightfv | FEnum_glGetLightiv => {
            kind = if function == FEnum_glGetLightfv {
                DG_GL_RESULT_FLOAT
            } else {
                DG_GL_RESULT_INT
            };
            if d == 0 {
                light_params(a, b)
            } else {
                0
            }
        }
        FEnum_glGetMaterialfv | FEnum_glGetMaterialiv => {
            kind = if function == FEnum_glGetMaterialfv {
                DG_GL_RESULT_FLOAT
            } else {
                DG_GL_RESULT_INT
            };
            if d == 0 && a != GL_FRONT_AND_BACK && b != GL_AMBIENT_AND_DIFFUSE {
                material_params(a, b)
            } else {
                0
            }
        }
        FEnum_glGetTexGenfv | FEnum_glGetTexGeniv | FEnum_glGetTexGendv => {
            kind = match function {
                FEnum_glGetTexGendv => DG_GL_RESULT_DOUBLE,
                FEnum_glGetTexGenfv => DG_GL_RESULT_FLOAT,
                _ => DG_GL_RESULT_INT,
            };
            if d == 0 {
                texgen_params(a, b)
            } else {
                0
            }
        }
        FEnum_glGetClipPlane => {
            kind = DG_GL_RESULT_DOUBLE;
            if b == 0 && d == 0 && (GL_CLIP_PLANE0..=GL_CLIP_PLANE5).contains(&a) {
                4
            } else {
                0
            }
        }
        _ => 0,
    };
    (count, kind)
}
fn query_result_bytes(function: u32, args: [u32; 3]) -> u32 {
    let (count, kind) = query_shape(function, args);
    count
        * match kind {
            DG_GL_RESULT_DOUBLE => 8,
            DG_GL_RESULT_INT | DG_GL_RESULT_FLOAT => 4,
            _ => 1,
        }
}
fn query_validate(function: u32, args: [u32; 3]) -> u32 {
    let words = function_words(function);
    if words == u32::MAX || words & DG_GL_FUNCTION_KIND_MASK != DG_GL_FUNCTION_QUERY {
        return DG_GL_ERROR_UNSUPPORTED;
    }
    if args[(words & !DG_GL_FUNCTION_KIND_MASK) as usize..]
        .iter()
        .any(|&v| v != 0)
    {
        return DG_GL_ERROR_BATCH;
    }
    if query_result_bytes(function, args) != 0 {
        0
    } else {
        DG_GL_ERROR_UNSUPPORTED
    }
}

// C callers supply immutable host-owned snapshots. Every pointer is borrowed only
// for the call; size/metadata validation precedes slice construction or indexing.
unsafe fn args8(function: u32, args: *const u8) -> Option<[u32; 8]> {
    let words = function_words(function);
    if words == u32::MAX {
        return None;
    }
    let n = (words & !DG_GL_FUNCTION_KIND_MASK).min(8) as usize;
    if n != 0 && args.is_null() {
        return None;
    }
    let mut a = [0; 8];
    for (i, v) in a.iter_mut().enumerate().take(n) {
        *v = unsafe {
            u32::from_le_bytes(core::ptr::read_unaligned(args.add(i * 4).cast::<[u8; 4]>()))
        };
    }
    Some(a)
}
unsafe fn args3(args: *const u8) -> Option<[u32; 3]> {
    if args.is_null() {
        return None;
    }
    let mut a = [0; 3];
    for (i, v) in a.iter_mut().enumerate() {
        *v = unsafe {
            u32::from_le_bytes(core::ptr::read_unaligned(args.add(i * 4).cast::<[u8; 4]>()))
        };
    }
    Some(a)
}
#[no_mangle]
pub extern "C" fn dreamgpu_gl_function_words(function: u32) -> u32 {
    function_words(function)
}
/// # Safety
/// args supplies the function's immutable little-endian argument words.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_gl_call_validate(function: u32, args: *const u8) -> u32 {
    // Capability/function-class rejection belongs to the outer batch validator.
    // Preserve this helper's no-read success for calls without extra constraints.
    if !matches!(
        function,
        FEnum_glDrawBuffer
            | FEnum_glReadBuffer
            | FEnum_glHint
            | FEnum_glCopyTexImage2D
            | FEnum_glCopyTexSubImage2D
            | FEnum_glCopyTexImage1D
            | FEnum_glCopyTexSubImage1D
    ) {
        return 0;
    }
    unsafe { args8(function, args) }
        .map_or(DG_GL_ERROR_UNSUPPORTED, |a| call_validate(function, &a))
}
/// # Safety
/// args supplies the declared argument words; data supplies bytes immutable bytes.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_gl_data_validate(
    function: u32,
    args: *const u8,
    data: *const u8,
    bytes: u32,
) -> u32 {
    let metadata = function_words(function);
    if metadata == u32::MAX || metadata & DG_GL_FUNCTION_KIND_MASK != DG_GL_FUNCTION_INLINE_DATA {
        return DG_GL_ERROR_UNSUPPORTED;
    }
    let Some(a) = (unsafe { args8(function, args) }) else {
        return DG_GL_ERROR_BATCH;
    };
    if bytes != 0 && data.is_null() {
        return DG_GL_ERROR_BATCH;
    }
    let data = if bytes == 0 {
        &[]
    } else {
        unsafe { core::slice::from_raw_parts(data, bytes as usize) }
    };
    data_validate(function, &a, data)
}
/// # Safety
/// args supplies three immutable little-endian words.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_gl_query_validate(function: u32, args: *const u8) -> u32 {
    unsafe { args3(args) }.map_or(DG_GL_ERROR_BATCH, |a| query_validate(function, a))
}
/// # Safety
/// args supplies three immutable little-endian words.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_gl_query_result_bytes(function: u32, args: *const u8) -> u32 {
    unsafe { args3(args) }.map_or(0, |a| query_result_bytes(function, a))
}
/// # Safety
/// args supplies three immutable words; kind is writable and disjoint.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_gl_query_shape(
    function: u32,
    args: *const u8,
    kind: *mut u32,
) -> u32 {
    if kind.is_null() {
        return 0;
    }
    let Some(a) = (unsafe { args3(args) }) else {
        return 0;
    };
    let (count, t) = query_shape(function, a);
    unsafe {
        *kind = t;
    }
    count
}
#[no_mangle]
pub extern "C" fn dreamgpu_gl_query_string(name: u32) -> *const core::ffi::c_char {
    query_string(name).map_or(core::ptr::null(), |s| s.as_ptr().cast())
}
#[no_mangle]
pub extern "C" fn dreamgpu_gl_vector_function(function: u32) -> u32 {
    u32::from(vector_function(function))
}
/// # Safety
/// args supplies eight host-endian argument words, as used by the platform dispatcher.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_gl_vector_bytes(function: u32, args: *const u32) -> u32 {
    if args.is_null() {
        0
    } else {
        vector_bytes(function, unsafe { &*args.cast::<[u32; 8]>() })
    }
}
#[no_mangle]
pub extern "C" fn dreamgpu_gl_index_bytes(kind: u32) -> u32 {
    index_bytes(kind)
}
#[no_mangle]
pub extern "C" fn dreamgpu_gl_texture_params(target: u32, pname: u32) -> u32 {
    texture_params(target, pname)
}
#[no_mangle]
pub extern "C" fn dreamgpu_gl_texture_env_params(target: u32, pname: u32) -> u32 {
    texture_env_params(target, pname)
}
#[no_mangle]
pub extern "C" fn dreamgpu_gl_buffer_selection(mode: u32, draw: u32) -> u32 {
    u32::from(buffer_selection(mode, draw != 0))
}

#[cfg(test)]
#[path = "gl_validation_tests.rs"]
mod tests;
