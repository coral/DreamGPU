/* SPDX-License-Identifier: GPL-2.0-or-later
 * Fixed-width context-free display escape. Define DG_TIMING_U32 to the
 * platform's unsigned 32-bit type before inclusion (including Win16).
 */
#ifndef DREAMGPU_DISPLAY_TIMING_H
#define DREAMGPU_DISPLAY_TIMING_H
#include "gpu.h"
#define DG_TIMING_ESCAPE 0x4a54
#define DG_TIMING_QUERY 0
#define DG_TIMING_REPLY_OK 0
#define DG_TIMING_REPLY_UNSUPPORTED 1
#define DG_TIMING_REPLY_TIMEOUT 2
#define DG_TIMING_REPLY_CANCELLED 3
#define DG_TIMING_REPLY_INVALID 4
typedef struct {
    DG_TIMING_U32 Version, Operation, Reserved0, Reserved1;
} DG_TIMING_REQUEST;
typedef struct {
    DG_TIMING_U32 Version, Status, RateHz, ScanLine, Height, InVBlank;
    DG_TIMING_U32 UntilBeginNs, UntilEndNs;
} DG_TIMING_REPLY;
typedef char DgTimingRequestSize[sizeof(DG_TIMING_REQUEST) == 16 ? 1 : -1];
typedef char DgTimingReplySize[sizeof(DG_TIMING_REPLY) == 32 ? 1 : -1];
static __inline int DgTimingRateValid(unsigned long rate) {
    return rate == 60 || rate == 75 || rate == 85 || rate == 100 || rate == 120;
}
#endif
