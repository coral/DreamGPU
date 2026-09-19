/* SPDX-License-Identifier: GPL-2.0-or-later */
/* actual channel policy, mock only OS/MMIO. */
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include "../../../guest/win9x/channel32.cpp"
#undef memcpy
#undef memset
struct Model {
    alignas(4096) BYTE input[69632], output[69632], returned[4096];
    DWORD registers[4096]{};
    unsigned pins{}, unpins{}, allocations{}, frees{}, sem_created{}, sem_destroyed{}, waits{},
        signals{}, submits{};
    unsigned pin_fail{}, alloc_fail{}, bad_alignment{};
    bool held{}, critical{}, cursor_safe{true}, primary_owned{}, pending{}, signaled{};
    DWORD context{22}, submit_status{}, fail_opcode{}, last_opcode{}, last_operation{},
        query_sequence{}, retire_on_submit{};
} m;
static DWORD DREAMGPU_CDECL check_pages(void *, DWORD, DWORD count) {
    return count;
}
static DWORD DREAMGPU_CDECL pin_pages(void *, DWORD first, DWORD, void **address) {
    ++m.pins;
    if (m.pins == m.pin_fail) {
        --m.pins;
        return 0;
    }
    *address = first == 0x10 ? m.input : first == 0x20 ? m.output : m.returned;
    return 0xc0000000u + (first << 12);
}
static int DREAMGPU_CDECL copy_ptes(void *, DWORD, DWORD count, DWORD *out) {
    for (DWORD i = 0; i < count; ++i)
        out[i] = 7;
    return 1;
}
static void DREAMGPU_CDECL unlock_pages(void *, DWORD, DWORD) {
    ++m.unpins;
}
static void *DREAMGPU_CDECL allocate_user(void *, DWORD) {
    std::abort();
}
static void DREAMGPU_CDECL free_user(void *, void *) {
    std::abort();
}
static const DG9_MEMORY_SERVICES memory_services{check_pages,  pin_pages,     copy_ptes,
                                                 unlock_pages, allocate_user, free_user};
static volatile DWORD *DREAMGPU_CDECL registers() {
    return m.registers;
}
static DWORD DREAMGPU_CDECL read_reg(DWORD reg) {
    return m.registers[reg / 4];
}
static void DREAMGPU_CDECL write_reg(DWORD reg, DWORD value) {
    m.registers[reg / 4] = value;
}
static int DREAMGPU_CDECL critical() {
    return m.critical;
}
static DWORD DREAMGPU_CDECL disable_irq() {
    return 17;
}
static void DREAMGPU_CDECL restore_irq(DWORD flags) {
    assert(flags == 17);
}
static DWORD DREAMGPU_CDECL current_context() {
    return m.context;
}
static DWORD DREAMGPU_CDECL create_semaphore(DWORD count) {
    assert(count == 1);
    return ++m.sem_created;
}
static void DREAMGPU_CDECL destroy_semaphore(DWORD value) {
    assert(value);
    ++m.sem_destroyed;
}
static void DREAMGPU_CDECL wait_semaphore(DWORD value) {
    assert(value && !m.held);
    m.held = true;
    ++m.waits;
}
static void DREAMGPU_CDECL signal_semaphore(DWORD value) {
    assert(value && m.held);
    m.held = false;
    ++m.signals;
}
static void *DREAMGPU_CDECL allocate_dma(DWORD pages, DWORD *physical) {
    assert(pages == 16);
    ++m.allocations;
    *physical = 0x40000 + m.allocations * 65536 + (m.allocations == m.bad_alignment ? 1 : 0);
    return m.allocations == m.alloc_fail ? nullptr : std::calloc(pages, 4096);
}
static void DREAMGPU_CDECL free_dma(void *address) {
    assert(address);
    ++m.frees;
    std::free(address);
}
static DWORD DREAMGPU_CDECL submit(DWORD physical, DWORD bytes, DG_ESCAPE_REPLY *reply) {
    assert(physical == dg_gl_physical && bytes && bytes <= 65536 && m.held);
    ++m.submits;
    m.last_opcode = dg_gl_commands[0];
    m.last_operation = dg_gl_commands[8];
    if (m.submit_status && (!m.fail_opcode || m.fail_opcode == m.last_opcode))
        return m.submit_status;
    reply->CompletedSequence = m.submits;
    if (m.last_opcode == DG_GL_QUERY) {
        m.query_sequence = reply->CompletedSequence;
        dg_gl_result[0] = 1;
        m.registers[DG_GL_REG_RESULT_TYPE / 4] = DG_GL_RESULT_BOOL;
        m.registers[DG_GL_REG_RESULT_BYTES / 4] = 1;
        if (m.retire_on_submit)
            assert(Dg9OwnerRetireToken(&dg_gl_owners, m.retire_on_submit));
    }
    if (m.last_opcode == DG_GL_PRESENT) {
        m.registers[DG_GL_REG_PRESENT_SLOT / 4] = 2;
        m.registers[DG_GL_REG_PRESENT_EPOCH_LO / 4] = 3;
        m.registers[DG_GL_REG_PRESENT_FRAME_LO / 4] = 4;
    }
    return DG_ESCAPE_OK;
}
static int DREAMGPU_CDECL pending() {
    return m.pending;
}
static void DREAMGPU_CDECL signal_wait() {
    m.signaled = true;
}
static int DREAMGPU_CDECL cursor_safe() {
    return m.cursor_safe;
}
static int DREAMGPU_CDECL primary(DWORD *w, DWORD *h, DWORD *s, DWORD *o) {
    *w = 800;
    *h = 600;
    *s = 3200;
    *o = 0;
    return 1;
}
static void DREAMGPU_CDECL primary_owned(int owned) {
    m.primary_owned = owned;
}
static void DREAMGPU_CDECL fatal(DWORD) {
    std::abort();
}
static const DG9_CHANNEL_SERVICES services{
    registers,         read_reg,       write_reg,        critical,
    disable_irq,       restore_irq,    current_context,  create_semaphore,
    destroy_semaphore, wait_semaphore, signal_semaphore, allocate_dma,
    free_dma,          submit,         pending,          signal_wait,
    cursor_safe,       primary,        primary_owned,    fatal,
    nullptr,           nullptr,        nullptr,          &memory_services};
