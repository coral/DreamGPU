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
/* Slots 7-13 of the extended layout are texture coordinates of units 1-7
 * and follow every rule of slot 3. Slot 14 is the fog coordinate: one FLOAT
 * or DOUBLE (DG_CAP_GL_FOG_COORD). Slot 15 is reserved: DG_GL_ARRAY_EXT_MASK
 * excludes its bit and a disabled slot's descriptor must be zero. */
static inline unsigned DgArraySlotRole(unsigned slot) {
    return slot >= 7 && slot <= 13 ? 3 : slot;
}
static inline int DgRawArrayLayoutSlots(unsigned mask, unsigned count, const unsigned *descriptors,
                                        unsigned *offsets, unsigned *length, unsigned slots) {
    const unsigned allowed =
        slots == DG_GL_ARRAY_EXT_SLOTS ? DG_GL_ARRAY_EXT_MASK : DG_GL_ARRAY_MASK;
    unsigned at = slots * 4;
    if ((slots != 7 && slots != DG_GL_ARRAY_EXT_SLOTS) ||
        mask != ((mask & allowed) | DG_GL_ARRAY_RAW) || !(mask & DG_GL_ARRAY_POSITION) ||
        count > DG_GL_MAX_VERTICES)
        return 0;
    for (unsigned slot = 0; slot < slots; ++slot) {
        unsigned d = descriptors[slot], type = d & 65535, n = d >> 16, unit,
                 i = DgArraySlotRole(slot);
        offsets[slot] = 0;
        if (!(mask & (1U << slot))) {
            if (d)
                return 0;
            continue;
        }
        unit = DgArrayComponentBytes(type);
        if (!unit ||
            n < (i == 0   ? 2U
                 : i == 1 ? 3U
                          : 1U) ||
            n > 4 || ((i == 2 || i == 4) && n != 3) || ((i == 5 || i == 6 || i == 14) && n != 1))
            return 0;
        if (i == 14  ? type != 0x1406 && type != 0x140a
            : i == 6 ? type != 0x1401
            : i == 5
                ? (type != 0x1402 && type != 0x1404 && type != 0x1406 && type != 0x140a)
                : ((i != 1 && i != 4 && (type == 0x1401 || type == 0x1403 || type == 0x1405)) ||
                   (i != 1 && i != 2 && i != 4 && type == 0x1400)))
            return 0;
        at = (at + unit - 1) & ~(unit - 1);
        offsets[slot] = at;
        // count<=65536, n<=4, unit<=8, at most fifteen attributes: no u32 overflow.
        at += count * n * unit;
    }
    *length = (at + 3) & ~3U;
    return 1;
}
/* Seven-slot DrawArrays layout; older kernels validate exactly this. */
static inline int DgRawArrayLayout(unsigned mask, unsigned count, const unsigned *descriptors,
                                   unsigned *offsets, unsigned *length) {
    return DgRawArrayLayoutSlots(mask, count, descriptors, offsets, length, 7);
}
#endif
