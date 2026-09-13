// SPDX-License-Identifier: GPL-2.0-or-later
#define DG_SETUP_ADAPTER_TEST
#include "test-win32.h"
#include <cassert>
#include <array>
constexpr DWORD SYNCHRONIZE = 0x100000, PROCESS_QUERY_INFORMATION = 0x400,
                ERROR_INVALID_PARAMETER = 87, CREATE_SUSPENDED = 4, WAIT_FAILED = 0xffffffff,
                WAIT_OBJECT_0 = 0, WAIT_TIMEOUT = 258;
constexpr bool FALSE = false;
struct STARTUPINFOA {
    DWORD cb{};
};
struct PROCESS_INFORMATION {
    HANDLE hProcess{}, hThread{};
    DWORD dwProcessId{};
};
namespace child_test {
struct Child {
    DWORD pid{}, exit{};
    unsigned index{};
    bool resumed = false, done = false;
};
inline std::map<HANDLE, Child> children;
inline std::vector<std::string> applications;
inline unsigned launched = 0, resumed = 0, fail_index = 99;
inline bool corrupt_provider = false, timeout = false, resume_failure = false, reused_pid = false;
inline void reset() {
    children.clear();
    applications.clear();
    launched = resumed = 0;
    fail_index = 99;
    corrupt_provider = timeout = resume_failure = reused_pid = false;
}
} // namespace child_test
inline bool CreateProcessA(const char *application, char *, void *, void *, bool inherit,
                           DWORD flags, void *, const char *cwd, STARTUPINFOA *,
                           PROCESS_INFORMATION *out) {
    using namespace child_test;
    assert(!inherit && flags == CREATE_SUSPENDED);
    assert(std::string(application).starts_with(std::string(cwd) + "\\"));
    assert(fake_win32::files.count(fake_win32::canon(application)));
    if (fake_win32::fault())
        return false;
    applications.emplace_back(application);
    HANDLE h = fake_win32::next++;
    out->hProcess = h;
    out->hThread = h;
    out->dwProcessId = DWORD(h);
    children[h] = {DWORD(h), 0, launched++, false, false};
    return true;
}
inline DWORD ResumeThread(HANDLE h) {
    using namespace child_test;
    auto &child = children.at(h);
    if (resume_failure)
        return DWORD(-1);
    child.resumed = true;
    ++resumed;
    child.done = !timeout;
    child.exit = child.index == fail_index ? 1 : 0;
    if (corrupt_provider)
        fake_win32::files[fake_win32::canon("C:\\WINDOWS\\SYSTEM\\ddraw.dll")].bytes = {'x'};
    return 1;
}
inline DWORD WaitForSingleObject(HANDLE h, DWORD) {
    return child_test::children.at(h).done ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
}
inline bool GetExitCodeProcess(HANDLE h, DWORD *out) {
    *out = child_test::children.at(h).exit;
    return true;
}
inline bool TerminateProcess(HANDLE h, unsigned code) {
    auto &child = child_test::children.at(h);
    child.done = true;
    child.exit = code;
    return true;
}
inline HANDLE OpenProcess(DWORD, bool, DWORD pid) {
    if (child_test::reused_pid)
        return HANDLE(pid);
    for (const auto &[h, child] : child_test::children)
        if (child.pid == pid && !child.done)
            return h;
    fake_win32::error = ERROR_INVALID_PARAMETER;
    return 0;
}
#include "runtime-verify.h"
using namespace setup;
using namespace setup::lifecycle;
struct Payload {
    unsigned os, id, size;
    const char *path;
    char sha[65];
};
static std::array<Payload, 6> payloads;
constexpr const char *Names[] = {"tools/common/DGSYSGL.EXE", "tools/common/DGSYSGR.EXE",
                                 "tools/common/DGSYS6.EXE",  "tools/common/DGSYS7.EXE",
                                 "tools/common/DGSYS8.EXE",  "tools/common/DGSYS9.EXE"};
