// SPDX-License-Identifier: GPL-2.0-or-later
#define DG_SETUP_ADAPTER_TEST
#include "test-win32.h"
inline DWORD GetModuleFileNameA(void *, char *out, DWORD size) {
    constexpr char path[] = "C:\\WINDOWS\\DreamGPU\\setup.exe";
    if (size < sizeof(path))
        return size;
    strcpy(out, path);
    return sizeof(path) - 1;
}
#include "runtime-boot-store.h"
#include "runtime-resume.h"
#include <cassert>
using namespace setup;
using namespace setup::lifecycle;
constexpr char Owner[] = "C:\\WINDOWS\\DreamGPU";
constexpr char RunKey[] = "Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce";
static Image value(const char *s) {
    Image out;
    out.exists = 1;
    out.size = DWORD(strlen(s));
    Sha256 hash;
    hash.update(reinterpret_cast<const BYTE *>(s), out.size);
    hash.finish(out.sha);
    return out;
}
static void initial(Win32Store &store, Journal &j, bool present = true) {
    using namespace fake_win32;
    reset();
    files[canon(Owner)].directory = true;
    files[canon("C:\\WINDOWS\\DreamGPU\\setup.exe")].bytes = {'p', 'e'};
    keys[canon(RunKey)];
    if (present)
        files[canon("C:\\WINDOWS\\SYSTEM\\ddraw.dll")].bytes = {'o', 'l', 'd'};
    resources[100] = {'n', 'e', 'w'};
    j = {};
    j.generation = 1;
    j.count = 1;
    strcpy(j.items[0].path, "ddraw.dll");
    j.items[0].resource = 100;
    j.items[0].desired = value("new");
    assert(store.init(Owner) && store.create_generation(1) && store.capture(j, 0) &&
           store.persist(j));
    fake_win32::mutation = 0;
}
static void wait_receipt() {
    unsigned points = 0;
    for (unsigned failure = 0; failure <= points; ++failure) {
        Win32Store store;
        Journal j;
        initial(store, j);
        RuntimeBootStore bridge(store, j, Os::nt5, false);
        boot::Record record;
        bool needed;
        assert(bridge.prepare(record, needed) && needed);
        boot::RawQueue raw;
        constexpr char source[] = "\\??\\C:\\WINDOWS\\SYSTEM\\OLD3.tmp";
        raw.exists = true;
        for (unsigned n = 0; n < sizeof(source) - 1; ++n)
            raw.data[raw.words++] = source[n];
        raw.data[raw.words++] = 0;
        raw.data[raw.words++] = 0;
        raw.data[raw.words++] = 0;
        fake_win32::mutation = 0;
        fake_win32::fail = failure;
        assert(bridge.waiting(record, &raw) == !failure);
        if (!failure)
            points = fake_win32::mutation;
        fake_win32::fail = 0;
        RuntimeBootStore recovery(store, j, Os::nt5, false);
        assert(recovery.waiting(record, &raw));
        const unsigned before = fake_win32::mutation;
        assert(recovery.waiting(record, &raw) && before == fake_win32::mutation);
        assert(recovery.waiting(record));
        assert(record.phase == boot::Phase::prepared);
    }
    printf("PASS foreign-queue wait receipt: %u mutation failure/recovery "
           "points, repeated waiting "
           "idempotent, owned boot record remains prepared\n",
           points);
}
int main() {
    wait_receipt();
    unsigned prepare_points = 0;
    {
        Win32Store store;
        Journal j;
        initial(store, j);
        RuntimeBootStore bridge(store, j, Os::nt5, false);
        boot::Record record;
        bool exists = true;
        assert(bridge.load(record, exists) && !exists);
        bool needed = false;
        assert(bridge.prepare(record, needed) && needed && record.count == 1);
        prepare_points = fake_win32::mutation;
        assert(record.entries[0].protected_target == 1 && bridge.approved(record.entries[0]));
        assert(fake_win32::files.at(fake_win32::canon("C:\\WINDOWS\\SYSTEM\\ddraw.dll")).bytes ==
               std::vector<BYTE>({'o', 'l', 'd'}));
        boot::Entry foreign = record.entries[0];
        strcpy(foreign.destination, "C:\\foreign.dll");
        assert(!bridge.approved(foreign));
        foreign = record.entries[0];
        strcpy(foreign.source, "C:\\foreign.dll");
        assert(!bridge.approved(foreign));
        foreign = record.entries[0];
        foreign.desired.sha[0] ^= 1;
        assert(!bridge.approved(foreign));
        boot::File image;
        assert(!bridge.inspect("C:\\foreign.dll", image));
        record.phase = boot::Phase::queued;
        assert(bridge.save(record));
        boot::Record loaded;
        assert(bridge.load(loaded, exists) && exists && loaded.phase == boot::Phase::queued);
        auto &log =
            fake_win32::files.at(fake_win32::canon("C:\\WINDOWS\\DreamGPU\\T00000001\\APPLY.BOOT"))
                .bytes;
        const auto intact = log;
        log.push_back(1);
        log.push_back(2);
        assert(bridge.load(loaded, exists) && log == intact);
        log[0] ^= 1;
        assert(!bridge.load(loaded, exists));
    }
    for (unsigned fault = 1; fault <= prepare_points; ++fault) {
        Win32Store store;
        Journal j;
        initial(store, j);
        fake_win32::fail = fault;
        RuntimeBootStore bridge(store, j, Os::nt5, false);
        boot::Record record;
        bool needed;
        assert(!bridge.prepare(record, needed));
        assert(fake_win32::files.at(fake_win32::canon("C:\\WINDOWS\\SYSTEM\\ddraw.dll")).bytes ==
               std::vector<BYTE>({'o', 'l', 'd'}));
    }
    // Rollback of a newly installed DLL is an exact owned delayed deletion.
    {
        Win32Store store;
        Journal j;
        initial(store, j, false);
        fake_win32::files[fake_win32::canon("C:\\WINDOWS\\SYSTEM\\ddraw.dll")].bytes = {'n', 'e',
                                                                                        'w'};
        RuntimeBootStore bridge(store, j, Os::nt5, true);
        boot::Record record;
        bool needed;
        assert(bridge.prepare(record, needed) && needed);
        assert(record.count == 1 && !record.entries[0].source[0] &&
               !record.entries[0].desired.exists && record.entries[0].before.exists);
    }
    unsigned resume_points = 0;
    for (bool prior : {false, true}) {
        auto reset = [&]() {
            Win32Store store;
            Journal j;
            initial(store, j);
            if (prior)
                fake_win32::keys[fake_win32::canon(RunKey)]["dreamgpu.runtime"] = {
                    REG_SZ, {'o', 'l', 'd', 0}};
            return store;
        };
        {
            auto store = reset();
            RuntimeResume resume(store, 1, Os::nt5);
            assert(resume.prepare() && resume.arm() && resume.finish());
            resume_points = fake_win32::mutation;
        }
        for (unsigned fault = 1; fault <= resume_points; ++fault) {
            auto store = reset();
            fake_win32::fail = fault;
            RuntimeResume resume(store, 1, Os::nt5);
            bool prepared = resume.prepare();
            if (prepared) {
                (void)resume.arm();
                (void)resume.finish();
            }
            fake_win32::fail = 0;
            RuntimeResume recovery(store, 1, Os::nt5);
            assert(recovery.prepare() && recovery.finish());
            const auto &values = fake_win32::keys.at(fake_win32::canon(RunKey));
            if (prior)
                assert(values.at("dreamgpu.runtime").bytes ==
                       std::vector<BYTE>({'o', 'l', 'd', 0}));
            else
                assert(!values.count("dreamgpu.runtime"));
        }
        auto store = reset();
        RuntimeResume resume(store, 1, Os::nt5);
        assert(resume.prepare() && resume.arm());
        fake_win32::keys[fake_win32::canon(RunKey)]["dreamgpu.runtime"] = {REG_SZ, {'f', 0}};
        assert(!resume.arm() && !resume.finish());
        // Windows consuming the owned RunOnce command is expected, not a conflict.
        fake_win32::keys[fake_win32::canon(RunKey)].erase("dreamgpu.runtime");
        assert(resume.finish());
    }
    {
        Win32Store store;
        Journal j;
        initial(store, j);
        fake_win32::files[fake_win32::canon("C:\\WINDOWS\\DreamGPU\\G00000009")].directory = true;
        fake_win32::keys[fake_win32::canon(RunKey)]["dreamgpu.setup"] = {REG_DWORD, {7, 0, 0, 0}};
        RuntimeResume runtime(store, 1, Os::nt5), global(store, 9, Os::nt5, ResumeScope::global);
        assert(runtime.prepare() && runtime.arm() && global.prepare() && global.arm());
        auto &values = fake_win32::keys.at(fake_win32::canon(RunKey));
        assert(values.count("dreamgpu.runtime") && values.count("dreamgpu.setup"));
        const auto &command = values.at("dreamgpu.setup").bytes;
        assert(std::string(command.begin(), command.end()).find("G00000009\\setup.exe") !=
               std::string::npos);
        assert(global.finish());
        assert(values.at("dreamgpu.setup").type == REG_DWORD &&
               values.at("dreamgpu.setup").bytes == std::vector<BYTE>({7, 0, 0, 0}));
        assert(values.count("dreamgpu.runtime") && runtime.finish());
    }
    printf("PASS actual runtime boot bridge: %u staging failure points, typed "
           "journal "
           "tails/ownership/deletion; RunOnce %u mutation boundaries with exact "
           "prior recovery\n",
           prepare_points, resume_points);
}
