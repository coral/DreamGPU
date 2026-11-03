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
/* An enabled conventional slot's descriptor: GL type | component count << 16. */
static inline int DgRawArraySlot(unsigned slot, unsigned d) {
    const unsigned type = d & 65535, n = d >> 16, i = DgArraySlotRole(slot);
    if (!DgArrayComponentBytes(type) ||
        n < (i == 0   ? 2U
             : i == 1 ? 3U
                      : 1U) ||
        n > 4 || ((i == 2 || i == 4) && n != 3) || ((i == 5 || i == 6 || i == 14) && n != 1))
        return 0;
    return !(i == 14  ? type != 0x1406 && type != 0x140a
             : i == 6 ? type != 0x1401
             : i == 5
                 ? (type != 0x1402 && type != 0x1404 && type != 0x1406 && type != 0x140a)
                 : ((i != 1 && i != 4 && (type == 0x1401 || type == 0x1403 || type == 0x1405)) ||
                    (i != 1 && i != 2 && i != 4 && type == 0x1400)));
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
        unsigned d = descriptors[slot], type = d & 65535, n = d >> 16, unit;
        offsets[slot] = 0;
        if (!(mask & (1U << slot))) {
            if (d)
                return 0;
            continue;
        }
        if (!DgRawArraySlot(slot, d))
            return 0;
        unit = DgArrayComponentBytes(type);
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
/* A generic attribute descriptor of the attribute draw: a type BYTE..UNSIGNED_INT,
 * FLOAT or DOUBLE, one to four components and DG_GL_ATTRIB_NORMALIZED only for
 * an integer type; every other bit is zero. */
static inline int DgRawAttribDescriptor(unsigned d) {
    const unsigned type = d & 65535, n = (d >> 16) & 255;
    if ((d & ~(0x00ffffffU | DG_GL_ATTRIB_NORMALIZED)) || !DgArrayComponentBytes(type) || n < 1 ||
        n > 4)
        return 0;
    return !(d & DG_GL_ATTRIB_NORMALIZED) || (type != 0x1406 && type != 0x140a);
}
/* The attribute draw (DG_CAP_GL_VERTEX_PROGRAM): descriptors 0-15 are the
 * DrawArraysEXT slots under their rules and 16-31 generic attributes 0-15. The
 * position bit may be clear only while generic attribute 0 is enabled. */
static inline int DgRawAttribLayout(unsigned mask, unsigned generic, unsigned count,
                                    const unsigned *descriptors, unsigned *offsets,
                                    unsigned *length) {
    unsigned at = DG_GL_ATTRIB_SLOTS * 4;
    if (mask != ((mask & DG_GL_ARRAY_EXT_MASK) | DG_GL_ARRAY_RAW) ||
        (generic & ~(unsigned)DG_GL_ATTRIB_MASK) ||
        !((mask & DG_GL_ARRAY_POSITION) || (generic & 1)) || count > DG_GL_MAX_VERTICES)
        return 0;
    for (unsigned slot = 0; slot < DG_GL_ATTRIB_SLOTS; ++slot) {
        const unsigned d = descriptors[slot], generic_slot = slot >= DG_GL_ARRAY_EXT_SLOTS;
        unsigned unit;
        offsets[slot] = 0;
        if (!((generic_slot ? generic >> (slot - DG_GL_ARRAY_EXT_SLOTS) : mask >> slot) & 1)) {
            if (d)
                return 0;
            continue;
        }
        if (!(generic_slot ? DgRawAttribDescriptor(d) : DgRawArraySlot(slot, d)))
            return 0;
        unit = DgArrayComponentBytes(d & 65535);
        at = (at + unit - 1) & ~(unit - 1);
        offsets[slot] = at;
        // count<=65536, n<=4, unit<=8, at most thirty-one attributes: no u32 overflow.
        at += count * ((d >> 16) & 255) * unit;
    }
    *length = (at + 3) & ~3U;
    return 1;
}
#endif
