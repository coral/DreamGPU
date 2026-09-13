/* SPDX-License-Identifier: GPL-2.0-or-later
 * Fixed-width flat 32-bit C ABI between Watcom VxD entrypoints and GCC C++23.
 * The callback is explicitly cdecl: Watcom otherwise uses register arguments.
 */
#ifndef DREAMGPU_PACKET32_H
#define DREAMGPU_PACKET32_H
#include "dg-gl-limits.h"
#ifdef __cplusplus
extern "C" {
#endif
int __cdecl DreamGpuValidatePacket(ULONG *, ULONG, ULONG, ULONG, const DG_GL_LIMITS *,
                                   ULONG(__cdecl *)(void *, ULONG), void *);
int __cdecl DreamGpuValidateResult(ULONG, ULONG, ULONG, const unsigned char *);
#ifdef __cplusplus
}
#endif
#endif
