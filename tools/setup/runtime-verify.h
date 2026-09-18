// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win32-lifecycle.h"
#include "durable-record.h"
namespace setup::lifecycle {
// Successful verification is bound to all installed file/registry identities
// and the six exact staged helper executables. A child is created suspended;
// its PID is durable before it can execute. No stale or reused PID is trusted.
template <class Payloads> class VerifiedRuntimeStore : public Win32Store {
    const Payloads &payloads_;
    Os os_;
    struct Record {
        uint32_t magic = 0x56474744, version = 1, generation = 0, completed = 0, pid = 0,
                 active = 0;
        char identity[68]{};
    };
    struct Handle {
        HANDLE h;
        explicit Handle(HANDLE value) : h(value) {}
        ~Handle() {
            if (h && h != INVALID_HANDLE_VALUE)
                CloseHandle(h);
        }
        Handle(const Handle &) = delete;
    };
    bool installed(const Journal &j) {
        for (unsigned n = 0; n < j.count; ++n)
            if (classify(j, n, false) != Actual::after)
                return false;
        return true;
    }
    bool helper_image(unsigned index, uint32_t &resource, Image &image) {
        if (index >= 6)
            return false;
        char source[96] = "tools/common/";
        lstrcatA(source, system_probes[index]);
        bool found = false;
        for (const auto &p : payloads_) {
            if (p.os != unsigned(os_) || lstrcmpA(p.path, source))
                continue;
            if (found)
                return false;
            found = true;
            resource = p.id;
            image = {};
            image.exists = 1;
            image.size = p.size;
            if (lstrlenA(p.sha) != 64)
                return false;
            lstrcpyA(image.sha, p.sha);
        }
        return found && resource && valid_image(image, Kind::file);
    }
    bool helper(uint32_t generation, unsigned index, char *executable, char *directory,
                Image &image) {
        uint32_t resource = 0;
        Image actual;
        return helper_image(index, resource, image) &&
               stage_helper(generation, index, resource, image, executable, directory) &&
               inspect_file(executable, actual) && same(actual, image);
    }
    bool verification_identity(const Journal &j, char (&identity)[65]) {
        Sha256 hash;
        for (unsigned n = 0; n < j.count; ++n) {
            const auto &item = j.items[n];
            hash.update(reinterpret_cast<const BYTE *>(item.path), sizeof(item.path));
            hash.update(reinterpret_cast<const BYTE *>(item.name), sizeof(item.name));
            hash.update(reinterpret_cast<const BYTE *>(&item.desired), sizeof(item.desired));
        }
        for (unsigned n = 0; n < 6; ++n) {
            uint32_t resource = 0;
            Image image;
            if (!helper_image(n, resource, image))
                return false;
            hash.update(reinterpret_cast<const BYTE *>(&image), sizeof(image));
        }
        hash.finish(identity);
        return true;
    }
    bool verification_path(uint32_t generation, char (&path)[MAX_PATH]) {
        char suffix[64];
        wsprintfA(suffix, "\\T%08lX\\VERIFY.JRN", generation);
        return private_path(path, suffix);
    }
    static bool record_matches(const Record &r, uint32_t generation, const char (&identity)[65]) {
        return r.magic == 0x56474744 && r.version == 1 && r.generation == generation &&
               r.completed < 64 && r.active < 6 &&
               text_equal(r.identity, identity, sizeof(r.identity)) &&
               (!r.pid || !(r.completed & (1u << r.active)));
    }

  public:
    VerifiedRuntimeStore(Os os, const Payloads &payloads) : payloads_(payloads), os_(os) {}
    // Preserve only prior completed proofs for unchanged normal API routes.
    // A fresh receipt is durable before publishing the repair generation; it
    // never treats the restored OS runtime itself as passing the affected API.
    bool seed_repair(const Journal &previous, const Journal &next, uint32_t proofs) {
        if (!valid(previous) || !valid(next) || previous.state != State::activated ||
            previous.uninstall || next.uninstall || next.state != State::staged ||
            previous.generation == UINT32_MAX || next.generation != previous.generation + 1 ||
            previous.count != next.count || !proofs || (proofs & ~0x3cu))
            return false;
        uint32_t needed = 0;
        for (unsigned n = 0; n < next.count; ++n) {
            const auto &a = next.items[n];
            const auto &b = previous.items[n];
            if (a.kind != b.kind || !destination_equal(a.path, b.path, sizeof(a.path)) ||
                !destination_equal(a.name, b.name, sizeof(a.name)) || !same(a.desired, b.desired) ||
                !same(a.original, b.original) || a.original_generation != b.original_generation ||
                a.original_index != b.original_index)
                return false;
            if (same(a.before, a.desired))
                continue;
            unsigned runtime = 3;
            if (a.kind == Kind::file && !a.name[0] && a.original.exists &&
                same(a.before, a.original))
                for (unsigned k = 0; k < 3; ++k)
                    if (destination_equal(a.path, public_runtime[k], sizeof(a.path)) ||
                        (os_ == Os::win98 &&
                         destination_equal(a.path, win98_runtime_cache[k], sizeof(a.path))))
                        runtime = k;
            if (runtime == 3)
                return false;
            needed |= runtime == 0 ? 0x0cu : runtime == 1 ? 0x10u : 0x20u;
        }
        if (needed != proofs)
            return false;
        char previous_path[MAX_PATH], next_path[MAX_PATH], identity[65], next_identity[65];
        if (!verification_path(previous.generation, previous_path) ||
            !verification_path(next.generation, next_path) ||
            !verification_identity(previous, identity) ||
            !verification_identity(next, next_identity))
            return false;
        auto original_valid = [&](const Record &r) {
            return record_matches(r, previous.generation, identity);
        };
        DurableRecord<Record> prior(previous_path);
        Record original;
        bool exists = false;
        if (!prior.load(original, exists, original_valid, true) || !exists ||
            original.completed != 63 || original.pid)
            return false;
        const uint32_t preserved = 63 & ~proofs;
        auto next_valid = [&](const Record &r) {
            return record_matches(r, next.generation, next_identity) &&
                   (r.completed & preserved) == preserved;
        };
        DurableRecord<Record> log(next_path);
        Record record;
        if (!log.load(record, exists, next_valid))
            return false;
        if (exists)
            return true;
        record = {};
        record.generation = next.generation;
        record.completed = preserved;
        lstrcpyA(record.identity, next_identity);
        return log.save(record, next_valid);
    }
    bool verify_activation(const Journal &j) {
        if (!installed(j))
            return false;
        char path[MAX_PATH], identity[65];
        if (!verification_path(j.generation, path) || !verification_identity(j, identity))
            return false;
        for (unsigned n = 0; n < 6; ++n) {
            char executable[MAX_PATH], directory[MAX_PATH];
            Image image;
            if (!helper(j.generation, n, executable, directory, image))
                return false;
        }
        Record record;
        record.generation = j.generation;
        lstrcpyA(record.identity, identity);
        auto valid_record = [&](const Record &r) {
            return record_matches(r, j.generation, identity);
        };
        DurableRecord<Record> log(path);
        Record loaded;
        bool exists;
        if (!log.load(loaded, exists, valid_record))
            return false;
        if (exists)
            record = loaded;
        else if (!log.save(record, valid_record))
            return false;
        if (record.pid) {
            Handle active(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, record.pid));
            // Without the original process handle we cannot authenticate PID
            // reuse on Win98. A still-existing PID requires explicit recovery.
            if (active.h || GetLastError() != ERROR_INVALID_PARAMETER)
                return false;
            record.pid = 0;
            if (!log.save(record, valid_record))
                return false;
        }
        for (unsigned n = 0; n < 6; ++n) {
            if (record.completed & (1u << n))
                continue;
            constexpr const char *names[] = {"OpenGL",     "Glide 2",    "Direct3D 6",
                                             "Direct3D 7", "Direct3D 8", "Direct3D 9"};
            progress("Testing graphics", names[n]);
            if (!installed(j))
                return false;
            char executable[MAX_PATH], directory[MAX_PATH];
            Image image;
            if (!helper(j.generation, n, executable, directory, image))
                return false;
            char command[MAX_PATH + 4] = "\"";
            lstrcatA(command, executable);
            lstrcatA(command, "\"");
            STARTUPINFOA startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            if (!CreateProcessA(executable, command, nullptr, nullptr, FALSE, CREATE_SUSPENDED,
                                nullptr, directory, &startup, &process))
                return false;
            Handle child(process.hProcess), thread(process.hThread);
            record.pid = process.dwProcessId;
            record.active = n;
            if (!log.save(record, valid_record)) {
                TerminateProcess(child.h, 125);
                WaitForSingleObject(child.h, 5000);
                return false;
            }
            bool resumed = ResumeThread(thread.h) != DWORD(-1);
            DWORD waited = resumed ? WaitForSingleObject(child.h, 45000) : WAIT_FAILED;
            DWORD exit = 1;
            bool done = waited == WAIT_OBJECT_0 && GetExitCodeProcess(child.h, &exit);
            if (!done) {
                // Only this retained handle can authorize termination; never
                // kill a process reopened by a possibly reused persisted PID.
                if (!TerminateProcess(child.h, 124) ||
                    WaitForSingleObject(child.h, 5000) != WAIT_OBJECT_0)
                    return false;
            }
            record.pid = 0;
            Image after;
            const bool passed = done && exit == 0 && installed(j) &&
                                inspect_file(executable, after) && same(after, image);
            if (passed)
                record.completed |= 1u << n;
            if (!log.save(record, valid_record) || !passed)
                return false;
        }
        return record.completed == 63 && installed(j);
    }
};
} // namespace setup::lifecycle
