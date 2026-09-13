// SPDX-License-Identifier: GPL-2.0-or-later
#define DG_BOOT_QUEUE_TEST
#include "test-win32.h"
#include <cassert>
using UINT = unsigned;
constexpr bool FALSE = false;
constexpr DWORD HEAP_ZERO_MEMORY = 8, VER_PLATFORM_WIN32_WINDOWS = 1;
struct OSVERSIONINFOA {
    DWORD dwOSVersionInfoSize = 0, dwMajorVersion = 4, dwMinorVersion = 10, dwPlatformId = 1;
};
static bool GetVersionExA(OSVERSIONINFOA *) {
    return true;
}
static int lstrcmpiA(const char *a, const char *b) {
    return fake_win32::canon(a).compare(fake_win32::canon(b));
}
static UINT GetWindowsDirectoryA(char *p, UINT) {
    strcpy(p, "C:\\WINDOWS");
    return 10;
}
static std::map<std::string, std::string> dos_aliases;
static std::string short_missing;
static bool full_path_truncated = false;
static DWORD GetFullPathNameA(const char *p, DWORD cap, char *out, char **) {
    if (full_path_truncated)
        return cap;
    std::vector<std::string> parts;
    std::string path(p);
    for (size_t start = 3; start < path.size();) {
        size_t end = path.find('\\', start);
        if (end == std::string::npos)
            end = path.size();
        std::string part = path.substr(start, end - start);
        if (part == "..") {
            if (!parts.empty())
                parts.pop_back();
        } else if (!part.empty() && part != ".")
            parts.push_back(part);
        start = end + 1;
    }
    std::string result = path.substr(0, 3);
    for (const auto &part : parts) {
        if (result.back() != '\\')
            result += '\\';
        result += part;
    }
    if (result.size() >= cap)
        return DWORD(result.size() + 1);
    strcpy(out, result.c_str());
    return DWORD(result.size());
}
static DWORD GetShortPathNameA(const char *p, char *out, DWORD cap) {
    if (!short_missing.empty() && fake_win32::canon(p) == short_missing) {
        fake_win32::error = ERROR_FILE_NOT_FOUND;
        return 0;
    }
    auto alias = dos_aliases.find(fake_win32::canon(p));
    if (alias != dos_aliases.end())
        p = alias->second.c_str();
    if (strlen(p) >= cap)
        return cap;
    strcpy(out, p);
    return DWORD(strlen(p));
}
static void *zero_alloc(HANDLE, DWORD flags, size_t bytes) {
    return flags & HEAP_ZERO_MEMORY ? calloc(1, bytes) : malloc(bytes);
}
#define HeapAlloc zero_alloc
static void (*before_move)(const char *, const char *) = nullptr;
static void (*after_read)(HANDLE) = nullptr;
static bool fail_foreign_restore = false;
static bool observed_move(const char *a, const char *b) {
    if (before_move)
        before_move(a, b);
    if (fail_foreign_restore && std::string(a).ends_with(".old")) {
        fail_foreign_restore = false;
        return false;
    }
    return MoveFileA(a, b);
}
static bool observed_read(HANDLE h, void *out, DWORD bytes, DWORD *got, void *overlap) {
    bool result = ReadFile(h, out, bytes, got, overlap);
    if (after_read)
        after_read(h);
    return result;
}
#define MoveFileA observed_move
#define ReadFile observed_read
#include "boot-queue-win98.h"
using namespace setup::boot;
static constexpr char root[] = "C:\\WINDOWS\\DGPU\\T00000001";
static constexpr char ini[] = "C:\\WINDOWS\\WININIT.INI";
static std::string content(const char *p) {
    auto &v = fake_win32::files[fake_win32::canon(p)].bytes;
    return {v.begin(), v.end()};
}
static void put(const char *p, const std::string &v) {
    fake_win32::files[fake_win32::canon(p)] = {{v.begin(), v.end()}, false, true};
}
static File identity(const char *p) {
    File f{};
    auto it = fake_win32::files.find(fake_win32::canon(p));
    if (it == fake_win32::files.end())
        return f;
    f.exists = 1;
    f.size = DWORD(it->second.bytes.size());
    setup::Sha256 h;
    h.update(it->second.bytes.data(), f.size);
    h.finish(f.sha);
    return f;
}
struct Store {
    Record durable{};
    bool save(const Record &r) {
        if (fake_win32::fault())
            return false;
        durable = r;
        return true;
    }
    bool inspect(const char *p, File &f) {
        f = identity(p);
        return true;
    }
    bool approved(const Entry &e) {
        return strstr(e.destination, "C:\\WINDOWS\\SYSTEM\\") == e.destination;
    }
};
static Record record() {
    Record r;
    r.generation = 1;
    r.count = 3;
    for (unsigned n = 0; n < r.count; ++n) {
        auto &e = r.entries[n];
        snprintf(e.destination, path_capacity, "C:\\WINDOWS\\SYSTEM\\D%u.DLL", n);
        put(e.destination, "original" + std::to_string(n));
        e.before = identity(e.destination);
        if (n != 1) {
            snprintf(e.source, path_capacity, "C:\\WINDOWS\\DGPU\\S%u.DLL", n);
            put(e.source, "desired" + std::to_string(n));
            e.desired = identity(e.source);
        }
    }
    return r;
}
static void reboot(const Record &r) {
    put("C:\\WINDOWS\\WININIT.BAK", content(ini));
    fake_win32::files.erase(fake_win32::canon(ini));
    for (unsigned n = 0; n < r.count; ++n) {
        auto &e = r.entries[n];
        fake_win32::files.erase(fake_win32::canon(e.destination));
        if (e.desired.exists) {
            fake_win32::files[fake_win32::canon(e.destination)] =
                fake_win32::files.at(fake_win32::canon(e.source));
            fake_win32::files.erase(fake_win32::canon(e.source));
        }
    }
}
static unsigned exercise(unsigned fail) {
    fake_win32::reset();
    auto r = record();
    Store s;
    s.durable = r;
    constexpr char foreign[] = "; foreign "
                               "comment\r\n[other]\r\nx=y\r\n[rename]\r\nNUL=C:\\FOREIGN1."
                               "DLL\r\nNUL=C:\\FOREIGN2.DLL\r\n[tail]\r\nz=q\r\n";
    put(ini, foreign);
    fake_win32::files[fake_win32::canon(ini)].attributes = 6; // hidden+system
    fake_win32::mutation = 0;
    fake_win32::fail = fail;
    {
        Win98Ops ops(s, r, root);
        if (ops.open(true)) {
            Queue q(ops, r);
            q.resume();
        }
    }
    unsigned count = fake_win32::mutation;
    fake_win32::fail = 0;
    r = s.durable;
    {
        Win98Ops ops(s, r, root);
        assert(ops.open(true));
        Queue q(ops, r);
        auto result = q.resume();
        assert(result == Result::pending);
        auto text = content(ini);
        assert(text.find("NUL=C:\\FOREIGN1.DLL\r\nNUL=C:\\FOREIGN2.DLL") != std::string::npos);
        assert(q.cancel() == Result::cancelled);
        assert(content(ini) == foreign);
        assert(fake_win32::files[fake_win32::canon(ini)].attributes == 6);
    }
    return count;
}
// Inject a real competing update at the rename boundary, after all earlier
// content checks. Exercise both successful restoration and a crash/failure
// before restoration, recovered by the next adapter instance.
static std::string racing_queue;
static unsigned race_failure = 0;
static void foreign_move_race(bool interrupted, bool exact_after = false, unsigned fail = 0) {
    fake_win32::reset();
    auto r = record();
    Store store;
    store.durable = r;
    put(ini, "[rename]\r\nNUL=C:\\FOREIGN.DLL\r\n");
    const std::string foreign =
        content(ini) +
        (exact_after ? std::string(r.entries[0].destination) + "=" + r.entries[0].source + "\r\n"
                     : "; racing installer update\r\n");
    racing_queue = foreign;
    race_failure = fail;
    before_move = [](const char *a, const char *b) {
        if (fake_win32::canon(a) == fake_win32::canon(ini) && std::string(b).ends_with(".old")) {
            put(ini, racing_queue);
            if (race_failure)
                fake_win32::fail = fake_win32::mutation + race_failure;
            before_move = nullptr;
        }
    };
    fail_foreign_restore = interrupted;
    {
        Win98Ops ops(store, r, root);
        assert(ops.open(true));
        Queue q(ops, r);
        assert(q.resume() == Result::io_error);
    }
    assert(!before_move);
    if (interrupted)
        assert(fake_win32::files.count(fake_win32::canon(ini)) == 0);
    else if (!fail)
        assert(content(ini) == foreign);
    fake_win32::fail = 0;
    r = store.durable;
    {
        Win98Ops ops(store, r, root);
        assert(!ops.open(true)); // restoration is not successful queue publication
    }
    assert(content(ini) == foreign);
    {
        Win98Ops ops(store, r, root);
        assert(!ops.open(true)); // cannot adopt restored bytes, including exact after
    }
    assert(identity(r.entries[0].source).exists);
    assert(same(identity(r.entries[0].destination), r.entries[0].before));
    assert(!fake_win32::files.count(fake_win32::canon("C:\\WINDOWS\\DGPU\\T00000001\\W900.done")));
}
static void snapshot_race() {
    fake_win32::reset();
    auto r = record();
    Store store;
    store.durable = r;
    put(ini, "[rename]\r\n");
    Win98Ops ops(store, r, root);
    assert(ops.open(true));
    after_read = [](HANDLE h) {
        if (fake_win32::handles.at(h).path == fake_win32::canon(ini)) {
            put(ini, "[rename]\r\nC:\\WINDOWS\\SYSTEM\\D0.DLL=C:\\FOREIGN.DLL\r\n");
            after_read = nullptr;
        }
    };
    assert(!ops.enqueue(r.entries[0]));
    assert(!after_read);
    assert(content(ini) == "[rename]\r\nC:\\WINDOWS\\SYSTEM\\D0.DLL=C:\\FOREIGN.DLL\r\n");
    assert(!fake_win32::files.count(fake_win32::canon("C:\\WINDOWS\\DGPU\\T00000001\\W900.bin")));
}
static void alias_paths() {
    for (const char *dest :
         {"C:\\WINDOWS\\SYSTEM\\..\\SYSTEM\\D0.DLL", "C:\\SYSTEM LONG NAME\\D0.DLL"}) {
        fake_win32::reset();
        auto r = record();
        Store store;
        store.durable = r;
        dos_aliases[fake_win32::canon("C:\\SYSTEM LONG NAME\\D0.DLL")] =
            "C:\\WINDOWS\\SYSTEM\\D0.DLL";
        const std::string foreign = std::string("[rename]\r\n") + dest + "=C:\\FOREIGN.DLL\r\n";
        put(ini, foreign);
        Win98Ops ops(store, r, root);
        assert(ops.open(true));
        Queue q(ops, r);
        assert(q.resume() == Result::conflict);
        assert(content(ini) == foreign);
        dos_aliases.clear();
    }
    fake_win32::reset();
    auto r = record();
    Store store;
    store.durable = r;
    Win98Ops ops(store, r, root);
    assert(ops.open(true));
    uint16_t encoded[path_capacity]{};
    short_missing = fake_win32::canon("C:\\WINDOWS\\SYSTEM\\D0.DLL");
    assert(ops.encode("C:\\WINDOWS\\SYSTEM\\..\\SYSTEM\\D0.DLL", encoded,
                      path_capacity)); // absent 8.3 leaf resolves through existing parent
    short_missing = fake_win32::canon("C:\\WINDOWS\\SYSTEM\\LONG MISSING.DLL");
    assert(!ops.encode("C:\\WINDOWS\\SYSTEM\\LONG MISSING.DLL", encoded, path_capacity));
    short_missing.clear();
    full_path_truncated = true;
    assert(!ops.encode(r.entries[0].source, encoded, path_capacity));
    full_path_truncated = false;
}
static void system_cache_pair() {
    struct CacheStore : Store {
        bool approved(const Entry &) {
            return true;
        }
    } store;
    fake_win32::reset();
    Record r;
    r.generation = 1;
    r.count = 2;
    for (unsigned n = 0; n < 2; ++n) {
        auto &e = r.entries[n];
        strcpy(e.destination,
               n ? "C:\\WINDOWS\\SYSTEM\\ddraw.dll" : "C:\\WINDOWS\\SYSBCKUP\\ddraw.dll");
        snprintf(e.source, path_capacity, "C:\\WINDOWS\\DGPU\\S%u.DLL", n);
        put(e.destination, "native");
        put(e.source, "provider");
        e.before = identity(e.destination);
        e.desired = identity(e.source);
    }
    {
        Win98Ops ops(store, r, root);
        assert(ops.open(true));
        auto bad = r.entries[0];
        strcpy(bad.destination, "C:\\WINDOWS\\SYSBCKUP\\kernel32.dll");
        assert(!ops.approved(bad));
        strcpy(bad.destination, "C:\\WINDOWS\\SYSBCKUP\\..\\SYSTEM\\ddraw.dll");
        assert(!ops.approved(bad));
        Queue queue(ops, r);
        assert(queue.resume() == Result::pending);
        assert(content(ini).find("C:\\WINDOWS\\SYSBCKUP\\DDRAW.DLL=") != std::string::npos);
    }
    reboot(r);
    {
        Win98Ops ops(store, r, root);
        assert(ops.open(true));
        Queue queue(ops, r);
        assert(queue.resume() == Result::complete);
    }
    Record undo;
    undo.generation = 2;
    undo.count = 2;
    for (unsigned n = 0; n < 2; ++n) {
        auto &e = undo.entries[n];
        strcpy(e.destination, r.entries[n].destination);
        snprintf(e.source, path_capacity, "C:\\WINDOWS\\DGPU\\B%u.DLL", n);
        put(e.source, "native");
        e.before = identity(e.destination);
        e.desired = identity(e.source);
    }
    {
        Win98Ops ops(store, undo, root);
        assert(ops.open(true));
        Queue queue(ops, undo);
        assert(queue.resume() == Result::pending);
    }
    reboot(undo);
    {
        Win98Ops ops(store, undo, root);
        assert(ops.open(true));
        Queue queue(ops, undo);
        assert(queue.resume() == Result::complete);
    }
    for (const auto &e : undo.entries)
        if (e.destination[0])
            assert(content(e.destination) == "native");
}
int main() {
    system_cache_pair();
    unsigned mutations = exercise(0);
    for (unsigned n = 1; n <= mutations; ++n)
        exercise(n);
    fake_win32::reset();
    auto r = record();
    Store s;
    s.durable = r;
    {
        Win98Ops ops(s, r, root);
        assert(ops.open(true));
        Queue q(ops, r);
        assert(q.resume() == Result::pending);
    }
    reboot(r);
    {
        Win98Ops ops(s, r, root);
        assert(ops.open(true));
        Queue q(ops, r);
        assert(q.resume() == Result::complete);
    }
    // The final public rename happened, but power failed before its durable
    // completion marker. A boot consumes the INI; the exact BAK plus staged
    // marker establishes consumption without replaying already moved sources.
    fake_win32::reset();
    r = record();
    s.durable = r;
    {
        Win98Ops ops(s, r, root);
        assert(ops.open(true));
        Queue q(ops, r);
        assert(q.resume() == Result::pending);
    }
    fake_win32::files.erase(fake_win32::canon("C:\\WINDOWS\\DGPU\\T00000001\\W902.done"));
    reboot(r);
    {
        Win98Ops ops(s, r, root);
        assert(ops.open(true));
        Queue q(ops, r);
        assert(q.resume() == Result::complete);
    }
    // A completed INI with a tampered durable receipt is never adopted.
    fake_win32::files.at(fake_win32::canon("C:\\WINDOWS\\DGPU\\T00000001\\W901.bin")).bytes[23] ^=
        1;
    {
        Win98Ops ops(s, r, root);
        assert(!ops.open(true));
    }
    fake_win32::reset();
    r = record();
    s.durable = r;
    put(ini, "[rename]\r\nC:\\WINDOWS\\SYSTEM\\D0.DLL=C:\\FOREIGN.DLL\r\n");
    {
        Win98Ops ops(s, r, root);
        assert(ops.open(true));
        Queue q(ops, r);
        assert(q.resume() == Result::conflict);
    }
    foreign_move_race(false);
    foreign_move_race(true);
    foreign_move_race(false, true);
    for (unsigned fail = 2; fail <= 6; ++fail)
        foreign_move_race(false, true, fail);
    snapshot_race();
    alias_paths();
    printf("PASS Win98 real queue adapter: %u mutation failures, mixed "
           "replacement/deletion, "
           "reboot, cancellation, foreign sections/pairs, alias collisions and "
           "competing-update "
           "recovery\n",
           mutations);
}
