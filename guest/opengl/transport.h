/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef DG_OPENGL_TRANSPORT_H
#define DG_OPENGL_TRANSPORT_H
#ifdef __cplusplus
extern "C" {
#endif
/* Called once at process attach; performs no GDI, device open or allocation. */
void JglTransportInitialize(void);
/* Same immutable request/reply ABI on both OS families. Callers retain the
 * common frontend's client lifecycle serialization. Window binding/publishing
 * has its own platform boundary and cannot bypass authoritative clipping. */
int JglTransportRequest(HDC display, int input_bytes, LPCSTR input, int output_bytes, LPSTR output);
#include "dg-window.h"
int JglTransportBind(HDC, const DG_WINDOW_BIND *, DG_WINDOW_REPLY *);
int JglTransportPresent(HDC, const DG_WINDOW_PRESENT *);
void JglTransportUnbind(ULONG binding);

#ifdef __cplusplus
}
#endif
#endif
