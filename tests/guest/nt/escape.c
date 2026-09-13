/* SPDX-License-Identifier: GPL-2.0-or-later
 * Test actual DrvEscape output handling: ExtEscape exposes a capacity, not a
 * returned-length pointer, so unreturned bytes must never reveal stale data. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define _FRAMEBUF_PCH_
#define APIENTRY
#define FILE_DEVICE_VIDEO 0x23
#define METHOD_BUFFERED 0
#define FILE_ANY_ACCESS 0
#define CTL_CODE(d, f, m, a) (((d) << 16) | ((f) << 2))
#define QUERYESCSUPPORT 8
#define WNDOBJ_SETUP 4354
#define DG_DRAW_ESCAPE 0x4a524702
typedef uint32_t ULONG;
typedef uint8_t BYTE;
typedef void *PVOID;
typedef struct {
    void *dhpdev;
} SURFOBJ;
typedef struct {
    void *hDriver;
} PDEV, *PPDEV;
#include "dg-ioctl.h"
static ULONG returned_bytes = 4, calls, io_error;
static ULONG DgBindWindow(SURFOBJ *s, ULONG a, PVOID b, ULONG c, PVOID d) {
    (void)s;
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    return 0;
}
static ULONG EngDeviceIoControl(void *driver, ULONG code, PVOID input, ULONG input_bytes,
                                PVOID output, ULONG output_bytes, ULONG *returned) {
    DG_ESCAPE_REQUEST request;
    DG_ESCAPE_REPLY reply = {0};
    assert(driver && code == IOCTL_VIDEO_DG_GL && input_bytes == sizeof(request));
    memcpy(&request, input, sizeof(request));
    assert(request.Version == DG_ESCAPE_VERSION);
    assert(output_bytes == sizeof(reply) + request.ResultCapacity);
    ++calls;
    reply.Version = DG_ESCAPE_VERSION;
    reply.ResultBytes = returned_bytes;
    reply.ResultType = returned_bytes ? DG_GL_RESULT_INT : 0;
    reply.Status = returned_bytes ? DG_ESCAPE_OK : DG_ESCAPE_HOST;
    memcpy(output, &reply, sizeof(reply));
    if (returned_bytes)
        memset((BYTE *)output + sizeof(reply), 0x78, returned_bytes);
    *returned = sizeof(reply) + returned_bytes;
    return io_error;
}
#include "../display/escape.c"
int main(void) {
    PDEV device = {(void *)1};
    SURFOBJ surface = {&device};
    DG_ESCAPE_REQUEST request = {0};
    BYTE output[sizeof(DG_ESCAPE_REPLY) + DG_ESCAPE_MAX_RESULT_BYTES];
    ULONG i;
    request.Version = DG_ESCAPE_VERSION;
    request.ResultCapacity = 512;
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
    request.ResultCapacity = 65537;
    assert(!DrvEscape(&surface, DG_ESCAPE, sizeof(request), &request, sizeof(output), output));
    assert(calls == 4);
    puts("NT escape: exact result extent, zeroed failed-query tail, aliasing and capacity bounds "
         "passed");
    return 0;
}
