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
/* The index size of a buffer draw's index type word: zero for an array draw,
 * (unsigned)-1 for any other value. */
static inline unsigned DgBufferIndexBytes(unsigned type) {
    return type == 0        ? 0
           : type == 0x1401 ? 1
           : type == 0x1403 ? 2
           : type == 0x1405 ? 4
                            : (unsigned)-1;
}
/* A BGRA buffer slot's descriptor (DG_CAP_GL_VERTEX_ARRAY_BGRA): colour or
 * secondary colour UNSIGNED_BYTE 4, or a normalized generic UNSIGNED_BYTE 4. */
static inline int DgRawBgraDescriptor(unsigned slot, unsigned d) {
    const unsigned base = 0x1401 | (4U << 16) | DG_GL_ARRAY_BGRA;
    return slot >= DG_GL_ARRAY_EXT_SLOTS ? d == (base | DG_GL_ATTRIB_NORMALIZED)
                                         : (slot == 1 || slot == 4) && d == base;
}
/* The buffer draw (DG_CAP_GL_VERTEX_BUFFER_OBJECT), or with bgra the BGRA
 * buffer draw (DG_CAP_GL_VERTEX_ARRAY_BGRA): a[0..9] are its scalar words,
 * descriptors the thirty-two attribute draw descriptors and references the
 * packed (buffer, offset, stride) words of its buffer slots in slot order.
 * Computes each client section's payload offset (zero for other slots), the
 * inline index offset (zero without inline indices) and the padded length. */
static inline int DgRawBufferLayoutOf(const unsigned *a, const unsigned *descriptors,
                                      const unsigned *references, unsigned *offsets,
                                      unsigned *indices, unsigned *length, int bgra) {
    const unsigned mask = a[3], generic = a[4], buffered = a[5], vertices = a[6];
    const unsigned enabled = (mask & DG_GL_ARRAY_EXT_MASK) | (generic << DG_GL_ARRAY_EXT_SLOTS);
    const unsigned client = enabled & ~buffered, index = DgBufferIndexBytes(a[7]);
    unsigned at = DG_GL_ATTRIB_DESCRIPTOR_BYTES, n = 0;
    *indices = 0;
    if (a[0] > 9 || mask != ((mask & DG_GL_ARRAY_EXT_MASK) | DG_GL_ARRAY_RAW) ||
        (generic & ~(unsigned)DG_GL_ATTRIB_MASK) ||
        !((mask & DG_GL_ARRAY_POSITION) || (generic & 1)) || (buffered & ~enabled) ||
        index == (unsigned)-1 || a[2] > DG_GL_MAX_BUFFER_ELEMENTS)
        return 0;
    if (!index) {
        /* Array draw: vertices base..base+count-1 within 32 bits. */
        if (a[8] || a[9] || vertices != (client ? a[2] : 0) || vertices > DG_GL_MAX_VERTICES ||
            (a[2] && a[1] > 0xffffffffU - (a[2] - 1)))
            return 0;
    } else if (client ? !vertices || vertices > DG_GL_MAX_VERTICES ||
                            a[1] > 0xffffffffU - (vertices - 1)
                      : vertices || a[1]) {
        return 0;
    } else if (a[8] ? a[9] % index : a[9]) {
        return 0;
    }
    for (unsigned slot = 0; slot < DG_GL_ATTRIB_SLOTS; ++slot) {
        const unsigned d = descriptors[slot];
        offsets[slot] = 0;
        if (!((enabled >> slot) & 1)) {
            if (d)
                return 0;
            continue;
        }
        if (!(slot >= DG_GL_ARRAY_EXT_SLOTS ? DgRawAttribDescriptor(d) : DgRawArraySlot(slot, d)) &&
            !(bgra && ((buffered >> slot) & 1) && DgRawBgraDescriptor(slot, d)))
            return 0;
        if ((buffered >> slot) & 1) {
            const unsigned unit = DgArrayComponentBytes(d & 65535), *r = references + 3 * n++;
            if (!r[0] || r[2] > DG_GL_MAX_BUFFER_STRIDE || r[1] % unit || r[2] % unit)
                return 0;
        }
    }
    at += n * DG_GL_BUFFER_REFERENCE_BYTES;
    for (unsigned slot = 0; slot < DG_GL_ATTRIB_SLOTS; ++slot) {
        const unsigned d = descriptors[slot];
        unsigned unit;
        if (!((client >> slot) & 1))
            continue;
        unit = DgArrayComponentBytes(d & 65535);
        at = (at + unit - 1) & ~(unit - 1);
        offsets[slot] = at;
        // vertices<=65536, n<=4, unit<=8, at most thirty-one sections: no u32 overflow.
        at += vertices * ((d >> 16) & 255) * unit;
    }
    if (index && !a[8]) {
        at = (at + index - 1) & ~(index - 1);
        *indices = at;
        /* Inline indices travel in one packet: far below 32-bit overflow. */
        if (a[2] > DG_GL_MAX_INDICES)
            return 0;
        at += a[2] * index;
    }
    *length = (at + 3) & ~3U;
    return 1;
}
static inline int DgRawBufferLayout(const unsigned *a, const unsigned *descriptors,
                                    const unsigned *references, unsigned *offsets,
                                    unsigned *indices, unsigned *length) {
    return DgRawBufferLayoutOf(a, descriptors, references, offsets, indices, length, 0);
}
#endif
