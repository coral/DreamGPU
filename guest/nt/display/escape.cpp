/* SPDX-License-Identifier: GPL-2.0-or-later */
extern "C" {
#include "framebuf.h"
#include "dg-ioctl.h"

ULONG APIENTRY DrvEscape(SURFOBJ *surface, ULONG escape, ULONG input_bytes, PVOID input,
                         ULONG output_bytes, PVOID output) {
    PPDEV device;
    ULONG returned, capacity;
    DG_ESCAPE_REQUEST *request;
    DG_ESCAPE_REPLY *reply;
    if (escape == QUERYESCSUPPORT)
        return input_bytes >= sizeof(ULONG) && input &&
               (*(ULONG *)input == DG_ESCAPE || *(ULONG *)input == WNDOBJ_SETUP ||
                *(ULONG *)input == DG_DRAW_ESCAPE);
    if (escape == WNDOBJ_SETUP)
        return DgBindWindow(surface, input_bytes, input, output_bytes, output);
    if (escape != DG_ESCAPE || !surface || !surface->dhpdev || !input || !output ||
        input_bytes < sizeof(DG_ESCAPE_REQUEST) ||
        input_bytes > sizeof(DG_ESCAPE_REQUEST) + DG_ESCAPE_MAX_BYTES ||
        output_bytes < sizeof(DG_ESCAPE_REPLY) ||
        output_bytes > sizeof(DG_ESCAPE_REPLY) + DG_ESCAPE_MAX_RESULT_BYTES)
        return 0;
    request = (DG_ESCAPE_REQUEST *)input;
    capacity = request->ResultCapacity;
    if (capacity > DG_ESCAPE_MAX_RESULT_BYTES || capacity > output_bytes - sizeof(DG_ESCAPE_REPLY))
        return 0;
    device = (PPDEV)surface->dhpdev;
    /* GDI supplies kernel buffers for DrvEscape. EngDeviceIoControl preserves
     * calling-process context; only the miniport allocates/verifies identity. */
    if (EngDeviceIoControl(device->hDriver, IOCTL_VIDEO_DG_GL, input, input_bytes, output,
                           sizeof(DG_ESCAPE_REPLY) + capacity, &returned) ||
        returned < sizeof(DG_ESCAPE_REPLY) || returned > sizeof(DG_ESCAPE_REPLY) + capacity) {
        memset(output, 0, output_bytes);
        return 0;
    }
    /* ExtEscape has no returned-byte-count parameter: GDI may copy the full
     * supplied output extent back to user mode. Clear its unused tail only
     * after input consumption, so aliasing cannot destroy the request. */
    memset((BYTE *)output + returned, 0, output_bytes - returned);
    reply = (DG_ESCAPE_REPLY *)output;
    return reply->Version == DG_ESCAPE_VERSION &&
           reply->ResultBytes == returned - sizeof(DG_ESCAPE_REPLY);
}

} /* extern C */
