/* SPDX-License-Identifier: GPL-2.0-or-later */
/* actual channel policy, mock only OS/MMIO. */
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <initializer_list>
#include <utility>
#include <sys/wait.h>
#include <unistd.h>
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
    /* Live FBHDA primary as validated by the VxD adapter. */
    DWORD width{800}, height{600}, stride{3200}, bpp{32};
    bool primary_ok{true}, expect_fatal{};
    unsigned fatal_submits{}, record_count{};
    DWORD records[64][24]{};
    /* Queued transport model: DMA allocations by physical address, the
     * device's counters, and failure/observation hooks. */
    bool queue{};
    struct {
        DWORD physical;
        BYTE *memory;
        DWORD pages;
    } dma[16]{};
    unsigned dma_count{}, reads{}, writes{}, doorbells{}, queue_fail_function{};
    DWORD consumed{}, last_stream{}, last_client{};
    /* Bulk results: the driver's fixed pages (Memory->Allocate), the frames
     * their page table entries name, and what the device writes there. */
    BYTE *bulk_pages{};
    unsigned user_allocations{}, user_frees{}, user_alloc_fail{}, absent_pte{}, pin_fail_first{};
    DWORD bulk_bytes{}, bulk_seed{}, bulk_list{}, bulk_word{};
} m;
/* Caller output of a bulk request at linear pages BULK_FIRST.. (4 MiB result,
 * reply header and an unaligned start). Outside Model: m is reset by value. */
constexpr DWORD BULK_FIRST = 0x1000;
alignas(4096) static BYTE bulk_output[DG_ESCAPE_BULK_RESULT_BYTES + 8192];
/* Frame of bulk page i: descending with gaps, so the list is truly scattered. */
static DWORD bulk_frame(DWORD i) {
    return 0x80000 + (DG_GL_BULK_PAGES - 1 - i) * 3;
}
static BYTE *bulk_page(DWORD frame) {
    assert(m.bulk_pages && frame >= 0x80000 && (frame - 0x80000) % 3 == 0);
    const DWORD i = DG_GL_BULK_PAGES - 1 - (frame - 0x80000) / 3;
    assert(i < DG_GL_BULK_PAGES);
    return m.bulk_pages + i * 4096;
}
static BYTE bulk_byte(DWORD seed, DWORD at) {
    return static_cast<BYTE>(at * 131 + (at >> 12) * 7 + (at >> 16) + seed);
}
static BYTE *dma_memory(DWORD physical) {
    for (unsigned i = 0; i < m.dma_count; ++i)
        if (physical >= m.dma[i].physical && physical < m.dma[i].physical + m.dma[i].pages * 4096)
            return m.dma[i].memory + (physical - m.dma[i].physical);
    std::abort();
}
/* The device's page-list write (gl.h DG_GL_RESULT_PAGES): byte i lands at
 * frame[i / 4096] << 12 + i % 4096, within the capacity. */
static void write_bulk(DWORD list, DWORD word, DWORD bytes) {
    assert((word & DG_GL_RESULT_PAGES) && bytes <= (word & ~DG_GL_RESULT_PAGES) && !(list & 4095));
    const auto *frames = reinterpret_cast<const DWORD *>(dma_memory(list));
    m.bulk_list = list;
    m.bulk_word = word;
    for (DWORD at = 0; at < bytes; ++at)
        bulk_page(frames[at / 4096])[at % 4096] = bulk_byte(m.bulk_seed, at);
}
static DWORD *page_word(DWORD offset) {
    return reinterpret_cast<DWORD *>(dma_memory(m.registers[DG_GL_REG_QUEUE_ADDR_LO / 4]) + offset);
}
/* The modelled device consumes and completes at the doorbell, as a host that
 * keeps up does: no wait in the driver ever sleeps. */
