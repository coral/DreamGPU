/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef DG_IOCTL_H
#define DG_IOCTL_H
#include "gpu.h"
#include "gl.h"
#include "dg-escape.h"
#define DG_TIMING_U32 ULONG
#include "display-timing.h"
#undef DG_TIMING_U32
#define IOCTL_VIDEO_DG_CAPS CTL_CODE(FILE_DEVICE_VIDEO, 0x900, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIDEO_DG_TIMING CTL_CODE(FILE_DEVICE_VIDEO, 0x904, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIDEO_DG_GL CTL_CODE(FILE_DEVICE_VIDEO, 0x902, METHOD_BUFFERED, FILE_ANY_ACCESS)
typedef struct {
    ULONG Opcode, Bpp, Source, Destination, SourceStride, DestinationStride;
    ULONG Width, Height, Color, Reserved;
} DG_COMMAND;
typedef struct {
    ULONG Count;
    DG_COMMAND Commands[DG_MAX_COMMANDS];
} DG_BATCH;
typedef char DgCommandSizeCheck[sizeof(DG_COMMAND) == DG_COMMAND_BYTES ? 1 : -1];
#endif
