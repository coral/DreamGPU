/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Bounded format conversion for Wine's transformed fixed-function vertices.
 * This prepares client data; rasterization and interpolation remain on the GPU.
 */
#ifndef DG_WINE_VERTEX_PACK_H
#define DG_WINE_VERTEX_PACK_H
#include <stdint.h>
#include <string.h>

/* Small primitives dominate the measured workload; stay below one x86 stack
 * page so an ineligible fallback draw does not pay a stack-probe call. */
#define DG_WINE_PACKED_VERTICES 64
struct dg_wine_vertex {
    float position[4], texture[2];
    unsigned char diffuse[4], specular[4];
};

static inline int dg_wine_vertex_address(const void *base, unsigned int stride, int64_t index,
                                         unsigned int bytes, const void **out) {
    uintptr_t address = (uintptr_t)base;
    uint64_t offset;
    if (!base || index < 0 || (stride && (uint64_t)index > UINTPTR_MAX / stride))
        return 0;
    offset = (uint64_t)index * stride;
    if (offset > UINTPTR_MAX || address > UINTPTR_MAX - (uintptr_t)offset ||
        bytes > UINTPTR_MAX - address - (uintptr_t)offset)
        return 0;
    *out = (const void *)(address + (uintptr_t)offset);
    return 1;
}

static inline void dg_wine_pack_vertex(struct dg_wine_vertex *out, const void *position,
                                       const void *texture, const void *diffuse,
                                       const void *specular) {
    float p[4];
    const unsigned char *c = (const unsigned char *)diffuse;
    const unsigned char *s = (const unsigned char *)specular;
    memcpy(p, position, sizeof(p));
    /* Match Wine position_float4 exactly, including RHW zero and one. */
    if (p[3] != 0.0f && p[3] != 1.0f) {
        float w = 1.0f / p[3];
        out->position[0] = p[0] * w;
        out->position[1] = p[1] * w;
        out->position[2] = p[2] * w;
        out->position[3] = w;
    } else {
        memcpy(out->position, p, 3 * sizeof(float));
        out->position[3] = 1.0f;
    }
    memcpy(out->texture, texture, sizeof(out->texture));
    out->diffuse[0] = c[2];
    out->diffuse[1] = c[1];
    out->diffuse[2] = c[0];
    out->diffuse[3] = c[3];
    out->specular[0] = s ? s[2] : 0;
    out->specular[1] = s ? s[1] : 0;
    out->specular[2] = s ? s[0] : 0;
    out->specular[3] = 0;
}
#endif
