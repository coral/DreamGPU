/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Source: vendor/vmdisp9x/control.c @718b3d51a1532fe1ba2e133cf76186f1a609d35e;
 * copied descriptor ABI with modifications: diagnostic DGPUICD name, explicit
 * layout checks, null/16-bit segment-bound validation. DreamGPU modifications
 * are GPL-2.0-or-later; the copied donor descriptor retains its MIT notice below.
 */
/*****************************************************************************

Copyright (c) 2022-2024 Jaroslav Hensl <emulator@emulace.cz>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*****************************************************************************/

#ifndef DG_ICD16_H
#define DG_ICD16_H
#include <i86.h>
#include <stddef.h>
/* Win16-only discovery adapter, compiled by Watcom. The pinned donor writes
 * two 32-bit fields plus262 ANSI bytes (270 total). Its comment mentions a
 * 532-byte loader buffer: that is NOT this structure or NT's wide-name ABI.
 * No caller capacity accompanies Control; the loader must supply >=270 bytes.
 */
#pragma pack(push)
#pragma pack(2)
typedef struct {
    DWORD Version;
    DWORD DriverVersion;
    char Name[262];
} DG_ICD_INFO16;
#pragma pack(pop)
typedef char dg_icd_dword_is_32[(sizeof(DWORD) == 4) ? 1 : -1];
typedef char dg_icd_name_offset[(offsetof(DG_ICD_INFO16, Name) == 8) ? 1 : -1];
typedef char dg_icd_response_bytes[(sizeof(DG_ICD_INFO16) == 270) ? 1 : -1];
#ifdef __WATCOMC__
typedef char dg_icd_far_pointer_is_32[(sizeof(LPVOID) == 4 && sizeof(void *) == 2) ? 1 : -1];
#endif
static LONG DgIcdGetInfo16(LPVOID output) {
    static const DG_ICD_INFO16 info = {2UL, 1UL, "DGPUICD"};
    if (!output || (DWORD)FP_OFF(output) + sizeof(info) > 0x10000UL)
        return -1;
    /* _fmemcpy retains output's selector; a near-pointer cast is invalid. */
    _fmemcpy(output, &info, sizeof(info));
    return 1;
}
#endif
