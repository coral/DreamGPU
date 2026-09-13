/* SPDX-License-Identifier: GPL-2.0-or-later
 * Freestanding helpers: guest drivers must not depend on a user-mode CRT.
 */
extern "C" {
#include <stddef.h>

void *memset(void *destination, int value, size_t length) {
    unsigned char *out = static_cast<unsigned char *>(destination);
    while (length--)
        *out++ = (unsigned char)value;
    return destination;
}

void *memcpy(void *destination, const void *source, size_t length) {
    unsigned char *out = static_cast<unsigned char *>(destination);
    const unsigned char *in = static_cast<const unsigned char *>(source);
    while (length--)
        *out++ = *in++;
    return destination;
}

} /* extern C */
