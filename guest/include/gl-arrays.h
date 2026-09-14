/* SPDX-License-Identifier: GPL-2.0-or-later
 * Freestanding compact client-array layout, shared frontend/kernel admission. */
#ifndef DG_GL_ARRAYS_H
#define DG_GL_ARRAYS_H
#include "gl.h"
static inline unsigned DgArrayComponentBytes(unsigned type) {
    switch (type) {
        case 0x1400:
        case 0x1401:
            return 1;
        case 0x1402:
        case 0x1403:
            return 2;
        case 0x1404:
        case 0x1405:
        case 0x1406:
            return 4;
        case 0x140a:
            return 8;
        default:
            return 0;
    }
}
static inline int DgRawArrayLayout(unsigned mask, unsigned count, const unsigned *descriptors,
                                   unsigned *offsets, unsigned *length) {
    unsigned at = DG_GL_ARRAY_DESCRIPTOR_BYTES;
    if (mask != ((mask & DG_GL_ARRAY_MASK) | DG_GL_ARRAY_RAW) || !(mask & DG_GL_ARRAY_POSITION) ||
        count > DG_GL_MAX_VERTICES)
        return 0;
    for (unsigned i = 0; i < 7; ++i) {
        unsigned d = descriptors[i], type = d & 65535, n = d >> 16, unit;
        offsets[i] = 0;
        if (!(mask & (1U << i))) {
            if (d)
                return 0;
            continue;
        }
        unit = DgArrayComponentBytes(type);
        if (!unit ||
            n < (i == 0   ? 2U
                 : i == 1 ? 3U
                          : 1U) ||
            n > 4 || ((i == 2 || i == 4) && n != 3) || ((i == 5 || i == 6) && n != 1))
            return 0;
        if (i == 6 ? type != 0x1401
            : i == 5
                ? (type != 0x1402 && type != 0x1404 && type != 0x1406 && type != 0x140a)
                : ((i != 1 && i != 4 && (type == 0x1401 || type == 0x1403 || type == 0x1405)) ||
                   (i != 1 && i != 2 && i != 4 && type == 0x1400)))
            return 0;
        at = (at + unit - 1) & ~(unit - 1);
        offsets[i] = at;
        // count<=65536, n<=4, unit<=8, seven attributes: cannot overflow u32.
        at += count * n * unit;
    }
    *length = (at + 3) & ~3U;
    return 1;
}
#endif