static DG9_CHANNEL_CONTROL control(DWORD code, DWORD input, DWORD output) {
    return {code, 0, input, output, 0x10000, 0x20000, 0x30001, 11, 33, 0};
}
static void cleaned() {
    assert(m.pins == m.unpins && m.waits == m.signals && !m.held);
}
static DG_ESCAPE_REPLY request(DWORD operation, DWORD client = 0, DWORD bytes = 0,
                               DWORD capacity = 0, DWORD function = 0) {
    DG_ESCAPE_REQUEST input{DG_ESCAPE_VERSION, operation, client, bytes, function, 0, capacity, 0};
    std::memcpy(m.input, &input, sizeof(input));
    auto io = control(DG_ESCAPE, sizeof(input) + bytes, sizeof(DG_ESCAPE_REPLY) + capacity);
    DWORD result = 999;
    assert(DreamGpuGlControl(&io, &result) && result == 0);
    cleaned();
    DG_ESCAPE_REPLY reply;
    std::memcpy(&reply, m.output, sizeof(reply));
    return reply;
}
static DG9_WINDOW_REPLY window(DWORD op, DWORD client, DWORD binding = 0, DWORD width = 64,
                               DWORD height = 64) {
    DG9_WINDOW_REQUEST input{DG9_WINDOW_VERSION, op, client, binding, 0, 0, 0, 0, 0};
    if (op == DG9_WINDOW_BIND) {
        input.Context = 4;
        input.Drawable = 5;
        input.Width = width;
        input.Height = height;
    }
    std::memcpy(m.input, &input, sizeof(input));
    auto io = control(DG9_WINDOW_IOCTL, sizeof(input), sizeof(DG9_WINDOW_REPLY));
    DWORD result = 99;
    assert(DreamGpuWindowControl(&io, &result) && result == 0);
    cleaned();
    DG9_WINDOW_REPLY reply;
    std::memcpy(&reply, m.output, sizeof(reply));
    return reply;
}
static void initialize() {
    m = Model{};
    dg_gl_initialized = dg_gl_faulted = 0;
    dg_gl_mutex = 0;
    dg_gl_commands = nullptr;
    dg_gl_result = nullptr;
    dg_gl_limits = {};
    dg_gl_function_cache = {};
    dg_gl_owners = {};
    std::memset(dg_windows, 0, sizeof(dg_windows));
    dg_window_next = 1;
    dg_desktop_active = 0;
    DreamGpuChannelBind(&services);
    m.registers[DG_REG_GENERATION / 4] = 1;
    m.registers[DG_REG_CAPS / 4] =
        DG_CAP_GL_TRANSPORT | DG_CAP_GL_PRESENT_BOUNDS | DG_CAP_GL_BULK_READBACK;
    m.registers[DG_GL_REG_VERSION / 4] = DG_GL_VERSION;
    m.registers[DG_GL_REG_MAX_BYTES / 4] = 65536;
    m.registers[DG_GL_REG_MAX_RECORDS / 4] = 1024;
}
int main() {
    assert(Dg9GlResultLimit(0) == 512 && Dg9GlResultLimit(DG_CAP_GL_TRANSPORT) == 512 &&
           Dg9GlResultLimit(DG_CAP_GL_BULK_READBACK) == 65536);
    for (unsigned mode = 1; mode <= 4; ++mode) {
        initialize();
        if (mode <= 2)
            m.alloc_fail = mode;
        else
            m.bad_alignment = mode - 2;
        auto reply = request(DG_ESCAPE_OPEN);
        assert(reply.Status == DG_ESCAPE_RESOURCES && m.allocations == 2);
        assert(m.frees == (m.alloc_fail ? 1 : 2) && !dg_gl_commands && !dg_gl_result);
        DreamGpuGlShutdown();
        assert(m.sem_created == m.sem_destroyed);
    }
    initialize();
    /* Malformed envelopes and failed pins never touch caller output/count. */
    for (unsigned stage = 1; stage <= 3; ++stage) {
        m.pins = m.unpins = 0;
        m.pin_fail = stage;
        std::memset(m.output, 0xa5, sizeof(m.output));
        DG_ESCAPE_REQUEST input{DG_ESCAPE_VERSION, DG_ESCAPE_OPEN, 0, 0, 0, 0, 0, 0};
        std::memcpy(m.input, &input, sizeof(input));
        auto io = control(DG_ESCAPE, sizeof(input), sizeof(DG_ESCAPE_REPLY));
        DWORD result = 0;
        assert(DreamGpuGlControl(&io, &result) && result == 87);
        assert(m.output[0] == 0xa5);
        cleaned();
    }
    m.pin_fail = 0;
    const auto opened = request(DG_ESCAPE_OPEN);
    assert(opened.Status == 0 && opened.Client && opened.MaxResultBytes == 65536);
    const DWORD client = opened.Client;
    assert(request(DG_ESCAPE_QUERY, client).Status == 0);
    const auto legacy_caps = request(DG_ESCAPE_CAPABILITIES, client);
    assert(legacy_caps.Status == DG_ESCAPE_OK && legacy_caps.Client == client &&
           legacy_caps.FunctionWords == m.registers[DG_REG_CAPS / 4] &&
           !(legacy_caps.FunctionWords & DG_CAP_GL_DEPTH_STENCIL_READBACK) &&
           !legacy_caps.ResultBytes && !legacy_caps.ResultType);
    m.registers[DG_REG_CAPS / 4] |= DG_CAP_GL_DEPTH_STENCIL_READBACK;
    assert(request(DG_ESCAPE_CAPABILITIES, client).FunctionWords &
           DG_CAP_GL_DEPTH_STENCIL_READBACK);
    assert(request(DG_ESCAPE_CAPABILITIES, client, 0, 0, 1).Status == DG_ESCAPE_INVALID);
    assert(request(DG_ESCAPE_CAPABILITIES, client + 100).Status == DG_ESCAPE_OWNER);
    m.context = 23;
    assert(request(DG_ESCAPE_QUERY, client).Status == DG_ESCAPE_OWNER);
    assert(request(DG_ESCAPE_CAPABILITIES, client).Status == DG_ESCAPE_OWNER);
    m.context = 22;
    DWORD command[12] = {DG_GL_CALL, 36, 999, 0, 0, 0, 0, 999, 0, 0, 0, 0};
    std::memcpy(m.input + 32, command, 36);
    assert(request(DG_ESCAPE_SUBMIT, client, 36).Status == 0);
    assert(dg_gl_commands[2] == client && dg_gl_commands[7] == 1);
    command[5] = DG_GL_PRESENT_RETAIN;
    std::memcpy(m.input + 32, command, 36);
    unsigned before = m.submits;
    assert(request(DG_ESCAPE_SUBMIT, client, 36).Status == DG_ESCAPE_INVALID &&
           m.submits == before);
    const DWORD retired = request(DG_ESCAPE_OPEN).Client;
    m.registers[DG_GL_REG_FUNCTION_WORDS / 4] = DG_GL_FUNCTION_QUERY;
    Dg9FunctionCacheRefresh(&dg_gl_function_cache, 1, 1);
    command[0] = DG_GL_QUERY;
    command[1] = 48;
    command[5] = 0;
    std::memcpy(m.input + 32, command, 48);
    m.retire_on_submit = retired;
    const auto query = request(DG_ESCAPE_SUBMIT, client, 48, 1);
    assert(query.Status == 0 && query.ResultBytes == 1 && query.ResultType == DG_GL_RESULT_BOOL);
    assert(query.CompletedSequence == m.query_sequence && m.output[sizeof(query)] == 1);
    assert(!Dg9OwnerFind(&dg_gl_owners, retired));
    m.retire_on_submit = 0;
    /* Retained frames are owned by bindings and discarded before client close. */
    auto bound = window(DG9_WINDOW_BIND, client);
    assert(bound.Status == 0 && bound.Binding);
    assert(window(DG9_WINDOW_BIND, client, 0, 4097, 64).Status == DG_ESCAPE_INVALID);
    assert(window(DG9_WINDOW_PREPARE, client, bound.Binding).Status == 0);
    before = m.submits;
    assert(window(DG9_WINDOW_PREPARE, client, bound.Binding).Status == DG_ESCAPE_INVALID &&
           m.submits == before);
    DG9_CHANNEL_BLT blt{bound.Binding, 0, 10 | (20u << 16), 32 | (32u << 16)};
    assert(DreamGpuWindowBlt(&blt) == 1 && m.primary_owned);
    cleaned();
    before = m.submits;
    blt.Client_EBX = 60;
    assert(!DreamGpuWindowBlt(&blt) && m.submits == before);
    cleaned();
    DreamGpuPrimaryAccess();
    assert(!m.primary_owned);
    cleaned();
    assert(window(DG9_WINDOW_FINISH, client, bound.Binding).Status == 0 &&
           m.last_operation == DG_DESKTOP_DISCARD);
    assert(window(DG9_WINDOW_PREPARE, client, bound.Binding).Status == 0);
    assert(request(DG_ESCAPE_CLOSE, client).Status == 0 && !Dg9OwnerFind(&dg_gl_owners, client));
    assert(!dg_windows[0].Token);
    DreamGpuGlShutdown();
    assert(m.allocations == m.frees && m.sem_created == m.sem_destroyed);
    /* Uncertain completion poisons future submits; shutdown retains DMA while
     * an actual OS waiter is still unwinding, then frees after it drains. */
    initialize();
    const DWORD timeout_client = request(DG_ESCAPE_OPEN).Client;
    command[0] = DG_GL_CALL;
    command[1] = 36;
    command[5] = 0;
    std::memcpy(m.input + 32, command, 36);
    m.submit_status = DG_ESCAPE_TIMEOUT;
    assert(request(DG_ESCAPE_SUBMIT, timeout_client, 36).Status == DG_ESCAPE_TIMEOUT &&
           dg_gl_faulted);
    cleaned();
    m.pending = true;
    DreamGpuGlShutdown();
    assert(m.signaled && m.frees == 0);
    m.pending = false;
    DreamGpuGlShutdown();
    assert(m.allocations == m.frees);
    cleaned();
    std::puts("PASS actual C++ Win98 channel/window: pin/allocation failure ownership, OS "
              "identity, privileged packet rejection, typed result identity across cleanup, "
              "retained-frame lifecycle, GDI clips/coherence, timeout and waiter-safe teardown");
}
