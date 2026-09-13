// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "runtime-boot-store.h"
#include "boot-queue-win32.h"
#include "boot-queue-win98.h"
namespace setup::lifecycle {
class RuntimeFiles {
    Win32Store &store_;
    Journal &journal_;
    Os os_;
    static Result result(boot::Result value) {
        switch (value) {
            case boot::Result::complete:
            case boot::Result::cancelled:
            case boot::Result::needs_restore:
                return Result::complete;
            case boot::Result::pending:
                return Result::pending_reboot;
            case boot::Result::conflict:
                return Result::conflict;
            case boot::Result::invalid:
                return Result::invalid;
            default:
                return Result::io_error;
        }
    }
    template <class Ops>
    static Result execute(Ops &ops, boot::Record &record, bool cancel,
                          RuntimeBootStore *wait = nullptr) {
        // NT5 protected rename has been proved on both Windows2000 and XP;
        // Win98 uses the independently checked WININIT mechanism instead.
        if (!ops.open(true))
            return Result::io_error;
        if (wait && !cancel && record.phase == boot::Phase::prepared) {
            boot::RawQueue foreign;
            boot::Permission permission;
            const auto deferred = boot::wait_before_registration(ops, record, foreign, permission);
            if (deferred == boot::Deferral::conflict)
                return Result::conflict;
            if (deferred == boot::Deferral::io_error)
                return Result::io_error;
            if (deferred == boot::Deferral::pending)
                return wait->waiting(record, &foreign, permission) ? Result::pending_reboot
                                                                   : Result::io_error;
            if (!wait->waiting(record))
                return Result::io_error;
        }
        boot::Queue queue(ops, record);
        return result(cancel ? queue.cancel() : queue.resume());
    }
    Result dispatch(RuntimeBootStore &bridge, boot::Record &record, bool cancel) {
        if (os_ == Os::nt5) {
            boot::Win32Ops ops(bridge);
            return execute(ops, record, cancel, &bridge);
        }
        if (os_ == Os::win98) {
            boot::Win98Ops ops(bridge, record, bridge.directory());
            return execute(ops, record, cancel);
        }
        return Result::invalid;
    }

  public:
    RuntimeFiles(Win32Store &store, Journal &journal, Os os)
        : store_(store), journal_(journal), os_(os) {}
    Result transition(bool undo) {
        RuntimeBootStore bridge(store_, journal_, os_, undo);
        boot::Record record;
        bool exists;
        if (!bridge.load(record, exists))
            return Result::invalid;
        if (!exists) {
            bool needed;
            if (!bridge.prepare(record, needed))
                return Result::conflict;
            if (!needed)
                return Result::complete;
        }
        return dispatch(bridge, record, false);
    }
    Result cancel_apply() {
        RuntimeBootStore bridge(store_, journal_, os_, false);
        boot::Record record;
        bool exists;
        if (!bridge.load(record, exists))
            return Result::invalid;
        if (!exists || record.phase == boot::Phase::prepared ||
            record.phase == boot::Phase::cancelled)
            return Result::complete;
        return dispatch(bridge, record, true);
    }
};
} // namespace setup::lifecycle