static void doorbell() {
    const DWORD produced = *page_word(DG_GL_QUEUE_PRODUCED);
    ++m.doorbells;
    while (m.consumed != produced) {
        const DWORD entry = DG_GL_QUEUE_RING + (m.consumed % DG_GL_QUEUE_DEPTH) * 64;
        const auto *batch =
            reinterpret_cast<const DWORD *>(dma_memory(*page_word(entry + DG_GL_ENTRY_ADDR_LO)));
        const DWORD bytes = *page_word(entry + DG_GL_ENTRY_BYTES);
        assert(bytes && bytes <= DG_ESCAPE_QUEUE_MAX_BYTES &&
               *page_word(entry + DG_GL_ENTRY_GENERATION) == m.registers[DG_REG_GENERATION / 4]);
        m.last_opcode = batch[0];
        m.last_operation = batch[8];
        m.last_client = *page_word(entry + DG_GL_ENTRY_CLIENT);
        m.last_stream = *page_word(entry + DG_GL_ENTRY_STREAM);
        ++m.submits;
        const bool failed =
            m.queue_fail_function && batch[0] == DG_GL_CALL && batch[8] == m.queue_fail_function;
        *page_word(entry + DG_GL_ENTRY_STATUS) = DG_STATUS_DONE | (failed ? DG_STATUS_ERROR : 0);
        *page_word(entry + DG_GL_ENTRY_ERROR) = failed ? DG_GL_ERROR_HOST : 0;
        *page_word(entry + DG_GL_ENTRY_RESULT_BYTES) = 0;
        if (batch[0] == DG_GL_QUERY &&
            (*page_word(entry + DG_GL_ENTRY_RESULT_CAPACITY) & DG_GL_RESULT_PAGES)) {
            assert(!*page_word(entry + DG_GL_ENTRY_RESULT_ADDR_HI));
            write_bulk(*page_word(entry + DG_GL_ENTRY_RESULT_ADDR_LO),
                       *page_word(entry + DG_GL_ENTRY_RESULT_CAPACITY), m.bulk_bytes);
            *page_word(entry + DG_GL_ENTRY_RESULT_BYTES) = m.bulk_bytes;
            *page_word(entry + DG_GL_ENTRY_RESULT_TYPE) = DG_GL_RESULT_INT;
        } else if (batch[0] == DG_GL_QUERY) {
            assert(*page_word(entry + DG_GL_ENTRY_RESULT_CAPACITY));
            *dma_memory(*page_word(entry + DG_GL_ENTRY_RESULT_ADDR_LO)) = 1;
            *page_word(entry + DG_GL_ENTRY_RESULT_BYTES) = 1;
            *page_word(entry + DG_GL_ENTRY_RESULT_TYPE) = DG_GL_RESULT_BOOL;
        }
        if (batch[0] == DG_GL_PRESENT) {
            *page_word(DG_GL_QUEUE_PRESENT_SLOT) = 2;
            *page_word(DG_GL_QUEUE_PRESENT_EPOCH_LO) = 3;
            *page_word(DG_GL_QUEUE_PRESENT_FRAME_LO) = 4;
        }
        ++m.consumed;
        *page_word(DG_GL_QUEUE_CONSUMED) = m.consumed;
        *page_word(DG_GL_QUEUE_COMPLETED) = m.consumed;
    }
}
static DWORD DREAMGPU_CDECL check_pages(void *, DWORD, DWORD count) {
    return count;
}
static DWORD DREAMGPU_CDECL pin_pages(void *, DWORD first, DWORD count, void **address) {
    ++m.pins;
    if (m.pins == m.pin_fail ||
        (m.pin_fail_first && first <= m.pin_fail_first && m.pin_fail_first < first + count)) {
        --m.pins;
        return 0;
    }
    if (first >= BULK_FIRST) {
        assert((first - BULK_FIRST + count) * 4096 <= sizeof(bulk_output));
        *address = bulk_output + (first - BULK_FIRST) * 4096;
    } else
        *address = first == 0x10 ? m.input : first == 0x20 ? m.output : m.returned;
    return 0xc0000000u + (first << 12);
}
static int DREAMGPU_CDECL copy_ptes(void *, DWORD first, DWORD count, DWORD *out) {
    const bool bulk = m.bulk_pages &&
                      first == static_cast<DWORD>(reinterpret_cast<uintptr_t>(m.bulk_pages) >> 12);
    for (DWORD i = 0; i < count; ++i)
        out[i] = bulk ? (bulk_frame(i) << 12 | (m.absent_pte == i + 1 ? 6 : 7)) : 7;
    return 1;
}
static void DREAMGPU_CDECL unlock_pages(void *, DWORD, DWORD) {
    ++m.unpins;
}
/* Fixed system pages: only the bulk buffer asks the channel for them. */
static void *DREAMGPU_CDECL allocate_user(void *, DWORD pages) {
    assert(pages == DG_GL_BULK_PAGES && !m.bulk_pages);
    if (++m.user_allocations == m.user_alloc_fail)
        return nullptr;
    m.bulk_pages = static_cast<BYTE *>(std::aligned_alloc(4096, pages * 4096));
    assert(m.bulk_pages);
    std::memset(m.bulk_pages, 0, pages * 4096);
    return m.bulk_pages;
}
static void DREAMGPU_CDECL free_user(void *, void *address) {
    assert(address && address == m.bulk_pages);
    ++m.user_frees;
    std::free(address);
    m.bulk_pages = nullptr;
}
static const DG9_MEMORY_SERVICES memory_services{check_pages,  pin_pages,     copy_ptes,
                                                 unlock_pages, allocate_user, free_user};
