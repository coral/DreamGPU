/* SPDX-License-Identifier: GPL-2.0-or-later
 * Freestanding helpers: guest drivers must not depend on a user-mode CRT.
 */
extern "C" {
#include <stddef.h>

/* These freestanding routines also back the OpenGL packet writer. Access only
 * complete words inside the caller's range. The packed, aliasable word permits
 * unaligned client arrays without relying on C++ alignment or aliasing UB. */
struct __attribute__((packed, may_alias)) MemoryWord {
    unsigned int value;
};
struct __attribute__((packed, may_alias)) MemoryHalf {
    unsigned short value;
};
static_assert(sizeof(MemoryWord) == 4 && alignof(MemoryWord) == 1);
static_assert(sizeof(MemoryHalf) == 2 && alignof(MemoryHalf) == 1);

void *memset(void *destination, int value, size_t length) {
    unsigned char *out = static_cast<unsigned char *>(destination);
    const unsigned int word = (unsigned char)value * 0x01010101u;
    while (length >= 16) {
        reinterpret_cast<MemoryWord *>(out)->value = word;
        reinterpret_cast<MemoryWord *>(out + 4)->value = word;
        reinterpret_cast<MemoryWord *>(out + 8)->value = word;
        reinterpret_cast<MemoryWord *>(out + 12)->value = word;
        out += 16;
        length -= 16;
    }
    if (length & 8) {
        reinterpret_cast<MemoryWord *>(out)->value = word;
        reinterpret_cast<MemoryWord *>(out + 4)->value = word;
        out += 8;
    }
    if (length & 4) {
        reinterpret_cast<MemoryWord *>(out)->value = word;
        out += 4;
    }
    if (length & 2) {
        reinterpret_cast<MemoryHalf *>(out)->value = (unsigned short)word;
        out += 2;
    }
    if (length & 1)
        *out = (unsigned char)value;
    return destination;
}

void *memcpy(void *destination, const void *source, size_t length) {
    unsigned char *out = static_cast<unsigned char *>(destination);
    const unsigned char *in = static_cast<const unsigned char *>(source);
    while (length >= 16) {
        reinterpret_cast<MemoryWord *>(out)->value =
            reinterpret_cast<const MemoryWord *>(in)->value;
        reinterpret_cast<MemoryWord *>(out + 4)->value =
            reinterpret_cast<const MemoryWord *>(in + 4)->value;
        reinterpret_cast<MemoryWord *>(out + 8)->value =
            reinterpret_cast<const MemoryWord *>(in + 8)->value;
        reinterpret_cast<MemoryWord *>(out + 12)->value =
            reinterpret_cast<const MemoryWord *>(in + 12)->value;
        out += 16;
        in += 16;
        length -= 16;
    }
    if (length & 8) {
        reinterpret_cast<MemoryWord *>(out)->value =
            reinterpret_cast<const MemoryWord *>(in)->value;
        reinterpret_cast<MemoryWord *>(out + 4)->value =
            reinterpret_cast<const MemoryWord *>(in + 4)->value;
        out += 8;
        in += 8;
    }
    if (length & 4) {
        reinterpret_cast<MemoryWord *>(out)->value =
            reinterpret_cast<const MemoryWord *>(in)->value;
        out += 4;
        in += 4;
    }
    if (length & 2) {
        reinterpret_cast<MemoryHalf *>(out)->value =
            reinterpret_cast<const MemoryHalf *>(in)->value;
        out += 2;
        in += 2;
    }
    if (length & 1)
        *out = *in;
    return destination;
}

} /* extern C */
