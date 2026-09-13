// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "lifecycle.h"
namespace setup::lifecycle {
// Boot owns only the separately journalled file queue. Resume owns the exact
// prior RunOnce value. Both are armed only after durable transaction direction;
// Engine remains the authority for registry ownership and final state.
template <class Store, class Boot, class Resume> class SystemEngine {
    Store &store_;
    Journal &journal_;
    Boot &boot_;
    Resume &resume_;

  public:
    SystemEngine(Store &store, Journal &journal, Boot &boot, Resume &resume)
        : store_(store), journal_(journal), boot_(boot), resume_(resume) {}
    Result apply(bool ready) {
        if (!valid(journal_) || journal_.state == State::rolling_back ||
            journal_.state == State::failed ||
            (journal_.state == State::removed && !journal_.uninstall))
            return Result::invalid;
        if (!journal_.uninstall && !ready)
            return Result::provider_not_ready;
        // An interrupted final RunOnce restoration must not rerun API probes.
        if (journal_.state == State::activated || journal_.state == State::removed) {
            for (unsigned n = 0; n < journal_.count; ++n)
                if (store_.classify(journal_, n, false) != Actual::after)
                    return Result::conflict;
            return resume_.prepare() && resume_.finish() ? Result::complete : Result::io_error;
        }
        journal_.state = State::applying;
        if (!store_.persist(journal_) || !resume_.prepare() || !resume_.arm())
            return Result::io_error;
        Result files = boot_.transition(false);
        if (files != Result::complete) {
            if (files == Result::pending_reboot) {
                journal_.state = State::pending_reboot;
                if (!store_.persist(journal_))
                    return Result::io_error;
            }
            return files;
        }
        Engine engine(store_, journal_);
        Result result = engine.continue_apply(ready);
        // The file queue has completed and the driver is already verified.
        // A failed normal-API oracle is an error, not another reboot promise.
        if (result == Result::pending_reboot)
            return Result::io_error;
        if (result == Result::complete && !resume_.finish())
            return Result::io_error;
        return result;
    }
    Result rollback() {
        if (!valid(journal_))
            return Result::invalid;
        journal_.state = State::rolling_back;
        if (!store_.persist(journal_) || !resume_.prepare() || !resume_.arm())
            return Result::io_error;
        // Cancelling outstanding forward renames precedes all restore work;
        // otherwise a later boot could reinstall bytes we just rolled back.
        Result cancelled = boot_.cancel_apply();
        if (cancelled != Result::complete)
            return cancelled;
        Result files = boot_.transition(true);
        if (files != Result::complete)
            return files;
        Engine engine(store_, journal_);
        Result result = engine.rollback();
        if (result == Result::complete && !resume_.finish())
            return Result::io_error;
        return result;
    }
};
} // namespace setup::lifecycle