static Journal initial() {
    using namespace fake_win32;
    reset();
    child_test::reset();
    files[canon("C:\\WINDOWS\\DreamGPU")].directory = true;
    for (unsigned n = 0; n < 6; ++n) {
        std::string path = "C:\\WINDOWS\\DreamGPU\\";
        path += Names[n];
        std::replace(path.begin(), path.end(), '/', '\\');
        auto &bytes = files[canon(path.c_str())].bytes;
        bytes = {'p', 'e', BYTE(n)};
        payloads[n] = {unsigned(Os::nt5), 100 + n, 3, Names[n], {}};
        native_alias::hash(bytes, payloads[n].sha);
        resources[100 + n] = bytes;
    }
    files[canon("C:\\WINDOWS\\SYSTEM\\ddraw.dll")].bytes = {'n', 'e', 'w'};
    Journal j;
    j.generation = 1;
    j.count = 1;
    strcpy(j.items[0].path, "ddraw.dll");
    j.items[0].desired.exists = 1;
    j.items[0].desired.size = 3;
    native_alias::hash(files[canon("C:\\WINDOWS\\SYSTEM\\ddraw.dll")].bytes,
                       j.items[0].desired.sha);
    Win32Store store;
    assert(store.init("C:\\WINDOWS\\DreamGPU") && store.create_generation(1));
    mutation = 0;
    return j;
}
static void repair_verification() {
    unsigned failures = 0;
    for (unsigned failure = 0; failure <= failures; ++failure) {
        auto previous = initial();
        auto &item = previous.items[0];
        previous.state = State::activated;
        item.phase = Phase::applied;
        item.original_generation = 1;
        item.original_index = 0;
        item.original.exists = 1;
        item.original.size = 3;
        const BYTE original[] = {'o', 'l', 'd'};
        native_alias::hash(original, item.original.sha);
        item.before = item.original;
        assert(valid(previous));
        VerifiedRuntimeStore store(Os::nt5, payloads);
        assert(store.init("C:\\WINDOWS\\DreamGPU") && store.verify_activation(previous));
        assert(child_test::resumed == 6);
        Journal next = previous;
        next.generation = 2;
        next.state = State::staged;
        next.items[0].phase = Phase::prepared;
        next.items[0].before = item.original;
        assert(store.create_generation(2));
        fake_win32::files[fake_win32::canon("C:\\WINDOWS\\SYSTEM\\ddraw.dll")].bytes = {'o', 'l',
                                                                                        'd'};
        fake_win32::mutation = 0;
        assert(!store.seed_repair(previous, next, 0x10));
        assert(!fake_win32::mutation && child_test::resumed == 6);
        if (!failure) {
            auto &receipt =
                fake_win32::files[fake_win32::canon("C:\\WINDOWS\\DreamGPU\\T00000001\\VERIFY.JRN")]
                    .bytes;
            const auto saved = receipt;
            receipt.back() ^= 1;
            assert(!store.seed_repair(previous, next, 0x0c));
            receipt = saved;
            const char original_hash = payloads[0].sha[0];
            payloads[0].sha[0] = original_hash == 'a' ? 'b' : 'a';
            assert(!store.seed_repair(previous, next, 0x0c));
            payloads[0].sha[0] = original_hash;
            assert(!fake_win32::mutation && child_test::resumed == 6);
        }
        fake_win32::fail = failure;
        assert(store.seed_repair(previous, next, 0x0c) == !failure);
        if (!failure)
            failures = fake_win32::mutation;
        fake_win32::fail = 0;
        assert(store.seed_repair(previous, next, 0x0c));
        assert(!store.verify_activation(next)); // OS bytes are never an API proof.
        fake_win32::files[fake_win32::canon("C:\\WINDOWS\\SYSTEM\\ddraw.dll")].bytes = {'n', 'e',
                                                                                        'w'};
        assert(store.verify_activation(next) && child_test::resumed == 8);
        assert(child_test::applications[6].ends_with("DGSYS6.EXE"));
        assert(child_test::applications[7].ends_with("DGSYS7.EXE"));
        assert(store.verify_activation(next) && child_test::resumed == 8);
    }
    printf("PASS repair proof seeding: %u mutation failure/recovery points, only D3D6/7 rerun "
           "for known-original DDRAW, no repeated completed proof\n",
           failures);
}
int main() {
    repair_verification();
    unsigned points;
    {
        auto j = initial();
        VerifiedRuntimeStore store(Os::nt5, payloads);
        assert(store.init("C:\\WINDOWS\\DreamGPU"));
        assert(store.verify_activation(j) && child_test::resumed == 6);
        points = fake_win32::mutation;
        assert(store.verify_activation(j) &&
               child_test::resumed == 6); // durable successes not replayed
        fake_win32::files[fake_win32::canon("C:\\WINDOWS\\DreamGPU\\T00000001\\PROOFS\\DGSYS9.EXE")]
            .bytes[0] ^= 1;
        assert(!store.verify_activation(j) && child_test::resumed == 6);
    }
    {
        auto j = initial();
        VerifiedRuntimeStore store(Os::nt5, payloads);
        assert(store.init("C:\\WINDOWS\\DreamGPU"));
        child_test::fail_index = 2;
        assert(!store.verify_activation(j) && child_test::resumed == 3);
        child_test::fail_index = 99;
        assert(store.verify_activation(j) && child_test::resumed == 7);
    }
    for (unsigned failure = 1; failure <= points; ++failure) {
        auto j = initial();
        VerifiedRuntimeStore store(Os::nt5, payloads);
        assert(store.init("C:\\WINDOWS\\DreamGPU"));
        fake_win32::fail = failure;
        assert(!store.verify_activation(j));
        fake_win32::fail = 0;
        // Completed children may have lost their acknowledgement. The next
        // valid record can rerun only unacknowledged helpers, never assert a pass.
        if (store.verify_activation(j)) {
            assert(child_test::resumed >= 6 && child_test::resumed <= 7);
        } else {
            // Interrupted private staging can leave a reserved path containing
            // unknown bytes. Fail closed rather than overwrite that file; the
            // public transaction remains available for owned rollback.
            bool incomplete_private = false;
            for (unsigned n = 0; n < 6; ++n) {
                std::string path = "C:\\WINDOWS\\DreamGPU\\T00000001\\PROOFS\\";
                path += setup::system_probes[n];
                auto found = fake_win32::files.find(fake_win32::canon(path.c_str()));
                if (found != fake_win32::files.end() &&
                    found->second.bytes != fake_win32::resources[100 + n])
                    incomplete_private = true;
            }
            assert(incomplete_private);
        }
        assert(fake_win32::files.at(fake_win32::canon("C:\\WINDOWS\\SYSTEM\\ddraw.dll")).bytes ==
               std::vector<BYTE>({'n', 'e', 'w'}));
    }
    for (unsigned failure = 0; failure < 3; ++failure) {
        auto j = initial();
        VerifiedRuntimeStore store(Os::nt5, payloads);
        assert(store.init("C:\\WINDOWS\\DreamGPU"));
        child_test::timeout = failure == 0;
        child_test::resume_failure = failure == 1;
        child_test::corrupt_provider = failure == 2;
        assert(!store.verify_activation(j));
        for (const auto &[h, child] : child_test::children) {
            (void)h;
            assert(child.done);
        }
    }
    printf(
        "PASS actual normal-API verification: six authenticated helpers, no acknowledged replay, "
        "%u mutation failures, failed exit/timeout/suspended cleanup/provider tampering\n",
        points);
}
