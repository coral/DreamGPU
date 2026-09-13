/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Wine fixed-function fallback: convert its common XYZRHW / D3DCOLOR / UV
 * format once and submit one immutable GL1.1 array packet per primitive.
 */
#include "dg-wine-vertex-pack.h"

static BOOL dg_wine_draw_transformed(const struct wined3d_device *device,
        struct wined3d_context *context, const struct wined3d_stream_info *si,
        UINT count, GLenum primitive, const void *indices, UINT index_size, UINT first)
{
    const struct wined3d_gl_info *gl_info = context->gl_info;
    const struct wined3d_state *state = &device->state;
    const struct wined3d_stream_info_element *p = &si->elements[WINED3D_FFP_POSITION];
    const struct wined3d_stream_info_element *d = &si->elements[WINED3D_FFP_DIFFUSE];
    const struct wined3d_stream_info_element *s = &si->elements[WINED3D_FFP_SPECULAR];
    const struct wined3d_stream_info_element *t = &si->elements[WINED3D_FFP_TEXCOORD0];
    const unsigned int required = (1u << WINED3D_FFP_POSITION)
            | (1u << WINED3D_FFP_DIFFUSE) | (1u << WINED3D_FFP_TEXCOORD0);
    /* The donor advertises ARB_MULTITEXTURE after installing one-unit
     * emulation wrappers. Its actual limits, not that synthetic flag, bound
     * this path. Extra declaration UV sets are unused by its one stage. */
    const unsigned int texture_inputs = 0xffu << WINED3D_FFP_TEXCOORD0;
    struct dg_wine_vertex vertices[DG_WINE_PACKED_VERTICES];
    unsigned int i;
    BOOL secondary = !!(si->use_map & (1u << WINED3D_FFP_SPECULAR));

    /* In this Wine state, streamsrc() has unloaded every named array. No
     * application owns this GL context. Restore that disabled state below;
     * a later named-array draw always installs its own pointers in full. */
    if (!count || count > DG_WINE_PACKED_VERTICES || !si->position_transformed
            || !context->use_immediate_mode_draw || context->namedArraysLoaded
            || context->numberedArraysLoaded || context->num_untracked_materials
            || context->d3d_info->xyzrhw || use_ps(state)
            || gl_info->limits.textures != 1 || gl_info->limits.texture_coords != 1
            || context->d3d_info->limits.ffp_blend_stages != 1
            || gl_info->supported[ARB_VERTEX_BUFFER_OBJECT]
            || (context->fog_coord && state->render_states[WINED3D_RS_FOGENABLE])
            || !state->textures[0] || context->tex_unit_map[0] != 0
            || state->texture_states[0][WINED3D_TSS_TEXCOORD_INDEX] != 0
            || (si->use_map & ~(required | texture_inputs | (1u << WINED3D_FFP_SPECULAR)))
            || (si->use_map & required) != required
            || p->format->id != WINED3DFMT_R32G32B32A32_FLOAT
            || d->format->id != WINED3DFMT_B8G8R8A8_UNORM
            || t->format->id != WINED3DFMT_R32G32_FLOAT
            || p->data.buffer_object || d->data.buffer_object || t->data.buffer_object
            || (secondary && (s->format->id != WINED3DFMT_B8G8R8A8_UNORM
                    || s->data.buffer_object || !gl_info->supported[EXT_SECONDARY_COLOR]))
            || (index_size != 0 && index_size != 2 && index_size != 4)
            || (index_size && !indices) || (!index_size && indices))
        return FALSE;
    /* Explicit fog-coordinate emulation remains in drawStridedSlow. */
    if (secondary && gl_info->supported[EXT_FOG_COORD]
            && state->render_states[WINED3D_RS_FOGENABLE]
            && state->render_states[WINED3D_RS_FOGTABLEMODE] == WINED3D_FOG_NONE)
        return FALSE;

    for (i = 0; i < count; ++i) {
        int64_t index = (int64_t)first + i;
        const void *position, *texture, *diffuse, *specular = NULL, *source;
        if (index_size) {
            uint32_t value = 0;
            if (!dg_wine_vertex_address(indices, index_size, index, index_size, &source))
                return FALSE;
            memcpy(&value, source, index_size);
            index = (int64_t)value + state->base_vertex_index;
        }
        if (!dg_wine_vertex_address(p->data.addr, p->stride, index, 16, &position)
                || !dg_wine_vertex_address(t->data.addr, t->stride, index, 8, &texture)
                || !dg_wine_vertex_address(d->data.addr, d->stride, index, 4, &diffuse)
                || (secondary && !dg_wine_vertex_address(s->data.addr, s->stride,
                        index, 4, &specular)))
            return FALSE;
        dg_wine_pack_vertex(&vertices[i], position, texture, diffuse, specular);
    }
    gl_info->gl_ops.gl.p_glNormal3f(0, 0, 0);
    if (!secondary && gl_info->supported[EXT_SECONDARY_COLOR])
        GL_EXTCALL(glSecondaryColor3fEXT)(0, 0, 0);
    gl_info->gl_ops.gl.p_glVertexPointer(4, GL_FLOAT, sizeof(vertices[0]), vertices[0].position);
    gl_info->gl_ops.gl.p_glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(vertices[0]), vertices[0].diffuse);
    gl_info->gl_ops.gl.p_glTexCoordPointer(2, GL_FLOAT, sizeof(vertices[0]), vertices[0].texture);
    gl_info->gl_ops.gl.p_glEnableClientState(GL_VERTEX_ARRAY);
    gl_info->gl_ops.gl.p_glEnableClientState(GL_COLOR_ARRAY);
    gl_info->gl_ops.gl.p_glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    if (secondary) {
        GL_EXTCALL(glSecondaryColorPointerEXT)(3, GL_UNSIGNED_BYTE,
                sizeof(vertices[0]), vertices[0].specular);
        gl_info->gl_ops.gl.p_glEnableClientState(GL_SECONDARY_COLOR_ARRAY_EXT);
    }
    gl_info->gl_ops.gl.p_glDrawArrays(primitive, 0, count);
    gl_info->gl_ops.gl.p_glDisableClientState(GL_VERTEX_ARRAY);
    gl_info->gl_ops.gl.p_glDisableClientState(GL_COLOR_ARRAY);
    gl_info->gl_ops.gl.p_glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    /* No client descriptor may retain this stack allocation. */
    gl_info->gl_ops.gl.p_glVertexPointer(4, GL_FLOAT, 0, NULL);
    gl_info->gl_ops.gl.p_glColorPointer(4, GL_UNSIGNED_BYTE, 0, NULL);
    gl_info->gl_ops.gl.p_glTexCoordPointer(2, GL_FLOAT, 0, NULL);
    if (secondary) {
        gl_info->gl_ops.gl.p_glDisableClientState(GL_SECONDARY_COLOR_ARRAY_EXT);
        GL_EXTCALL(glSecondaryColorPointerEXT)(3, GL_UNSIGNED_BYTE, 0, NULL);
        GL_EXTCALL(glSecondaryColor3ubvEXT)(vertices[count - 1].specular);
    }
    /* Array drawing leaves current attributes unspecified. Reproduce the
     * final immediate-mode vertex's values for the following Wine draw. */
    /* Use the same wrapped setter as diffuse_d3dcolor: Wine also retains
     * this value for a later draw which enables its fog emulation. */
    gl_info->gl_ops.gl.p_glColor4ub(vertices[count - 1].diffuse[0],
            vertices[count - 1].diffuse[1], vertices[count - 1].diffuse[2],
            vertices[count - 1].diffuse[3]);
    gl_info->gl_ops.gl.p_glTexCoord2fv(vertices[count - 1].texture);
    return TRUE;
}
