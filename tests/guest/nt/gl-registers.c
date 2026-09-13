/* SPDX-License-Identifier: GPL-2.0-or-later
 * Exercise the production MMIO cache against a counted register bank. These
 * checks cover the reset/migration boundaries that make skipped writes safe.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t ULONG;
#include "gpu.h"
#include "gl.h"
#include "dg-gl-registers.h"

typedef struct {
    ULONG Registers[DG_MMIO_SIZE / 4];
    ULONG Reads[DG_MMIO_SIZE / 4], Writes[DG_MMIO_SIZE / 4];
    ULONG TotalReads, TotalWrites;
} Host;

static ULONG Read(void *context, ULONG reg) {
    Host *host = context;
    assert(!(reg & 3) && reg < DG_MMIO_SIZE);
    ++host->TotalReads;
    ++host->Reads[reg / 4];
    return host->Registers[reg / 4];
}

static void Write(void *context, ULONG reg, ULONG value) {
    Host *host = context;
    assert(!(reg & 3) && reg < DG_MMIO_SIZE);
    ++host->TotalWrites;
    ++host->Writes[reg / 4];
    host->Registers[reg / 4] = value;
}

static void ResetHost(Host *host, ULONG generation) {
    memset(host->Registers, 0, sizeof(host->Registers));
    host->Registers[DG_REG_GENERATION / 4] = generation;
    host->Registers[DG_REG_CAPS / 4] = DG_CAP_GL_TRANSPORT;
    host->Registers[DG_GL_REG_VERSION / 4] = DG_GL_VERSION;
}

int main(void) {
    Host host = {0};
    DG_GL_REGISTERS cache = {0};
    ULONG i, reads, writes;
    ResetHost(&host, 7);
    assert(DgGlRegistersRefresh(&cache, 7, 0, Read, &host));
    DgGlProgramCommands(&cache, 0x10000, 0, Write, &host);
    DgGlProgramResult(&cache, 0x20000, 0, 512, Write, &host);
    assert(host.TotalReads == 2 && host.TotalWrites == 6);
    assert(host.Registers[DG_GL_REG_GENERATION / 4] == 7);
    for (i = 0; i < 10000; ++i) {
        assert(DgGlRegistersRefresh(&cache, 7, 0, Read, &host));
        DgGlProgramCommands(&cache, 0x10000, 0, Write, &host);
        DgGlProgramResult(&cache, 0x20000, 0, 512, Write, &host);
        assert(!DgGlCompletionError(DG_STATUS_DONE, Read, &host));
    }
    assert(host.TotalReads == 2 && host.TotalWrites == 6);
    /* Error details are read once only for ERROR; a following success must
     * not reuse a prior error in either the reply or diagnostics. */
    host.Registers[DG_GL_REG_ERROR / 4] = DG_GL_ERROR_CONTEXT;
    assert(DgGlCompletionError(DG_STATUS_DONE | DG_STATUS_ERROR, Read, &host) ==
           DG_GL_ERROR_CONTEXT);
    assert(host.Reads[DG_GL_REG_ERROR / 4] == 1);
    assert(!DgGlCompletionError(DG_STATUS_DONE, Read, &host));
    assert(host.Reads[DG_GL_REG_ERROR / 4] == 1);

    /* Native reset clears all descriptor registers and increments generation.
     * A retained guest cache must program every required field again. */
    ResetHost(&host, 8);
    reads = host.TotalReads;
    writes = host.TotalWrites;
    assert(DgGlRegistersRefresh(&cache, 8, 0, Read, &host));
    DgGlProgramCommands(&cache, 0x10000, 0, Write, &host);
    DgGlProgramResult(&cache, 0x20000, 0, 512, Write, &host);
    assert(host.TotalReads == reads + 2 && host.TotalWrites == writes + 6);
    assert(host.Registers[DG_GL_REG_ADDR_LO / 4] == 0x10000);
    assert(host.Registers[DG_GL_REG_GENERATION / 4] == 8);
    assert(host.Registers[DG_GL_REG_RESULT_CAPACITY / 4] == 512);

    /* Explicit driver reset invalidates even if a fault made the reset write
     * ineffective. OPEN likewise distrusts same-generation restored RAM. */
    DgGlRegistersInvalidate(&cache);
    assert(DgGlRegistersRefresh(&cache, 8, 0, Read, &host));
    assert(!cache.CommandsValid && !cache.ResultValid);
    DgGlProgramCommands(&cache, 0x10000, 0, Write, &host);
    host.Registers[DG_REG_CAPS / 4] = 0;
    assert(!DgGlRegistersRefresh(&cache, 8, 1, Read, &host));
    assert(!cache.CommandsValid && !cache.ResultValid);
    reads = host.TotalReads;
    assert(!DgGlRegistersRefresh(&cache, 8, 0, Read, &host));
    assert(host.TotalReads == reads);
    host.Registers[DG_REG_CAPS / 4] = DG_CAP_GL_TRANSPORT;
    host.Registers[DG_GL_REG_VERSION / 4] = DG_GL_VERSION + 1;
    assert(!DgGlRegistersRefresh(&cache, 8, 1, Read, &host));
    host.Registers[DG_GL_REG_VERSION / 4] = DG_GL_VERSION;
    assert(DgGlRegistersRefresh(&cache, 8, 1, Read, &host));
    writes = host.TotalWrites;
    DgGlProgramCommands(&cache, 0x10000, 0, Write, &host);
    DgGlProgramResult(&cache, 0x20000, 0, 512, Write, &host);
    assert(host.TotalWrites == writes + 6);

    /* Capacity changes are separate from addresses; neither old capacity nor
     * an old high address word may silently survive a later query/buffer. */
    writes = host.TotalWrites;
    DgGlProgramResult(&cache, 0x20000, 0, 128, Write, &host);
    assert(host.TotalWrites == writes + 1);
    DgGlProgramResult(&cache, 0x30000, 1, 128, Write, &host);
    assert(host.TotalWrites == writes + 3);
    DgGlProgramCommands(&cache, 0, 1, Write, &host);
    assert(host.TotalWrites == writes + 6);
    assert(host.Registers[DG_GL_REG_ADDR_LO / 4] == 0);
    assert(host.Registers[DG_GL_REG_ADDR_HI / 4] == 1);
    assert(host.Registers[DG_GL_REG_RESULT_ADDR_HI / 4] == 1);

    assert(DgGlRegistersRefresh(&cache, UINT32_MAX, 0, Read, &host));
    DgGlProgramCommands(&cache, 0x10000, 0, Write, &host);
    assert(host.Registers[DG_GL_REG_GENERATION / 4] == UINT32_MAX);
    assert(DgGlRegistersRefresh(&cache, 1, 0, Read, &host));
    DgGlProgramCommands(&cache, 0x10000, 0, Write, &host);
    assert(host.Registers[DG_GL_REG_GENERATION / 4] == 1);
    puts("GL registers: persistent descriptors, zero steady cache MMIO, reset/OPEN, old host, "
         "errors and generation wrap passed");
    return 0;
}
