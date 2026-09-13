/* SPDX-License-Identifier: GPL-2.0-or-later
 * DreamGPU-authored protocol. Canonical file: include/dreamgpu/transport.h;
 * mirrored into vendor/qemu/include/standard-headers/dreamgpu/transport.h.
 */
#ifndef DREAMGPU_TRANSPORT_H
#define DREAMGPU_TRANSPORT_H

/* Fixed 128-byte little-endian records on a Unix SOCK_STREAM connection.
 * Receiver sends HELLO after accept. QEMU sends FRAME. Receiver sends RELEASE
 * only after its GPU has finished using the slot. Unknown versions disconnect.
 * Linux FRAME attaches one DMA-BUF fd and optional ready sync-fd to the first
 * byte of the record via SCM_RIGHTS. Mac FRAME travels in a complex Mach
 * message (header, body, one COPY_SEND port descriptor, then this record),
 * attaching an IOSurface send right. Other records always use the Unix socket.
 */
#define DG_TRANSPORT_MAGIC 0x4a475055
#define DG_TRANSPORT_VERSION 2
#define DG_TRANSPORT_PACKET_BYTES 128
#define DG_TRANSPORT_MAX_EXPORT_SLOTS 96
#define DG_TRANSPORT_MAX_DRAWABLES 32
#define DG_TRANSPORT_CPU_SLOT_BASE 96
#define DG_TRANSPORT_MAX_CPU_SLOTS 8
#define DG_TRANSPORT_MAX_CPU_BYTES 0x04000000
#define DG_TRANSPORT_KIND_HELLO 1
#define DG_TRANSPORT_KIND_FRAME 2
#define DG_TRANSPORT_KIND_RELEASE 3
#define DG_TRANSPORT_KIND_ERROR 4
#define DG_TRANSPORT_KIND_DRAWABLE 5
#define DG_TRANSPORT_KIND_CPU_DESKTOP 6
#define DG_TRANSPORT_KIND_DESKTOP_OP 7
#define DG_TRANSPORT_KIND_DESKTOP_REPLY 8
#define DG_TRANSPORT_KIND_RESOURCE_DROP 9
#define DG_TRANSPORT_KIND_RESET 10
#define DG_TRANSPORT_PLATFORM_MACOS 1
#define DG_TRANSPORT_PLATFORM_LINUX 2
#define DG_TRANSPORT_FLAG_TOP_LEFT 1
#define DG_TRANSPORT_FLAG_READY_FENCE 2
#define DG_TRANSPORT_FLAG_RETAIN_FOR_DESKTOP 4
#define DG_TRANSPORT_FORMAT_ARGB8888 0x34325241 /* DRM AR24, bytes BGRA */

/* FRAME is a complete canonical desktop only. DRAWABLE has the same payload
 * and handle transfer but is an offscreen/window resource; importing it must
 * never replace the desktop without an ordered guest-driver composition op.
 */

#define DG_TRANSPORT_OFF_MAGIC 0
#define DG_TRANSPORT_OFF_VERSION 4
#define DG_TRANSPORT_OFF_KIND 8
#define DG_TRANSPORT_OFF_SIZE 12
#define DG_TRANSPORT_OFF_FLAGS 16
#define DG_TRANSPORT_OFF_WIDTH 20
#define DG_TRANSPORT_OFF_HEIGHT 24
#define DG_TRANSPORT_OFF_STRIDE 28
#define DG_TRANSPORT_OFF_FOURCC 32
#define DG_TRANSPORT_OFF_OFFSET 36
#define DG_TRANSPORT_OFF_SLOT 40
#define DG_TRANSPORT_OFF_CLIENT 44
#define DG_TRANSPORT_OFF_DRAWABLE 48
#define DG_TRANSPORT_OFF_EPOCH 56
#define DG_TRANSPORT_OFF_GENERATION 64
#define DG_TRANSPORT_OFF_MODIFIER 72
#define DG_TRANSPORT_OFF_DEVICE_UUID 80

/* HELLO reuses bytes 16..127: platform at16, bootstrap service at24 (56-byte
 * NUL-terminated array), Vulkan device UUID at80, render node at96 (32-byte
 * NUL-terminated array, e.g. /dev/dri/renderD128). Mac ignores UUID/render node;
 * Linux ignores bootstrap service. FRAME bytes52..55 and96..127 are zero.
 * RELEASE contains only slot/epoch/generation; other payload bytes are zero.
 */
