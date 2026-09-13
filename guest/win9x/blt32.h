/* SPDX-License-Identifier: GPL-2.0-or-later
 * Flat checked 2D preparation ABI. */
#ifndef DREAMGPU_BLT32_H
#define DREAMGPU_BLT32_H
#ifndef DREAMGPU_CDECL
#if defined(__WATCOMC__)
#define DREAMGPU_CDECL __cdecl
#elif defined(__i386__)
#define DREAMGPU_CDECL __attribute__((cdecl))
#else
#define DREAMGPU_CDECL
#endif
#endif
typedef struct {
    DWORD opcode, bpp, src_offset, dst_offset, src_stride, dst_stride;
    DWORD width, height, color, reserved;
} DG9_COMMAND;
typedef struct {
    DWORD Width, Height, Bpp, Pitch, Offset;
} DG9_SURFACE;
#ifdef __cplusplus
extern "C" {
#endif
int DREAMGPU_CDECL DreamGpuPrepareBlt(const DG9_SURFACE *, DWORD, DWORD, DWORD, DWORD,
                                      DG9_COMMAND *);
int DREAMGPU_CDECL DreamGpuPrimaryMetadata(const DG9_SURFACE *);
#ifdef __cplusplus
}
#endif
#endif