static volatile DWORD *DREAMGPU_CDECL registers() {
    return m.registers;
}
static DWORD DREAMGPU_CDECL read_reg(DWORD reg) {
    ++m.reads;
    return m.registers[reg / 4];
}
static void DREAMGPU_CDECL write_reg(DWORD reg, DWORD value) {
    ++m.writes;
    if (m.queue && reg == DG_GL_REG_QUEUE_DOORBELL) {
        doorbell();
        return;
    }
    if (m.queue && reg == DG_GL_REG_QUEUE_CONTROL) {
        const bool enable = value && value == m.registers[DG_REG_GENERATION / 4];
        m.registers[reg / 4] = enable;
        if (enable) {
            std::memset(page_word(0), 0, 64);
            *page_word(DG_GL_QUEUE_GENERATION) = value;
            *page_word(DG_GL_QUEUE_STATE) = DG_GL_QUEUE_ACTIVE;
            m.consumed = 0;
        }
        return;
    }
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
    /* Commands, results, then (queued transport) the page and 64 KiB buffers. */
    assert(pages == 16 || pages == 1);
    ++m.allocations;
    *physical = 0x40000 + m.allocations * 0x100000 + (m.allocations == m.bad_alignment ? 1 : 0);
    BYTE *memory = m.allocations == m.alloc_fail
                       ? nullptr
                       : static_cast<BYTE *>(std::aligned_alloc(4096, pages * 4096));
    if (memory) {
        std::memset(memory, 0, pages * 4096);
        assert(m.dma_count < 16);
        m.dma[m.dma_count++] = {*physical, memory, pages};
    }
    return memory;
}
static void DREAMGPU_CDECL free_dma(void *address) {
    assert(address);
    ++m.frees;
    std::free(address);
}
static DWORD DREAMGPU_CDECL submit(DWORD physical, DWORD bytes, DG_ESCAPE_REPLY *reply) {
    assert(physical == dg_gl_physical && bytes && bytes <= 65536 && m.held && !m.queue);
    ++m.submits;
    m.last_opcode = dg_gl_commands[0];
    m.last_operation = dg_gl_commands[8];
    if (m.last_opcode == DG_GL_DESKTOP) {
        assert(bytes == 96 && m.record_count < 64);
        std::memcpy(m.records[m.record_count++], dg_gl_commands, 96);
    }
    if (m.submit_status && (!m.fail_opcode || m.fail_opcode == m.last_opcode))
        return m.submit_status;
    reply->CompletedSequence = m.submits;
    if (m.last_opcode == DG_GL_QUERY &&
        (m.registers[DG_GL_REG_RESULT_CAPACITY / 4] & DG_GL_RESULT_PAGES)) {
        assert(!m.registers[DG_GL_REG_RESULT_ADDR_HI / 4]);
        write_bulk(m.registers[DG_GL_REG_RESULT_ADDR_LO / 4],
                   m.registers[DG_GL_REG_RESULT_CAPACITY / 4], m.bulk_bytes);
        m.registers[DG_GL_REG_RESULT_TYPE / 4] = DG_GL_RESULT_INT;
        m.registers[DG_GL_REG_RESULT_BYTES / 4] = m.bulk_bytes;
    } else if (m.last_opcode == DG_GL_QUERY) {
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
static int DREAMGPU_CDECL primary(DWORD *w, DWORD *h, DWORD *s, DWORD *o, DWORD *bpp) {
    if (!m.primary_ok)
        return 0;
    *w = m.width;
    *h = m.height;
    *s = m.stride;
    *o = 0;
    *bpp = m.bpp;
    return 1;
}
static void DREAMGPU_CDECL primary_owned(int owned) {
    m.primary_owned = owned;
}
static int DREAMGPU_CDECL queue_prepare() {
    std::abort(); /* the modelled device never leaves the driver waiting */
}
static int DREAMGPU_CDECL queue_wait() {
    std::abort();
}
static void DREAMGPU_CDECL queue_finish() {
    std::abort();
}
static void DREAMGPU_CDECL fatal(DWORD status) {
    /* A forked child proves the fault happened before any further record. */
    if (m.expect_fatal)
        _exit(status == DG_ESCAPE_STOPPED && dg_gl_faulted && m.submits == m.fatal_submits ? 42
                                                                                           : 43);
    std::abort();
}
static const DG9_CHANNEL_SERVICES services{
    registers,         read_reg,       write_reg,        critical,
    disable_irq,       restore_irq,    current_context,  create_semaphore,
    destroy_semaphore, wait_semaphore, signal_semaphore, allocate_dma,
    free_dma,          submit,         pending,          signal_wait,
    cursor_safe,       primary,        primary_owned,    fatal,
    nullptr,           nullptr,        nullptr,          &memory_services,
    queue_prepare,     queue_wait,     queue_finish};
static DG9_CHANNEL_CONTROL control(DWORD code, DWORD input, DWORD output) {
    return {code, 0, input, output, 0x10000, 0x20000, 0x30001, 11, 33, 0};
}
static void cleaned() {
    assert(m.pins == m.unpins && m.waits == m.signals && !m.held);
}
static DG_ESCAPE_REPLY request(DWORD operation, DWORD client = 0, DWORD bytes = 0,
                               DWORD capacity = 0, DWORD function = 0, DWORD flags = 0,
                               DWORD stream = 0) {
    DG_ESCAPE_REQUEST input{DG_ESCAPE_VERSION, operation, client,   bytes,
                            function,          flags,     capacity, stream};
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
    dg_gl_queue = {};
    dg_gl_queue_supported = dg_gl_build_queued = FALSE;
    dg_gl_build = nullptr;
    dg_gl_caps_valid = FALSE;
    dg_gl_initialized = dg_gl_faulted = 0;
    dg_gl_mutex = 0;
    dg_gl_commands = nullptr;
    dg_gl_result = nullptr;
    dg_gl_bulk = nullptr;
    dg_gl_bulk_list = nullptr;
    dg_gl_bulk_physical = 0;
    dg_gl_limits = {};
    dg_gl_function_cache = {};
    dg_gl_owners = {};
    std::memset(dg_windows, 0, sizeof(dg_windows));
    dg_window_next = 1;
    dg_desktop_active = 0;
    dg_desktop_width = dg_desktop_height = dg_desktop_stride = dg_desktop_bpp = 0;
    DreamGpuChannelBind(&services);
    m.registers[DG_REG_GENERATION / 4] = 1;
    m.registers[DG_REG_CAPS / 4] =
        DG_CAP_GL_TRANSPORT | DG_CAP_GL_PRESENT_BOUNDS | DG_CAP_GL_BULK_READBACK;
    m.registers[DG_GL_REG_VERSION / 4] = DG_GL_VERSION;
    m.registers[DG_GL_REG_MAX_BYTES / 4] = 65536;
    m.registers[DG_GL_REG_MAX_RECORDS / 4] = 1024;
}
/* Full 96-byte record identity: header, op, primary format word 16 and the
 * given record-relative words; every other word must be zero. */
static void expect_record(unsigned index, DWORD op, DWORD bpp, DWORD client, DWORD drawable,
                          std::initializer_list<std::pair<unsigned, DWORD>> words) {
    DWORD expected[24]{};
    expected[0] = DG_GL_DESKTOP;
    expected[1] = 96;
    expected[2] = client ? client : 0xffffffffu;
    expected[4] = drawable;
    expected[7] = m.registers[DG_REG_GENERATION / 4];
    expected[8] = op;
    expected[8 + DG_DESKTOP_PRIMARY_BPP / 4] = bpp == 16 ? 16 : 0;
    for (const auto &[word, value] : words)
        expected[8 + word] = value;
    assert(index < m.record_count && !std::memcmp(m.records[index], expected, 96));
}
static void expect_primary(unsigned index, DWORD op, DWORD bpp) {
    expect_record(index, op, bpp, 0, 0, {{4, m.width}, {5, m.height}, {11, m.stride}});
}
static void expect_blit(unsigned index, DWORD bpp, DWORD client) {
    expect_record(index, DG_DESKTOP_BLIT, bpp, client, 5,
                  {{2, 10}, {3, 20}, {4, 32}, {5, 32}, {10, 2}, {12, 3}, {14, 4}});
}
static void expect_discard(unsigned index, DWORD bpp, DWORD client) {
    expect_record(index, DG_DESKTOP_DISCARD, bpp, client, 5, {{10, 2}, {12, 3}, {14, 4}});
}
static void set_primary(DWORD bpp, DWORD stride) {
    m.bpp = bpp;
    m.stride = stride;
}
/* Every Win98 record type carries the primary format exactly like NT. */
static void primary_formats() {
    initialize();
    const DWORD client = request(DG_ESCAPE_OPEN).Client;
    const auto bound = window(DG9_WINDOW_BIND, client);
    assert(bound.Status == 0 && bound.Binding);
    DG9_CHANNEL_BLT blt{bound.Binding, 0, 10 | (20u << 16), 32 | (32u << 16)};
    for (const auto &[bpp, stride] :
         {std::pair<DWORD, DWORD>{32, 3200}, {16, 1600}, {16, 3200}, {32, 3200}}) {
        set_primary(bpp, stride);
        m.record_count = 0;
        assert(window(DG9_WINDOW_PREPARE, client, bound.Binding).Status == 0);
        assert(DreamGpuWindowBlt(&blt) == 1 && m.primary_owned);
        assert(DreamGpuWindowBlt(&blt) == 1 && dg_desktop_bpp == bpp);
        cleaned();
        DreamGpuPrimaryAccess();
        assert(!m.primary_owned && !dg_desktop_active);
        cleaned();
        assert(window(DG9_WINDOW_FINISH, client, bound.Binding).Status == 0);
        assert(m.record_count == 6);
        expect_primary(0, DG_DESKTOP_SEED, bpp);
        expect_blit(1, bpp, client);
        expect_blit(2, bpp, client);
        expect_primary(3, DG_DESKTOP_READBACK, bpp);
        expect_primary(4, DG_DESKTOP_RETURN, bpp);
        expect_discard(5, bpp, client);
    }
    /* XRGB8888 records stay byte-identical to the original layout: header,
     * SEED, zero flags/origin, 800x600, zero format word 16, stride 3200. */
    set_primary(32, 3200);
    m.record_count = 0;
    assert(window(DG9_WINDOW_PREPARE, client, bound.Binding).Status == 0);
    assert(DreamGpuWindowBlt(&blt) == 1);
    const DWORD seed32[24] = {
        DG_GL_DESKTOP, 96,  0xffffffffu, 0, 0, 0, 0, 1,   DG_DESKTOP_SEED, 0, 0, 0,
        800,           600, 0,           0, 0, 0, 0, 3200};
    assert(!std::memcmp(m.records[0], seed32, sizeof(seed32)));
    DreamGpuPrimaryAccess();
    /* DISCARD names the live mode even after the last desktop was 32 bpp. */
    set_primary(16, 1600);
    m.record_count = 0;
    assert(window(DG9_WINDOW_FINISH, client, bound.Binding).Status == 0);
    assert(m.record_count == 1);
    expect_discard(0, 16, client);
    assert(window(DG9_WINDOW_PREPARE, client, bound.Binding).Status == 0);
    m.record_count = 0;
    assert(window(DG9_WINDOW_RELEASE, client, bound.Binding).Status == 0 && !dg_windows[0].Token);
    assert(m.record_count == 1);
    expect_discard(0, 16, client);
    /* Unsupported live depth: bind/present refuse without any record. */
    const auto second = window(DG9_WINDOW_BIND, client);
    assert(second.Status == 0 && second.Binding);
    m.primary_ok = false;
    m.record_count = 0;
    const unsigned before = m.submits;
    assert(window(DG9_WINDOW_BIND, client).Status == DG_ESCAPE_UNSUPPORTED);
    assert(window(DG9_WINDOW_PREPARE, client, second.Binding).Status == DG_ESCAPE_UNSUPPORTED);
    blt.Client_EDI = second.Binding;
    assert(!DreamGpuWindowBlt(&blt) && m.submits == before && !m.record_count);
    cleaned();
    /* Client rundown discards with the live format, then zero if unsupported. */
    m.primary_ok = true;
    assert(window(DG9_WINDOW_PREPARE, client, second.Binding).Status == 0);
    const DWORD other = request(DG_ESCAPE_OPEN).Client;
    const auto third = window(DG9_WINDOW_BIND, other);
    assert(window(DG9_WINDOW_PREPARE, other, third.Binding).Status == 0);
    m.record_count = 0;
    assert(request(DG_ESCAPE_CLOSE, client).Status == 0);
    assert(m.record_count == 1 && m.last_opcode == DG_GL_CLOSE_CLIENT);
    expect_discard(0, 16, client);
    m.primary_ok = false;
    m.record_count = 0;
    assert(request(DG_ESCAPE_CLOSE, other).Status == 0);
    assert(m.record_count == 1);
    expect_discard(0, 0, other);
    DreamGpuGlShutdown();
    assert(m.allocations == m.frees && m.sem_created == m.sem_destroyed);
}
/* A primary format change while GPU-owned is treated like a geometry change:
 * the device is stopped before any record names the new format. */
static void format_fault(DWORD from, DWORD from_stride, DWORD to, DWORD to_stride) {
    initialize();
    set_primary(from, from_stride);
    const DWORD client = request(DG_ESCAPE_OPEN).Client;
    const auto bound = window(DG9_WINDOW_BIND, client);
    assert(window(DG9_WINDOW_PREPARE, client, bound.Binding).Status == 0);
    DG9_CHANNEL_BLT blt{bound.Binding, 0, 10 | (20u << 16), 32 | (32u << 16)};
    assert(DreamGpuWindowBlt(&blt) == 1 && dg_desktop_active && dg_desktop_bpp == from);
    cleaned();
    std::fflush(nullptr);
    const pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        set_primary(to, to_stride);
        m.expect_fatal = true;
        m.fatal_submits = m.submits;
        DreamGpuWindowBlt(&blt);
        _exit(1);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 42);
    DreamGpuGlShutdown();
}
/* Queued transport: OPEN negotiates it, an asynchronous submission costs one
 * doorbell write and no register read, a later failure is reported once to
 * the client as DEFERRED (the reporting request does not run), queries and
 * presents read their outcome from the page, and cleanup frees everything. */
static void queued() {
    initialize();
    m.queue = true;
    m.registers[DG_GL_REG_QUEUE_VERSION / 4] = DG_GL_QUEUE_VERSION;
    m.registers[DG_GL_REG_MAX_BYTES / 4] = DG_GL_MAX_BYTES;
    m.registers[DG_GL_REG_MAX_RECORDS / 4] = DG_GL_MAX_RECORDS;
    m.registers[DG_GL_REG_FUNCTION_WORDS / 4] = 2;
    const auto opened = request(DG_ESCAPE_OPEN);
    assert(opened.Status == 0 && opened.DeviceError == DG_ESCAPE_FEATURE_QUEUE);
    /* Regression (Win98 stale pixels): a negotiated packet must be pinnable.
     * 128 KiB packets exceeded DG9_MEMORY_MAX_BYTES, so every large packet
     * (uploads, DrawPixels, LFB writes) failed to pin and its context died. */
    assert(opened.MaxBytes == DG_ESCAPE_MAX_BYTES &&
           opened.MaxRecords == DG_ESCAPE_MAX_BYTES / DG_GL_HEADER_BYTES);
    assert(sizeof(DG_ESCAPE_REQUEST) + opened.MaxBytes <= DG9_MEMORY_MAX_BYTES);
    assert(dg_gl_queue_supported && dg_gl_queue.BufferCount == DG_GL_QUEUE_MAX_BUFFERS);
    const DWORD client = opened.Client;
    DWORD command[12] = {DG_GL_CALL, 44, 0, 0, 0, 0, 0, 1, 0x77, 0, 0, 0};
    std::memcpy(m.input + 32, command, 44);
    assert(request(DG_ESCAPE_SUBMIT, client, 44, 0, 0, DG_ESCAPE_SUBMIT_ASYNC, 4).Status == 0);
    for (unsigned i = 0; i < 40; ++i) {
        const unsigned reads = m.reads, writes = m.writes, doorbells = m.doorbells;
        const auto reply = request(DG_ESCAPE_SUBMIT, client, 44, 0, 0, DG_ESCAPE_SUBMIT_ASYNC, 4);
        assert(reply.Status == 0 && !reply.CompletedSequence);
        assert(m.reads == reads && m.writes == writes + 1 && m.doorbells == doorbells + 1);
        assert(m.last_client == client && m.last_stream == 4);
    }
    /* A maximal negotiated packet pins through the real memory core and is
     * queued intact; one word more is a malformed envelope. */
    {
        m.registers[DG_GL_REG_FUNCTION_WORDS / 4] = DG_GL_FUNCTION_INLINE_DATA;
        Dg9FunctionCacheRefresh(&dg_gl_function_cache, 1, 1);
        const DWORD bytes = opened.MaxBytes;
        DWORD header[10] = {DG_GL_DATA_CALL, bytes, 0, 0, 0, 0, 0, 1, 0x66, bytes - 40};
        std::memset(m.input + 32, 0, bytes);
        std::memcpy(m.input + 32, header, sizeof(header));
        const unsigned doorbells = m.doorbells;
        assert(request(DG_ESCAPE_SUBMIT, client, bytes, 0, 0, DG_ESCAPE_SUBMIT_ASYNC, 4).Status ==
               0);
        assert(m.doorbells == doorbells + 1 && m.last_opcode == DG_GL_DATA_CALL &&
               m.last_operation == 0x66);
        DG_ESCAPE_REQUEST input{DG_ESCAPE_VERSION,
                                DG_ESCAPE_SUBMIT,
                                client,
                                bytes + 4,
                                0,
                                DG_ESCAPE_SUBMIT_ASYNC,
                                0,
                                4};
        std::memcpy(m.input, &input, sizeof(input));
        auto io = control(DG_ESCAPE, sizeof(input) + bytes + 4, sizeof(DG_ESCAPE_REPLY));
        DWORD status = 0;
        assert(DreamGpuGlControl(&io, &status) && status == 87 && m.doorbells == doorbells + 1);
        cleaned();
        m.registers[DG_GL_REG_FUNCTION_WORDS / 4] = 2;
        Dg9FunctionCacheRefresh(&dg_gl_function_cache, 1, 1);
    }
    /* Stream 4 fails after its request returned. Stream 5's next request is
     * not executed; it reports the failure instead, once. */
    command[8] = 0x55;
    std::memcpy(m.input + 32, command, 44);
    m.queue_fail_function = 0x55;
    assert(request(DG_ESCAPE_SUBMIT, client, 44, 0, 0, DG_ESCAPE_SUBMIT_ASYNC, 4).Status == 0);
    command[8] = 0x77;
    std::memcpy(m.input + 32, command, 44);
    unsigned submits = m.submits;
    auto deferred = request(DG_ESCAPE_SUBMIT, client, 44, 0, 0, DG_ESCAPE_SUBMIT_ASYNC, 5);
    assert(deferred.Status == DG_ESCAPE_DEFERRED && deferred.FunctionWords == 4 &&
           deferred.DeviceError == DG_GL_ERROR_HOST && deferred.CompletedSequence &&
           m.submits == submits);
    assert(request(DG_ESCAPE_SUBMIT, client, 44, 0, 0, DG_ESCAPE_SUBMIT_ASYNC, 5).Status == 0 &&
           m.submits == submits + 1);
    /* A synchronous stream request reports its own failure as before. */
    command[8] = 0x55;
    std::memcpy(m.input + 32, command, 44);
    const auto failed = request(DG_ESCAPE_SUBMIT, client, 44, 0, 0, 0, 6);
    assert(failed.Status == DG_ESCAPE_HOST && failed.DeviceError == DG_GL_ERROR_HOST &&
           failed.CompletedSequence);
    m.queue_fail_function = 0;
    /* Query result through the entry and the driver's result page. */
    m.registers[DG_GL_REG_FUNCTION_WORDS / 4] = DG_GL_FUNCTION_QUERY;
    Dg9FunctionCacheRefresh(&dg_gl_function_cache, 1, 1);
    DWORD query[12] = {DG_GL_QUERY, 48, 0, 0, 0, 0, 0, 1, 0x88, 0, 0, 0};
    std::memcpy(m.input + 32, query, 48);
    const auto result = request(DG_ESCAPE_SUBMIT, client, 48, 1, 0, 0, 5);
    assert(result.Status == 0 && result.ResultBytes == 1 &&
           result.ResultType == DG_GL_RESULT_BOOL && m.output[sizeof(result)] == 1);
    /* An asynchronous query is a malformed envelope, rejected before any work. */
    {
        DG_ESCAPE_REQUEST input{
            DG_ESCAPE_VERSION, DG_ESCAPE_SUBMIT, client, 48, 0, DG_ESCAPE_SUBMIT_ASYNC, 1, 5};
        std::memcpy(m.input, &input, sizeof(input));
        auto io = control(DG_ESCAPE, sizeof(input) + 48, sizeof(DG_ESCAPE_REPLY) + 1);
        DWORD status = 0;
        submits = m.submits;
        assert(DreamGpuGlControl(&io, &status) && status == 87 && m.submits == submits);
        cleaned();
    }
    /* PRESENT identity comes from the page, not the registers. */
    const auto bound = window(DG9_WINDOW_BIND, client);
    assert(window(DG9_WINDOW_PREPARE, client, bound.Binding).Status == 0);
    assert(dg_windows[0].Slot == 2 && dg_windows[0].EpochLow == 3 && dg_windows[0].FrameLow == 4);
    m.record_count = 0;
    assert(request(DG_ESCAPE_CLOSE, client).Status == 0 && m.last_opcode == DG_GL_CLOSE_CLIENT);
    DreamGpuGlShutdown();
    assert(m.allocations == m.frees && m.sem_created == m.sem_destroyed);
    m.queue = false;
}
/* One QUERY whose result goes to the caller's output at linear page
 * BULK_FIRST plus `offset`; the reply header is copied out of that buffer. */
static DG_ESCAPE_REPLY bulk_query(DWORD client, DWORD capacity, DWORD offset, DWORD *status,
                                  DWORD stream = 0, DWORD out_bytes = 0) {
    const DWORD query[12] = {DG_GL_QUERY, 48, 0, 0, 0, 0, 0, 1, 0x88, 0, 0, 0};
    std::memcpy(m.input + 32, query, 48);
    DG_ESCAPE_REQUEST input{
        DG_ESCAPE_VERSION, DG_ESCAPE_SUBMIT, client, 48, 0, 0, capacity, stream};
    std::memcpy(m.input, &input, sizeof(input));
    std::memset(bulk_output, 0xa5, sizeof(bulk_output));
    std::memset(m.returned, 0xa5, sizeof(m.returned));
    auto io = control(DG_ESCAPE, sizeof(input) + 48,
                      out_bytes ? out_bytes : sizeof(DG_ESCAPE_REPLY) + capacity);
    io.lpOutBuffer = (BULK_FIRST << 12) + offset;
    *status = 999;
    assert(DreamGpuGlControl(&io, status));
    cleaned();
    DG_ESCAPE_REPLY reply;
    std::memcpy(&reply, bulk_output + offset, sizeof(reply));
    return reply;
}
static DWORD returned_count() {
    DWORD count;
    std::memcpy(&count, m.returned + 1, sizeof(count));
    return count;
}
/* A successful bulk result: exactly its bytes behind the header, through the
 * scattered frame list, and nothing written before or after them. */
static void expect_bulk(DWORD client, DWORD bytes, DWORD offset, DWORD stream = 0) {
    m.bulk_bytes = bytes;
    m.bulk_seed += 17;
    m.bulk_list = m.bulk_word = 0;
    DWORD status;
    const auto reply = bulk_query(client, bytes, offset, &status, stream);
    assert(status == 0 && reply.Status == DG_ESCAPE_OK && reply.ResultBytes == bytes &&
           reply.ResultType == DG_GL_RESULT_INT && returned_count() == sizeof(reply) + bytes);
    assert(m.bulk_list == dg_gl_bulk_physical && m.bulk_word == (bytes | DG_GL_RESULT_PAGES));
    const BYTE *result = bulk_output + offset + sizeof(reply);
    for (DWORD at = 0; at < bytes; ++at)
        assert(result[at] == bulk_byte(m.bulk_seed, at));
    for (DWORD at = 0; at < offset; ++at)
        assert(bulk_output[at] == 0xa5);
    for (DWORD at = offset + sizeof(reply) + bytes; at < sizeof(bulk_output); ++at)
        assert(bulk_output[at] == 0xa5);
}
static void enable_bulk_host() {
    m.registers[DG_GL_REG_CAPS2 / 4] = DG_CAP2_GL_DEPTH_TEXTURE | DG_CAP2_GL_BULK_READ;
    m.registers[DG_GL_REG_FUNCTION_WORDS / 4] = DG_GL_FUNCTION_QUERY;
}
/* Bulk results (gl.h DG_CAP2_GL_BULK_READ): OPEN offers 4 MiB only with the
 * capability and the driver's buffer, results larger than the contiguous
 * buffer reach the caller exactly through the frame list and pinned output
 * windows, and everything else is as before. */
static void bulk() {
    assert(DgGlResultLimit(0, 0, 0) == 512 && DgGlResultLimit(DG_CAP_GL_TRANSPORT, 0, 1) == 512 &&
           DgGlResultLimit(DG_CAP_GL_BULK_READBACK, 0, 1) == 65536 &&
           DgGlResultLimit(DG_CAP_GL_BULK_READBACK, DG_CAP2_GL_BULK_READ, 0) == 65536 &&
           DgGlResultLimit(0, DG_CAP2_GL_BULK_READ, 1) == DG_ESCAPE_BULK_RESULT_BYTES);
    /* Without the capability nothing is allocated; failed allocations leave
     * the contiguous limit and free what they took; a later OPEN retries. */
    initialize();
    m.registers[DG_GL_REG_CAPS2 / 4] = DG_CAP2_GL_DEPTH_TEXTURE;
    assert(request(DG_ESCAPE_OPEN).MaxResultBytes == 65536 && !m.user_allocations && !dg_gl_bulk &&
           m.allocations == 2);
    DreamGpuGlShutdown();
    for (unsigned mode = 0; mode < 3; ++mode) {
        initialize();
        enable_bulk_host();
        if (mode == 0)
            m.alloc_fail = 3; /* the list page, after commands and results */
        else if (mode == 1)
            m.user_alloc_fail = 1;
        else
            m.absent_pte = 300;
        assert(request(DG_ESCAPE_OPEN).MaxResultBytes == 65536 && !dg_gl_bulk);
        assert(m.allocations == 3 && m.frees == (mode ? 1u : 0u) &&
               m.user_allocations == (mode ? 1u : 0u) && m.user_frees == (mode == 2 ? 1u : 0u));
        m.alloc_fail = m.user_alloc_fail = m.absent_pte = 0;
        const auto reopened = request(DG_ESCAPE_OPEN);
        assert(reopened.MaxResultBytes == DG_ESCAPE_BULK_RESULT_BYTES && dg_gl_bulk);
        DreamGpuGlShutdown();
        /* The model counts a failed allocation, which has nothing to free. */
        assert(m.allocations == m.frees + (mode == 0) &&
               m.user_allocations == m.user_frees + (mode == 1) && !dg_gl_bulk);
    }
    /* Register transport. */
    initialize();
    enable_bulk_host();
    const auto opened = request(DG_ESCAPE_OPEN);
    assert(opened.Status == 0 && opened.MaxResultBytes == DG_ESCAPE_BULK_RESULT_BYTES);
    assert(dg_gl_bulk == m.bulk_pages && dg_gl_bulk_physical && !(dg_gl_bulk_physical & 4095));
    for (DWORD i = 0; i < DG_GL_BULK_PAGES; ++i)
        assert(dg_gl_bulk_list[i] == bulk_frame(i));
    const DWORD client = opened.Client;
    Dg9FunctionCacheRefresh(&dg_gl_function_cache, 1, 1);
    for (const DWORD bytes : {65536u + 4, 3 * 65536u + 12, 1024u * 768 * 4, 4096u * 100 + 4,
                              static_cast<DWORD>(DG_ESCAPE_BULK_RESULT_BYTES)})
        expect_bulk(client, bytes, 0);
    /* An unaligned caller buffer, and a result whose head ends mid-page. */
    expect_bulk(client, 1024 * 768 * 4, 0x10);
    expect_bulk(client, 65536 + 4, 4093);
    /* Contiguous results keep their buffer, plain capacity and cleared tail. */
    std::memcpy(m.input + 32, (const DWORD[12]){DG_GL_QUERY, 48, 0, 0, 0, 0, 0, 1, 0x88, 0, 0, 0},
                48);
    for (const DWORD capacity : {1u, 65536u}) {
        dg_gl_result[capacity - 1] = 0x5a;
        const auto small = request(DG_ESCAPE_SUBMIT, client, 48, capacity);
        assert(small.Status == 0 && small.ResultBytes == 1 && m.output[sizeof(small)] == 1);
        assert(m.registers[DG_GL_REG_RESULT_ADDR_LO / 4] == dg_gl_result_physical &&
               m.registers[DG_GL_REG_RESULT_CAPACITY / 4] == capacity &&
               dg_gl_result[capacity - 1] == (capacity == 1));
    }
    /* An output window that cannot be pinned fails the request after the
     * query ran: no result bytes are claimed, nothing is left pinned. */
    {
        m.bulk_bytes = 1024 * 768 * 4;
        m.pin_fail_first = BULK_FIRST + 40;
        const unsigned submits = m.submits;
        DWORD status;
        const auto failed = bulk_query(client, m.bulk_bytes, 0, &status);
        assert(status == 0 && failed.Status == DG_ESCAPE_INVALID && !failed.ResultBytes &&
               !failed.ResultType && returned_count() == sizeof(failed) &&
               m.submits == submits + 1);
        m.pin_fail_first = 0;
    }
    /* Bounds: a capacity or output beyond the bulk size is a malformed
     * envelope; neither runs. */
    {
        const unsigned submits = m.submits;
        DWORD status;
        bulk_query(client, DG_ESCAPE_BULK_RESULT_BYTES + 4, 0, &status);
        assert(status == 87 && m.submits == submits);
        bulk_query(client, DG_ESCAPE_BULK_RESULT_BYTES + 4, 0, &status, 0,
                   sizeof(DG_ESCAPE_REPLY) + DG_ESCAPE_BULK_RESULT_BYTES);
        assert(status == 87 && m.submits == submits && bulk_output[0] == 0xa5);
        bulk_query(client, 65536 + 4, 0, &status, 0, sizeof(DG_ESCAPE_REPLY) + 65536);
        assert(status == 87 && m.submits == submits);
        /* A desktop restored on a host without the capability: the next
         * OPEN rereads CAPS2, and every client gets the contiguous limit. */
        m.registers[DG_GL_REG_CAPS2 / 4] = DG_CAP2_GL_DEPTH_TEXTURE;
        assert(request(DG_ESCAPE_OPEN).MaxResultBytes == 65536);
        const auto refused = bulk_query(client, 65536 + 4, 0, &status);
        assert(status == 0 && refused.Status == DG_ESCAPE_INVALID &&
               refused.MaxResultBytes == 65536 && m.submits == submits);
        enable_bulk_host();
    }
    assert(request(DG_ESCAPE_CLOSE, client).Status == 0);
    DreamGpuGlShutdown();
    assert(m.allocations == m.frees && m.user_allocations == m.user_frees && !dg_gl_bulk &&
           !dg_gl_bulk_list && m.sem_created == m.sem_destroyed);
    /* Queued transport: the entry names the list and the capacity word. */
    initialize();
    m.queue = true;
    m.registers[DG_GL_REG_QUEUE_VERSION / 4] = DG_GL_QUEUE_VERSION;
    m.registers[DG_GL_REG_MAX_BYTES / 4] = DG_GL_MAX_BYTES;
    m.registers[DG_GL_REG_MAX_RECORDS / 4] = DG_GL_MAX_RECORDS;
    enable_bulk_host();
    const auto queued_open = request(DG_ESCAPE_OPEN);
    assert(queued_open.DeviceError == DG_ESCAPE_FEATURE_QUEUE &&
           queued_open.MaxResultBytes == DG_ESCAPE_BULK_RESULT_BYTES);
    Dg9FunctionCacheRefresh(&dg_gl_function_cache, 1, 1);
    for (const DWORD bytes : {65536u + 4, 1024u * 768 * 4, 3 * 65536u + 12,
                              static_cast<DWORD>(DG_ESCAPE_BULK_RESULT_BYTES)}) {
        const unsigned doorbells = m.doorbells;
        expect_bulk(queued_open.Client, bytes, 0, 5);
        assert(m.doorbells == doorbells + 1);
    }
    assert(request(DG_ESCAPE_CLOSE, queued_open.Client).Status == 0);
    DreamGpuGlShutdown();
    assert(m.allocations == m.frees && m.user_allocations == m.user_frees);
    m.queue = false;
}
int main() {
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
    /* A host without the second capability word reads zero there: DeviceError
     * stays zero, as older drivers left it. */
    assert(!legacy_caps.DeviceError);
    m.registers[DG_REG_CAPS / 4] |= DG_CAP_GL_DEPTH_STENCIL_READBACK;
    assert(request(DG_ESCAPE_CAPABILITIES, client).FunctionWords &
           DG_CAP_GL_DEPTH_STENCIL_READBACK);
    /* CAPS2 travels unchanged in DeviceError; every request rereads it with
     * CAPS, and a host without the GL transport reports neither. */
    m.registers[DG_GL_REG_CAPS2 / 4] = DG_CAP2_GL_DEPTH_TEXTURE | 0x80000000u;
    {
        const auto caps = request(DG_ESCAPE_CAPABILITIES, client);
        assert(caps.Status == DG_ESCAPE_OK &&
               caps.DeviceError == (DG_CAP2_GL_DEPTH_TEXTURE | 0x80000000u) &&
               (caps.FunctionWords & DG_CAP_GL_DEPTH_STENCIL_READBACK) && !caps.ResultBytes);
    }
    m.registers[DG_GL_REG_CAPS2 / 4] = 0;
    assert(request(DG_ESCAPE_CAPABILITIES, client).DeviceError == 0);
    m.registers[DG_GL_REG_CAPS2 / 4] = DG_CAP2_GL_DEPTH_TEXTURE;
    assert(request(DG_ESCAPE_CAPABILITIES, client).DeviceError == DG_CAP2_GL_DEPTH_TEXTURE);
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
    primary_formats();
    /* Same width/height/stride: only the pixel format changes. */
    format_fault(32, 3200, 16, 3200);
    format_fault(16, 3200, 32, 3200);
    format_fault(16, 1600, 32, 3200);
    queued();
    bulk();
    std::puts("PASS actual C++ Win98 channel/window: pin/allocation failure ownership, OS "
              "identity, privileged packet rejection, typed result identity across cleanup, "
              "retained-frame lifecycle, GDI clips/coherence, timeout and waiter-safe teardown, "
              "XRGB8888/RGB565 format word on SEED/BLIT/READBACK/RETURN/DISCARD, "
              "format-change fault, queued transport doorbells and deferred failures, bulk results "
              "through scattered frames and pinned output windows");
}
