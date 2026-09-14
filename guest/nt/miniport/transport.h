/* SPDX-License-Identifier: GPL-2.0-or-later
 * Keep ntddk.h isolated from MinGW's overlapping video-miniport declarations.
 */
#ifndef DG_TRANSPORT_H
#define DG_TRANSPORT_H
PVOID DgAllocatePaged(ULONG Bytes, ULONG Tag);
VOID DgFreePool(PVOID Memory);
VOID DgDiagnosticsInitialize(PVOID RegistryPath);
VOID DgRecordStatus(PCWSTR Name, ULONG Value);
VOID DgDiagnosticsIrq(ULONG Level, ULONG Vector, ULONG Mode, ULONG AssignedLevel,
                      ULONG AssignedVector);
VOID DgDiagnosticsIsrEntry(PVOID Device, PVOID Transport, PVOID Handler);
BOOLEAN DgTransportTimingSupported(PVOID Transport);
BOOLEAN DgTransportSetRate(PVOID Transport, ULONG Rate);
ULONG DgTransportTiming(PVOID Transport, PVOID Input, ULONG InputBytes, PVOID Output,
                        ULONG OutputBytes);
PVOID DgTransportCreate(PVOID Registers);
VOID DgTransportDestroy(PVOID Transport);
BOOLEAN DgTransportInterrupt(PVOID Transport);
BOOLEAN DgTransportSubmit(PVOID Transport, const PVOID Commands, ULONG Count);
VOID DgTransportReset(PVOID Transport);
VOID DgDriverInitialize(PVOID DriverObject);
ULONG DgTransportGl(PVOID Transport, PVOID Input, ULONG InputBytes, PVOID Output,
                    ULONG OutputBytes);
BOOLEAN DgTransportEnter(PVOID Transport);
VOID DgTransportLeave(PVOID Transport);
BOOLEAN DgTransportKernel(PVOID Transport, PVOID Input, ULONG InputBytes, PVOID Output,
                          ULONG OutputBytes);
#endif
