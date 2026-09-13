/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef JGL_PACKING_H
#define JGL_PACKING_H

/* These wrappers only copy argument bits. On i686, x87 load/store substitutions
 * would quiet signaling NaNs and change floating-point status for subnormals.
 * Keep their copies in integer registers; actual arithmetic lives elsewhere.
 * All public scalar arguments use the unchanged i686 stdcall stack ABI. */
#if defined(__i386__)
#define JGL_PACKING_ONLY __attribute__((target("general-regs-only")))
#else
#define JGL_PACKING_ONLY
#endif

#endif
