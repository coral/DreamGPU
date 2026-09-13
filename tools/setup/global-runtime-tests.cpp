// SPDX-License-Identifier: GPL-2.0-or-later
#define DG_SETUP_ADAPTER_TEST
#define DG_GLOBAL_RUNTIME_TEST
#include "test-win32.h"
#include <cassert>
static std::string executing = "C:\\WINDOWS\\DreamGPU\\setup.exe";
static DWORD GetWindowsDirectoryA(char *out, DWORD cap) {
    constexpr char path[] = "C:\\WINDOWS";
    if (cap < sizeof(path))
        return cap;
    strcpy(out, path);
    return sizeof(path) - 1;
}
static DWORD GetModuleFileNameA(void *, char *out, DWORD cap) {
    if (cap <= executing.size())
        return cap;
    strcpy(out, executing.c_str());
    return DWORD(executing.size());
}
constexpr DWORD ERROR_PATH_NOT_FOUND = 3;
#include "global-record.h"
#include "lifecycle-runtime.h"
#include "runtime-resume.h"
#include "global-recovery.h"
namespace setup::global {
struct NativeComponents {
    static inline bool available = true, driver_desired = false, driver_pause = false;
    static inline bool provider_pause = false, created_pause = false;
    static inline unsigned driver_calls = 0, provider_calls = 0, undo_calls = 0, creations = 0;
    static bool ready(Os) {
        return available;
    }
    static lifecycle::RepairAssessment repair_assessment(lifecycle::Win32Store &store,
                                                         const lifecycle::Journal &j) {
        lifecycle::RepairAssessment result;
        if (!system_journal(j) || j.state != lifecycle::State::activated || j.uninstall)
            return result;
        result.result = lifecycle::Result::complete;
        for (unsigned n = 0; n < j.count; ++n) {
            if (store.classify(j, n, false) == lifecycle::Actual::after)
                continue;
            if (!j.items[n].original.exists || !store.matches_original(j.items[n])) {
                result.result = lifecycle::Result::conflict;
                return result;
            }
            result.changed |= 1u << n;
        }
        return result;
    }
    static lifecycle::Result uninstall_assessment(lifecycle::Win32Store &store,
                                                  const lifecycle::Journal &j) {
        if (j.uninstall ||
            (j.state != lifecycle::State::activated && j.state != lifecycle::State::failed))
            return lifecycle::Result::invalid;
        const bool restored = j.state == lifecycle::State::failed;
        for (unsigned n = 0; n < j.count; ++n) {
            if (store.classify(j, n, restored) != lifecycle::Actual::after)
                return lifecycle::Result::conflict;
            if (restored &&
                (j.generation <= 1 || lifecycle::same(j.items[n].before, j.items[n].original)))
                return lifecycle::Result::invalid; // Component seam: no installed parent.
        }
        return lifecycle::Result::complete;
    }
    static bool system_journal(const lifecycle::Journal &j) {
        if (j.count != 3)
            return false;
        for (unsigned n = 0; n < 3; ++n)
            if (lstrcmpA(j.items[n].path, public_runtime[n]))
                return false;
        return true;
    }
    static inline DriverIdentity generation{};
    static inline bool prior_driver_desired = false, restore_baseline = false,
                       driver_created_pause = false;
    static inline bool reverse_pause = false;
    static bool driver_recovery_ready(Os) {
        return true;
    }
    static bool retire_driver_resume(Os) {
        return !fake_win32::fault();
    }
    static inline unsigned driver_creations = 0, driver_rollbacks = 0, driver_uninstalls = 0;
    static bool driver_query(Os, DriverIdentity &out) {
        out = generation;
        return true;
    }
    static bool driver_verify(Os, const DriverIdentity &current, bool original) {
        if (!same_driver(current, generation) || current.phase != generation.phase)
            return false;
        return driver_desired ==
               (original ? (restore_baseline ? false : prior_driver_desired) : true);
    }
    template <class P>
    static lifecycle::Result driver_act(Os, int action, const P &, const Recovery * = nullptr) {
        ++driver_calls;
        if (driver_pause && (action == 0 || action == 3))
            return lifecycle::Result::pending_reboot;
        if (fake_win32::fault())
            return lifecycle::Result::io_error;
        if (action == 0 || action == 3) {
            ++driver_creations;
            DriverIdentity next{};
            next.id = generation.id + 1;
            next.parent = generation.id;
            next.baseline = driver_desired ? generation.baseline : next.id;
            next.phase = driver::Phase::captured;
            const auto &bytes = fake_win32::files.at(fake_win32::canon(executing.c_str())).bytes;
            Sha256 hash;
            hash.update(bytes.data(), bytes.size());
            hash.finish(next.installer_sha);
            prior_driver_desired = driver_desired;
            restore_baseline = false;
            generation = next;
            if (driver_created_pause)
                return lifecycle::Result::pending_reboot;
        }
        if (action == 2 || action == 4) {
            if (reverse_pause)
                return lifecycle::Result::pending_reboot;
            if (action == 2)
                ++driver_rollbacks;
            else
                ++driver_uninstalls;
            restore_baseline = action == 4;
            driver_desired = restore_baseline ? false : prior_driver_desired;
            generation.phase = driver::Phase::restored;
        } else {
            driver_desired = true;
            generation.phase = driver::Phase::verified;
        }
        return lifecycle::Result::complete;
    }
    template <class P>
    static lifecycle::Result providers_act(Os, lifecycle::Action action, const P &);
};
} // namespace setup::global
#include "global-runtime.h"
using namespace setup;
using namespace setup::global;
using Result = lifecycle::Result;
constexpr char Owner[] = "C:\\WINDOWS\\DreamGPU";
constexpr char RunKey[] = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr char Prior[] = "foreign-original-command";
static void put(const std::string &path, const std::string &bytes) {
    fake_win32::files[fake_win32::canon(path.c_str())].bytes.assign(bytes.begin(), bytes.end());
}
static std::string target(const char *name) {
    return std::string("C:\\WINDOWS\\SYSTEM\\") + name;
}
static lifecycle::Image image(const std::string &text) {
    lifecycle::Image i;
    i.exists = 1;
    i.size = uint32_t(text.size());
    Sha256 h;
    h.update(reinterpret_cast<const BYTE *>(text.data()), text.size());
    h.finish(i.sha);
    return i;
}
static std::string runonce() {
    const auto &key = fake_win32::keys.at(fake_win32::canon(RunKey));
    auto it = key.find("dreamgpu.setup");
    if (it == key.end())
        return {};
    const auto &v = it->second.bytes;
    return {reinterpret_cast<const char *>(v.data()), v.size() - 1};
}
static void prior() {
    auto i = lifecycle::string_value(Prior);
    fake_win32::keys[fake_win32::canon(RunKey)]["dreamgpu.setup"] = {i.type,
                                                                     {i.value, i.value + i.size}};
}
static void initial(bool activated = false) {
    fake_win32::reset();
    executing = "C:\\WINDOWS\\DreamGPU\\setup.exe";
    NativeComponents::generation = {};
    NativeComponents::reverse_pause = false;
    NativeComponents::prior_driver_desired = NativeComponents::restore_baseline =
        NativeComponents::driver_created_pause = false;
    NativeComponents::driver_creations = NativeComponents::driver_rollbacks =
        NativeComponents::driver_uninstalls = 0;
    NativeComponents::available = true;
    NativeComponents::driver_desired = activated;
    NativeComponents::driver_pause = NativeComponents::provider_pause =
        NativeComponents::created_pause = false;
    NativeComponents::driver_calls = NativeComponents::provider_calls =
        NativeComponents::undo_calls = NativeComponents::creations = 0;
    fake_win32::files[fake_win32::canon(Owner)].directory = true;
    put(executing, "installer-one");
    put(std::string(Owner) + "\\RESULT.json", "{\"schema\":1,\"state\":\"staged\",\"system_"
                                              "activated\":false,\"provider\":\"not_ready\"}\r\n");
    prior();
    lifecycle::Win32Store store;
    assert(lifecycle::verified_owner(store));
    lifecycle::Journal j;
    j.count = 3;
    j.generation = 1;
    j.state = activated ? lifecycle::State::activated : lifecycle::State::staged;
    assert(store.create_generation(1));
    for (unsigned n = 0; n < 3; ++n) {
        auto &i = j.items[n];
        strcpy(i.path, public_runtime[n]);
        i.resource = 100 + n;
        put(target(i.path), "old");
        i.desired = image("new");
        assert(store.capture(j, n));
        if (activated) {
            put(target(i.path), "new");
            i.phase = lifecycle::Phase::applied;
        }
    }
    assert(store.persist(j));
    fake_win32::mutation = 0;
}
static Record global_record() {
    char p[MAX_PATH];
    assert(journal_path(p));
    DurableRecord<Record> log(p);
    Record r;
    bool exists = false;
    assert(log.load(r, exists, valid_record) && exists);
    return r;
}
static lifecycle::Journal runtime_record() {
    lifecycle::Win32Store store;
    lifecycle::Journal j;
    assert(lifecycle::verified_owner(store) && store.load(j));
    return j;
}
static Result invoke(Request request, Intent *out = nullptr) {
    Intent ignored = Intent::install;
    auto result = act(Os::win98, request, 0, out ? *out : ignored);
    assert(fake_win32::handles.empty() && fake_win32::key_handles.empty());
    return result;
}
template <class P> Result NativeComponents::providers_act(Os, lifecycle::Action action, const P &) {
    using namespace lifecycle;
    ++provider_calls;
    if (provider_pause)
        return Result::pending_reboot;
    lifecycle::Win32Store store;
    Journal j;
    if (!verified_owner(store) || !store.load(j))
        return Result::invalid;
    if (action == Action::upgrade || action == Action::uninstall || action == Action::repair) {
        const bool inherited = j.state == State::activated && !j.uninstall;
        assert(inherited || ((action == Action::upgrade || action == Action::uninstall) &&
                             provider_terminal(j.state, j.uninstall)));
        const bool from_failed = j.state == State::failed;
        ++creations;
        ++j.generation;
        j.uninstall = action == Action::uninstall;
        for (unsigned n = 0; n < j.count; ++n) {
            auto actual = from_failed ? j.items[n].before : j.items[n].desired;
            if (action == Action::repair)
                assert(store.inspect_file(target(j.items[n].path).c_str(), actual));
            j.items[n].before = actual;
            if (!inherited && action != Action::uninstall)
                j.items[n].original = actual;
            j.items[n].desired = j.uninstall ? j.items[n].original : image("new");
        }
        j.state = State::applying;
        if (!store.persist(j))
            return Result::io_error;
        if (created_pause)
            return Result::pending_reboot;
    }
    if (action == Action::rollback)
        ++undo_calls;
    const bool undo = action == Action::rollback;
    for (unsigned n = 0; n < j.count; ++n) {
        const auto &expected = undo ? j.items[n].before : j.items[n].desired;
        put(target(j.items[n].path), same(expected, image("old")) ? "old" : "new");
        j.items[n].phase = undo ? lifecycle::Phase::restored : lifecycle::Phase::applied;
    }
    j.state = undo ? State::failed : j.uninstall ? State::removed : State::activated;
    return store.persist(j) ? Result::complete : Result::io_error;
}
static void install_failures() {
    initial();
    assert(invoke(Request::start) == Result::complete);
    unsigned total = fake_win32::mutation;
    assert(runonce() == Prior);
    for (unsigned n = 1; n <= total; ++n) {
        initial();
        fake_win32::fail = n;
        assert(invoke(Request::start) != Result::complete);
        fake_win32::fail = 0;
        assert(invoke(Request::start) == Result::complete);
        assert(global_record().flow.phase == Phase::activated && runonce() == Prior);
        assert(NativeComponents::driver_creations == 1);
        unsigned drivers = NativeComponents::driver_calls,
                 providers = NativeComponents::provider_calls;
        assert(invoke(Request::resume) == Result::complete);
        assert(drivers == NativeComponents::driver_calls &&
               providers == NativeComponents::provider_calls);
    }
    printf("PASS global Win32 install: %u syscall/component interruption points\n", total);
}
static void active_rollback() {
    initial();
    NativeComponents::driver_pause = true;
    assert(invoke(Request::start) == Result::pending_reboot);
    auto before = global_record();
    assert(before.flow.epoch == 1 && runonce().find("G00000001") != std::string::npos);
    executing = "C:\\other.exe";
    put(executing, "different-installer");
    assert(invoke(Request::rollback) == Result::conflict);
    executing = "C:\\WINDOWS\\DreamGPU\\setup.exe";
    Intent completed;
    assert(invoke(Request::rollback, &completed) == Result::complete &&
           completed == Intent::rollback);
    auto after = global_record();
    assert(after.flow.epoch == before.flow.epoch &&
           lifecycle::same(after.installer, before.installer));
    assert(runonce() == Prior && after.flow.phase == Phase::restored);
    assert(!fake_win32::files.count(fake_win32::canon("C:\\WINDOWS\\DreamGPU\\G00000002")));
}
static void upgrade_before_provider_rollback() {
    initial();
    assert(invoke(Request::start) == Result::complete);
    executing = "C:\\new.exe";
    put(executing, "installer-two");
    NativeComponents::driver_pause = true;
    assert(invoke(Request::upgrade) == Result::pending_reboot);
    auto before = global_record();
    assert(before.flow.epoch == 2 && before.flow.provider_generation == 2 &&
           before.origin_provider_generation == 1);
    unsigned undo = NativeComponents::undo_calls, providers = NativeComponents::provider_calls;
    assert(invoke(Request::rollback) == Result::complete);
    auto after = global_record();
    assert(after.flow.epoch == 2 && after.flow.provider_generation == 2 &&
           after.flow.phase == Phase::restored);
    auto runtime = runtime_record();
    assert(runtime.generation == 1 && runtime.state == lifecycle::State::activated);
    assert(undo == NativeComponents::undo_calls && providers == NativeComponents::provider_calls &&
           runonce() == Prior);
    assert(invoke(Request::resume) == Result::complete);
}
static void target_generation_resume(bool removal) {
    initial();
    assert(invoke(Request::start) == Result::complete);
    NativeComponents::created_pause = true;
    assert(invoke(removal ? Request::uninstall : Request::upgrade) == Result::pending_reboot);
    assert(runtime_record().generation == 2 && NativeComponents::creations == 1);
    if (removal)
        assert(invoke(Request::rollback) == Result::conflict);
    NativeComponents::created_pause = false;
    assert(invoke(removal ? Request::uninstall : Request::resume) == Result::complete);
    assert(runtime_record().generation == 2 && NativeComponents::creations == 1 &&
           runonce() == Prior);
    if (!removal) {
        assert(invoke(Request::rollback) == Result::complete);
        assert(runtime_record().generation == 2 &&
               runtime_record().state == lifecycle::State::failed);
    }
}
static void terminal_epoch_disarm() {
    initial();
    NativeComponents::driver_pause = true;
    assert(invoke(Request::start) == Result::pending_reboot);
    const std::string old_arm = runonce();
    NativeComponents::driver_pause = false;
    assert(invoke(Request::resume) == Result::complete);
    // Model interruption after terminal journal but before old RunOnce finish.
    auto i = lifecycle::string_value(old_arm.c_str());
    fake_win32::keys[fake_win32::canon(RunKey)]["dreamgpu.setup"] = {i.type,
                                                                     {i.value, i.value + i.size}};
    executing = "C:\\new.exe";
    put(executing, "installer-two");
    assert(invoke(Request::upgrade) == Result::complete);
    assert(global_record().flow.epoch == 2 && runonce() == Prior);
    assert(invoke(Request::resume) == Result::complete && runonce() == Prior);
}
static void rollback_setup(bool upgrade) {
    initial();
    if (upgrade)
        assert(invoke(Request::start) == Result::complete);
    NativeComponents::driver_pause = true;
    assert(invoke(upgrade ? Request::upgrade : Request::start) == Result::pending_reboot);
    fake_win32::mutation = 0;
}
static void rollback_failures() {
    unsigned total = 0;
    for (bool upgrade : {false, true}) {
        rollback_setup(upgrade);
        assert(invoke(Request::rollback) == Result::complete);
        unsigned points = fake_win32::mutation;
        total += points;
        for (unsigned fault = 1; fault <= points; ++fault) {
            rollback_setup(upgrade);
            auto before = global_record();
            fake_win32::fail = fault;
            assert(invoke(Request::rollback) != Result::complete);
            fake_win32::fail = 0;
            assert(invoke(Request::rollback) == Result::complete);
            auto after = global_record();
            assert(after.flow.epoch == before.flow.epoch &&
                   lifecycle::same(before.installer, after.installer) && runonce() == Prior);
            if (upgrade)
                assert(runtime_record().generation == 1 &&
                       runtime_record().state == lifecycle::State::activated &&
                       NativeComponents::undo_calls == 0);
        }
    }
    printf("PASS global rollback: %u syscall/component interruption points\n", total);
}
static void next_epoch_setup() {
    initial();
    NativeComponents::driver_pause = true;
    assert(invoke(Request::start) == Result::pending_reboot);
    auto old_arm = lifecycle::string_value(runonce().c_str());
    NativeComponents::driver_pause = false;
    assert(invoke(Request::resume) == Result::complete);
    fake_win32::keys[fake_win32::canon(RunKey)]["dreamgpu.setup"] = {
        old_arm.type, {old_arm.value, old_arm.value + old_arm.size}};
    executing = R"(C:\new.exe)";
    put(executing, "installer-two");
    fake_win32::mutation = 0;
}
static void next_epoch_failures() {
    next_epoch_setup();
    assert(invoke(Request::upgrade) == Result::complete);
    unsigned points = fake_win32::mutation;
    for (unsigned fault = 1; fault <= points; ++fault) {
        next_epoch_setup();
        fake_win32::fail = fault;
        assert(invoke(Request::upgrade) != Result::complete);
        fake_win32::fail = 0;
        assert(invoke(Request::start) == Result::complete);
        assert(global_record().flow.epoch == 2 && runtime_record().generation == 2 &&
               runonce() == Prior);
    }
    printf("PASS global terminal-to-upgrade: %u syscall/component interruption points\n", points);
}
static void driver_generation_recovery() {
    initial();
    assert(invoke(Request::start) == Result::complete);
    NativeComponents::driver_created_pause = true;
    assert(invoke(Request::upgrade) == Result::pending_reboot);
    auto record = global_record();
    assert(record.driver_before.id == 1 && record.driver_target.id == 2 &&
           record.driver_target.parent == 1);
    assert(runtime_record().generation == 1);
    NativeComponents::driver_created_pause = false;
    assert(invoke(Request::resume) == Result::complete);
    assert(NativeComponents::driver_creations == 2 && runtime_record().generation == 2);
    assert(invoke(Request::rollback) == Result::complete);
    assert(NativeComponents::driver_rollbacks == 1 && NativeComponents::driver_uninstalls == 0 &&
           NativeComponents::driver_desired); // immediate prior installed driver, not baseline
    // Same-installer reinstall after rollback starts a new epoch/generation.
    assert(invoke(Request::start) == Result::complete);
    assert(global_record().driver_before.id == 2 && global_record().driver_target.id == 3 &&
           NativeComponents::generation.baseline == 1 && runtime_record().generation == 3);
    assert(invoke(Request::uninstall) == Result::complete);
    assert(NativeComponents::driver_uninstalls == 1 && !NativeComponents::driver_desired);
    // Fresh cycle after baseline removal gets its own original baseline.
    assert(invoke(Request::start) == Result::complete);
    assert(global_record().driver_target.id == 4 && NativeComponents::generation.baseline == 4);
    assert(invoke(Request::rollback) == Result::complete);
    assert(!NativeComponents::driver_desired && NativeComponents::driver_rollbacks == 2);
}
static void rollback_after_driver_publication() {
    initial();
    assert(invoke(Request::start) == Result::complete);
    NativeComponents::driver_created_pause = true;
    assert(invoke(Request::upgrade) == Result::pending_reboot);
    auto epoch = global_record().flow.epoch;
    assert(invoke(Request::rollback) == Result::complete);
    assert(global_record().flow.epoch == epoch && NativeComponents::driver_rollbacks == 1 &&
           NativeComponents::driver_uninstalls == 0 && NativeComponents::driver_desired &&
           NativeComponents::driver_creations == 2 && runtime_record().generation == 1 &&
           runtime_record().state == lifecycle::State::activated);
    // An unrelated replacement may not be adopted as the expected child.
    initial();
    assert(invoke(Request::start) == Result::complete);
    NativeComponents::driver_pause = true;
    assert(invoke(Request::upgrade) == Result::pending_reboot);
    NativeComponents::generation.id = 2;
    NativeComponents::generation.parent = 0; // wrong operation parent
    assert(invoke(Request::rollback) == Result::conflict);
    assert(!NativeComponents::driver_rollbacks && !NativeComponents::driver_uninstalls);
}
static void reinstall_before_target_rollback() {
    for (bool removal : {false, true}) {
        initial();
        assert(invoke(Request::start) == Result::complete);
        assert(invoke(removal ? Request::uninstall : Request::rollback) == Result::complete);
        const auto provider_before = runtime_record();
        const auto driver_before = NativeComponents::generation;
        const unsigned undo = NativeComponents::driver_rollbacks;
        NativeComponents::driver_pause = true;
        assert(invoke(Request::start) == Result::pending_reboot);
        assert(global_record().flow.intent == Intent::upgrade);
        assert(invoke(Request::rollback) == Result::complete);
        assert(runtime_record().generation == provider_before.generation &&
               runtime_record().state == provider_before.state &&
               same_driver(driver_before, NativeComponents::generation) &&
               NativeComponents::driver_rollbacks == undo && runonce() == Prior);
    }
}
static void fail_closed() {
    initial();
    NativeComponents::available = false;
    assert(invoke(Request::start) == Result::provider_not_ready);
    assert(fake_win32::mutation == 0 && presence() == Presence::absent &&
           NativeComponents::driver_calls == 0);
    NativeComponents::available = true;
    NativeComponents::driver_pause = true;
    assert(invoke(Request::start) == Result::pending_reboot);
    auto &value = fake_win32::keys[fake_win32::canon(RunKey)]["dreamgpu.setup"];
    const std::string foreign = "different-owner";
    value.bytes.assign(foreign.begin(), foreign.end());
    value.bytes.push_back(0);
    assert(invoke(Request::resume) == Result::io_error && runonce() == foreign);
    initial();
    assert(invoke(Request::start) == Result::complete);
    put(target(public_runtime[0]), "tampered");
    assert(invoke(Request::resume) == Result::conflict);
    auto unchanged = global_record().flow.epoch;
    assert(invoke(Request::upgrade) == Result::conflict && global_record().flow.epoch == unchanged);
}
static void repair_setup() {
    initial();
    assert(invoke(Request::start) == Result::complete);
    put(target(public_runtime[0]), "old");
    fake_win32::mutation = 0;
}
static void repair_flow() {
    repair_setup();
    auto driver = NativeComponents::generation;
    auto calls = NativeComponents::driver_calls;
    assert(invoke(Request::resume) == Result::conflict); // readonly continuation cannot repair
    assert(invoke(Request::start) == Result::complete);  // ordinary rerun admits known OS drift
    assert(global_record().flow.intent == Intent::repair && global_record().flow.epoch == 2 &&
           runtime_record().generation == 2 && NativeComponents::creations == 1 &&
           same_driver(driver, NativeComponents::generation) &&
           NativeComponents::driver_calls == calls && runonce() == Prior);
    auto checked = global_record();
    assert(valid_record(checked));
    checked.driver_target = checked.driver_before;
    assert(!valid_record(checked)); // Repair records cannot own/undo a driver generation.
    checked = global_record();
    checked.flow.phase = Phase::driver;
    assert(!valid_record(checked));
    auto providers = NativeComponents::provider_calls;
    assert(invoke(Request::repair) == Result::complete && global_record().flow.epoch == 2 &&
           providers == NativeComponents::provider_calls); // no-op, no extra proof/generation
    assert(invoke(Request::rollback) == Result::complete &&
           NativeComponents::driver_calls == calls && NativeComponents::driver_desired &&
           runtime_record().state == lifecycle::State::failed && runonce() == Prior);
    lifecycle::Win32Store store;
    assert(lifecycle::verified_owner(store));
    auto j = runtime_record();
    assert(store.matches_original(j.items[0])); // repair's recognized drift restored

    repair_setup();
    NativeComponents::provider_pause = true;
    assert(invoke(Request::repair) == Result::pending_reboot);
    auto epoch = global_record().flow.epoch;
    NativeComponents::provider_pause = false;
    calls = NativeComponents::driver_calls;
    assert(invoke(Request::rollback) == Result::complete && global_record().flow.epoch == epoch &&
           runtime_record().generation == 1 && NativeComponents::driver_calls == calls &&
           !NativeComponents::undo_calls && runonce() == Prior);

    repair_setup();
    NativeComponents::created_pause = true;
    assert(invoke(Request::repair) == Result::pending_reboot && runtime_record().generation == 2);
    NativeComponents::created_pause = false;
    calls = NativeComponents::driver_calls;
    assert(invoke(Request::repair) == Result::complete && NativeComponents::creations == 1 &&
           NativeComponents::driver_calls == calls);

    repair_setup();
    put(target(public_runtime[1]), "foreign");
    auto writes = fake_win32::mutation;
    assert(invoke(Request::repair) == Result::conflict && fake_win32::mutation == writes);
    repair_setup();
    NativeComponents::driver_desired = false;
    writes = fake_win32::mutation;
    assert(invoke(Request::repair) != Result::complete && fake_win32::mutation == writes);
}
static void repair_failures() {
    repair_setup();
    assert(invoke(Request::repair) == Result::complete);
    const auto points = fake_win32::mutation;
    for (unsigned fault = 1; fault <= points; ++fault) {
        repair_setup();
        auto driver = NativeComponents::generation;
        const auto driver_calls = NativeComponents::driver_calls;
        fake_win32::fail = fault;
        assert(invoke(Request::repair) != Result::complete);
        fake_win32::fail = 0;
        assert(invoke(Request::repair) == Result::complete);
        assert(global_record().flow.phase == Phase::activated && runtime_record().generation == 2 &&
               same_driver(driver, NativeComponents::generation) &&
               driver_calls == NativeComponents::driver_calls && runonce() == Prior);
    }
    printf("PASS global provider-only repair: %u syscall/component interruption points\n", points);
}
static void rollback_removal_setup() {
    initial();
    assert(invoke(Request::start) == Result::complete);
    executing = "C:\\new.exe";
    put(executing, "installer-two");
    assert(invoke(Request::upgrade) == Result::complete);
    assert(invoke(Request::rollback) == Result::complete);
    assert(runtime_record().state == lifecycle::State::failed);
    fake_win32::mutation = 0;
}
static void rollback_removal() {
    rollback_removal_setup();
    assert(invoke(Request::uninstall) == Result::complete);
    unsigned total = fake_win32::mutation;
    assert(global_record().flow.phase == Phase::restored &&
           global_record().origin_provider_state == lifecycle::State::failed &&
           runtime_record().state == lifecycle::State::removed &&
           !NativeComponents::driver_desired && runonce() == Prior);
    for (unsigned n = 1; n <= total; ++n) {
        rollback_removal_setup();
        fake_win32::fail = n;
        auto result = invoke(Request::uninstall);
        fake_win32::fail = 0;
        if (result != Result::complete)
            assert(invoke(Request::uninstall) == Result::complete);
        assert(runtime_record().state == lifecycle::State::removed &&
               !NativeComponents::driver_desired);
    }
    initial();
    assert(invoke(Request::start) == Result::complete);
    assert(invoke(Request::rollback) == Result::complete);
    assert(invoke(Request::uninstall) == Result::invalid); // No installed parent left.
    printf("PASS GLOBAL uninstall restored upgrade: %u mutation interruptions, first-baseline "
           "refusal\n",
           total);
}
int main() {
    rollback_removal();
    repair_flow();
    repair_failures();
    install_failures();
    active_rollback();
    upgrade_before_provider_rollback();
    target_generation_resume(false);
    target_generation_resume(true);
    terminal_epoch_disarm();
    rollback_failures();
    next_epoch_failures();
    driver_generation_recovery();
    rollback_after_driver_publication();
    reinstall_before_target_rollback();
    fail_closed();
    printf("PASS actual global Win32 adapter: epoch/frozen installer, prior RunOnce, provider "
           "provenance, exact generation resume, terminal readonly and readiness gates\n");
    return 0;
}
