/* SPDX-License-Identifier: GPL-2.0-or-later
 * Owned bounded fast paths for Wine's CPU DirectDraw row operations.
 * Callers retain the existing pitched/overlapping fallback. No allocation,
 * format conversion, pixel-state change, or rendering occurs here.
 */
#ifndef DREAMGPU_WINE_ROW_COPY_H
#define DREAMGPU_WINE_ROW_COPY_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

static int dg_wine_span(const void *data, size_t row_bytes, size_t rows, size_t *bytes) {
    if (!row_bytes || !rows || rows > SIZE_MAX / row_bytes)
        return 0;
    *bytes = row_bytes * rows;
    return data && (uintptr_t)data <= UINTPTR_MAX - *bytes;
}

static int dg_wine_copy_rows(void *destination, const void *source, size_t row_bytes, size_t rows,
                             size_t destination_pitch, size_t source_pitch) {
    size_t bytes, source_bytes;
    uintptr_t dst = (uintptr_t)destination, src = (uintptr_t)source;
    if (destination_pitch != row_bytes || source_pitch != row_bytes ||
        !dg_wine_span(destination, row_bytes, rows, &bytes) ||
        !dg_wine_span(source, row_bytes, rows, &source_bytes))
        return 0;
    /* A whole-span memcpy must not turn safe per-row overlap into UB. */
    if (dst < src ? src - dst < bytes : dst - src < bytes)
        return 0;
    memcpy(destination, source, bytes);
    return 1;
}

static int dg_wine_repeat_row(void *destination, size_t width, size_t rows, size_t bytes_per_pixel,
                              size_t pitch) {
    size_t row_bytes, bytes, filled;
    unsigned char *data = (unsigned char *)destination;
    if (!bytes_per_pixel || width > SIZE_MAX / bytes_per_pixel)
        return 0;
    row_bytes = width * bytes_per_pixel;
    if (pitch != row_bytes || !dg_wine_span(destination, row_bytes, rows, &bytes))
        return 0;
    /* The caller initialized row zero. Every copy reads only initialized bytes
     * and its source and destination spans are disjoint, including the tail. */
    for (filled = row_bytes; filled < bytes;) {
        size_t count = bytes - filled;
        if (count > filled)
            count = filled;
        memcpy(data + filled, data, count);
        filled += count;
    }
    return 1;
}
#endif
