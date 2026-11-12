/* SPDX-License-Identifier: GPL-2.0-or-later
 * Driver side of DG_CAP2_GL_BULK_READ results (dg-gl-bulk.h): which requests
 * use the page list, the capacity word and offered limit, the frame list and
 * the result validator's bound, plus the register cache with a PAGES word. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t ULONG;
#include "gpu.h"
#include "gl.h"
#include "dg-escape.h"
#include "dg-gl-result.h"
#include "dg-gl-bulk.h"
#include "dg-gl-registers.h"

typedef struct {
    ULONG Registers[DG_MMIO_SIZE / 4];
    ULONG Writes[DG_MMIO_SIZE / 4], TotalWrites;
} Host;

static ULONG Read(void *context, ULONG reg) {
    Host *host = context;
    assert(!(reg & 3) && reg < DG_MMIO_SIZE);
    return host->Registers[reg / 4];
}

static void Write(void *context, ULONG reg, ULONG value) {
    Host *host = context;
    assert(!(reg & 3) && reg < DG_MMIO_SIZE);
    ++host->TotalWrites;
    ++host->Writes[reg / 4];
    host->Registers[reg / 4] = value;
}

static void Selection(void) {
    /* Contiguous up to the existing buffer, the page list above it. */
    assert(!DgGlBulkResult(0) && !DgGlBulkResult(512) && !DgGlBulkResult(65536));
    assert(DgGlBulkResult(65537) && DgGlBulkResult(DG_ESCAPE_BULK_RESULT_BYTES));
    assert(DgGlResultWord(512) == 512 && DgGlResultWord(65536) == 65536);
    assert(DgGlResultWord(65537) == (65537 | DG_GL_RESULT_PAGES));
    assert(DgGlResultWord(DG_ESCAPE_BULK_RESULT_BYTES) ==
           (DG_ESCAPE_BULK_RESULT_BYTES | DG_GL_RESULT_PAGES));
    assert((DgGlResultWord(DG_ESCAPE_BULK_RESULT_BYTES) & ~DG_GL_RESULT_PAGES) ==
           DG_ESCAPE_BULK_RESULT_BYTES);
    assert(DG_GL_BULK_PAGES == 1024 && DG_ESCAPE_BULK_RESULT_BYTES == DG_GL_MAX_BULK_RESULT_BYTES);
}

static void Limits(void) {
    const ULONG legacy = DG_ESCAPE_LEGACY_RESULT_BYTES, wide = DG_ESCAPE_MAX_RESULT_BYTES;
    ULONG caps2, ready, readback, n = 0;
    for (caps2 = 0; caps2 < 2; ++caps2)
        for (ready = 0; ready < 2; ++ready)
            for (readback = 0; readback < 2; ++readback) {
                const ULONG caps = readback ? DG_CAP_GL_BULK_READBACK : 0;
                /* Other second-word bits never enable it. */
                const ULONG word = (caps2 ? DG_CAP2_GL_BULK_READ : 0) | DG_CAP2_GL_DEPTH_TEXTURE |
                                   DG_CAP2_GL_RGB565;
                const ULONG limit = DgGlResultLimit(caps, word, (int)ready);
                if (caps2 && ready)
                    assert(limit == DG_ESCAPE_BULK_RESULT_BYTES);
                else
                    assert(limit == (readback ? wide : legacy));
                ++n;
            }
    assert(n == 8);
    /* A missing buffer keeps exactly the older driver's answer. */
    assert(DgGlResultLimit(DG_CAP_GL_BULK_READBACK, DG_CAP2_GL_BULK_READ, 0) == wide);
    assert(DgGlResultLimit(0, 0xffffffffUL & ~DG_CAP2_GL_BULK_READ, 1) == legacy);
}

static void Frames(void) {
    static ULONG list[DG_GL_RESULT_PAGE_BYTES / 4];
    ULONG i;
    memset(list, 0xa5, sizeof(list));
    for (i = 0; i < DG_GL_BULK_PAGES; ++i)
        assert(DgGlBulkSetFrame(list, i,
                                0x100000ULL + (unsigned long long)(DG_GL_BULK_PAGES - i) * 4096));
    for (i = 0; i < DG_GL_BULK_PAGES; ++i)
        assert(list[i] == 0x100 + DG_GL_BULK_PAGES - i);
    /* Highest 32-bit frame (a PAE address) fits; anything else stores nothing. */
    assert(DgGlBulkSetFrame(list, 3, 0xffffffffULL << 12) && list[3] == 0xffffffffUL);
    list[5] = 0x1234;
    assert(!DgGlBulkSetFrame(list, 5, 0x100000000000ULL));
    assert(!DgGlBulkSetFrame(list, 5, 0x10000800ULL));
    assert(!DgGlBulkSetFrame(list, 5, 0x10000001ULL));
    assert(!DgGlBulkSetFrame(list, 5, 0));
    assert(!DgGlBulkSetFrame(NULL, 5, 0x1000));
    assert(list[5] == 0x1234);
    assert(!DgGlBulkSetFrame(list, DG_GL_BULK_PAGES, 0x1000));
    assert(!DgGlBulkSetFrame(list, 0xffffffffUL, 0x1000));
}

