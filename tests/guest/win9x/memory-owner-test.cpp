/* SPDX-License-Identifier: GPL-2.0-or-later
 * Execute the production memory core against a bounded VMM service model.
 */
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <utility>
using DWORD = uint32_t;
#include "../../../guest/win9x/dg-memory.h"
#include "../../../guest/include/ownership.hpp"
namespace {
constexpr DWORD input_address = 0x10ffe, output_address = 0x12001, returned_address = 0x14003;
struct model {
    std::array<unsigned char, 32768> memory{};
    std::array<unsigned char, 69632> staging{};
    std::array<DWORD, 3> live{};
    std::array<DWORD, 3> released{};
    unsigned calls = 0, fail = 0, pins = 0, unpins = 0, allocations = 0, frees = 0;
    bool readonly = false, absent = false, bad_alias = false, exit_on_allocate = false;
    bool process_exited = false;
    bool succeeds() {
        return ++calls != fail;
    }
};
DWORD DREAMGPU_CDECL check(void *opaque, DWORD, DWORD pages) {
    auto &m = *static_cast<model *>(opaque);
    return m.succeeds() ? pages : 0;
}
DWORD DREAMGPU_CDECL pin(void *opaque, DWORD first, DWORD, void **address) {
    auto &m = *static_cast<model *>(opaque);
    if (!m.succeeds())
        return 0;
    assert(first >= 0x10 && first < 0x18 && m.pins < 3);
    *address = m.memory.data() + (first - 0x10) * 4096;
    const DWORD alias = 0xc0000000u + first * 4096 + (m.bad_alias ? 1 : 0);
    m.live[m.pins++] = alias;
    return alias;
}
int DREAMGPU_CDECL ptes(void *opaque, DWORD first, DWORD pages, DWORD *out) {
    auto &m = *static_cast<model *>(opaque);
    if (!m.succeeds())
        return 0;
    for (DWORD i = 0; i < pages; ++i)
        out[i] = ((first + i) << 12) | (m.readonly ? 5 : 7);
    if (m.absent)
        out[pages - 1] &= ~1u;
    return 1;
}
void DREAMGPU_CDECL unpin(void *opaque, DWORD alias, DWORD pages) {
    auto &m = *static_cast<model *>(opaque);
    assert(pages && pages <= DG9_MEMORY_MAX_PAGES);
    bool found = false;
    for (auto &entry : m.live)
        if (entry == alias) {
            entry = 0;
            found = true;
            break;
        }
    assert(found && m.unpins < 3);
    m.released[m.unpins++] = alias;
}
void *DREAMGPU_CDECL allocate(void *opaque, DWORD pages) {
    auto &m = *static_cast<model *>(opaque);
    assert(pages && pages * 4096 <= m.staging.size());
    if (!m.succeeds())
        return nullptr;
    ++m.allocations;
    /* Process teardown can remove original mappings; the retained aliases
     * remain valid until unlock. Nothing in the core reuses user addresses. */
    if (m.exit_on_allocate)
        m.process_exited = true;
    return m.staging.data();
}
void DREAMGPU_CDECL free_pages(void *opaque, void *address) {
    auto &m = *static_cast<model *>(opaque);
    assert(address == m.staging.data() && m.frees < m.allocations);
    ++m.frees;
}
const DG9_MEMORY_SERVICES services{check, pin, ptes, unpin, allocate, free_pages};
void clean(const model &m) {
    assert(m.pins == m.unpins && m.allocations == m.frees);
    for (auto alias : m.live)
        assert(alias == 0);
}
void initialize(model &m) {
    m.memory.fill(0xa5);
    for (unsigned i = 0; i < 8192; ++i)
        m.memory[input_address - 0x10000 + i] = i & 255;
}
struct count_deleter {
    unsigned *calls;
    void operator()(unsigned *) noexcept {
        ++*calls;
    }
};
} // namespace
int main() {
    /* Prove the actual owner primitive used by the transaction cannot double
     * release when moved, assigned, reset or transferred back across C ABI. */
    unsigned calls = 0, value = 1;
    {
        dreamgpu::unique_owner<unsigned, count_deleter> a(&value, {&calls});
        auto b = std::move(a);
        assert(!a && b);
        a = std::move(b);
        assert(a && !b);
    }
    assert(calls == 1);
    for (unsigned failure = 1; failure <= 10; ++failure) {
        model m;
        initialize(m);
        m.fail = failure;
        const auto before = m.memory;
        const auto status =
            DreamGpuCopyUser(&services, &m, input_address, output_address, returned_address, 100);
        assert(status == (failure == 10 ? 8 : 87));
        assert(m.memory == before);
        clean(m);
    }
    for (unsigned mode = 0; mode < 4; ++mode) {
        model m;
        initialize(m);
        m.exit_on_allocate = true;
        DWORD output = mode == 0 ? output_address : input_address + (mode == 1 ? 1 : 0);
        DWORD returned = mode == 3 ? output : returned_address;
        std::array<unsigned char, 100> expected{};
        std::memcpy(expected.data(), m.memory.data() + input_address - 0x10000, expected.size());
        assert(DreamGpuCopyUser(&services, &m, input_address, output, returned, expected.size()) ==
               0);
        if (returned == output) {
            const DWORD count = expected.size();
            std::memcpy(expected.data(), &count, 4);
        }
        assert(std::memcmp(expected.data(), m.memory.data() + output - 0x10000, expected.size()) ==
               0);
        DWORD count;
        std::memcpy(&count, m.memory.data() + returned - 0x10000, 4);
        assert(count == expected.size() && m.process_exited);
        assert(m.released[0] == 0xc0000000u + (returned >> 12) * 4096);
        clean(m);
    }
    for (unsigned mode = 0; mode < 3; ++mode) {
        model m;
        initialize(m);
        m.readonly = mode == 0;
        m.absent = mode == 1;
        m.bad_alias = mode == 2;
        const auto before = m.memory;
        assert(DreamGpuCopyUser(&services, &m, input_address, output_address, returned_address,
                                100) == 87);
        assert(m.memory == before);
        clean(m);
    }
    model m;
    initialize(m);
    assert(DreamGpuCopyUser(&services, &m, input_address, output_address, returned_address, 0) ==
           87);
    assert(DreamGpuCopyUser(&services, &m, input_address, output_address, returned_address,
                            DG9_MEMORY_MAX_BYTES + 1) == 87);
    assert(m.calls == 0);
    clean(m);
    assert(!Dg9UserSpan(0x10000, 1, nullptr));
    assert(!Dg9UserPtes(nullptr, 1, 0));
    puts("Win9x C++ memory owners: all 10 failure stages, overlap, unaligned returns, permission "
         "rejection and process teardown pass");
}
