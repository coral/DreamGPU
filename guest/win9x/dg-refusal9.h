/* SPDX-License-Identifier: GPL-2.0-or-later
 * Win9x DeviceIoControl refusal contract between the DreamGPU VxD
 * (channel32.cpp DreamGpuGlControl/DreamGpuWindowControl, dg-timing-vxd.c)
 * and the frontend (guest/opengl/transport9-retry.h). VWIN32 turns a nonzero
 * VxD result into DeviceIoControl FALSE with that value as GetLastError.
 *
 * DG9_ERROR_BUSY: the VxD refused the request before executing any part of
 * it: it took no GL mutex, submitted nothing, wrote no reply or output and
 * changed no client, window, owner or device state (GL and window requests
 * refuse before pinning or reading the request; the timing escape releases
 * its validation pins). Today the cause is that the calling thread may not
 * sleep: it owns the VMM critical section or the VMM is reentered
 * (dg-critical9.h); the timing escape also uses it for a concurrent waiter.
 * The request provably did not execute, so the caller may resend the
 * identical request on the same handle. Every path that returns this value
 * must keep that guarantee; anything that may have executed even partly must
 * report through the reply instead.
 *
 * DG9_ERROR_INVALID: malformed or unacceptable request (sizes, buffers,
 * versions, driver state). Deterministic for the same request, so it is
 * never retried.
 */
#ifndef DG_REFUSAL9_H
#define DG_REFUSAL9_H
#define DG9_ERROR_INVALID 87UL /* ERROR_INVALID_PARAMETER */
#define DG9_ERROR_BUSY 170UL   /* ERROR_BUSY */
#endif
