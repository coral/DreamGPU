/* SPDX-License-Identifier: GPL-2.0-or-later
 * Test actual DrvEscape output handling: ExtEscape exposes a capacity, not a
 * returned-length pointer, so unreturned bytes must never reveal stale data.
 * Bulk results (DG_ESCAPE_BULK_RESULT_BYTES) pass the same bounds. GL
 * escapes take the miniport's direct kernel entry (DG_KERNEL_VERSION 8), never
 * an IRP; a PDEV without that entry fails them without reaching the miniport. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define _FRAMEBUF_PCH_
#define APIENTRY
#define DG_KERNEL_CALL
#define FILE_DEVICE_VIDEO 0x23
#define METHOD_BUFFERED 0
#define FILE_ANY_ACCESS 0
#define CTL_CODE(d, f, m, a) (((d) << 16) | ((f) << 2))
#define QUERYESCSUPPORT 8
#define WNDOBJ_SETUP 4354
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef uint8_t BYTE;
typedef uint16_t WCHAR;
typedef void *PVOID;
typedef struct {
    void *dhpdev;
} SURFOBJ;
#include "dg-kernel.h"
typedef struct {
    void *hDriver;
    DG_KERNEL_INTERFACE Kernel;
} PDEV, *PPDEV;
static ULONG returned_bytes = 4, calls, io_error, last_output;
static int context;
static ULONG DgBindWindow(SURFOBJ *, ULONG, PVOID, ULONG, PVOID) {
    return 0;
}
/* Only the timing escape still issues an IOCTL. */
static ULONG EngDeviceIoControl(void *, ULONG, PVOID, ULONG, PVOID, ULONG, ULONG *) {
    assert(!"GL escape issued an IRP");
    return 1;
}
/* The miniport's direct entry: the reply plus result bytes, zero on failure. */
static ULONG KernelGl(void *kernel, PVOID input, ULONG input_bytes, PVOID output,
                      ULONG output_bytes) {
    DG_ESCAPE_REQUEST request;
    DG_ESCAPE_REPLY reply = {};
    assert(kernel == &context && input_bytes == sizeof(request));
    memcpy(&request, input, sizeof(request));
    assert(request.Version == DG_ESCAPE_VERSION);
    /* The miniport is handed exactly the reply and the requested capacity. */
    assert(output_bytes == sizeof(reply) + request.ResultCapacity);
    last_output = output_bytes;
    ++calls;
    reply.Version = DG_ESCAPE_VERSION;
    reply.ResultBytes = returned_bytes;
    reply.ResultType = returned_bytes ? DG_GL_RESULT_INT : 0;
    reply.Status = returned_bytes ? DG_ESCAPE_OK : DG_ESCAPE_HOST;
    memcpy(output, &reply, sizeof(reply));
    if (returned_bytes)
        memset((BYTE *)output + sizeof(reply), 0x78, returned_bytes);
    return io_error ? 0 : sizeof(reply) + returned_bytes;
}
#include "../../../guest/nt/display/escape.cpp"
static void Bulk(PDEV *device) {
    SURFOBJ surface = {device};
    DG_ESCAPE_REQUEST request = {};
    const ULONG size = sizeof(DG_ESCAPE_REPLY) + DG_ESCAPE_BULK_RESULT_BYTES;
    BYTE *output = (BYTE *)malloc(size + 4);
    ULONG i;
    assert(output);
    request.Version = DG_ESCAPE_VERSION;
    /* A full 1024x768 RGBA8 level in one escape, its tail zeroed. */
    request.ResultCapacity = 3 << 20;
    returned_bytes = 3 << 20;
    memset(output, 0xa5, size);
    assert(DrvEscape(&surface, DG_ESCAPE, sizeof(request), &request, size, output));
    assert(last_output == sizeof(DG_ESCAPE_REPLY) + (3 << 20));
    for (i = sizeof(DG_ESCAPE_REPLY); i < sizeof(DG_ESCAPE_REPLY) + (3 << 20); ++i)
        assert(output[i] == 0x78);
    for (; i < size; ++i)
        assert(!output[i]);
    /* Exactly the bulk maximum. */
    request.ResultCapacity = DG_ESCAPE_BULK_RESULT_BYTES;
    returned_bytes = DG_ESCAPE_BULK_RESULT_BYTES;
    assert(DrvEscape(&surface, DG_ESCAPE, sizeof(request), &request, size, output));
    assert(output[size - 1] == 0x78);
    /* One past it, in the capacity or the output extent, never reaches the
     * miniport. */
    calls = 0;
    request.ResultCapacity = DG_ESCAPE_BULK_RESULT_BYTES + 1;
    assert(!DrvEscape(&surface, DG_ESCAPE, sizeof(request), &request, size + 1, output));
    request.ResultCapacity = 512;
    assert(!DrvEscape(&surface, DG_ESCAPE, sizeof(request), &request, size + 1, output));
    /* A capacity larger than the supplied output extent. */
    request.ResultCapacity = 65537;
    assert(!DrvEscape(&surface, DG_ESCAPE, sizeof(request), &request,
                      sizeof(DG_ESCAPE_REPLY) + 65536, output));
    assert(!calls);
    returned_bytes = 4;
    free(output);
}
int main(void) {
    PDEV device = {(void *)1, {}};
    SURFOBJ surface = {&device};
    DG_ESCAPE_REQUEST request = {};
    static BYTE output[sizeof(DG_ESCAPE_REPLY) + DG_ESCAPE_MAX_RESULT_BYTES];
    ULONG i;
    request.Version = DG_ESCAPE_VERSION;
    request.ResultCapacity = 512;
    /* Without the version 8 entry nothing reaches the miniport. */
    memset(output, 0xa5, sizeof(output));
    assert(!DrvEscape(&surface, DG_ESCAPE, sizeof(request), &request, sizeof(output), output));
    for (i = 0; i < sizeof(output); ++i)
        assert(!output[i]);
    assert(!calls);
    device.Kernel.Context = &context;
    device.Kernel.Gl = KernelGl;
    memset(output, 0xa5, sizeof(output));
    assert(DrvEscape(&surface, DG_ESCAPE, sizeof(request), &request, sizeof(output), output));
    for (i = sizeof(DG_ESCAPE_REPLY); i < sizeof(DG_ESCAPE_REPLY) + 4; ++i)
        assert(output[i] == 0x78);
    for (; i < sizeof(output); ++i)
        assert(!output[i]);
    returned_bytes = 0;
    memset(output, 0xa5, sizeof(output));
    assert(DrvEscape(&surface, DG_ESCAPE, sizeof(request), &request, sizeof(output), output));
    for (i = sizeof(DG_ESCAPE_REPLY); i < sizeof(output); ++i)
        assert(!output[i]);
    /* Buffered input/output alias is consumed before clearing any tail. */
    memset(output, 0xa5, sizeof(output));
    memcpy(output, &request, sizeof(request));
    assert(DrvEscape(&surface, DG_ESCAPE, sizeof(request), output, sizeof(output), output));
    for (i = sizeof(DG_ESCAPE_REPLY); i < sizeof(output); ++i)
        assert(!output[i]);
    io_error = 1;
    memset(output, 0xa5, sizeof(output));
    assert(!DrvEscape(&surface, DG_ESCAPE, sizeof(request), &request, sizeof(output), output));
    for (i = 0; i < sizeof(output); ++i)
        assert(!output[i]);
    io_error = 0;
    request.ResultCapacity = 65537;
    assert(!DrvEscape(&surface, DG_ESCAPE, sizeof(request), &request, sizeof(output), output));
    assert(calls == 4);
    returned_bytes = 4;
    Bulk(&device);
    puts("NT escape: exact result extent, zeroed failed-query tail, aliasing, 64 KiB and 4 MiB "
         "capacity bounds passed");
    return 0;
}
