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
#include "boot-record-log.h"
#include <algorithm>
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
// Persists through the production record log, as RuntimeBootStore does. The
// Win98 adapter still restricts destinations to SYSTEM and the SYSBCKUP copies.
struct LogStore : Store {
    std::string path;
    static bool check(const Record &r) {
        return valid(r);
    }
    bool approved(const Entry &) {
        return true;
    }
    bool save(const Record &r) {
        RecordLog log(path.c_str());
        Record previous;
        bool exists = false;
        return log.load(previous, exists, check) && log.save(r, check);
    }
    Record load(uint32_t *schema = nullptr) {
        RecordLog log(path.c_str());
        Record r;
        bool exists = false;
        assert(log.load(r, exists, check) && exists);
        if (schema)
            *schema = log.schema;
        return r;
    }
};
static unsigned rename_lines() {
    const auto text = content(ini);
    return unsigned(std::count(text.begin(), text.end(), '='));
}
static void settle(LogStore &store, Record &r, const char *dir, Result first = Result::pending) {
    {
        Win98Ops ops(store, r, dir);
        assert(ops.open(true));
        Queue q(ops, r);
        assert(q.resume() == first);
    }
    if (first == Result::pending) {
        reboot(r);
        r = store.load();
        Win98Ops ops(store, r, dir);
        assert(ops.open(true));
        Queue q(ops, r);
        assert(q.resume() == Result::complete);
    }
    assert(store.load().phase == Phase::complete);
}
// The complete Windows 98 system plan with all three SYSBCKUP copies: seven
// shared providers, the ICD and three aliases are new; the public runtimes
// and their cache copies are replaced. 17 operations exceed schema 1.
static constexpr const char *plan_files[] = {
    "SYSTEM\\dgpugl.dll", "SYSTEM\\glide2x.dll", "SYSTEM\\wined3d.dll", "SYSTEM\\winedd.dll",
    "SYSTEM\\wined8.dll", "SYSTEM\\wined9.dll",  "SYSTEM\\glide3x.dll", "SYSTEM\\dgpuicd.dll",
    "SYSTEM\\ddsys.dll",  "SYSTEM\\msd3d8.dll",  "SYSTEM\\msd3d9.dll",  "SYSBCKUP\\ddraw.dll",
    "SYSTEM\\ddraw.dll",  "SYSBCKUP\\d3d8.dll",  "SYSTEM\\d3d8.dll",    "SYSBCKUP\\d3d9.dll",
    "SYSTEM\\d3d9.dll"};
