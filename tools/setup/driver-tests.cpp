// SPDX-License-Identifier: GPL-2.0-or-later
#include "driver-test-win32.h"
#include "driver-store.h"
#include <array>
#include <deque>
#include <cassert>
using namespace setup::driver;
using setup::Os;
static constexpr char root[] = "C:\\WINDOWS\\DreamGPU";
struct Payload {
    unsigned os, id, size;
    const char *path, *sha;
};
static std::vector<Payload> payloads;
static std::deque<std::string> strings;
static std::string digest(const std::vector<BYTE> &v) {
    setup::Sha256 hash;
    hash.update(v.data(), v.size());
    char out[65];
    hash.finish(out);
    return out;
}
static void file(const std::string &path, const std::string &bytes) {
    fake_win32::files[fake_win32::canon(path.c_str())] = {
        {bytes.begin(), bytes.end()}, false, true};
}
static void payload(Os os, const std::string &name, const std::string &bytes) {
    auto path = std::string(root) + "\\" + name;
    std::replace(path.begin(), path.end(), '/', '\\');
    file(path, bytes);
    strings.push_back(name);
    const char *name_ptr = strings.back().c_str();
    strings.push_back(digest({bytes.begin(), bytes.end()}));
    payloads.push_back({unsigned(os), unsigned(payloads.size() + 100), unsigned(bytes.size()),
                        name_ptr, strings.back().c_str()});
}
static void setup_case(Os os, bool own) {
    fake_win32::reset();
    payloads.clear();
    strings.clear();
    driver_fake::compatible.clear();
    driver_fake::params = {};
    driver_fake::live = false;
    driver_fake::leave_running = false;
    driver_fake::times_available = driver_fake::path_available = true;
    driver_fake::created_low = driver_fake::created_high = 1;
    driver_fake::started = true;
    driver_fake::present = driver_fake::driver_key = true;
    driver_fake::property_error = 0;
    driver_fake::malformed_detail = false;
    driver_fake::ambiguous = false;
    driver_fake::binds = driver_fake::launches = driver_fake::resumes = driver_fake::destroys = 0;
    driver_fake::execute = {};
    driver_fake::node_changed = {};
    driver_fake::short_paths.clear();
    driver_fake::detail_inf_override.clear();
    file("C:\\dreamgpu.exe", "installer");
    fake_win32::files[fake_win32::canon(root)].directory = true;
    Node original{};
    strcpy(original.device, "PCI\\VEN_1234&DEV_1113&SUBSYS_00000000\\1");
    strcpy(original.inf, "C:\\WINDOWS\\INF\\old.inf");
    strcpy(original.section, own ? "Dg" : "Vga");
    strcpy(original.provider, own ? "DreamGPU" : "Microsoft");
    strcpy(original.description, own ? "DreamGPU" : "Standard VGA");
    file(original.inf, own ? "old dreamgpu inf" : "microsoft vga inf");
    strcpy(original.inf_sha,
           digest(fake_win32::files[fake_win32::canon(original.inf)].bytes).c_str());
    driver_fake::set_node(original);
    driver_fake::compatible.push_back(original);
    driver_fake::reg("service", own ? "dgpumini" : "VgaSave");
    auto &defaults = fake_win32::keys["default"];
    const char *drv = own ? "dgpumini.drv" : "vga.drv";
    defaults["drv"] = {REG_SZ,
                       {reinterpret_cast<const BYTE *>(drv),
                        reinterpret_cast<const BYTE *>(drv) + strlen(drv) + 1}};
    constexpr char mini[] = "dgpumini.vxd";
    defaults["minivdd"] = {REG_SZ, {mini, mini + sizeof(mini)}};
    std::string dir = os == Os::win98 ? "drivers/win98/" : "drivers/nt5/";
    const char *inf = os == Os::win98 ? "dg9x.inf" : "dreamgpu.inf";
    payload(os, dir + inf, "new dreamgpu inf");
    payload(os, dir + (os == Os::win98 ? "dgpumini.drv" : "dgpudisp.dll"), "new display");
    payload(os, dir + (os == Os::win98 ? "dgpumini.vxd" : "dgpumini.sys"), "new miniport");
    payload(os, os == Os::win98 ? "tools/common/DG9INST.EXE" : "tools/nt5/DGDRV.EXE",
            "binding helper");
    payload(os, os == Os::win98 ? "tools/common/DG9AUDIT.EXE" : "tools/nt5/DGAUDIT.EXE",
            "audit helper");
    Node desired = original;
    strcpy(desired.description, "DreamGPU");
    strcpy(desired.provider, "DreamGPU");
    strcpy(desired.section, "Dg");
    auto source = std::string(root) + "\\" + dir + inf;
    std::replace(source.begin(), source.end(), '/', '\\');
    strcpy(desired.inf, source.c_str());
    strcpy(desired.inf_sha,
           digest(fake_win32::files[fake_win32::canon(desired.inf)].bytes).c_str());
    driver_fake::compatible.push_back(desired);
    if (own) {
        file(os == Os::win98 ? "C:\\WINDOWS\\SYSTEM\\dgpumini.drv"
                             : "C:\\WINDOWS\\SYSTEM\\dgpudisp.dll",
             "old display");
        file(os == Os::win98 ? "C:\\WINDOWS\\SYSTEM\\dgpumini.vxd"
                             : "C:\\WINDOWS\\SYSTEM\\drivers\\dgpumini.sys",
             "old miniport");
        for (auto &[path, node] : fake_win32::files)
            if (path.find("\\system\\") != std::string::npos)
                node.attributes = FILE_ATTRIBUTE_READONLY | 32;
    } else {
        file(os == Os::win98 ? "C:\\WINDOWS\\SYSTEM\\vga.drv" : "C:\\WINDOWS\\SYSTEM\\vga.dll",
             "microsoft display");
        if (os == Os::nt5)
            file("C:\\WINDOWS\\SYSTEM\\drivers\\vga.sys", "microsoft miniport");
    }
}
static void execution(Journal &j) {
    driver_fake::execute = [&](const std::string &exe) {
        if (exe.find("DGDBIND.EXE") == std::string::npos)
            return;
        bool rollback = exe.find("driver-backup") != std::string::npos;
        for (unsigned n = 0; n < j.count; ++n)
            if (j.files[n].replaced) {
                const auto &f = j.files[n];
                std::string source;
                if (rollback)
                    source = std::string(root) + f.backup;
                else {
                    source = j.desired.inf;
                    source.resize(source.find_last_of('\\') + 1);
                    source += strrchr(f.path, '\\') + 1;
                }
                fake_win32::files[fake_win32::canon(f.path)] =
                    fake_win32::files[fake_win32::canon(source.c_str())];
            }
        if (rollback)
            driver_fake::set_node(j.original);
        else {
            auto installed = j.desired;
            strcpy(installed.inf, "C:\\WINDOWS\\INF\\new.inf");
            fake_win32::files[fake_win32::canon(installed.inf)] =
                fake_win32::files[fake_win32::canon(j.desired.inf)];
            driver_fake::set_node(installed);
        }
    };
}
static void capture_and_restore(Os os, bool own) {
    setup_case(os, own);
    Win32Store store;
    Journal j;
    assert(store.init(os, root));
    assert(store.capture(j, payloads));
    assert(j.phase == Phase::captured);
    assert(driver_fake::binds == 0 && driver_fake::launches == 0);
    execution(j);
    Engine engine(store, j);
    assert(engine.install() == Result::pending_reboot);
    assert(j.phase == Phase::pending_reboot);
    assert(driver_fake::resumes == 1);
    assert(engine.resume() == Result::verified);
    assert(engine.rollback() == Result::pending_reboot);
    assert(engine.resume() == Result::restored);
    assert(store.observe(j) == Actual::original);
    for (unsigned n = 0; n < j.count; ++n) {
        auto &f = j.files[n];
        auto it = fake_win32::files.find(fake_win32::canon(f.path));
        if (!f.exists)
            assert(it == fake_win32::files.end());
        else
            assert(it != fake_win32::files.end() && digest(it->second.bytes) == f.sha);
    }
    auto key = fake_win32::keys.find("software\\microsoft\\windows\\currentversion\\run");
    assert(key == fake_win32::keys.end() || !key->second.count("dreamgpu.driver"));
}
static unsigned failure_path(Os os, bool own, unsigned fail) {
    setup_case(os, own);
    Win32Store store;
    Journal j;
    assert(store.init(os, root));
    assert(store.capture(j, payloads));
    execution(j);
    fake_win32::mutation = 0;
    fake_win32::fail = fail;
    Engine engine(store, j);
    auto result = engine.install();
    unsigned calls = fake_win32::mutation;
    fake_win32::fail = 0;
    Journal loaded;
    assert(store.load(loaded));
    j = loaded;
    // Recovery always reloads the durable record. A failure before the helper
    // was resumed cannot adopt a partially changed global binding.
    if (result != Result::pending_reboot) {
        Engine resume(store, j);
        auto r = resume.resume();
        assert(r == Result::pending_reboot || r == Result::verified);
    }
    Engine cleanup(store, j);
    auto r = cleanup.rollback();
    assert(r == Result::pending_reboot || r == Result::restored);
    if (r == Result::pending_reboot)
        assert(cleanup.resume() == Result::restored);
    return calls;
}
static unsigned rollback_failure(Os os, bool own, unsigned fail) {
    setup_case(os, own);
    Win32Store store;
    Journal j;
    assert(store.init(os, root));
    assert(store.capture(j, payloads));
    execution(j);
    Engine engine(store, j);
    assert(engine.install() == Result::pending_reboot);
    assert(engine.resume() == Result::verified);
    fake_win32::mutation = 0;
    fake_win32::fail = fail;
    auto result = engine.rollback();
    unsigned count = fake_win32::mutation;
    fake_win32::fail = 0;
    Journal loaded;
    assert(store.load(loaded));
    j = loaded;
    Engine recovery(store, j);
    // An action whose first durable write failed has not committed its intent;
    // explicit retry remains safe. Otherwise continuation follows the journal.
    auto r = j.phase == Phase::verified ? recovery.rollback() : recovery.resume();
    assert(r == Result::pending_reboot || r == Result::restored);
    if (r == Result::pending_reboot)
        assert(recovery.resume() == Result::restored);
    (void)result;
    return count;
}
static unsigned capture_failure(Os os, bool own, unsigned fail) {
    setup_case(os, own);
    auto original = fake_win32::files;
    Win32Store store;
    Journal j;
    assert(store.init(os, root));
    fake_win32::mutation = 0;
    fake_win32::fail = fail;
    bool okay = store.capture(j, payloads);
    unsigned count = fake_win32::mutation;
    fake_win32::fail = 0;
    for (auto &[p, f] : original)
        assert(fake_win32::files[p].bytes == f.bytes &&
               fake_win32::files[p].attributes == f.attributes);
    assert(driver_fake::binds == 0 && driver_fake::launches == 0);
    if (!okay) {
        Journal loaded;
        if (store.load(loaded)) {
            assert(loaded.phase == Phase::capturing || loaded.phase == Phase::captured);
            if (loaded.phase == Phase::capturing)
                assert(store.complete_capture(loaded));
        }
        // A failed first journal append has no published full record and no
        // public mutations. It is deliberately not treated as successful capture.
    }
    return count;
}
static void binding_schema_and_absence() {
    setup_case(Os::nt5, false);
    Win32Store store;
    Journal j;
    assert(store.init(Os::nt5, root) && store.capture(j, payloads));
    j.version = 1;
    assert(valid(j));
    assert(store.persist(j));
    Journal read;
    assert(store.load(read) && read.version == 1);
    assert(read.original_binding == Binding::stock);
    Node absent_node{};
    strcpy(absent_node.device, j.original.device);
    j.original = absent_node;
    j.original_binding = Binding::unbound;
    assert(!valid(j)); // V1 has no encoding for absence.
    j.version = 2;
    assert(valid(j));
    j.os = Os::win98;
    assert(!valid(j)); // Only NT PCI first-install absence is implemented.
    j.os = Os::nt5;
    j.original.inf_sha[67] = 'x';
    assert(!valid(j)); // Hidden bytes cannot smuggle an invented original node.
    j.original.inf_sha[67] = 0;
    assert(valid(j));
    driver_fake::set_node(absent_node);
    // Merely changing a bound journal to "unbound" cannot invent the independent
    // OEM/service ownership receipts. The complete first-install suite supplies them.
    assert(store.observe(j) == Actual::error);
    driver_fake::reg("service", "foreign-driver");
    assert(store.observe(j) == Actual::error);
    fake_win32::keys["driver"].erase("service");
    driver_fake::driver_key = true;
    assert(store.observe(j) == Actual::error); // No binding property but an owned key.
    driver_fake::driver_key = false;
    driver_fake::property_error = ERROR_ACCESS_DENIED;
    assert(store.observe(j) == Actual::error);
    driver_fake::property_error = 0;
    driver_fake::present = false;
    assert(store.refresh(j.original.device));  // Removed device can be reconciled.
    assert(store.observe(j) == Actual::error); // Not proof of completed restoration.
    driver_fake::present = true;
    strcpy(driver_fake::current.device, "PCI\\VEN_1234&DEV_1113&SUBSYS_00000000\\2");
    assert(!store.refresh(j.original.device)); // Never adopt a different adapter.
}
static void identical_driver_rollback(Os os) {
    setup_case(os, true);
    auto original = driver_fake::current;
    file(original.inf, "new dreamgpu inf");
    strcpy(original.inf_sha, driver_fake::compatible.back().inf_sha);
    driver_fake::compatible[0] = original;
    driver_fake::set_node(original);
    file(os == Os::win98 ? R"(C:\WINDOWS\SYSTEM\dgpumini.drv)"
                         : R"(C:\WINDOWS\SYSTEM\dgpudisp.dll)",
         "new display");
    file(os == Os::win98 ? R"(C:\WINDOWS\SYSTEM\dgpumini.vxd)"
                         : R"(C:\WINDOWS\SYSTEM\drivers\dgpumini.sys)",
         "new miniport");
    Win32Store store;
    Journal j;
    assert(store.init(os, root) && store.capture(j, payloads));
    j.phase = Phase::verified;
    assert(store.persist(j) && store.observe(j) == Actual::desired);
    unsigned rebindings = 0;
    driver_fake::execute = [&](const std::string &exe) {
        if (exe.find("DGDBIND.EXE") != std::string::npos)
            ++rebindings;
    };
    Engine engine(store, j);
    assert(engine.rollback() == Result::restored && j.phase == Phase::restored);
    assert(rebindings == 0 && driver_fake::binds == 0 && store.verify_readonly(j, true));
}
static void child_process_ownership() {
    setup_case(Os::nt5, true);
    Win32Store store;
    Journal j;
    assert(store.init(Os::nt5, root) && store.capture(j, payloads));
    driver_fake::leave_running = true;
    Engine engine(store, j);
    assert(engine.install() == Result::pending_reboot && j.child_pid == 9000);
    const unsigned launches = driver_fake::launches;
    auto journal_bytes =
        fake_win32::files[fake_win32::canon((std::string(root) + "\\driver.bin").c_str())].bytes;
    Journal pending;
    assert(store.load(pending, true) && pending.child_pid == 9000);
    assert(
        fake_win32::files[fake_win32::canon((std::string(root) + "\\driver.bin").c_str())].bytes ==
        journal_bytes);
    Engine retry(store, pending);
    assert(retry.resume() == Result::pending_reboot && driver_fake::launches == launches);
    // A reused PID with a different creation identity is stale, even when the
    // new process uses the same executable. Read-only inspection never appends.
    driver_fake::created_low++;
    assert(store.load(pending, true) && !pending.child_pid);
    fake_win32::enforce_file_sharing = true;
    assert(store.load(pending) && !pending.child_pid);
    fake_win32::enforce_file_sharing = false;
    j.child_pid = 9000;
    assert(store.persist(j));
    driver_fake::times_available = false;
    assert(!store.load(pending, true));
    driver_fake::times_available = true;
    // Historical PID-only records: unknown inspection fails closed; a foreign
    // process path proves reuse; an owned path with drift never authorizes replay.
    fake_win32::files.erase(fake_win32::canon((std::string(root) + "\\driver-child.bin").c_str()));
    driver_fake::path_available = false;
    assert(!store.load(pending, true));
    driver_fake::path_available = true;
    driver_fake::executable = R"(C:\WINNT\Explorer.exe)";
    assert(store.load(pending, true) && !pending.child_pid);
    driver_fake::executable = std::string(root) + R"(\drivers\nt5\DGDBIND.EXE)";
    assert(store.load(pending, true) && pending.child_pid);
    fake_win32::files[fake_win32::canon(driver_fake::executable.c_str())].bytes.push_back(0);
    assert(!store.load(pending, true));
    driver_fake::live = driver_fake::leave_running = false;
}
static void managed_legacy_driver_startup() {
    setup_case(Os::nt5, true);
    Win32Store store;
    Journal j;
    assert(store.init(Os::nt5, root) && store.capture(j, payloads));
    assert(j.version == 4);
    // Reconstruct the compatible older NT schema with an absent prior RunOnce.
    // Managed GLOBAL already owns its separate authenticated Run registration.
    j.version = 2;
    assert(!j.resume_existed && valid(j) && store.persist(j));
    const auto mutations = fake_win32::mutation;
    assert(!store.arm_resume(j, true) && fake_win32::mutation == mutations);
    store.managed_startup(true);
    assert(store.arm_resume(j, true) && fake_win32::mutation == mutations);
    assert(!fake_win32::keys["software\\microsoft\\windows\\currentversion\\runonce"].count(
        "dreamgpu.driver"));
    assert(!fake_win32::keys["software\\microsoft\\windows\\currentversion\\run"].count(
        "dreamgpu.driver"));
}
static void installed_inf_compatibility() {
    // Exercise real node(), compatible-list selection, capture and observation.
    for (auto os : {Os::win98, Os::nt5})
        for (unsigned scenario = 0; scenario < 7; ++scenario) {
            setup_case(os, false);
            auto original = driver_fake::current;
            const char *base = "C:\\WINDOWS\\INF\\JUKEJR~1.INF";
            const char *other = "C:\\WINDOWS\\INF\\OTHER\\JUKEJR~1.INF";
            // 0 root only, 1 OTHER only, 2 identical copies, 3 conflicting
            // copies, 4 neither, 5 directory, 6 reparse-point candidate.
            bool root_present = scenario == 0 || scenario == 2 || scenario == 3;
            bool other_present = scenario != 0 && scenario != 4;
            strcpy(original.inf, root_present ? base : other);
            if (root_present)
                file(base, "original OEM inf");
            if (other_present)
                file(other, scenario == 3 ? "different OEM inf" : "original OEM inf");
            if (scenario == 5)
                fake_win32::files[fake_win32::canon(other)].directory = true;
            if (scenario == 6)
                fake_win32::files[fake_win32::canon(other)].attributes = FILE_ATTRIBUTE_REPARSE_POINT;
            driver_fake::set_node(original);
            driver_fake::compatible[0] = original;
            Win32Store store;
            Journal j;
            assert(store.init(os, root));
            bool expected = os == Os::win98 ? scenario < 3 : root_present;
            assert(store.capture(j, payloads) == expected);
            assert(driver_fake::binds == 0 && driver_fake::launches == 0);
            if (expected) {
                assert(!strcmp(j.original.inf, original.inf));
                assert(store.observe(j) == Actual::original);
            }
        }
    for (const char *name : {"../old.inf", "..\\old.inf", "C:\\old.inf", "other/old.inf",
                             "other\\old.inf", ".", "..", "old.inf.", "old.inf ", ""}) {
        setup_case(Os::win98, false);
        driver_fake::reg("InfPath", name);
        Win32Store store;
        Journal j;
        assert(store.init(Os::win98, root) && !store.capture(j, payloads));
        assert(driver_fake::binds == 0 && driver_fake::launches == 0);
    }
    // Windows may return bounded string bytes without the terminal NUL.
    // Malformed identities must still fail before any driver mutation.
    for (unsigned scenario = 0; scenario < 7; ++scenario) {
        setup_case(Os::win98, false);
        for (const char *key : {"infpath", "infsection", "providername", "driverdesc"})
            fake_win32::keys["driver"][key].bytes.pop_back();
        fake_win32::keys["default"]["drv"].bytes.pop_back();
        auto &value = fake_win32::keys["driver"]["providername"];
        if (scenario == 1)
            value.bytes = {'a', 0, 'b'};
        if (scenario == 2)
            value.bytes = {0};
        if (scenario == 3)
            value.bytes = {};
        if (scenario == 4)
            value.type = REG_DWORD;
        if (scenario == 5)
            value.bytes.assign(256, 'a');
        if (scenario == 6)
            value.bytes = {'a', 0, 0};
        Win32Store store;
        Journal j;
        assert(store.init(Os::win98, root));
        assert(store.capture(j, payloads) == (scenario == 0));
        assert(driver_fake::binds == 0 && driver_fake::launches == 0);
        if (scenario == 0)
            assert(store.observe(j) == Actual::original);
    }
    // A maximum-length identity fits with or without its stored terminator.
    for (bool terminated : {false, true}) {
        setup_case(Os::win98, false);
        auto original = driver_fake::current;
        memset(original.provider, 'a', sizeof(original.provider) - 1);
        original.provider[sizeof(original.provider) - 1] = 0;
        driver_fake::set_node(original);
        driver_fake::compatible[0] = original;
        if (!terminated)
            fake_win32::keys["driver"]["providername"].bytes.pop_back();
        Win32Store store;
        Journal j;
        assert(store.init(Os::win98, root) && store.capture(j, payloads));
        assert(!strcmp(j.original.provider, original.provider));
    }
    // A mismatched legacy display/miniport pair is never adopted.
    setup_case(Os::win98, false);
    constexpr char legacy[] = "qemumini.drv";
    fake_win32::keys["default"]["drv"] = {REG_SZ, {legacy, legacy + sizeof(legacy)}};
    Win32Store store;
    Journal j;
    assert(store.init(Os::win98, root) && !store.capture(j, payloads));
    assert(driver_fake::binds == 0 && driver_fake::launches == 0);
}
#include "driver-legacy-tests.inc"
int main() {
    legacy_win98_tests();
    installed_inf_compatibility();
    managed_legacy_driver_startup();
    child_process_ownership();
    identical_driver_rollback(Os::nt5);
    identical_driver_rollback(Os::win98);
    binding_schema_and_absence();
    unsigned failures = 0;
    for (auto os : {Os::win98, Os::nt5})
        for (bool own : {false, true}) {
            capture_and_restore(os, own);
            unsigned count = failure_path(os, own, 0);
            for (unsigned n = 1; n <= count; ++n) {
                failure_path(os, own, n);
                ++failures;
            }
            count = rollback_failure(os, own, 0);
            for (unsigned n = 1; n <= count; ++n) {
                rollback_failure(os, own, n);
                ++failures;
            }
            count = capture_failure(os, own, 0);
            for (unsigned n = 1; n <= count; ++n) {
                capture_failure(os, own, n);
                ++failures;
            }
            setup_case(os, own);
            Win32Store store;
            Journal j;
            assert(store.init(os, root));
            {
                SP_DEVINFO_DATA device{};
                auto optical = driver_fake::current;
                strcpy(optical.inf, "E:\\DG9X.INF");
                driver_fake::compatible.push_back(optical);
                DriverList list(1, device);
                assert(list.select(optical));
                auto callback = driver_fake::params.InstallMsgHandler;
                auto context = driver_fake::params.InstallMsgHandlerContext;
                assert(callback && context);
                SOURCE_MEDIA_A media{"dgpumini.drv"};
                char output[MAX_PATH]{};
                assert(callback(context, SPFILENOTIFY_NEEDMEDIA, reinterpret_cast<UINT_PTR>(&media),
                                reinterpret_cast<UINT_PTR>(output)) == FILEOP_NEWPATH);
                assert(!strcmp(output, "E:\\")); // drive root must not become drive-relative E:
                driver_fake::compatible.pop_back();
                assert(callback(context, SPFILENOTIFY_NEEDMEDIA, reinterpret_cast<UINT_PTR>(&media),
                                reinterpret_cast<UINT_PTR>(output)) == FILEOP_ABORT);
                media.SourceFile = "..\\foreign.dll";
                assert(callback(context, SPFILENOTIFY_NEEDMEDIA, reinterpret_cast<UINT_PTR>(&media),
                                reinterpret_cast<UINT_PTR>(output)) == FILEOP_ABORT);
                assert(callback(context, SPFILENOTIFY_COPYERROR, 0, 0) == FILEOP_ABORT);
                assert(callback(context, SPFILENOTIFY_STARTDELETE, 0, 0) == FILEOP_ABORT);
            }
            driver_fake::ambiguous = true;
            assert(!store.capture(j, payloads));
            assert(fake_win32::mutation == 0);
            driver_fake::ambiguous = false;
            driver_fake::malformed_detail = true;
            assert(!store.capture(j, payloads));
            assert(fake_win32::mutation == 0);
            driver_fake::malformed_detail = false;
            assert(store.capture(j, payloads));
            if (own) {
                auto same_inf = j;
                same_inf.desired = same_inf.original;
                same_inf.phase = Phase::captured;
                assert(store.observe(same_inf) == Actual::original);
                same_inf.phase = Phase::binding;
                assert(store.observe(same_inf) == Actual::pending);
                same_inf.phase = Phase::pending_reboot;
                assert(store.observe(same_inf) == Actual::pending);
                same_inf.phase = Phase::restore_pending;
                assert(store.observe(same_inf) == Actual::original);
                auto old_files = fake_win32::files;
                file(j.files[0].path, "new display");
                file(j.files[1].path, "new miniport");
                same_inf.phase = Phase::verified;
                assert(store.observe(same_inf) == Actual::desired);
                same_inf.phase = Phase::restoring;
                assert(store.observe(same_inf) == Actual::pending);
                unsigned launches = driver_fake::launches;
                Engine pending_restore(store, same_inf);
                assert(pending_restore.rollback() == Result::pending_reboot);
                assert(driver_fake::launches == launches);
                fake_win32::files = old_files;
            }
            auto bad = j;
            bad.files[1] = bad.files[0];
            for (char *c = bad.files[1].path; *c; ++c)
                *c = char(toupper(*c));
            assert(!valid(bad));
            bad = j;
            bad.original.inf_sha[3] = 'g';
            assert(!valid(bad));
            bad = j;
            memset(bad.files[1].path, 'a', sizeof(bad.files[1].path));
            assert(!valid(bad));
            // A foreign startup owner is never overwritten during continuation.
            const char *runkey = "software\\microsoft\\windows\\currentversion\\run";
            fake_win32::keys[runkey]["dreamgpu.driver"] = {REG_SZ, {'x', 0}};
            assert(!store.arm_resume(j, false));
            fake_win32::keys[runkey].clear();
            // A previously resumed timed-out child prevents another transaction.
            j.child_pid = 9000;
            assert(store.persist(j));
            driver_fake::live = true;
            driver_fake::executable = std::string(root) + "\\driver-backup\\DGDBIND.EXE";
            fake_win32::files.erase(
                fake_win32::canon((std::string(root) + "\\driver-child.bin").c_str()));
            // VGA restoration may not own a backup binder: use the captured
            // forward binder for this legacy pending-child identity test.
            driver_fake::executable =
                std::string(root) +
                (os == Os::nt5 ? "\\drivers\\nt5\\DGDBIND.EXE" : "\\drivers\\win98\\DGDBIND.EXE");
            assert(store.load(bad) && bad.child_pid == 9000);
            assert(store.observe(bad) == Actual::pending && !store.verify_readonly(bad, true));
            driver_fake::live = false;
            fake_win32::enforce_file_sharing = true;
            assert(store.load(bad));
            assert(!bad.child_pid);
            fake_win32::enforce_file_sharing = false;
        }
    printf("PASS actual driver adapter: Win98/NT5 VGA+upgrade restore, %u mutation failure points, "
           "exact-INF ambiguity, ownership and live-child guards\n",
           failures);
    return 0;
}
