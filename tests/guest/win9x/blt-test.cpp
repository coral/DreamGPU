/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <initializer_list>
using DWORD = uint32_t;
#include "../../../guest/win9x/blt32.h"
#include "gpu.h"
#include "gl.h"
int main() {
    DG9_SURFACE surface{800, 600, 32, 3200, 4096};
    DG9_COMMAND command{};
    assert(DreamGpuPrimaryMetadata(&surface));
    for (DWORD op = 1; op <= 3; ++op) {
        assert(DreamGpuPrepareBlt(&surface, op, 10 | (20u << 16), 30 | (40u << 16),
                                  50 | (60u << 16), &command));
        assert(command.opcode == (op == 3 ? DG_CMD_COPY : DG_CMD_FILL) && command.bpp == 4);
        assert(command.dst_offset == 4096 + 40 * 3200 + 30 * 4 && command.dst_stride == 3200);
        assert(command.src_offset == (op == 3 ? 4096 + 20 * 3200 + 10 * 4 : 0));
        assert(command.src_stride == (op == 3 ? 3200 : 0) && command.width == 50 &&
               command.height == 60);
        assert(command.color == (op == 2 ? UINT32_MAX : 0) && !command.reserved);
    }
    const auto original = command;
    for (DWORD bad : {0u, 4u, UINT32_MAX}) {
        assert(!DreamGpuPrepareBlt(&surface, bad, 0, 0, 1 | (1u << 16), &command));
        assert(!std::memcmp(&command, &original, sizeof(command)));
    }
    assert(!DreamGpuPrepareBlt(&surface, 3, 800, 0, 1 | (1u << 16), &command));
    assert(!DreamGpuPrepareBlt(&surface, 1, 0, 800, 1 | (1u << 16), &command));
    assert(!DreamGpuPrepareBlt(&surface, 1, 0, 0, 1, &command));
    assert(!DreamGpuPrepareBlt(&surface, 1, 0, 0, 1u << 16, &command));
    for (DWORD bpp : {8u, 16u, 32u}) {
        surface.Bpp = bpp;
        assert(DreamGpuPrepareBlt(&surface, 3, 0, 799 | (599u << 16), 1 | (1u << 16), &command));
        assert(command.bpp == bpp / 8);
    }
    surface.Bpp = 24;
    assert(!DreamGpuPrepareBlt(&surface, 1, 0, 0, 1 | (1u << 16), &command));
    surface = {800, 600, 32, 3200, UINT32_MAX};
    command = original;
    assert(!DreamGpuPrepareBlt(&surface, 1, 0, 1, 1 | (1u << 16), &command));
    assert(!DreamGpuPrepareBlt(&surface, 3, 1, 0, 1 | (1u << 16), &command));
    assert(!std::memcmp(&command, &original, sizeof(command)));
    surface.Offset = 0;
    surface.Pitch = UINT32_MAX;
    assert(!DreamGpuPrepareBlt(&surface, 1, 0, 2u << 16, 1 | (1u << 16), &command));
    for (unsigned mode = 0; mode < 7; ++mode) {
        surface = {800, 600, 32, 3200, 0};
        switch (mode) {
            case 0:
                surface.Width = 0;
                break;
            case 1:
                surface.Height = 0;
                break;
            case 2:
                surface.Bpp = 24;
                break;
            case 3:
                surface.Pitch = 3196;
                break;
            case 4:
                surface.Pitch = 3201;
                break;
            case 5:
                surface.Width = 4097;
                break;
            case 6:
                surface.Pitch = UINT32_MAX - 3;
                break;
        }
        assert(!DreamGpuPrimaryMetadata(&surface));
    }
    assert(!DreamGpuPrimaryMetadata(nullptr));
    /* RGB565 primaries: two bytes per pixel, the same four-byte stride rule. */
    for (DWORD pitch : {1600u, 1604u, 3200u}) {
        surface = {800, 600, 16, pitch, 0};
        assert(DreamGpuPrimaryMetadata(&surface));
    }
    for (DWORD pitch : {0u, 1596u, 1598u, 1599u, 1601u, 1602u, 1603u}) {
        surface = {800, 600, 16, pitch, 0};
        assert(!DreamGpuPrimaryMetadata(&surface));
    }
    surface = {801, 600, 16, 1602, 0};
    assert(!DreamGpuPrimaryMetadata(&surface));
    surface.Pitch = 1604;
    assert(DreamGpuPrimaryMetadata(&surface));
    surface = {1, 1, 16, 4, 0};
    assert(DreamGpuPrimaryMetadata(&surface));
    surface.Pitch = 2;
    assert(!DreamGpuPrimaryMetadata(&surface));
    surface = {4096, 4096, 16, 8192, 0};
    assert(DreamGpuPrimaryMetadata(&surface));
    surface.Pitch = DG_DESKTOP_MAX_BYTES / 4096;
    assert(DreamGpuPrimaryMetadata(&surface));
    surface.Pitch += 4;
    assert(!DreamGpuPrimaryMetadata(&surface));
    surface = {4097, 600, 16, 8196, 0};
    assert(!DreamGpuPrimaryMetadata(&surface));
    surface = {800, 4097, 16, 1600, 0};
    assert(!DreamGpuPrimaryMetadata(&surface));
    surface = {0, 600, 16, 1600, 0};
    assert(!DreamGpuPrimaryMetadata(&surface));
    surface = {800, 0, 16, 1600, 0};
    assert(!DreamGpuPrimaryMetadata(&surface));
    /* XRGB8888 keeps its four-byte row; 16 bpp never relaxes 32 bpp. */
    surface = {800, 600, 32, 1600, 0};
    assert(!DreamGpuPrimaryMetadata(&surface));
    surface = {4096, 4096, 32, 16384, 0};
    assert(DreamGpuPrimaryMetadata(&surface));
    surface.Pitch += 4;
    assert(!DreamGpuPrimaryMetadata(&surface));
    /* Palettized, 15/24-bpp and other depths remain unsupported. */
    for (DWORD bpp : {0u, 1u, 4u, 8u, 15u, 24u, 31u, 33u, 64u, UINT32_MAX}) {
        surface = {800, 600, bpp, 3200, 0};
        assert(!DreamGpuPrimaryMetadata(&surface));
    }
    std::puts("PASS actual C++ Win98 2D preparation: exact fill/copy records, 8/16/32bpp, "
              "clipping, overflow rejection without writes and 16/32bpp primary metadata "
              "pitch/alignment/size bounds with 8/15/24bpp rejected");
}
