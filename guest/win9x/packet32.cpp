/* SPDX-License-Identifier: GPL-2.0-or-later
 * No Windows headers, allocations, runtime or OS calls. This object is linked
 * directly into the LE VxD alongside its legacy entrypoint thunks.
 */
#include <stdint.h>
typedef uint32_t ULONG;
static_assert(sizeof(ULONG) == 4);
#if defined(__i386__)
static_assert(sizeof(void *) == 4);
#endif
#ifndef __cdecl
#if defined(__i386__)
#define __cdecl __attribute__((cdecl))
#else
#define __cdecl
#endif
#endif
#include "gl.h"
#include "dg-escape.h"
#include "packet32.h"
#include "dg-gl-validate.h"
#include "dg-gl-result.h"
extern "C" int __cdecl DreamGpuValidatePacket(ULONG *words, ULONG bytes, ULONG token,
                                              ULONG generation, const DG_GL_LIMITS *limits,
                                              ULONG(__cdecl *function_words)(void *, ULONG),
                                              void *context) {
    if (!words || !limits || !function_words)
        return 0;
    return DgValidateUserGl(words, bytes, token, generation, limits, function_words, context);
}
extern "C" int __cdecl DreamGpuValidateResult(ULONG type, ULONG bytes, ULONG capacity,
                                              const unsigned char *data) {
    return DgValidateGlResult(type, bytes, capacity, data);
}