static void Results(void) {
    static unsigned char data[DG_ESCAPE_BULK_RESULT_BYTES + 4];
    data[0] = 2;
    assert(DgValidateGlResult(DG_GL_RESULT_INT, DG_ESCAPE_BULK_RESULT_BYTES,
                              DG_ESCAPE_BULK_RESULT_BYTES, data));
    assert(DgValidateGlResult(DG_GL_RESULT_FLOAT, 3 << 20, DG_ESCAPE_BULK_RESULT_BYTES, data));
    assert(DgValidateGlResult(DG_GL_RESULT_INT, 65540, 65540, data));
    assert(!DgValidateGlResult(DG_GL_RESULT_INT, 4, DG_ESCAPE_BULK_RESULT_BYTES + 1, data));
    assert(!DgValidateGlResult(DG_GL_RESULT_INT, DG_ESCAPE_BULK_RESULT_BYTES + 4,
                               DG_ESCAPE_BULK_RESULT_BYTES, data));
    assert(!DgValidateGlResult(DG_GL_RESULT_INT, 65542, 65544, data));
    /* The capacity word's flag is never a capacity. */
    assert(!DgValidateGlResult(DG_GL_RESULT_INT, 4, DgGlResultWord(65540), data));
    assert(!DgValidateGlResult(DG_GL_RESULT_BOOL, 1, DG_ESCAPE_BULK_RESULT_BYTES, data));
}

static void Registers(void) {
    static Host host;
    DG_GL_REGISTERS cache = {0};
    ULONG i;
    host.Registers[DG_REG_GENERATION / 4] = 3;
    host.Registers[DG_REG_CAPS / 4] = DG_CAP_GL_TRANSPORT | DG_CAP_GL_BULK_READBACK;
    host.Registers[DG_GL_REG_VERSION / 4] = DG_GL_VERSION;
    host.Registers[DG_GL_REG_CAPS2 / 4] = DG_CAP2_GL_BULK_READ;
    assert(DgGlRegistersRefresh(&cache, 3, 1, Read, &host));
    assert(cache.Caps2 & DG_CAP2_GL_BULK_READ);
    /* Alternating small and bulk queries reprograms exactly what changed. */
    for (i = 0; i < 100; ++i) {
        const ULONG before = host.TotalWrites;
        DgGlProgramResult(&cache, 0x20000, 0, 512, Write, &host);
        assert(host.Registers[DG_GL_REG_RESULT_ADDR_LO / 4] == 0x20000);
        assert(host.Registers[DG_GL_REG_RESULT_CAPACITY / 4] == 512);
        DgGlProgramResult(&cache, 0x31000, 0, DgGlResultWord(3 << 20), Write, &host);
        assert(host.Registers[DG_GL_REG_RESULT_ADDR_LO / 4] == 0x31000);
        assert(host.Registers[DG_GL_REG_RESULT_CAPACITY / 4] == ((3u << 20) | DG_GL_RESULT_PAGES));
        assert(host.TotalWrites - before == 6);
    }
    /* Repeated bulk queries of one size write nothing again. */
    i = host.TotalWrites;
    DgGlProgramResult(&cache, 0x31000, 0, DgGlResultWord(3 << 20), Write, &host);
    assert(host.TotalWrites == i);
    /* A bulk size change rewrites only the capacity word. */
    DgGlProgramResult(&cache, 0x31000, 0, DgGlResultWord(DG_ESCAPE_BULK_RESULT_BYTES), Write,
                      &host);
    assert(host.TotalWrites == i + 1);
    /* Native reset clears the descriptor: the next bulk query programs all. */
    memset(host.Registers, 0, sizeof(host.Registers));
    host.Registers[DG_REG_GENERATION / 4] = 4;
    host.Registers[DG_REG_CAPS / 4] = DG_CAP_GL_TRANSPORT;
    host.Registers[DG_GL_REG_VERSION / 4] = DG_GL_VERSION;
    assert(DgGlRegistersRefresh(&cache, 4, 0, Read, &host));
    /* The new host lacks the capability: a driver now offers 64 KiB. */
    assert(!(cache.Caps2 & DG_CAP2_GL_BULK_READ) &&
           DgGlResultLimit(cache.Caps, cache.Caps2, 1) == DG_ESCAPE_LEGACY_RESULT_BYTES);
    i = host.TotalWrites;
    DgGlProgramResult(&cache, 0x31000, 0, DgGlResultWord(DG_ESCAPE_BULK_RESULT_BYTES), Write,
                      &host);
    assert(host.TotalWrites == i + 3);
}

int main(void) {
    Selection();
    Limits();
    Frames();
    Results();
    Registers();
    puts("NT bulk results: page-list selection, offered limits, frame list, 4 MiB validator bound "
         "and register cache passed");
    return 0;
}
