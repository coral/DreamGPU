// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "global-record.h"
#include "lifecycle-runtime.h"
#include "runtime-resume.h"
#include "global-recovery.h"
#ifndef DG_GLOBAL_RUNTIME_TEST
#include "system-runtime.h"
#include "driver-runtime.h"
#endif
namespace setup::global {
enum class Request { start, resume, rollback, uninstall, upgrade, repair, recover };
enum class Presence { absent, present, error };
inline bool journal_path(char (&out)[MAX_PATH]) {
    if (!lifecycle::owner_path(out) || lstrlenA(out) >= MAX_PATH - 12)
        return false;
    lstrcatA(out, "\\GLOBAL.JRN");
    return true;
}
inline Presence presence() {
    char path[MAX_PATH];
    if (!journal_path(path))
        return Presence::error;
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? Presence::absent
                                                                              : Presence::error;
    }
    return attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)
               ? Presence::error
               : Presence::present;
}
inline bool terminal(const Flow &flow) {
    return terminal(flow.phase);
}
#ifndef DG_GLOBAL_RUNTIME_TEST
inline lifecycle::Result component(driver::Result result, bool undo) {
    using R = lifecycle::Result;
    if ((!undo && result == driver::Result::verified) ||
        (undo && result == driver::Result::restored))
        return R::complete;
    if (result == driver::Result::pending_reboot)
        return R::pending_reboot;
    if (result == driver::Result::conflict)
        return R::conflict;
    if (result == driver::Result::invalid)
        return R::invalid;
    return R::io_error;
}
// Only independently journalled component gateways are substituted by the
// global adapter test. File/registry journals, RunOnce and coordinator are real.
struct NativeComponents {
    static bool ready(Os os) {
        return providers(os).ready();
    }
    static lifecycle::RepairAssessment repair_assessment(lifecycle::Win32Store &store,
                                                         const lifecycle::Journal &j) {
        return lifecycle::assess_repair(store, j);
    }
    static lifecycle::Result uninstall_assessment(lifecycle::Win32Store &store,
                                                  const lifecycle::Journal &j) {
        return lifecycle::assess_uninstall(store, j);
    }
    static bool system_journal(const lifecycle::Journal &j) {
        return lifecycle::is_system_journal(j);
    }
    static bool driver_query(Os os, DriverIdentity &out) {
        driver::GenerationInfo info{};
        auto result = driver::query_generation(os, info);
        if (result == driver::GenerationRead::error)
            return false;
        out = {};
        if (result == driver::GenerationRead::present) {
            out.id = info.id;
            out.parent = info.parent;
            out.baseline = info.baseline;
            out.phase = info.phase;
            for (unsigned n = 0; n < sizeof(out.installer_sha); ++n)
                out.installer_sha[n] = info.installer_sha[n];
        }
        return valid_driver(out);
    }
    static bool driver_verify(Os os, const DriverIdentity &current, bool original) {
        driver::GenerationInfo info{};
        info.id = current.id;
        info.parent = current.parent;
        info.baseline = current.baseline;
        info.phase = current.phase;
        for (unsigned n = 0; n < sizeof(info.installer_sha); ++n)
            info.installer_sha[n] = current.installer_sha[n];
        return current.id && driver::verify_generation(os, info, original);
    }
    template <class Payloads>
    static lifecycle::Result driver_act(Os os, int action, const Payloads &payloads,
                                        const Recovery *recovery = nullptr) {
        driver::ReverseExecutor executor{recovery ? recovery->executable() : nullptr,
                                         recovery ? recovery->sha() : nullptr};
        return component(driver::act(os, action, payloads, recovery ? &executor : nullptr, true),
                         action == 2 || action == 4);
    }
    static bool driver_recovery_ready(Os os) {
        return driver::reverse_inputs_ready(os);
    }
    static bool retire_driver_resume(Os os) {
        return driver::retire_reverse_resume(os);
    }
    template <class Payloads>
    static lifecycle::Result providers_act(Os os, lifecycle::Action action,
                                           const Payloads &payloads) {
        return lifecycle::system_act(os, action, payloads);
    }
};
#endif

