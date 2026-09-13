// SPDX-License-Identifier: GPL-2.0-or-later
#define DG_SETUP_ADAPTER_TEST
#define main BoundLegacyMain
#include "driver-tests.cpp"
#undef main
#include "driver-lineage-store.h"
int main() {
    setup_case(Os::nt5, false);
    for (const auto &p : payloads) {
        std::string source = std::string(root) + "\\" + p.path;
        std::replace(source.begin(), source.end(), '/', '\\');
        fake_win32::resources[p.id] = fake_win32::files.at(fake_win32::canon(source.c_str())).bytes;
    }
    Win32Store store;
    Journal j;
    assert(store.init(Os::nt5, root) && store.capture(j, payloads));
    j.phase = Phase::verified;
    Lineage l, empty;
    assert(import_first_generation(j, l));
    assert(save_lineage(root, empty, l, true));
    auto old = l;
    assert(reserve_generation(l, Phase::verified, j.installer_sha));
    assert(GenerationStager::absent(root, l.pending));
    assert(save_lineage(root, old, l));
    assert(!save_lineage(root, old, l)); // Lost-update protection.
    const auto baseline_files = fake_win32::files;
    const auto baseline_resources = fake_win32::resources;
    fake_win32::mutation = 0;
    assert(GenerationStager::stage(root, l, payloads));
    const unsigned operations = fake_win32::mutation;
    for (unsigned fail = 1; fail <= operations; ++fail) {
        fake_win32::files = baseline_files;
        fake_win32::fail = fail;
        fake_win32::mutation = 0;
        assert(!GenerationStager::stage(root, l, payloads));
        fake_win32::fail = 0;
        assert(GenerationStager::stage(root, l, payloads));
    }
    char generation[MAX_PATH];
    assert(generation_root(root, 2, generation));
    const auto inf =
        fake_win32::canon((std::string(generation) + "\\drivers\\nt5\\dreamgpu.inf").c_str());
    auto &bytes = fake_win32::files.at(inf).bytes;
    const auto expected = bytes;
    bytes.resize(bytes.size() / 2); // Interrupted resource write.
    assert(GenerationStager::stage(root, l, payloads) && bytes == expected);
    bytes[0] ^= 1;
    const auto foreign = bytes;
    assert(!GenerationStager::stage(root, l, payloads) && bytes == foreign);
    fake_win32::files = baseline_files;
    fake_win32::resources[payloads.front().id][0] ^= 1;
    fake_win32::mutation = 0;
    assert(!GenerationStager::stage(root, l, payloads) && fake_win32::mutation == 0);
    fake_win32::resources = baseline_resources;
    bool exists;
    Lineage found;
    auto mutations = fake_win32::mutation;
    assert(load_lineage(root, Os::nt5, found, exists, true) && exists && found.selected == 1 &&
           found.pending == 2 && fake_win32::mutation == mutations);
    printf("PASS driver generation store: %u staging failures, exact partial recovery, corrupt "
           "resource refusal, unpublished identity\n",
           operations);
}
