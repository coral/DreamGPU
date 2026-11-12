/* SPDX-License-Identifier: GPL-2.0-or-later
 * Driver side of bulk query results (gl.h DG_CAP2_GL_BULK_READ), shared by
 * the NT miniport and the Win9x VxD. Freestanding C: ULONG is the driver's
 * unsigned 32-bit type; gl.h and dg-escape.h precede this header.
 *
 * A request whose ResultCapacity exceeds the contiguous result buffer
 * (DG_ESCAPE_MAX_RESULT_BYTES) is served from a driver-owned buffer of
 * DG_ESCAPE_BULK_RESULT_BYTES, allocated once for the adapter's life and never
 * a caller's memory, described to the device by a driver-owned list page of
 * its page frames. Smaller requests keep the contiguous buffer unchanged.
 */
#ifndef DG_GL_BULK_H
#define DG_GL_BULK_H

#define DG_GL_BULK_PAGES (DG_ESCAPE_BULK_RESULT_BYTES / DG_GL_RESULT_PAGE_BYTES)
typedef char DgGlBulkListFits[DG_GL_BULK_PAGES * 4 <= DG_GL_RESULT_PAGE_BYTES ? 1 : -1];
typedef char DgGlBulkCapacityFits[DG_ESCAPE_BULK_RESULT_BYTES <= DG_GL_MAX_BULK_RESULT_BYTES &&
                                          DG_ESCAPE_BULK_RESULT_BYTES < DG_GL_RESULT_PAGES
                                      ? 1
                                      : -1];

/* Nonzero when a request's result goes through the page list. */
static inline int DgGlBulkResult(ULONG capacity) {
    return capacity > DG_ESCAPE_MAX_RESULT_BYTES;
}

/* The RESULT_CAPACITY word (register or queue entry) for a request. */
static inline ULONG DgGlResultWord(ULONG capacity) {
    return DgGlBulkResult(capacity) ? capacity | DG_GL_RESULT_PAGES : capacity;
}

/* MaxResultBytes a driver offers: the bulk size only while the host has the
 * capability and the driver holds its buffer, else the contiguous limits. */
static inline ULONG DgGlResultLimit(ULONG caps, ULONG caps2, int bulk_ready) {
    if ((caps2 & DG_CAP2_GL_BULK_READ) && bulk_ready)
        return DG_ESCAPE_BULK_RESULT_BYTES;
    return caps & DG_CAP_GL_BULK_READBACK ? DG_ESCAPE_MAX_RESULT_BYTES
                                          : DG_ESCAPE_LEGACY_RESULT_BYTES;
}

/* Record page `index` of the bulk buffer at `physical` in the list. Zero
 * (nothing stored) for an index past the list, an unaligned or zero address
 * or one whose frame does not fit 32 bits. */
static inline int DgGlBulkSetFrame(ULONG *list, ULONG index, unsigned long long physical) {
    if (!list || index >= DG_GL_BULK_PAGES || !physical ||
        (physical & (DG_GL_RESULT_PAGE_BYTES - 1)) || (physical >> 12) > 0xffffffffULL)
        return 0;
    list[index] = (ULONG)(physical >> 12);
    return 1;
}
#endif
