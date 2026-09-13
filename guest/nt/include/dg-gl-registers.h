/* SPDX-License-Identifier: GPL-2.0-or-later
 * Adapter-owned GL MMIO state. All callers hold the miniport's GlMutex.
 * Cache only registers that persist between submissions; OPEN and generation
 * changes invalidate every assumption, including same-generation migration.
 * ULONG is the driver's unsigned 32-bit type. Kept freestanding for tests.
 */
#ifndef DG_GL_REGISTERS_H
#define DG_GL_REGISTERS_H
typedef struct {
    ULONG Generation, Caps, Version, Valid;
    ULONG CommandsLo, CommandsHi, CommandsValid;
    ULONG ResultLo, ResultHi, ResultCapacity, ResultValid;
} DG_GL_REGISTERS;

static inline void DgGlRegistersInvalidate(DG_GL_REGISTERS *state) {
    state->Valid = state->CommandsValid = state->ResultValid = 0;
}

static inline int DgGlRegistersRefresh(DG_GL_REGISTERS *state, ULONG generation, int force,
                                       ULONG (*read)(void *, ULONG), void *context) {
    if (!state->Valid || state->Generation != generation || force) {
        DgGlRegistersInvalidate(state);
        state->Generation = generation;
        state->Caps = read(context, DG_REG_CAPS);
        state->Version = read(context, DG_GL_REG_VERSION);
        state->Valid = 1;
    }
    return (state->Caps & DG_CAP_GL_TRANSPORT) && state->Version == DG_GL_VERSION;
}

static inline void DgGlProgramCommands(DG_GL_REGISTERS *state, ULONG low, ULONG high,
                                       void (*write)(void *, ULONG, ULONG), void *context) {
    if (!state->CommandsValid || state->CommandsLo != low || state->CommandsHi != high) {
        write(context, DG_GL_REG_ADDR_LO, low);
        write(context, DG_GL_REG_ADDR_HI, high);
        write(context, DG_GL_REG_GENERATION, state->Generation);
        state->CommandsLo = low;
        state->CommandsHi = high;
        state->CommandsValid = 1;
    }
}

static inline void DgGlProgramResult(DG_GL_REGISTERS *state, ULONG low, ULONG high, ULONG capacity,
                                     void (*write)(void *, ULONG, ULONG), void *context) {
    if (!state->ResultValid || state->ResultLo != low || state->ResultHi != high) {
        write(context, DG_GL_REG_RESULT_ADDR_LO, low);
        write(context, DG_GL_REG_RESULT_ADDR_HI, high);
    }
    if (!state->ResultValid || state->ResultCapacity != capacity)
        write(context, DG_GL_REG_RESULT_CAPACITY, capacity);
    state->ResultLo = low;
    state->ResultHi = high;
    state->ResultCapacity = capacity;
    state->ResultValid = 1;
}

/* DONE without ERROR is a successful completion with error code zero. Read
 * the detailed code only for a failed completion, once for reply+diagnostics. */
static inline ULONG DgGlCompletionError(ULONG status, ULONG (*read)(void *, ULONG), void *context) {
    return status & DG_STATUS_ERROR ? read(context, DG_GL_REG_ERROR) : 0;
}
#endif
