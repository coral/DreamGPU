/* SPDX-License-Identifier: GPL-2.0-or-later
 * The frontend's GL_EXTENSIONS model for the suites: every name in order with
 * its gate, DG_CAP_GL_* bits and DG_CAP2_GL_* bits of the second capability
 * word (all required), or neither for secondary colour (negotiated by its own
 * query). Legacy names come first because games of 1997-2001 search only a
 * fixed-size copy of the string (Quake III 1.11: 1023 characters). */
#ifndef JGL_TEST_EXTENSION_NAMES_H
#define JGL_TEST_EXTENSION_NAMES_H
#include <string.h>
typedef struct {
    const char *Name;
    unsigned Cap, Cap2;
} JGL_TEST_EXTENSION;
static const JGL_TEST_EXTENSION ExtensionNames[] = {
    {"GL_ARB_multitexture", DG_CAP_GL_MULTITEXTURE, 0},
    {"GL_EXT_compiled_vertex_array", DG_CAP_GL_VERTEX_BUFFER_OBJECT, 0},
    {"GL_ARB_texture_env_add", DG_CAP_GL_FIXED_STATE, 0},
    {"GL_EXT_texture_env_add", DG_CAP_GL_FIXED_STATE, 0},
    {"GL_ARB_texture_env_combine", DG_CAP_GL_MULTITEXTURE, 0},
    {"GL_EXT_texture_env_combine", DG_CAP_GL_MULTITEXTURE, 0},
    {"GL_ARB_texture_env_dot3", DG_CAP_GL_MULTITEXTURE, 0},
    {"GL_EXT_texture_filter_anisotropic", DG_CAP_GL_FIXED_STATE, 0},
    {"GL_EXT_texture_edge_clamp", DG_CAP_GL_FIXED_STATE, 0},
    {"GL_SGIS_texture_edge_clamp", DG_CAP_GL_FIXED_STATE, 0},
    {"GL_ARB_point_parameters", DG_CAP_GL_FIXED_STATE, 0},
    {"GL_EXT_point_parameters", DG_CAP_GL_FIXED_STATE, 0},
    {"GL_ARB_texture_compression", DG_CAP_GL_COMPRESSED_TEXTURES, 0},
    {"GL_EXT_texture_compression_s3tc", DG_CAP_GL_COMPRESSED_TEXTURES, 0},
    {"GL_EXT_secondary_color", 0, 0},
    {"GL_EXT_separate_specular_color", 0, 0},
    {"GL_EXT_fog_coord", DG_CAP_GL_FOG_COORD, 0},
    {"GL_ARB_texture_cube_map", DG_CAP_GL_TEXTURE_CUBE_MAP, 0},
    {"GL_EXT_texture_cube_map", DG_CAP_GL_TEXTURE_CUBE_MAP, 0},
    {"GL_EXT_blend_color", DG_CAP_GL_FIXED_STATE, 0},
    {"GL_EXT_blend_minmax", DG_CAP_GL_FIXED_STATE, 0},
    {"GL_EXT_blend_subtract", DG_CAP_GL_FIXED_STATE, 0},
    {"GL_EXT_stencil_wrap", DG_CAP_GL_FIXED_STATE, 0},
    {"GL_EXT_texture_lod_bias", DG_CAP_GL_FIXED_STATE, 0},
    {"GL_EXT_texture3D", DG_CAP_GL_TEXTURE_3D, 0},
    {"GL_ARB_texture_border_clamp", DG_CAP_GL_FIXED_STATE, 0},
    {"GL_ARB_texture_mirrored_repeat", DG_CAP_GL_FIXED_STATE, 0},
    {"GL_EXT_blend_func_separate", DG_CAP_GL_FIXED_STATE, 0},
    {"GL_EXT_blend_equation_separate", DG_CAP_GL_FIXED_STATE, 0},
    {"GL_ATI_separate_stencil", DG_CAP_GL_FIXED_STATE, 0},
    {"GL_ARB_point_sprite", DG_CAP_GL_FIXED_STATE, 0},
    {"GL_ATI_texture_env_combine3", DG_CAP_GL_TEXTURE_CONTROL, 0},
    {"GL_ATI_texture_mirror_once", DG_CAP_GL_TEXTURE_CONTROL, 0},
    {"GL_SGIS_generate_mipmap", DG_CAP_GL_TEXTURE_CONTROL, 0},
    {"GL_ARB_fragment_program", DG_CAP_GL_FRAGMENT_PROGRAM, 0},
    {"GL_ARB_vertex_program", DG_CAP_GL_VERTEX_PROGRAM, 0},
    {"GL_ARB_vertex_buffer_object", DG_CAP_GL_VERTEX_BUFFER_OBJECT, 0},
    {"GL_EXT_framebuffer_object", DG_CAP_GL_FRAMEBUFFER_OBJECT, 0},
    {"GL_EXT_framebuffer_blit", DG_CAP_GL_FRAMEBUFFER_OBJECT, 0},
    {"GL_EXT_packed_depth_stencil", DG_CAP_GL_FRAMEBUFFER_OBJECT, 0},
    {"GL_ARB_vertex_array_bgra", DG_CAP_GL_VERTEX_ARRAY_BGRA, 0},
    {"GL_ARB_occlusion_query", DG_CAP_GL_OCCLUSION_QUERY, 0},
    {"GL_NV_fence", DG_CAP_GL_OCCLUSION_QUERY, 0},
    {"GL_ARB_texture_rectangle", DG_CAP_GL_TEXTURE_RECTANGLE, 0},
    {"GL_NV_texture_rectangle", DG_CAP_GL_TEXTURE_RECTANGLE, 0},
    {"GL_ARB_depth_texture", 0, DG_CAP2_GL_DEPTH_TEXTURE},
    {"GL_ARB_shadow", 0, DG_CAP2_GL_DEPTH_TEXTURE},
    {"GL_ARB_fragment_program_shadow", DG_CAP_GL_FRAGMENT_PROGRAM, DG_CAP2_GL_DEPTH_TEXTURE},
};
/* The string a host with `caps` and `caps2` (and secondary colour when
 * `secondary`) negotiates, in a static buffer. */
static const char *ExpectedExtensions(unsigned caps, int secondary, unsigned caps2 = 0) {
    static char out[2048];
    out[0] = 0;
    for (const JGL_TEST_EXTENSION &e : ExtensionNames)
        if (e.Cap || e.Cap2 ? (caps & e.Cap) == e.Cap && (caps2 & e.Cap2) == e.Cap2
                            : secondary != 0) {
            if (*out)
                strcat(out, " ");
            strcat(out, e.Name);
        }
    return out;
}
#endif