static Record plan(unsigned generation, const char *dir) {
    Record r;
    r.generation = generation;
    r.count = sizeof(plan_files) / sizeof(plan_files[0]);
    for (unsigned n = 0; n < r.count; ++n) {
        auto &e = r.entries[n];
        snprintf(e.destination, path_capacity, "C:\\WINDOWS\\%s", plan_files[n]);
        if (n > 10 && !identity(e.destination).exists)
            put(e.destination, std::string("native ") + plan_files[n]);
        e.before = identity(e.destination);
        snprintf(e.source, path_capacity, "%s\\%02u.NEW", dir, n);
        put(e.source, "provider " + std::to_string(n));
        e.desired = identity(e.source);
    }
    return r;
}
// Return every destination to its original: delete the new files, restore the
// replaced ones from private copies of the original bytes.
static Record reverse(const Record &installed, unsigned generation, const char *dir) {
    Record r;
    r.generation = generation;
    r.count = installed.count;
    for (unsigned n = 0; n < r.count; ++n) {
        auto &e = r.entries[n];
        strcpy(e.destination, installed.entries[n].destination);
        e.before = identity(e.destination);
        if (n > 10) {
            snprintf(e.source, path_capacity, "%s\\%02u.RST", dir, n);
            put(e.source, std::string("native ") + plan_files[n]);
            e.desired = identity(e.source);
        }
    }
    return r;
}
static void original_plan() {
    for (unsigned n = 0; n < sizeof(plan_files) / sizeof(plan_files[0]); ++n) {
        char path[path_capacity];
        snprintf(path, path_capacity, "C:\\WINDOWS\\%s", plan_files[n]);
        if (n > 10)
            assert(content(path) == std::string("native ") + plan_files[n]);
        else
            assert(!identity(path).exists);
    }
}
static void full_plan(bool uninstall) {
    fake_win32::reset();
    constexpr char dir[] = "C:\\WINDOWS\\DGPU\\T00000001", dir2[] = "C:\\WINDOWS\\DGPU\\T00000002";
    Record r = plan(1, dir);
    assert(r.count == 17 && r.count > v1_entries && valid(r));
    LogStore apply;
    apply.path = std::string(dir) + "\\APPLY.BOOT";
    assert(apply.save(r));
    {
        Win98Ops ops(apply, r, dir);
        assert(ops.open(true));
        Queue q(ops, r);
        assert(q.resume() == Result::pending);
    }
    assert(rename_lines() == 17);
    reboot(r);
    r = apply.load();
    {
        Win98Ops ops(apply, r, dir);
        assert(ops.open(true));
        Queue q(ops, r);
        assert(q.resume() == Result::complete);
    }
    for (unsigned n = 0; n < r.count; ++n)
        assert(content(r.entries[n].destination) == "provider " + std::to_string(n));
    // Rollback reuses the generation directory (34 WININIT receipts in one
    // directory); removal is a new generation.
    LogStore restore;
    restore.path =
        std::string(uninstall ? dir2 : dir) + (uninstall ? "\\APPLY.BOOT" : "\\RESTORE.BOOT");
    Record back = reverse(r, uninstall ? 2 : 1, uninstall ? dir2 : dir);
    assert(restore.save(back));
    settle(restore, back, uninstall ? dir2 : dir);
    original_plan();
}
// The capacity boundary: 24 entries install and restore in one generation
// directory, using 48 of its 50 WININIT receipts.
static void boundary() {
    fake_win32::reset();
    Record r;
    r.generation = 1;
    r.count = max_entries;
    for (unsigned n = 0; n < r.count; ++n) {
        auto &e = r.entries[n];
        snprintf(e.destination, path_capacity, "C:\\WINDOWS\\SYSTEM\\D%u.DLL", n);
        put(e.destination, "original" + std::to_string(n));
        e.before = identity(e.destination);
        snprintf(e.source, path_capacity, "%s\\S%u.DLL", root, n);
        put(e.source, "desired" + std::to_string(n));
        e.desired = identity(e.source);
    }
    LogStore store;
    store.path = std::string(root) + "\\APPLY.BOOT";
    assert(store.save(r));
    settle(store, r, root);
    assert(r.completed == (1u << max_entries) - 1);
    Record back;
    back.generation = 1;
    back.count = max_entries;
    for (unsigned n = 0; n < back.count; ++n) {
        auto &e = back.entries[n];
        strcpy(e.destination, r.entries[n].destination);
        e.before = identity(e.destination);
        snprintf(e.source, path_capacity, "%s\\R%u.DLL", root, n);
        put(e.source, "original" + std::to_string(n));
        e.desired = identity(e.source);
    }
    LogStore restore;
    restore.path = std::string(root) + "\\RESTORE.BOOT";
    assert(restore.save(back));
    settle(restore, back, root);
    for (unsigned n = 0; n < back.count; ++n)
        assert(content(back.entries[n].destination) == "original" + std::to_string(n));
    unsigned receipts = 0;
    for (const auto &[path, node] : fake_win32::files)
        receipts += path.find("\\w9") != std::string::npos && path.ends_with(".bin");
    assert(receipts == 2 * max_entries && receipts <= wininit_slots);
}
// A schema 1 log as an earlier installer wrote it.
static std::string v1_image(const Record &r) {
    RecordV1 v;
    v.generation = r.generation;
    v.count = r.count;
    v.phase = r.phase;
    v.completed = r.completed;
    for (unsigned n = 0; n < r.count; ++n)
        v.entries[n] = r.entries[n];
    char sha[65];
    setup::Sha256 h;
    h.update(reinterpret_cast<const BYTE *>(&v), sizeof(v));
    h.finish(sha);
    return std::string(reinterpret_cast<const char *>(&v), sizeof(v)) + std::string(sha, 64);
}
// Interrupt the old installer's run at every mutation, keep its durable record
// as schema 1, and continue with this code: registration resumes, the boot
// completes, and the log becomes schema 1 followed by schema 2.
static unsigned v1_resume(unsigned fail) {
    fake_win32::reset();
    Record r = record();
    LogStore store;
    store.path = std::string(root) + "\\APPLY.BOOT";
    put(store.path.c_str(), v1_image(r));
    fake_win32::mutation = 0;
    fake_win32::fail = fail;
    {
        Record in = store.load();
        Win98Ops ops(store, in, root);
        if (ops.open(true)) {
            Queue q(ops, in);
            (void)q.resume();
        }
    }
    const unsigned points = fake_win32::mutation;
    fake_win32::fail = 0;
    put(store.path.c_str(), v1_image(store.load()));
    uint32_t schema = 0;
    r = store.load(&schema);
    assert(schema == 1);
    {
        Win98Ops ops(store, r, root);
        assert(ops.open(true));
        Queue q(ops, r);
        const auto result = q.resume();
        assert(result == Result::pending || result == Result::complete);
        if (result == Result::pending) {
            reboot(r);
        }
    }
    r = store.load(&schema);
    {
        Win98Ops ops(store, r, root);
        assert(ops.open(true));
        Queue q(ops, r);
        assert(q.resume() == Result::complete);
    }
    r = store.load(&schema);
    assert(schema == record_version && r.phase == Phase::complete);
    const auto size = fake_win32::files.at(fake_win32::canon(store.path.c_str())).bytes.size();
    assert(size > sizeof(RecordV1) + 64 &&
           (size - sizeof(RecordV1) - 64) % (sizeof(Record) + 64) == 0);
    for (unsigned n = 0; n < r.count; ++n)
        assert(same(identity(r.entries[n].destination), r.entries[n].desired));
    return points;
}
static void v1_log() {
    fake_win32::reset();
    Record r = record();
    r.phase = Phase::queued;
    r.completed = 2;
    LogStore store;
    store.path = std::string(root) + "\\APPLY.BOOT";
    const char *path = store.path.c_str();
    const std::string v1 = v1_image(r);
    auto bytes = [&] {
        const auto &b = fake_win32::files.at(fake_win32::canon(path)).bytes;
        return std::string(b.begin(), b.end());
    };
    auto loads = [&](bool read_only, uint32_t *schema = nullptr) {
        RecordLog log(path);
        Record out;
        bool exists = false;
        const bool ok = log.load(out, exists, LogStore::check, read_only) && exists;
        if (schema)
            *schema = log.schema;
        return ok;
    };
    uint32_t schema = 0;
    put(path, v1);
    Record loaded = store.load(&schema);
    assert(schema == 1 && loaded.version == record_version && loaded.count == 3 &&
           loaded.phase == Phase::queued && loaded.completed == 2 &&
           !strcmp(loaded.entries[2].destination, r.entries[2].destination) &&
           !loaded.entries[3].destination[0]);
    // An incomplete final append is discarded: a torn schema 2 record, or any
    // tail shorter than the smallest append, never a complete record.
    std::string torn(sizeof(RecordV1) + 64 + 100, '\0');
    const uint32_t header[2] = {record_magic, record_version};
    memcpy(torn.data(), header, sizeof(header));
    for (const std::string &tail : {torn, std::string(20, '\x7f')}) {
        put(path, v1 + tail);
        assert(loads(true) && bytes() == v1 + tail); // read-only keeps the bytes
        assert(loads(false, &schema) && schema == 1 && bytes() == v1);
    }
    // This code appends schema 2 after a schema 1 log.
    put(path, v1);
    loaded.phase = Phase::complete;
    assert(store.save(loaded));
    assert(bytes().size() == v1.size() + sizeof(Record) + 64);
    Record mixed = store.load(&schema);
    assert(schema == record_version && mixed.phase == Phase::complete && mixed.completed == 2);
    const std::string both = bytes();
    // Complete appends must be authentic, known and ordered.
    std::string flipped = v1;
    flipped[200] ^= 1;
    put(path, flipped);
    assert(!loads(false) && bytes() == flipped);
    put(path, both.substr(v1.size()) + v1); // schema 1 after schema 2
    assert(!loads(false));
    std::string unknown = v1;
    reinterpret_cast<uint32_t *>(unknown.data())[1] = 3;
    put(path, unknown);
    assert(!loads(false));
    RecordV1 wide;
    wide.generation = 1;
    wide.count = v1_entries + 1; // impossible for schema 1, even with a valid hash
    char sha[65];
    setup::Sha256 h;
    h.update(reinterpret_cast<const BYTE *>(&wide), sizeof(wide));
    h.finish(sha);
    put(path,
        std::string(reinterpret_cast<const char *>(&wide), sizeof(wide)) + std::string(sha, 64));
    assert(!loads(false));
    // A schema 1 record is never written by this code.
    put(path, both);
    RecordLog log(path);
    Record any;
    bool exists = false;
    assert(log.load(any, exists, LogStore::check) && exists);
    any.version = 1;
    assert(!log.save(any, LogStore::check) && bytes() == both);
}
int main() {
    v1_log();
    unsigned upgrade_points = v1_resume(0);
    for (unsigned n = 1; n <= upgrade_points; ++n)
        v1_resume(n);
    full_plan(false);
    full_plan(true);
    boundary();
    printf("PASS Win98 schema 2 boot records: schema 1 log reads, torn tails and ordering, "
           "%u interrupted schema 1 runs resumed, 17-entry SYSBCKUP plan installed, rolled back "
           "and uninstalled, %u-entry boundary within %u WININIT receipts\n",
           upgrade_points, max_entries, wininit_slots);
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
