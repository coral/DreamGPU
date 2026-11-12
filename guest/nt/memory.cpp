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

/* Long copies and fills (kernel command batches, query results, client
 * arrays) use x86 string instructions, which processors with fast strings (P6
 * onward) run a cache line at a time; the word loops below move four bytes per
 * instruction. Their fixed startup cost makes short ranges cheaper in the
 * loops, which also finish the string path's last bytes. Windows and the C
 * ABI enter functions with the direction flag clear. */
#if defined(__i386__) || defined(__x86_64__)
#define DG_STRING_BYTES 512
#endif

void *memset(void *destination, int value, size_t length) {
    unsigned char *out = static_cast<unsigned char *>(destination);
    const unsigned int word = (unsigned char)value * 0x01010101u;
#ifdef DG_STRING_BYTES
    if (length >= DG_STRING_BYTES) {
        size_t words = length / 4;
        __asm__ __volatile__("rep stosl" : "+D"(out), "+c"(words) : "a"(word) : "memory");
        length &= 3;
    }
#endif
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
#ifdef DG_STRING_BYTES
    if (length >= DG_STRING_BYTES) {
        size_t words = length / 4;
        __asm__ __volatile__("rep movsl" : "+D"(out), "+S"(in), "+c"(words) : : "memory");
        length &= 3;
    }
#endif
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