#define DG_TRANSPORT_HELLO_PLATFORM 16
#define DG_TRANSPORT_HELLO_MACH_SERVICE 24
#define DG_TRANSPORT_HELLO_MACH_SERVICE_LEN 56
#define DG_TRANSPORT_HELLO_RENDER_NODE 96
#define DG_TRANSPORT_HELLO_RENDER_NODE_LEN 32

/* RESOURCE_DROP contains client44/drawable48 and the last image epoch56 /
 * generation64 cutoff. It is ordered after prior desktop ops. Late Mach images
 * at or below the cutoff must be released, including after ID reuse. RESET supplies the
 * reset-coherent future CPU anchor at88/96, all other payload zero, and discards
 * graphics state after an explicit device reset. CPU presentation waits for
 * that anchor across independently ordered display/control socket messages.
 */

/* CPU_DESKTOP uses one immutable shared-memory fd (SCM_RIGHTS on both hosts).
 * Fields: subtype16, width20, height24, stride28, AR24fourcc32, offset36=0,
 * CPUslot40 (96..103), desktopEpoch56, contiguousSequence64, allocationBytes72,
 * destinationX80/Y84. SEED and RETURN_CPU cover the complete primary at(0,0).
 * RETURN_CPU additionally supplies the minimum legacy CPU display epoch88 /
 * generation96 that may supersede this exact returned frame. The consumer
 * keeps the returned FD image until a CPU frame reaches that anchor.
 * All other payload bytes are zero. RELEASE uses the normal slot/epoch/seq.
 */
#define DG_TRANSPORT_CPU_SEED 1
#define DG_TRANSPORT_CPU_PATCH 2
#define DG_TRANSPORT_CPU_RETURN 3
#define DG_TRANSPORT_CPU_OFF_SUBTYPE 16
#define DG_TRANSPORT_CPU_OFF_ALLOCATION 72
#define DG_TRANSPORT_CPU_OFF_DST_X 80
#define DG_TRANSPORT_CPU_OFF_DST_Y 84
#define DG_TRANSPORT_CPU_OFF_LEGACY_EPOCH 88
#define DG_TRANSPORT_CPU_OFF_LEGACY_FRAME 96

/* DESKTOP_OP: opcode16, destX20/Y24, width28/height32, srcX36/Y40,
 * client44/drawable48, GPUslot52, desktopEpoch56/sequence64,
 * drawableEpoch72/generation80, readbackToken88, color96, flags100.
 * Desktop ordering is independent of the referenced image resource epoch.
 * BLIT_RELEASE means the operation takes the final registry reference to the
 * retained DRAWABLE; earlier clipped blits may reference it without this bit.
 */
#define DG_TRANSPORT_DESKTOP_FILL 1
#define DG_TRANSPORT_DESKTOP_COPY 2
#define DG_TRANSPORT_DESKTOP_BLIT 3
#define DG_TRANSPORT_DESKTOP_READBACK 4
#define DG_TRANSPORT_DESKTOP_DISCARD 5
#define DG_TRANSPORT_DESKTOP_BLIT_RELEASE 1
#define DG_TRANSPORT_DESKTOP_OFF_OPCODE 16
#define DG_TRANSPORT_DESKTOP_OFF_DST_X 20
#define DG_TRANSPORT_DESKTOP_OFF_DST_Y 24
#define DG_TRANSPORT_DESKTOP_OFF_WIDTH 28
#define DG_TRANSPORT_DESKTOP_OFF_HEIGHT 32
#define DG_TRANSPORT_DESKTOP_OFF_SRC_X 36
#define DG_TRANSPORT_DESKTOP_OFF_SRC_Y 40
#define DG_TRANSPORT_DESKTOP_OFF_GPU_SLOT 52
#define DG_TRANSPORT_DESKTOP_OFF_GPU_EPOCH 72
#define DG_TRANSPORT_DESKTOP_OFF_GPU_FRAME 80
#define DG_TRANSPORT_DESKTOP_OFF_TOKEN 88
#define DG_TRANSPORT_DESKTOP_OFF_COLOR 96
#define DG_TRANSPORT_DESKTOP_OFF_FLAGS 100

/* DESKTOP_REPLY: status16 (zero=success), width20/height24/stride28,
 * desktopEpoch56/sequence64, allocationBytes72, readbackToken88. Success has
 * one immutable packed BGRA fd; errors have no fd. The sender can close its
 * descriptor after sendmsg; QEMU owns its received copy until DMA completes.
 * Version 2 DISCARD uses desktopEpoch=sequence=0 and releases the exact
 * retained resource in FIFO order without painting, requiring a desktop seed,
 * or changing CPU authority. Its image identity remains mandatory.
 * Sequenced DISCARD records retain their ordinary desktop barrier semantics.
 */

#endif
