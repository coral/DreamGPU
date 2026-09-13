// SPDX-License-Identifier: GPL-2.0-or-later
#define DG_SETUP_ADAPTER_TEST
#include "win32-lifecycle.h"
#include "plan.h"
#include <cassert>
using namespace setup::lifecycle;
constexpr const char *Owner = "C:\\WINDOWS\\DreamGPU";
constexpr const char *JournalPath = "C:\\WINDOWS\\DreamGPU\\lifecycle.bin";
static std::vector<BYTE> encoded(Journal j, bool legacy) {
    j.version = legacy ? 1 : 2;
    const size_t size = legacy ? 20568 : sizeof(j);
    const auto *data = reinterpret_cast<const BYTE *>(&j);
    std::vector<BYTE> result(data, data + size);
    setup::Sha256 sha;
    sha.update(data, size);
    char hex[65];
    sha.finish(hex);
    result.insert(result.end(), hex, hex + 64);
    return result;
}
static Journal captured() {
    fake_win32::reset();
    fake_win32::files[fake_win32::canon(Owner)].directory = true;
    Win32Store store;
    assert(store.init(Owner));
    Journal j;
    j.generation = 1;
    j.count = 1;
    strcpy(j.items[0].path, "glide2x.dll");
    j.items[0].desired = string_value("x");
    j.items[0].desired.type = 0;
    j.items[0].resource = 100;
    assert(store.create_generation(1) && store.capture(j, 0));
    return j;
}
int main() {
    Journal j = captured();
    auto legacy = encoded(j, true), modern = encoded(j, false);
    auto &file = fake_win32::files[fake_win32::canon(JournalPath)];
    file.bytes = legacy;
    Win32Store store;
    assert(store.init(Owner));
    Journal loaded;
    assert(store.load(loaded) && loaded.version == 2 && loaded.count == 1);
    assert(loaded.items[0].original_generation == j.items[0].original_generation);
    assert(store.persist(loaded));
    assert(file.bytes.size() == legacy.size() + modern.size());
    const auto mixed = file.bytes;
    // Both versions retain exact checksums; incomplete v2 tails can be trimmed
    // only after the complete legacy/v2 prefix was validated.
    for (size_t tail : {size_t(1), size_t(7), size_t(8), size_t(127), modern.size() - 1}) {
        file.bytes = mixed;
        file.bytes.insert(file.bytes.end(), modern.begin(), modern.begin() + tail);
        Win32Store reader;
        assert(reader.init(Owner) && reader.load(loaded));
        assert(file.bytes == mixed);
    }
    file.bytes = legacy;
    file.bytes.back() ^= 1;
    Win32Store corrupt;
    assert(corrupt.init(Owner) && !corrupt.load(loaded));
    j.count = 17;
    file.bytes = encoded(j, true);
    Win32Store oversized;
    assert(oversized.init(Owner) && !oversized.load(loaded));
    j = captured();
    j.count = 0;
    const char *names[] = {"dgpugl.dll", "glide2x.dll", "wined3d.dll", "winedd.dll", "wined8.dll",
                           "wined9.dll", "ddraw.dll",   "d3d8.dll",    "d3d9.dll",   "ddsys.dll",
                           "msd3d8.dll", "msd3d9.dll",  "dgpuicd.dll"};
    for (const auto *name : names) {
        auto &i = j.items[j.count++];
        i = {};
        strcpy(i.path, name);
        i.desired = number_value(0);
        i.desired.type = 0;
        i.resource = 100;
    }
    assert(append_icd_registration(setup::Os::nt5, j, true) && j.count == 18);
    assert(j.items[17].desired.size == 4 && j.items[17].desired.value[0] == 1);
    Win32Store full;
    assert(full.init(Owner));
    for (unsigned n = 0; n < j.count; n++)
        assert(full.capture(j, n));
    assert(valid(j) && full.persist(j));
    Win32Store reload;
    assert(reload.init(Owner) && reload.load(loaded) && loaded.count == 18);
    puts("PASS actual journal codec: legacy preservation, mixed v1/v2 appends, incomplete-tail "
         "recovery, 18-item system plan");
}
