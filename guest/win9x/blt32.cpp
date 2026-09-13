/* SPDX-License-Identifier: GPL-2.0-or-later
 * Flat rectangle/offset validation. Failure never modifies command storage.
 * C supplies only a snapshot of authoritative VMM framebuffer metadata.
 */
#include <stdint.h>
typedef uint32_t DWORD;
#include "gpu.h"
#include "gl.h"
#include "blt32.h"
static_assert(sizeof(DG9_COMMAND) == 40 && sizeof(DG9_SURFACE) == 20);
namespace {
bool offset(DWORD base, DWORD y, DWORD stride, DWORD x, DWORD bytes, DWORD &out) noexcept {
    if (y && stride > (UINT32_MAX - base) / y)
        return false;
    const DWORD row = base + y * stride;
    if (x && bytes > (UINT32_MAX - row) / x)
        return false;
    out = row + x * bytes;
    return true;
}
} // namespace
extern "C" int DREAMGPU_CDECL DreamGpuPrepareBlt(const DG9_SURFACE *surface, DWORD op, DWORD source,
                                                 DWORD destination, DWORD dimensions,
                                                 DG9_COMMAND *out) {
    if (!surface || !out || op < 1 || op > 3)
        return 0;
    const DWORD bytes = surface->Bpp / 8, width = dimensions & 65535, height = dimensions >> 16;
    const DWORD sx = source & 65535, sy = source >> 16, dx = destination & 65535,
                dy = destination >> 16;
    if ((bytes != 1 && bytes != 2 && bytes != 4) || !width || !height || dx > surface->Width ||
        width > surface->Width - dx || dy > surface->Height || height > surface->Height - dy ||
        (op == 3 && (sx > surface->Width || width > surface->Width - sx || sy > surface->Height ||
                     height > surface->Height - sy)))
        return 0;
    DG9_COMMAND command{};
    command.opcode = op == 3 ? DG_CMD_COPY : DG_CMD_FILL;
    command.bpp = bytes;
    if (op == 3) {
        if (!offset(surface->Offset, sy, surface->Pitch, sx, bytes, command.src_offset))
            return 0;
        command.src_stride = surface->Pitch;
    }
    if (!offset(surface->Offset, dy, surface->Pitch, dx, bytes, command.dst_offset))
        return 0;
    command.dst_stride = surface->Pitch;
    command.width = width;
    command.height = height;
    command.color = op == 2 ? UINT32_MAX : 0;
    *out = command;
    return 1;
}
extern "C" int DREAMGPU_CDECL DreamGpuPrimaryMetadata(const DG9_SURFACE *surface) {
    return surface && surface->Bpp == 32 && surface->Width && surface->Height &&
           surface->Width <= DG_GL_MAX_DIMENSION && surface->Height <= DG_GL_MAX_DIMENSION &&
           surface->Pitch >= surface->Width * 4 && !(surface->Pitch & 3) &&
           surface->Pitch <= DG_DESKTOP_MAX_BYTES / surface->Height;
}