template <class Payloads> class Win32Store {
    lifecycle::Win32Store &owner_;
    DurableRecord<Record> &disk_;
    Record &record_;
    const Payloads &payloads_;
    const Recovery *recovery_ = nullptr;
    bool runtime(lifecycle::Journal &j) {
        return owner_.load(j) && NativeComponents::system_journal(j);
    }
    bool original_providers(const lifecycle::Journal &j) {
        if ((record_.origin_intent != Intent::upgrade && record_.origin_intent != Intent::repair) ||
            j.generation != record_.origin_provider_generation ||
            j.uninstall != record_.origin_provider_uninstall ||
            j.state != record_.origin_provider_state || !provider_terminal(j.state, j.uninstall))
            return false;
        if (record_.origin_intent == Intent::repair)
            return NativeComponents::repair_assessment(owner_, j).result ==
                   lifecycle::Result::complete;
        for (unsigned n = 0; n < j.count; ++n)
            if (owner_.classify(j, n, j.state == lifecycle::State::failed) !=
                lifecycle::Actual::after)
                return false;
        return true;
    }
    lifecycle::Result driver_identity(DriverIdentity &current, bool &owned) {
        using R = lifecycle::Result;
        owned = false;
        if (!NativeComponents::driver_query(record_.os, current) || !valid_driver(current))
            return R::io_error;
        if (record_.driver_target.id) {
            owned = same_driver(current, record_.driver_target);
            return owned ? R::complete : R::conflict;
        }
        if (same_driver(current, record_.driver_before))
            return R::complete;
        Record observed = record_;
        observed.driver_target = current;
        // The driver gateway may publish its target then lose acknowledgement.
        // Adopt only this operation's checked child, never another installation.
        if (!current.id || record_.flow.intent == Intent::uninstall ||
            current.id <= record_.driver_before.id || !valid_record(observed))
            return R::conflict;
        if (!disk_.save(observed, valid_record))
            return R::io_error;
        record_ = observed;
        owned = true;
        return R::complete;
    }
    bool unchanged_driver(const DriverIdentity &current) {
        if (!same_driver(current, record_.driver_before) ||
            current.phase != record_.driver_before.phase)
            return false;
        if (!current.id)
            return true; // No driver journal means this operation never mutated it.
        if (current.phase != driver::Phase::verified && current.phase != driver::Phase::restored)
            return false;
        return NativeComponents::driver_verify(record_.os, current,
                                               current.phase == driver::Phase::restored);
    }
    bool retain_driver(const DriverIdentity &current) {
        Record next = record_;
        next.driver_target = current;
        if (!valid_record(next) || !disk_.save(next, valid_record))
            return false;
        record_ = next;
        return true;
    }
    lifecycle::Result driver_step(int action) {
        lifecycle::RuntimeResume resume(owner_, record_.flow.epoch, record_.os,
                                        recovery_ ? lifecycle::ResumeScope::recovery
                                                  : lifecycle::ResumeScope::global);
        if (!resume.armed())
            return lifecycle::Result::conflict;
        auto result = NativeComponents::driver_act(record_.os, action, payloads_, recovery_);
        DriverIdentity current;
        bool owned = false;
        auto identity = driver_identity(current, owned);
        if (identity != lifecycle::Result::complete)
            return identity;
        if (result == lifecycle::Result::complete && !owned)
            return lifecycle::Result::conflict;
        return result;
    }
    bool verified_driver(bool reverse) {
        DriverIdentity current;
        if (!NativeComponents::driver_query(record_.os, current))
            return false;
        if (!record_.driver_target.id)
            return (reverse || current.id) && unchanged_driver(current);
        return same_driver(current, record_.driver_target) &&
               current.phase == (reverse ? driver::Phase::restored : driver::Phase::verified) &&
               NativeComponents::driver_verify(record_.os, current, reverse);
    }
    bool directory() {
        char suffix[24], path[MAX_PATH];
        wsprintfA(suffix, "\\G%08lX", record_.flow.epoch);
        if (!owner_.private_path(path, suffix))
            return false;
        DWORD attributes = GetFileAttributesA(path);
        if (attributes == INVALID_FILE_ATTRIBUTES)
            return GetLastError() == ERROR_FILE_NOT_FOUND && CreateDirectoryA(path, nullptr);
        return (attributes & FILE_ATTRIBUTE_DIRECTORY) &&
               !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
    }

  public:
    Win32Store(lifecycle::Win32Store &owner, DurableRecord<Record> &disk, Record &record,
               const Payloads &payloads, const Recovery *recovery = nullptr)
        : owner_(owner), disk_(disk), record_(record), payloads_(payloads), recovery_(recovery) {}
    bool persist(const Flow &flow) {
        Record next = record_;
        next.flow = flow;
        if (!disk_.save(next, valid_record))
            return false;
        record_ = next;
        return true;
    }
    bool arm() {
        if (!recovery_ && !directory())
            return false;
        lifecycle::RuntimeResume resume(owner_, record_.flow.epoch, record_.os,
                                        recovery_ ? lifecycle::ResumeScope::recovery
                                                  : lifecycle::ResumeScope::global);
        return resume.prepare() && resume.arm();
    }
    bool disarm() {
        lifecycle::RuntimeResume resume(owner_, record_.flow.epoch, record_.os,
                                        recovery_ ? lifecycle::ResumeScope::recovery
                                                  : lifecycle::ResumeScope::global);
        return resume.prepare() && resume.finish();
    }
    lifecycle::RepairAssessment repair_origin() {
        static lifecycle::Journal j;
        if (record_.flow.phase != Phase::activated || !verified_driver(false) || !runtime(j) ||
            j.generation != record_.flow.provider_generation)
            return {};
        return NativeComponents::repair_assessment(owner_, j);
    }
    lifecycle::Result driver_apply(Intent intent) {
        if (intent == Intent::repair)
            return lifecycle::Result::invalid;
        DriverIdentity current;
        bool owned = false;
        auto result = driver_identity(current, owned);
        if (result != lifecycle::Result::complete)
            return result;
        if (owned)
            return driver_step(1);
        if (current.id && current.phase == driver::Phase::verified && intent == Intent::install &&
            lifecycle::text_equal(current.installer_sha, record_.driver_installer_sha, 68))
            return unchanged_driver(current) ? lifecycle::Result::complete
                                             : lifecycle::Result::conflict;
        if (current.id && current.phase != driver::Phase::verified &&
            current.phase != driver::Phase::restored) {
            if (!lifecycle::text_equal(current.installer_sha, record_.driver_installer_sha, 68))
                return lifecycle::Result::conflict;
            if (!retain_driver(current))
                return lifecycle::Result::io_error;
            return driver_step(1);
        }
        return driver_step(current.id ? 3 : 0);
    }
    lifecycle::Result driver_restore(Intent intent) {
        DriverIdentity current;
        bool owned = false;
        auto result = driver_identity(current, owned);
        if (result != lifecycle::Result::complete)
            return result;
        if (intent == Intent::uninstall) {
            if (!current.id)
                return unchanged_driver(current) ? lifecycle::Result::complete
                                                 : lifecycle::Result::conflict;
            if (!owned && !retain_driver(current))
                return lifecycle::Result::io_error;
            return driver_step(4);
        }
        if (!owned)
            return unchanged_driver(current) ? lifecycle::Result::complete
                                             : lifecycle::Result::conflict;
        return driver_step(2);
    }
    lifecycle::Result providers_apply(Intent intent, uint32_t generation) {
        static lifecycle::Journal j;
        if (!runtime(j))
            return lifecycle::Result::invalid;
        auto action = lifecycle::Action::continue_install;
        if ((intent == Intent::upgrade || intent == Intent::repair) && j.generation != generation) {
            if (j.generation != record_.origin_provider_generation || j.generation == UINT32_MAX ||
                j.generation + 1 != generation || !original_providers(j))
                return lifecycle::Result::conflict;
            if (intent == Intent::repair && !NativeComponents::repair_assessment(owner_, j).changed)
                return lifecycle::Result::conflict;
            action =
                intent == Intent::repair ? lifecycle::Action::repair : lifecycle::Action::upgrade;
        } else if (j.generation != generation || j.uninstall)
            return lifecycle::Result::conflict;
        return NativeComponents::providers_act(record_.os, action, payloads_);
    }
    lifecycle::Result providers_restore(Intent intent, uint32_t generation) {
        static lifecycle::Journal j;
        if (!runtime(j))
            return lifecycle::Result::invalid;
        if (intent == Intent::rollback) {
            if (j.generation != generation)
                return original_providers(j) ? lifecycle::Result::complete
                                             : lifecycle::Result::conflict;
            return NativeComponents::providers_act(record_.os, lifecycle::Action::rollback,
                                                   payloads_);
        }
        if (intent != Intent::uninstall)
            return lifecycle::Result::invalid;
        auto action = lifecycle::Action::continue_install;
        if (j.generation != generation) {
            if (j.generation != record_.origin_provider_generation || j.generation == UINT32_MAX ||
                j.generation + 1 != generation || j.uninstall)
                return lifecycle::Result::conflict;
            const auto assessment = NativeComponents::uninstall_assessment(owner_, j);
            if (assessment != lifecycle::Result::complete)
                return assessment;
            action = lifecycle::Action::uninstall;
        } else if (!j.uninstall)
            return lifecycle::Result::conflict;
        return NativeComponents::providers_act(record_.os, action, payloads_);
    }
    bool providers_verified(const Flow &flow) {
        static lifecycle::Journal j;
        if (!runtime(j))
            return false;
        const bool untouched = flow.intent == Intent::rollback &&
                               j.generation != flow.provider_generation && original_providers(j);
        if (!untouched) {
            if (j.generation != flow.provider_generation ||
                j.uninstall != unsigned(flow.intent == Intent::uninstall))
                return false;
            const auto expected = flow.intent == Intent::rollback    ? lifecycle::State::failed
                                  : flow.intent == Intent::uninstall ? lifecycle::State::removed
                                                                     : lifecycle::State::activated;
            if (j.state != expected)
                return false;
            for (unsigned n = 0; n < j.count; ++n)
                if (owner_.classify(j, n, flow.intent == Intent::rollback) !=
                    lifecycle::Actual::after)
                    return false;
        }
        return true;
    }
    bool terminal_verify(const Flow &flow) {
        return providers_verified(flow) &&
               verified_driver(flow.intent == Intent::rollback || flow.intent == Intent::uninstall);
    }
};
template <class Payloads>
lifecycle::Result act(Os os, Request request, const Payloads &payloads, Intent &result_intent) {
    using Result = lifecycle::Result;
    if ((os != Os::win98 && os != Os::nt5) || uint32_t(request) > uint32_t(Request::recover))
        return Result::invalid;
    lifecycle::Win32Store owner;
    if (!lifecycle::verified_owner(owner))
        return Result::invalid;
    char path[MAX_PATH], self[MAX_PATH];
    if (!journal_path(path))
        return Result::invalid;
    DurableRecord<Record> disk(path);
    static Record record;
    bool exists = false;
    if (!disk.load(record, exists, valid_record) || (exists && record.os != os))
        return Result::invalid;
    lifecycle::Image executing;
    DWORD size = GetModuleFileNameA(nullptr, self, sizeof(self));
    if (!size || size >= sizeof(self) || !owner.inspect_file(self, executing) || !executing.exists)
        return Result::invalid;
    const bool same_installer = exists && lifecycle::same(executing, record.installer);
    Recovery recovery(owner, exists ? record.flow.epoch : 0);
    bool recovered = false;
    if (exists &&
        (request == Request::recover || (request == Request::resume && !same_installer))) {
        if (record.os != Os::nt5 ||
            (record.flow.phase != Phase::restore_driver && record.flow.phase != Phase::restored) ||
            (record.flow.intent != Intent::rollback && record.flow.intent != Intent::uninstall) ||
            !record.driver_target.id || !recovery.load() ||
            (!recovery.exists() && request != Request::recover))
            return Result::conflict;
        Win32Store before(owner, disk, record, payloads);
        DriverIdentity current;
        if (!before.providers_verified(record.flow) ||
            (record.flow.phase == Phase::restore_driver &&
             !NativeComponents::driver_recovery_ready(os)) ||
            !NativeComponents::driver_query(os, current) ||
            !same_driver(current, record.driver_target))
            return Result::conflict;
        char suffix[40], old_program[MAX_PATH];
        lifecycle::Image old;
        wsprintfA(suffix, "\\G%08lX\\setup.exe", record.flow.epoch);
        if (!owner.private_path(old_program, suffix) || !owner.inspect_file(old_program, old) ||
            !lifecycle::same(old, record.installer) ||
            !recovery.prepare(record, executing, record.flow.phase != Phase::restored))
            return Result::conflict;
        if (!recovery.active()) {
            // The new arm is durable before retiring the exact old startup
            // commands. Interrupted retirement resumes from this same receipt.
            lifecycle::RuntimeResume prior(owner, record.flow.epoch, os,
                                           lifecycle::ResumeScope::global);
            if (!prior.prepare() || !prior.finish() ||
                !NativeComponents::retire_driver_resume(os) || !recovery.publish())
                return Result::io_error;
        }
        recovered = true;
        request = Request::resume;
    }
    if (request == Request::recover)
        return Result::invalid;
    if (request == Request::start && exists)
        request = terminal(record.flow) && (!same_installer || record.flow.phase == Phase::restored)
                      ? Request::upgrade
                  : record.flow.phase == Phase::activated ? Request::repair
                                                          : Request::resume;
    if (!exists && request != Request::start)
        return Result::invalid;
    if (exists && request == Request::resume && !same_installer && !recovered)
        return Result::conflict;
    bool begin = !exists || request == Request::rollback || request == Request::uninstall ||
                 request == Request::upgrade || request == Request::repair;
    if (exists && !terminal(record.flow)) {
        // Reverse the currently owned operation, not a new RunOnce generation:
        // otherwise its previous arm would be captured and resurrected later.
        if ((!same_installer && !recovered) ||
            (request != Request::resume && request != Request::rollback &&
             !(request == Request::uninstall && record.flow.intent == Intent::uninstall) &&
             !(request == Request::repair && record.flow.intent == Intent::repair)))
            return Result::conflict;
        if (request == Request::rollback && record.flow.intent != Intent::rollback) {
            if (!forward(record.flow.intent))
                return Result::conflict;
            Record reverse = record;
            reverse.flow.intent = Intent::rollback;
            reverse.flow.phase = Phase::restore_providers;
            if (!disk.save(reverse, valid_record))
                return Result::io_error;
            record = reverse;
        }
        begin = false;
    }
    if (exists && terminal(record.flow) &&
        ((request == Request::rollback && record.flow.intent == Intent::rollback) ||
         (request == Request::uninstall && record.flow.intent == Intent::uninstall))) {
        if (!same_installer)
            return Result::conflict;
        begin = false;
    }
    if ((begin && request != Request::rollback && request != Request::uninstall) ||
        (!begin && forward(record.flow.intent) && !terminal(record.flow))) {
        if (!NativeComponents::ready(os))
            return Result::provider_not_ready;
    }
    if (begin) {
        // Complete the old arm before capturing the next epoch's prior value.
        // This must also work from a new upgrade executable: the prior frozen
        // program and its immutable hash remain in the old epoch directory.
        if (exists) {
            if (!terminal(record.flow) || record.flow.epoch == UINT32_MAX)
                return Result::invalid;
            Win32Store prior(owner, disk, record, payloads);
            Flow previous = record.flow;
            Engine finish(prior, previous);
            Result result;
            if (request == Request::repair) {
                auto assessment = prior.repair_origin();
                if (assessment.result != Result::complete)
                    return assessment.result;
                // Only the explicitly recognized OS-original drift may bypass
                // desired-provider verification while closing the old arm.
                result = prior.disarm() ? Result::complete : Result::io_error;
                if (result == Result::complete && !assessment.changed) {
                    result_intent = Intent::repair;
                    return Result::complete; // No work, no new epoch/generation.
                }
            } else
                result = finish.resume();
            if (result != Result::complete)
                return result;
        }
        static lifecycle::Journal runtime;
        if (!owner.load(runtime) || !NativeComponents::system_journal(runtime))
            return Result::invalid;
        if (request == Request::start &&
            (runtime.uninstall || runtime.state == lifecycle::State::rolling_back ||
             runtime.state == lifecycle::State::failed ||
             runtime.state == lifecycle::State::removed))
            return Result::conflict;
        if (request == Request::uninstall) {
            const auto assessment = NativeComponents::uninstall_assessment(owner, runtime);
            if (assessment != Result::complete)
                return assessment;
        }
        Record next;
        next.os = os;
        next.installer = executing;
        if (!NativeComponents::driver_query(os, next.driver_before) ||
            !valid_driver(next.driver_before))
            return Result::conflict;
        for (unsigned n = 0; n < sizeof(next.driver_installer_sha); ++n)
            next.driver_installer_sha[n] = executing.sha[n];
        next.flow.epoch = exists ? record.flow.epoch + 1 : 1;
        next.flow.intent = request == Request::rollback    ? Intent::rollback
                           : request == Request::uninstall ? Intent::uninstall
                           : request == Request::upgrade   ? Intent::upgrade
                           : request == Request::repair    ? Intent::repair
                                                           : Intent::install;
        if (next.flow.intent == Intent::rollback) {
            // Rollback of a terminal activation still refers to that exact
            // origin/target, rather than inventing a new provider operation.
            if (!exists || record.flow.phase != Phase::activated)
                return Result::invalid;
            next.driver_before = record.driver_before;
            next.driver_target = record.driver_target;
            for (unsigned n = 0; n < sizeof(next.driver_installer_sha); ++n)
                next.driver_installer_sha[n] = record.driver_installer_sha[n];
            next.origin_intent = record.origin_intent;
            next.origin_provider_generation = record.origin_provider_generation;
            next.origin_provider_state = record.origin_provider_state;
            next.origin_provider_uninstall = record.origin_provider_uninstall;
            next.flow.provider_generation = record.flow.provider_generation;
        } else {
            next.origin_intent = next.flow.intent;
            next.origin_provider_generation = runtime.generation;
            next.origin_provider_state = runtime.state;
            next.origin_provider_uninstall = runtime.uninstall;
            const bool increment = next.flow.intent == Intent::upgrade ||
                                   next.flow.intent == Intent::uninstall ||
                                   next.flow.intent == Intent::repair;
            if (increment && (runtime.generation == UINT32_MAX ||
                              !provider_terminal(runtime.state, runtime.uninstall)))
                return Result::invalid;
            if (next.flow.intent == Intent::uninstall &&
                ((runtime.state != lifecycle::State::activated &&
                  runtime.state != lifecycle::State::failed) ||
                 runtime.uninstall))
                return Result::invalid;
            next.flow.provider_generation = runtime.generation + unsigned(increment);
        }
        next.flow.phase =
            next.flow.intent == Intent::rollback || next.flow.intent == Intent::uninstall
                ? Phase::restore_providers
            : next.flow.intent == Intent::repair ? Phase::providers
                                                 : Phase::driver;
        char suffix[24], directory[MAX_PATH];
        wsprintfA(suffix, "\\G%08lX", next.flow.epoch);
        if (!owner.private_path(directory, suffix) ||
            GetFileAttributesA(directory) != INVALID_FILE_ATTRIBUTES ||
            GetLastError() != ERROR_FILE_NOT_FOUND)
            return Result::conflict;
        if (!disk.save(next, valid_record))
            return Result::io_error;
        record = next;
    }
    result_intent = record.flow.intent;
    Win32Store store(owner, disk, record, payloads, recovered ? &recovery : nullptr);
    Flow flow = record.flow;
    Engine engine(store, flow);
    return engine.resume();
}
} // namespace setup::global
