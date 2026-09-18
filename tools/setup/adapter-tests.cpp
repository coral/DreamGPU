// SPDX-License-Identifier: GPL-2.0-or-later
#define DG_SETUP_ADAPTER_TEST
#include "win32-lifecycle.h"
#include <cassert>
using namespace setup::lifecycle;
static Image image(const char *text, bool registry = false) {
    Image i;
    i.exists = 1;
    i.size = DWORD(strlen(text)) + (registry ? 1 : 0);
    i.type = registry ? REG_SZ : 0;
    if (registry)
        memcpy(i.value, text, i.size);
    setup::Sha256 h;
    h.update(reinterpret_cast<const BYTE *>(text), i.size);
    h.finish(i.sha);
    return i;
}
static Journal plan() {
    Journal j;
    j.generation = 1;
    j.count = 2;
    j.items[0].resource = 100;
    strcpy(j.items[0].path, "glide2x.dll");
    j.items[0].desired = image("new glide");
    j.items[1].kind = Kind::registry;
    strcpy(j.items[1].path, setup::nt_icd_key);
    strcpy(j.items[1].name, "Dll");
    j.items[1].desired = image("dgpuicd.dll", true);
    return j;
}
static Journal key_plan() {
    Journal j;
    j.generation = 1;
    j.count = 2;
    j.items[0].kind = Kind::registry_key;
    strcpy(j.items[0].path, setup::nt_icd_key);
    j.items[0].desired.exists = 1;
    setup::Sha256 h;
    h.finish(j.items[0].desired.sha);
    j.items[1].kind = Kind::registry;
    strcpy(j.items[1].path, setup::nt_icd_key);
    strcpy(j.items[1].name, "Dll");
    j.items[1].desired = image("dgpuicd.dll", true);
    return j;
}
static void initial() {
    using namespace fake_win32;
    reset();
    files[canon("C:\\WINDOWS\\DreamGPU")].directory = true;
    files[canon("C:\\WINDOWS\\SYSTEM\\glide2x.dll")].bytes = {'o', 'l', 'd'};
    files[canon("C:\\WINDOWS\\SYSTEM\\glide2x.dll")].flushed = true;
    resources[100] = {'n', 'e', 'w', ' ', 'g', 'l', 'i', 'd', 'e'};
    keys[canon(setup::nt_icd_key)]["dll"] = {REG_SZ, {'o', 'l', 'd', 0}};
}
static bool prepare(Win32Store &s, Journal &j) {
    if (!s.init("C:\\WINDOWS\\DreamGPU") || !s.create_generation(j.generation))
        return false;
    for (unsigned n = 0; n < j.count; n++)
        if (!s.capture(j, n))
            return false;
    return s.persist(j);
}
int main() {
    {
        initial();
        // Hashing an installer launched from a read-only CD requires no writes.
        constexpr char cd[] = "D:\\DREAMGPU.EXE";
        auto &file = fake_win32::files[fake_win32::canon(cd)];
        file.bytes = {'i', 'n', 's', 't', 'a', 'l', 'l', 'e', 'r'};
        file.attributes = FILE_ATTRIBUTE_READONLY;
        Win32Store s;
        Image actual;
        auto writes = fake_win32::mutation;
        assert(s.inspect_file(cd, actual, true) && same(actual, image("installer")));
        assert(fake_win32::mutation == writes && fake_win32::handles.empty());
        fake_win32::enforce_file_sharing = true;
        HANDLE held = CreateFileA(cd, GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        assert(held != INVALID_HANDLE_VALUE);
        assert(!s.inspect_file(cd, actual, true));
        assert(setup::failure_error == ERROR_SHARING_VIOLATION);
        CloseHandle(held);
        assert(fake_win32::handles.empty());
        // A validation failure must not reuse a previous syscall's error.
        file.directory = true;
        assert(!s.inspect_file(cd, actual, true) && setup::failure_error == 0);
        fake_win32::enforce_file_sharing = false;
        puts("PASS read-only installer inspection and captured file errors");
    }
    unsigned capture_points, apply_points;
    {
        initial();
        Win32Store s;
        Journal j = plan();
        assert(prepare(s, j));
        capture_points = fake_win32::mutation;
        auto &backup =
            fake_win32::files[fake_win32::canon("C:\\WINDOWS\\DreamGPU\\T00000001\\00.before")];
        assert(backup.flushed);
        fake_win32::mutation = 0;
        Engine e(s, j);
        assert(e.continue_apply(true) == Result::pending_reboot);
        apply_points = fake_win32::mutation;
        assert(s.classify(j, 0, false) == Actual::after &&
               s.classify(j, 1, false) == Actual::after);
        assert(j.state != State::activated);
    }
    for (unsigned failure = 1; failure <= capture_points; failure++) {
        initial();
        fake_win32::fail = failure;
        Win32Store s;
        Journal j = plan();
        assert(!prepare(s, j));
        assert(fake_win32::files[fake_win32::canon("C:\\WINDOWS\\SYSTEM\\glide2x.dll")].bytes ==
               std::vector<BYTE>({'o', 'l', 'd'}));
        assert(fake_win32::keys[fake_win32::canon(setup::nt_icd_key)]["dll"].bytes ==
               std::vector<BYTE>({'o', 'l', 'd', 0}));
    }
    for (unsigned failure = 1; failure <= apply_points; failure++) {
        initial();
        Win32Store s;
        Journal j = plan();
        assert(prepare(s, j));
        fake_win32::mutation = 0;
        fake_win32::fail = failure;
        Engine e(s, j);
        auto result = e.continue_apply(true);
        assert(result != Result::complete);
        fake_win32::fail = 0;
        Journal recovered;
        assert(s.load(recovered));
        // A failed private temporary write can leave incomplete private bytes;
        // rollback must still restore originals without accepting those bytes.
        Engine recovery(s, recovered);
        assert(recovery.rollback() == Result::complete);
        assert(fake_win32::files[fake_win32::canon("C:\\WINDOWS\\SYSTEM\\glide2x.dll")].bytes ==
               std::vector<BYTE>({'o', 'l', 'd'}));
        assert(fake_win32::keys[fake_win32::canon(setup::nt_icd_key)]["dll"].bytes ==
               std::vector<BYTE>({'o', 'l', 'd', 0}));
        assert(fake_win32::handles.empty() && fake_win32::key_handles.empty());
    }
    unsigned rollback_points;
    {
        initial();
        Win32Store s;
        Journal j = plan();
        assert(prepare(s, j));
        Engine e(s, j);
        assert(e.continue_apply(true) == Result::pending_reboot);
        fake_win32::mutation = 0;
        assert(e.rollback() == Result::complete);
        rollback_points = fake_win32::mutation;
    }
    for (unsigned failure = 1; failure <= rollback_points; failure++) {
        initial();
        Win32Store s;
        Journal j = plan();
        assert(prepare(s, j));
        Engine e(s, j);
        assert(e.continue_apply(true) == Result::pending_reboot);
        fake_win32::mutation = 0;
        fake_win32::fail = failure;
        assert(e.rollback() != Result::complete);
        fake_win32::fail = 0;
        Journal recovered;
        assert(s.load(recovered));
        Engine recovery(s, recovered);
        assert(recovery.rollback() == Result::complete);
        assert(fake_win32::files[fake_win32::canon("C:\\WINDOWS\\SYSTEM\\glide2x.dll")].bytes ==
               std::vector<BYTE>({'o', 'l', 'd'}));
        assert(fake_win32::keys[fake_win32::canon(setup::nt_icd_key)]["dll"].bytes ==
               std::vector<BYTE>({'o', 'l', 'd', 0}));
    }
    {
        initial();
        Win32Store s;
        Journal j = plan();
        assert(prepare(s, j));
        Engine e(s, j);
        assert(e.continue_apply(true) == Result::pending_reboot);
        // Preserve external edits: never restore over a third party's bytes.
        fake_win32::files[fake_win32::canon("C:\\WINDOWS\\SYSTEM\\glide2x.dll")].bytes = {'u', 's',
                                                                                          'e', 'r'};
        assert(e.rollback() == Result::conflict);
        assert(fake_win32::files[fake_win32::canon("C:\\WINDOWS\\SYSTEM\\glide2x.dll")].bytes ==
               std::vector<BYTE>({'u', 's', 'e', 'r'}));
    }
    {
        Journal j = plan();
        j.items[0].original_generation = j.items[1].original_generation = 1;
        j.items[0].original_index = 0;
        j.items[1].original_index = 1;
        assert(valid(j));
        j.items[1].desired.size--;
        assert(!valid(j));
        j = plan();
        j.items[1] = j.items[0];
        strcpy(j.items[1].path, "GLIDE2X.DLL");
        j.items[0].original_generation = j.items[1].original_generation = 1;
        assert(!valid(j));
    }
    {
        initial();
        Win32Store s;
        Journal installed = plan();
        assert(prepare(s, installed));
        Engine e(s, installed);
        assert(e.continue_apply(true) == Result::pending_reboot);
        installed.state = State::activated;
        Journal removal;
        assert(uninstall_plan(installed, removal));
        assert(s.create_generation(removal.generation));
        for (unsigned n = 0; n < removal.count; n++) {
            Item retained = removal.items[n];
            assert(s.capture(removal, n));
            assert(same(removal.items[n].before, retained.before));
            removal.items[n] = retained;
        }
        assert(s.persist(removal));
        Engine uninstall(s, removal);
        assert(uninstall.continue_apply(false) == Result::complete);
        assert(removal.state == State::removed);
        assert(fake_win32::files[fake_win32::canon("C:\\WINDOWS\\SYSTEM\\glide2x.dll")].bytes ==
               std::vector<BYTE>({'o', 'l', 'd'}));
        // Removing this generation itself can be rolled back from its own
        // durable immediate-before backup, independent of first-install backup.
        assert(uninstall.rollback() == Result::complete);
        assert(fake_win32::files[fake_win32::canon("C:\\WINDOWS\\SYSTEM\\glide2x.dll")].bytes ==
               std::vector<BYTE>({'n', 'e', 'w', ' ', 'g', 'l', 'i', 'd', 'e'}));
    }
    {
        initial();
        fake_win32::keys.clear();
        Win32Store s;
        Journal j;
        j.generation = 1;
        j.count = 2;
        j.items[0].kind = Kind::registry_key;
        strcpy(j.items[0].path, setup::nt_icd_key);
        j.items[0].desired.exists = 1;
        setup::Sha256 empty;
        empty.finish(j.items[0].desired.sha);
        j.items[1].kind = Kind::registry;
        strcpy(j.items[1].path, setup::nt_icd_key);
        strcpy(j.items[1].name, "Dll");
        j.items[1].desired = image("dgpuicd.dll", true);
        assert(prepare(s, j));
        Engine e(s, j);
        assert(e.continue_apply(true) == Result::pending_reboot);
        assert(e.rollback() == Result::complete);
        assert(fake_win32::keys.empty());
    }
    unsigned key_apply, key_rollback;
    {
        initial();
        fake_win32::keys.clear();
        Win32Store s;
        Journal j = key_plan();
        assert(prepare(s, j));
        fake_win32::mutation = 0;
        Engine e(s, j);
        assert(e.continue_apply(true) == Result::pending_reboot);
        key_apply = fake_win32::mutation;
        fake_win32::mutation = 0;
        assert(e.rollback() == Result::complete);
        key_rollback = fake_win32::mutation;
    }
    for (unsigned mode = 0; mode < 2; mode++)
        for (unsigned failure = 1; failure <= (mode ? key_rollback : key_apply); failure++) {
            initial();
            fake_win32::keys.clear();
            Win32Store s;
            Journal j = key_plan();
            assert(prepare(s, j));
            Engine e(s, j);
            if (mode)
                assert(e.continue_apply(true) == Result::pending_reboot);
            fake_win32::mutation = 0;
            fake_win32::fail = failure;
            assert((mode ? e.rollback() : e.continue_apply(true)) != Result::complete);
            fake_win32::fail = 0;
            Journal recovered;
            assert(s.load(recovered));
            Engine recovery(s, recovered);
            assert(recovery.rollback() == Result::complete);
            assert(fake_win32::keys.empty());
        }
    printf("PASS actual Win32Store key ownership: %u apply, %u rollback syscall points\n",
           key_apply, key_rollback);
    printf("PASS actual Win32Store fault gates: %u capture, %u apply, %u rollback syscall points\n",
           capture_points, apply_points, rollback_points);
}
