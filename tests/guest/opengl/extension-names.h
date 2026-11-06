/* SPDX-License-Identifier: GPL-2.0-or-later
 * The frontend's GL_EXTENSIONS model for the suites: every name in order with
 * its gate, a DG_CAP_GL_* bit or zero for secondary colour (negotiated by its
 * own query). Legacy names come first because games of 1997-2001 search only
 * a fixed-size copy of the string (Quake III 1.11: 1023 characters). */
#ifndef JGL_TEST_EXTENSION_NAMES_H
#define JGL_TEST_EXTENSION_NAMES_H
#include <string.h>
typedef struct {
    const char *Name;
    unsigned Cap;
} JGL_TEST_EXTENSION;
static const JGL_TEST_EXTENSION ExtensionNames[] = {
    {"GL_ARB_multitexture", DG_CAP_GL_MULTITEXTURE},
    {"GL_EXT_compiled_vertex_array", DG_CAP_GL_VERTEX_BUFFER_OBJECT},
    {"GL_ARB_texture_env_add", DG_CAP_GL_FIXED_STATE},
    {"GL_EXT_texture_env_add", DG_CAP_GL_FIXED_STATE},
    {"GL_ARB_texture_env_combine", DG_CAP_GL_MULTITEXTURE},
    {"GL_EXT_texture_env_combine", DG_CAP_GL_MULTITEXTURE},
    {"GL_ARB_texture_env_dot3", DG_CAP_GL_MULTITEXTURE},
    {"GL_EXT_texture_filter_anisotropic", DG_CAP_GL_FIXED_STATE},
    {"GL_EXT_texture_edge_clamp", DG_CAP_GL_FIXED_STATE},
    {"GL_SGIS_texture_edge_clamp", DG_CAP_GL_FIXED_STATE},
    {"GL_ARB_point_parameters", DG_CAP_GL_FIXED_STATE},
    {"GL_EXT_point_parameters", DG_CAP_GL_FIXED_STATE},
    {"GL_ARB_texture_compression", DG_CAP_GL_COMPRESSED_TEXTURES},
    {"GL_EXT_texture_compression_s3tc", DG_CAP_GL_COMPRESSED_TEXTURES},
    {"GL_EXT_secondary_color", 0},
    {"GL_EXT_separate_specular_color", 0},
    {"GL_EXT_fog_coord", DG_CAP_GL_FOG_COORD},
    {"GL_ARB_texture_cube_map", DG_CAP_GL_TEXTURE_CUBE_MAP},
    {"GL_EXT_texture_cube_map", DG_CAP_GL_TEXTURE_CUBE_MAP},
    {"GL_EXT_blend_color", DG_CAP_GL_FIXED_STATE},
    {"GL_EXT_blend_minmax", DG_CAP_GL_FIXED_STATE},
    {"GL_EXT_blend_subtract", DG_CAP_GL_FIXED_STATE},
    {"GL_EXT_stencil_wrap", DG_CAP_GL_FIXED_STATE},
    {"GL_EXT_texture_lod_bias", DG_CAP_GL_FIXED_STATE},
    {"GL_EXT_texture3D", DG_CAP_GL_TEXTURE_3D},
    {"GL_ARB_texture_border_clamp", DG_CAP_GL_FIXED_STATE},
    {"GL_ARB_texture_mirrored_repeat", DG_CAP_GL_FIXED_STATE},
    {"GL_EXT_blend_func_separate", DG_CAP_GL_FIXED_STATE},
    {"GL_EXT_blend_equation_separate", DG_CAP_GL_FIXED_STATE},
    {"GL_ATI_separate_stencil", DG_CAP_GL_FIXED_STATE},
    {"GL_ARB_point_sprite", DG_CAP_GL_FIXED_STATE},
    {"GL_ATI_texture_env_combine3", DG_CAP_GL_TEXTURE_CONTROL},
    {"GL_ATI_texture_mirror_once", DG_CAP_GL_TEXTURE_CONTROL},
    {"GL_SGIS_generate_mipmap", DG_CAP_GL_TEXTURE_CONTROL},
    {"GL_ARB_fragment_program", DG_CAP_GL_FRAGMENT_PROGRAM},
    {"GL_ARB_vertex_program", DG_CAP_GL_VERTEX_PROGRAM},
    {"GL_ARB_vertex_buffer_object", DG_CAP_GL_VERTEX_BUFFER_OBJECT},
    {"GL_EXT_framebuffer_object", DG_CAP_GL_FRAMEBUFFER_OBJECT},
    {"GL_EXT_framebuffer_blit", DG_CAP_GL_FRAMEBUFFER_OBJECT},
    {"GL_EXT_packed_depth_stencil", DG_CAP_GL_FRAMEBUFFER_OBJECT},
    {"GL_ARB_vertex_array_bgra", DG_CAP_GL_VERTEX_ARRAY_BGRA},
    {"GL_ARB_occlusion_query", DG_CAP_GL_OCCLUSION_QUERY},
    {"GL_NV_fence", DG_CAP_GL_OCCLUSION_QUERY},
    {"GL_ARB_texture_rectangle", DG_CAP_GL_TEXTURE_RECTANGLE},
    {"GL_NV_texture_rectangle", DG_CAP_GL_TEXTURE_RECTANGLE},
};
/* The string a host with `caps` (and secondary colour when `secondary`)
 * negotiates, in a static buffer. */
static const char *ExpectedExtensions(unsigned caps, int secondary) {
    static char out[2048];
    out[0] = 0;
    for (const JGL_TEST_EXTENSION &e : ExtensionNames)
        if (e.Cap ? (caps & e.Cap) != 0 : secondary != 0) {
            if (*out)
                strcat(out, " ");
            strcat(out, e.Name);
        }
    return out;
}
#endif
