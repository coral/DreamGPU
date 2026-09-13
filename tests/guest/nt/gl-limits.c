/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t ULONG;
#include "gl.h"
#include "gl-funcs.h"
#include "dg-escape.h"
#include "dg-gl-limits.h"
#include "dg-gl-validate.h"

typedef struct {
    ULONG Bytes, Records, Reads;
} Host;
static ULONG Read(void *context, ULONG reg) {
    Host *host = context;
    ++host->Reads;
    assert(reg == DG_GL_REG_MAX_BYTES || reg == DG_GL_REG_MAX_RECORDS);
    return reg == DG_GL_REG_MAX_BYTES ? host->Bytes : host->Records;
}
static ULONG Signature(void *context, ULONG fn) {
    (void)context;
    return fn == FEnum_glColor4f ? 4 : (ULONG)-1;
}

int main(void) {
    Host host = {DG_GL_MAX_BYTES, 256, 0};
    DG_GL_LIMITS limits = {0};
    ULONG batch[(DG_GL_MAX_RECORDS + 1) * 13], i;
    memset(batch, 0, sizeof(batch));
    for (i = 0; i < DG_GL_MAX_RECORDS + 1; ++i) {
        ULONG *r = batch + i * 13;
        r[0] = DG_GL_CALL;
        r[1] = 52;
        r[3] = r[4] = 1;
        r[8] = FEnum_glColor4f;
    }
    /* New driver on old native: never submit an unsupported record count. */
    assert(DgGlLimitsRefresh(&limits, 1, 0, Read, &host));
    assert(limits.MaxRecords == 256 && limits.MaxBytes == 65536 && host.Reads == 2);
    assert(DgValidateUserGl(batch, 256 * 52, 9, 1, &limits, Signature, NULL));
    assert(!DgValidateUserGl(batch, 257 * 52, 9, 1, &limits, Signature, NULL));
    host.Records = DG_GL_MAX_RECORDS;
    for (i = 0; i < 10000; ++i)
        assert(DgGlLimitsRefresh(&limits, 1, 0, Read, &host));
    assert(host.Reads == 2 && limits.MaxRecords == 256);
    assert(DgGlLimitsRefresh(&limits, 2, 0, Read, &host));
    assert(host.Reads == 4 && limits.MaxRecords == 1024);
    assert(DgValidateUserGl(batch, 1024 * 52, 9, 2, &limits, Signature, NULL));
    assert(!DgValidateUserGl(batch, 1025 * 52, 9, 2, &limits, Signature, NULL));
    assert(!DgValidateUserGl(batch, 52, 9, 1, &limits, Signature, NULL));
    assert(!DgValidateUserGl(batch, 65540, 9, 2, &limits, Signature, NULL));
    /* A restored OPEN may see a different host with the same saved epoch. */
    host.Records = 256;
    assert(DgGlLimitsRefresh(&limits, 2, 1, Read, &host));
    assert(host.Reads == 6 && limits.MaxRecords == 256);
    /* Byte limits independently restrict records/data; round down, not up. */
    host.Bytes = 4099;
    host.Records = 1024;
    assert(DgGlLimitsRefresh(&limits, UINT32_MAX, 0, Read, &host));
    assert(limits.MaxBytes == 4096 && limits.MaxRecords == 128);
    assert(DgValidateUserGl(batch, 78 * 52, 9, UINT32_MAX, &limits, Signature, NULL));
    assert(!DgValidateUserGl(batch, 79 * 52, 9, UINT32_MAX, &limits, Signature, NULL));
    host.Bytes = host.Records = UINT32_MAX;
    assert(DgGlLimitsRefresh(&limits, 1, 0, Read, &host));
    assert(limits.MaxBytes == 65536 && limits.MaxRecords == 1024);
    host.Records = 0;
    assert(!DgGlLimitsRefresh(&limits, 1, 1, Read, &host));
    assert(!DgValidateUserGl(batch, 52, 9, 1, &limits, Signature, NULL));
    host.Records = 1024;
    host.Bytes = 32;
    assert(!DgGlLimitsRefresh(&limits, 2, 0, Read, &host));
    host.Bytes = 0;
    assert(!DgGlLimitsRefresh(&limits, 3, 0, Read, &host));
    puts("GL limits: old/new hosts, 1024/1025 boundary, 64 KiB bound, cached MMIO, OPEN and "
         "generation refresh passed");
    return 0;
}
