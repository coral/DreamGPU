/* SPDX-License-Identifier: GPL-2.0-or-later
 * DreamGPU-authored protocol. Canonical file: guest/include/gpu.h;
 * mirrored into vendor/qemu/include/standard-headers/dreamgpu/gpu.h.
 * DreamGPU guest GPU guest/host ABI. All fields are little endian uint32_t.
 * Keep this header usable by old Windows C compilers (no QEMU dependencies).
 */
#ifndef DREAMGPU_GPU_H
#define DREAMGPU_GPU_H

/* Experimental DreamGPU identity; not an upstream allocated PCI device ID. */
#define DG_PCI_VENDOR_ID 0x1234
#define DG_PCI_DEVICE_ID 0x1113
#define DG_ABI_VERSION 0x00010000
#define DG_MAGIC 0x47524a51 /* fixed wire signature */
#define DG_VRAM_BAR 0
#define DG_MMIO_BAR 2
#define DG_MMIO_SIZE 0x2000
#define DG_REG_MAGIC 0x1000
#define DG_REG_VERSION 0x1004
#define DG_REG_CAPS 0x1008
#define DG_REG_VRAM_SIZE 0x100c
#define DG_REG_BATCH_ADDR_LO 0x1010
#define DG_REG_BATCH_ADDR_HI 0x1014
#define DG_REG_BATCH_COUNT 0x1018
#define DG_REG_SUBMIT_SEQUENCE 0x101c
#define DG_REG_SUBMIT 0x1020
#define DG_REG_STATUS 0x1024
#define DG_REG_COMPLETED_SEQUENCE 0x1028
#define DG_REG_ERROR 0x102c
#define DG_REG_IRQ_ENABLE 0x1030
#define DG_REG_IRQ_STATUS 0x1034
#define DG_REG_RESET 0x1038
#define DG_REG_GENERATION 0x103c
#define DG_REG_MAX_COMMANDS 0x1040
#define DG_REG_MAX_WORK_BYTES 0x1044

#define DG_CAP_FILL 0x01
#define DG_CAP_COPY 0x02
#define DG_CAP_DAMAGE 0x04
#define DG_CAP_COMPLETION_IRQ 0x08
#define DG_CAP_INLINE_NO_IRQ 0x10
#define DG_CAP_CURSOR 0x20
#define DG_SUBMIT_START 0x01
#define DG_SUBMIT_INLINE_NO_IRQ 0x02
#define DG_STATUS_BUSY 0x01
#define DG_STATUS_DONE 0x02
#define DG_STATUS_ERROR 0x04
#define DG_IRQ_COMPLETION 0x01
#define DG_ERROR_NONE 0
#define DG_ERROR_BATCH_COUNT 1
#define DG_ERROR_DMA 2
#define DG_ERROR_COMMAND 3
#define DG_ERROR_BOUNDS 4
#define DG_ERROR_WORK_LIMIT 5
#define DG_MAX_COMMANDS 64
#define DG_MAX_WORK_BYTES 0x04000000
#define DG_COMMAND_BYTES 40
#define DG_CMD_FILL 1
#define DG_CMD_COPY 2
#define DG_CMD_DAMAGE 3

/* Ten uint32_t words, without padding, in this order. No host pointer fields.
 * Use these offsets with a guest/toolchain-specific unsigned 32-bit type.
 * Offsets and strides are bytes; width/height are pixels; bpp is 1, 2 or 4.
 * COPY requires equal source/destination stride and supports overlapping areas.
 * Unused source fields, unused color and reserved must be zero.
 */
#define DG_CMD_OPCODE 0
#define DG_CMD_BPP 4
#define DG_CMD_SRC_OFFSET 8
#define DG_CMD_DST_OFFSET 12
#define DG_CMD_SRC_STRIDE 16
#define DG_CMD_DST_STRIDE 20
#define DG_CMD_WIDTH 24
#define DG_CMD_HEIGHT 28
#define DG_CMD_COLOR 32
#define DG_CMD_RESERVED 36

#endif
