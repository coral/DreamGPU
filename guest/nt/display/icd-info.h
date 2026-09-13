/* SPDX-License-Identifier: GPL-2.0-or-later
 * Diagnostic NT5 OPENGL_GETINFO reply. Layout/name discovery follows
 * Source: vendor/reactos/dll/opengl/opengl32/icdload.c
 * @ 22fb3bb2c1d8196cf501edbb49b3814739f7b016
 * Upstream: https://github.com/reactos/reactos
 * Reimplemented with bounded buffers; source layout describes Version,
 * DriverVersion and WCHAR DriverName[256]. ReactOS code is GPL-2.0-or-later.
 * The actual Windows 2000 loader requests 532 bytes (262 WCHARs), verified
 * against the private installed OS loader. Accept these two bounded layouts
 * explicitly and initialize every requested byte; do not assume donor parity.
 * This is not the differently sized Win98 donor Control reply.
 */
#ifndef DREAMGPU_NT_ICD_INFO_H
#define DREAMGPU_NT_ICD_INFO_H
#define DG_OPENGL_GETINFO 0x1101
struct DgIcdInformation {
    ULONG Version;
    ULONG DriverVersion;
    WCHAR DriverName[262];
};
static_assert(sizeof(DgIcdInformation) == 532);
static ULONG DgIcdGetInfo(ULONG input_bytes, const void *input, ULONG output_bytes, void *output) {
    if (!output || (output_bytes != 520 && output_bytes != sizeof(DgIcdInformation)) ||
        (input_bytes && (!input || input_bytes != sizeof(ULONG))))
        return 0;
    if (input_bytes) {
        ULONG query;
        memcpy(&query, input, sizeof(query));
        if (query)
            return 0;
    }
    const DgIcdInformation result = {2, 1, {'D', 'G', 'P', 'U', 'I', 'C', 'D', 0}};
    memcpy(output, &result, output_bytes);
    return 1;
}
#endif
