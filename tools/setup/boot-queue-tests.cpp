// SPDX-License-Identifier: GPL-2.0-or-later
// Execute the production Win32 adapter through bounded syscall seams.
#define DG_BOOT_QUEUE_TEST
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
using BYTE = unsigned char;
using DWORD = uint32_t;
using UINT = unsigned;
using LONG = int32_t;
using HKEY = uintptr_t;
using HMODULE = uintptr_t;
using BOOL = bool;
using FARPROC = void (*)();
#define WINAPI

constexpr HKEY HKEY_LOCAL_MACHINE = 1;
constexpr LONG ERROR_SUCCESS = 0, ERROR_FILE_NOT_FOUND = 2, ERROR_ACCESS_DENIED = 5,
               ERROR_MORE_DATA = 234;
constexpr DWORD KEY_QUERY_VALUE = 1, KEY_SET_VALUE = 2, REG_MULTI_SZ = 7, REG_DWORD = 4, CP_ACP = 0,
                MB_ERR_INVALID_CHARS = 8;
constexpr DWORD MOVEFILE_DELAY_UNTIL_REBOOT = 4, MOVEFILE_REPLACE_EXISTING = 1,
                VER_PLATFORM_WIN32_NT = 2;
constexpr bool FALSE = false;
struct OSVERSIONINFOA {
    DWORD dwOSVersionInfoSize{}, dwMajorVersion = 5, dwMinorVersion = 0, dwPlatformId = 2;
};
struct Value {
    DWORD type;
    std::vector<BYTE> bytes;
};
static std::map<std::string, Value> values;
static unsigned mutations, fail_at;
static bool fail_after, foreign_after_enqueue;
static unsigned opens;
static unsigned platform = 2, major = 5, minor = 0;
static bool missing_export = false;
static bool before() {
    ++mutations;
    return !(fail_at == mutations && !fail_after);
}
static bool after() {
    return !(fail_at == mutations && fail_after);
}
static std::string narrow(const wchar_t *s) {
    std::string r;
    for (; *s; ++s)
        r += char(*s);
    return r;
}
static std::string lower(std::string s) {
    for (auto &c : s)
        if (c >= 'A' && c <= 'Z')
            c += 32;
    return s;
}
static int lstrcmpiA(const char *a, const char *b) {
    return lower(a).compare(lower(b));
}
static bool GetVersionExA(OSVERSIONINFOA *os) {
    os->dwPlatformId = platform;
    os->dwMajorVersion = major;
    os->dwMinorVersion = minor;
    return true;
}
static UINT GetSystemDirectoryA(char *out, unsigned cap) {
    constexpr char p[] = "C:\\WINDOWS\\SYSTEM32";
    if (cap < sizeof(p))
        return sizeof(p);
    std::memcpy(out, p, sizeof(p));
    return sizeof(p) - 1;
}
static LONG RegOpenKeyExA(HKEY, const char *, DWORD, DWORD, HKEY *out) {
    *out = 2;
    ++opens;
    return 0;
}
static LONG RegCloseKey(HKEY) {
    assert(opens);
    --opens;
    return 0;
}
static LONG RegQueryValueExW(HKEY, const wchar_t *name, DWORD *, DWORD *type, BYTE *out,
                             DWORD *size) {
    auto it = values.find(narrow(name));
    if (it == values.end())
        return ERROR_FILE_NOT_FOUND;
    if (type)
        *type = it->second.type;
    if (!size)
        return 0;
    DWORD required = unsigned(it->second.bytes.size()), cap = *size;
    *size = required;
    if (out && cap < required)
        return ERROR_MORE_DATA;
    if (out)
        std::memcpy(out, it->second.bytes.data(), required);
    return 0;
}
static LONG RegSetValueExW(HKEY, const wchar_t *name, DWORD, DWORD type, const BYTE *data,
                           DWORD size) {
    if (!before())
        return ERROR_ACCESS_DENIED;
    values[narrow(name)] = {type, {data, data + size}};
    return after() ? 0 : ERROR_ACCESS_DENIED;
}
static LONG RegDeleteValueW(HKEY, const wchar_t *name) {
    if (!before())
        return ERROR_ACCESS_DENIED;
    auto n = values.erase(narrow(name));
    return after() ? (n ? 0 : ERROR_FILE_NOT_FOUND) : ERROR_ACCESS_DENIED;
}
static LONG RegFlushKey(HKEY) {
    if (!before())
        return ERROR_ACCESS_DENIED;
    return after() ? 0 : ERROR_ACCESS_DENIED;
}
static int MultiByteToWideChar(DWORD, DWORD, const char *in, int, wchar_t *out, int cap) {
    unsigned n = unsigned(std::strlen(in)) + 1;
    if (n > unsigned(cap))
        return 0;
    for (unsigned i = 0; i < n; ++i)
        out[i] = (unsigned char)in[i];
    return int(n);
}
static bool MoveFileExW(const wchar_t *source, const wchar_t *dest, DWORD flags) {
    assert(flags == (MOVEFILE_DELAY_UNTIL_REBOOT | (dest ? MOVEFILE_REPLACE_EXISTING : 0)));
    if (!before())
        return false;
    std::vector<uint16_t> data;
    auto it = values.find("PendingFileRenameOperations");
    if (it != values.end()) {
        assert(it->second.type == REG_MULTI_SZ);
        data.resize(it->second.bytes.size() / 2);
        std::memcpy(data.data(), it->second.bytes.data(), it->second.bytes.size());
        data.pop_back();
    }
    for (unsigned side = 0; side < 2; ++side) {
        if (side && !dest) {
            data.push_back(0);
            continue;
        }
        if (side)
            data.push_back('!');
        for (char c : std::string("\\??\\"))
            data.push_back(c);
        const wchar_t *s = side ? dest : source;
        for (; *s; ++s)
            data.push_back(uint16_t(*s));
        data.push_back(0);
    }
    data.push_back(0);
    Value v{REG_MULTI_SZ, {}};
    v.bytes.resize(data.size() * 2);
    std::memcpy(v.bytes.data(), data.data(), v.bytes.size());
    values["PendingFileRenameOperations"] = v;
    if (foreign_after_enqueue) {
        values["PendingFileRenameOperations"].bytes[0] = 'X';
        foreign_after_enqueue = false;
    }
    return after();
}
static HMODULE GetModuleHandleA(const char *) {
    return 3;
}
static FARPROC GetProcAddress(HMODULE, const char *name) {
    if (missing_export)
        return nullptr;
    FARPROC result = nullptr;
    auto copy = [&result](auto function) {
        static_assert(sizeof(function) == sizeof(result));
        std::memcpy(&result, &function, sizeof(result));
    };
    if (!std::strcmp(name, "MoveFileExW"))
        copy(&MoveFileExW);
    if (!std::strcmp(name, "RegQueryValueExW"))
        copy(&RegQueryValueExW);
    if (!std::strcmp(name, "RegSetValueExW"))
        copy(&RegSetValueExW);
    if (!std::strcmp(name, "RegDeleteValueW"))
        copy(&RegDeleteValueW);
    return result;
}
#include "boot-queue-win32.h"
#include "boot-wait.h"
using namespace setup::boot;
struct Store {
    Record durable{};
    bool has = false;
    std::map<std::string, File> files;
    bool save(const Record &r) {
        if (!before())
            return false;
        durable = r;
        has = true;
        return after();
    }
    bool inspect(const char *p, File &f) {
        f = files[lower(p)];
        return true;
    }
    bool approved(const Entry &e) {
        return !e.desired.exists ||
               lower(e.source).starts_with("c:\\windows\\dreamgpu\\generation1\\");
    }
};
static File image(char c) {
    File f;
    f.exists = 1;
    f.size = 123;
    for (unsigned n = 0; n < 64; ++n)
        f.sha[n] = c;
    return f;
}
static Record initial(Store &s, unsigned count = 3) {
    values.clear();
    mutations = fail_at = 0;
    fail_after = foreign_after_enqueue = false;
    s = {};
    Record r;
    r.generation = 1;
    r.count = count;
    for (unsigned n = 0; n < count; ++n) {
        auto &e = r.entries[n];
        std::snprintf(e.source, sizeof(e.source), "C:\\WINDOWS\\DreamGPU\\generation1\\%u.new", n);
        const char *names[] = {"ddraw.dll", "d3d8.dll", "d3d9.dll"};
        if (n < 3)
            std::snprintf(e.destination, sizeof(e.destination), "C:\\WINDOWS\\SYSTEM32\\%s",
                          names[n]);
        else
            std::snprintf(e.destination, sizeof(e.destination),
                          "C:\\WINDOWS\\SYSTEM32\\owned%u.dll", n);
        e.protected_target = n < 3;
        e.before = image('a');
        e.desired = image('b');
        s.files[lower(e.source)] = e.desired;
        s.files[lower(e.destination)] = e.before;
    }
    return r;
}
static Result resume(Store &s, Record &r) {
    Win32Ops ops(s);
    assert(ops.open(true));
    Queue q(ops, r);
    return q.resume();
}
static Result cancel(Store &s, Record &r) {
    Win32Ops ops(s);
    assert(ops.open(true));
    Queue q(ops, r);
    return q.cancel();
}
static void boot(Store &s, const Record &r, unsigned count = 16) {
    unsigned consumed = 0;
    for (unsigned n = 0; n < r.count; ++n) {
        const auto &e = r.entries[n];
        if (e.desired.exists ? !s.files[lower(e.source)].exists
                             : !s.files[lower(e.destination)].exists)
            continue;
        if (consumed++ == count)
            break;
        s.files[lower(e.destination)] = e.desired;
        if (e.desired.exists)
            s.files[lower(e.source)] = {};
    }
    values.erase("PendingFileRenameOperations");
    values.erase("AllowProtectedRenames");
}
static void foreign_queue(const std::vector<std::u16string> &members) {
    std::vector<BYTE> bytes;
    for (const auto &member : members) {
        for (char16_t c : member) {
            bytes.push_back(BYTE(c));
            bytes.push_back(BYTE(c >> 8));
        }
        bytes.push_back(0);
        bytes.push_back(0);
    }
    bytes.push_back(0);
    bytes.push_back(0);
    values["PendingFileRenameOperations"] = {REG_MULTI_SZ, bytes};
}
static void foreign_wait_tests() {
    Store store;
    const std::vector<std::u16string> xp_deletes = {u"\\??\\C:\\WINDOWS\\system32\\OLD3.tmp", u"",
                                                    u"\\??\\C:\\WINDOWS\\system32\\OLD7.tmp", u"",
                                                    u"\\??\\C:\\WINDOWS\\system32\\OLDB.tmp", u""};
    for (bool permission : {false, true}) {
        auto r = initial(store);
        foreign_queue(xp_deletes);
        if (permission)
            values["AllowProtectedRenames"] = {REG_DWORD, {1, 0, 0, 0}};
        const auto original = values["PendingFileRenameOperations"].bytes;
        Win32Ops ops(store);
        assert(ops.open(true));
        RawQueue observed;
        Permission actual;
        assert(wait_before_registration(ops, r, observed, actual) == Deferral::pending);
        assert(actual == (permission ? Permission::one : Permission::absent));
        assert(mutations == 0 && r.phase == Phase::prepared);
        assert(values["PendingFileRenameOperations"].bytes == original);
        Queue unchanged(ops, r);
        assert(unchanged.resume() == Result::conflict); // Core ownership rule unchanged.
        values.clear(); // Model Windows consuming its own queue at a real boot.
        assert(wait_before_registration(ops, r, observed, actual) == Deferral::none);
        assert(unchanged.resume() == Result::pending);
    }
    for (const auto &path :
         {u"\\??\\c:\\windows\\system32\\DDRAW.DLL",
          u"\\??\\C:\\WINDOWS\\DreamGPU\\generation1\\0.new",
          u"\\??\\C:\\WINDOWS\\SYSTEM~1\\OLD3.tmp", u"\\??\\C:\\WINDOWS\\system32\\..\\other.tmp",
          u"\\??\\C:\\WINDOWS\\system32\\ddraw.dll:stream",
          u"\\??\\C:\\WINDOWS\\system32\\OLD3.tmp.", u"\\??\\UNC\\server\\path",
          u"C:\\WINDOWS\\system32\\old.tmp"}) {
        auto r = initial(store);
        foreign_queue({path, u""});
        Win32Ops ops(store);
        assert(ops.open(true));
        RawQueue observed;
        Permission permission;
        assert(wait_before_registration(ops, r, observed, permission) == Deferral::conflict);
        assert(!mutations);
    }
    for (unsigned conflict = 0; conflict < 4; ++conflict) {
        auto r = initial(store);
        foreign_queue(xp_deletes);
        if (conflict == 0)
            foreign_queue({u"\\??\\C:\\WINDOWS\\system32\\OTHER.tmp",
                           u"!\\??\\C:\\WINDOWS\\system32\\d3d8.dll"});
        if (conflict == 1)
            store.files[lower(r.entries[0].destination)] = image('c');
        if (conflict == 2)
            store.files[lower(r.entries[0].source)] = image('c');
        if (conflict == 3)
            values["PendingFileRenameOperations2"] = {REG_MULTI_SZ, {0, 0, 0, 0}};
        Win32Ops ops(store);
        assert(ops.open(true));
        RawQueue observed;
        Permission permission;
        assert(wait_before_registration(ops, r, observed, permission) == Deferral::conflict);
        assert(!mutations);
    }
    puts("PASS actual NT queue deferral: XP three deletions preserved, no registration or "
         "permission "
         "mutation, consumed queue resumes; collisions/aliases/malformed paths and drift rejected");
}
int main() {
    foreign_wait_tests();
    Store s;
    Record r = initial(s, 16);
    assert(resume(s, r) == Result::pending);
    unsigned points = mutations;
    assert(opens == 0);
    for (bool after_failure : {false, true})
        for (unsigned point = 1; point <= points; ++point) {
            r = initial(s, 16);
            Record input = r;
            fail_at = point;
            fail_after = after_failure;
            (void)resume(s, r);
            r = s.has ? s.durable : input;
            fail_at = 0;
            assert(resume(s, r) == Result::pending);
            boot(s, r);
            assert(resume(s, r) == Result::complete);
            assert(opens == 0);
        }
    r = initial(s);
    assert(resume(s, r) == Result::pending);
    boot(s, r, 1);
    r = s.durable;
    assert(resume(s, r) == Result::pending);
    assert(r.completed == 1);
    boot(s, r);
    assert(resume(s, r) == Result::complete);
    r = initial(s);
    assert(resume(s, r) == Result::pending);
    mutations = 0;
    assert(cancel(s, r) == Result::cancelled);
    unsigned cancel_points = mutations;
    for (bool after_failure : {false, true})
        for (unsigned point = 1; point <= cancel_points; ++point) {
            r = initial(s);
            assert(resume(s, r) == Result::pending);
            mutations = 0;
            fail_at = point;
            fail_after = after_failure;
            (void)cancel(s, r);
            r = s.durable;
            fail_at = 0;
            assert(cancel(s, r) == Result::cancelled);
            assert(values.empty());
        }
    r = initial(s);
    assert(resume(s, r) == Result::pending);
    boot(s, r, 1);
    assert(cancel(s, r) == Result::needs_restore);
    assert(r.completed == 1);
    // Original absence is supported for installing a new, manifest-approved runtime.
    r = initial(s);
    r.entries[0].before = {};
    s.files[lower(r.entries[0].destination)] = {};
    assert(resume(s, r) == Result::pending);
    boot(s, r);
    assert(resume(s, r) == Result::complete);
    // Restore uses a new immutable batch, not an inverted pending queue.
    Record restore = initial(s);
    for (unsigned n = 0; n < restore.count; ++n) {
        auto &e = restore.entries[n];
        e.before = image('b');
        e.desired = image('a');
        s.files[lower(e.source)] = e.desired;
        s.files[lower(e.destination)] = e.before;
    }
    assert(resume(s, restore) == Result::pending);
    boot(s, restore);
    assert(resume(s, restore) == Result::complete);
    // Foreign values are never adopted, widened, cancelled, or overwritten.
    for (const char *key :
         {"PendingFileRenameOperations", "PendingFileRenameOperations2", "AllowProtectedRenames"}) {
        r = initial(s);
        values[key] = {REG_DWORD, {1, 0, 0, 0}};
        auto old = values[key].bytes;
        assert(resume(s, r) == Result::conflict);
        assert(mutations == 0 && values[key].bytes == old);
    }
    r = initial(s);
    foreign_after_enqueue = true;
    assert(resume(s, r) == Result::conflict);
    auto foreign = values["PendingFileRenameOperations"].bytes;
    assert(cancel(s, r) == Result::conflict);
    assert(values["PendingFileRenameOperations"].bytes == foreign);
    r = initial(s);
    assert(resume(s, r) == Result::pending);
    values["AllowProtectedRenames"].bytes[0] = 2;
    assert(cancel(s, r) == Result::conflict);
    r = initial(s);
    assert(resume(s, r) == Result::pending);
    boot(s, r);
    s.files[lower(r.entries[0].destination)] = image('c');
    assert(resume(s, r) == Result::conflict);
    r = initial(s);
    std::strcpy(r.entries[1].destination, r.entries[0].destination);
    assert(resume(s, r) == Result::invalid);
    r = initial(s);
    r.entries[0].desired = {};
    assert(resume(s, r) == Result::invalid); // deletion must not carry a staging path
    r = initial(s);
    r.entries[0].protected_target = 0;
    assert(resume(s, r) == Result::invalid);
    r = initial(s);
    std::strcpy(r.entries[0].destination, "C:\\WINDOWS\\SYSTEM32\\..\\ddraw.dll");
    assert(resume(s, r) == Result::invalid);
    // Mixed deletion/replacement retains the interior empty second queue item.
    unsigned delete_points = 0;
    for (bool after_failure : {false, true}) {
        for (unsigned point = 0; point <= delete_points || point == 0; ++point) {
            r = initial(s);
            r.entries[0].desired = {};
            r.entries[0].source[0] = 0;
            Record input = r;
            fail_at = point;
            fail_after = after_failure;
            auto result = resume(s, r);
            if (!point) {
                assert(result == Result::pending);
                delete_points = mutations;
            }
            r = s.has ? s.durable : input;
            fail_at = 0;
            assert(resume(s, r) == Result::pending);
            boot(s, r, 1);
            assert(!s.files[lower(r.entries[0].destination)].exists);
            assert(resume(s, r) == Result::pending);
            assert(r.completed == 1);
            boot(s, r);
            assert(resume(s, r) == Result::complete);
        }
    }
    r = initial(s);
    for (unsigned n = 0; n < r.count; ++n) {
        r.entries[n].desired = {};
        r.entries[n].source[0] = 0;
    }
    assert(resume(s, r) == Result::pending);
    assert(cancel(s, r) == Result::cancelled);
    for (unsigned n = 0; n < r.count; ++n)
        assert(same(s.files[lower(r.entries[n].destination)], r.entries[n].before));
    r = initial(s);
    for (unsigned n = 0; n < r.count; ++n) {
        r.entries[n].desired = {};
        r.entries[n].source[0] = 0;
    }
    assert(resume(s, r) == Result::pending);
    boot(s, r, 1);
    assert(cancel(s, r) == Result::needs_restore);
    // Restore a removed originally present file through the ordinary new-file branch.
    r = initial(s, 1);
    r.entries[0].before = {};
    r.entries[0].desired = image('a');
    s.files[lower(r.entries[0].destination)] = {};
    s.files[lower(r.entries[0].source)] = image('a');
    assert(resume(s, r) == Result::pending);
    boot(s, r);
    assert(resume(s, r) == Result::complete);
    // A nonprotected batch never widens the one-boot protected-file policy.
    r = initial(s, 1);
    std::strcpy(r.entries[0].destination, "C:\\WINDOWS\\SYSTEM32\\dgpugl.dll");
    r.entries[0].protected_target = 0;
    s.files[lower(r.entries[0].destination)] = r.entries[0].before;
    assert(resume(s, r) == Result::pending);
    assert(!values.contains("AllowProtectedRenames"));
    boot(s, r);
    assert(resume(s, r) == Result::complete);
    // A byte-identical queue is still foreign to a new prepared record.
    r = initial(s);
    Record unowned = r;
    assert(resume(s, r) == Result::pending);
    unsigned prior = mutations;
    assert(resume(s, unowned) == Result::conflict);
    assert(mutations == prior);
    // NT-only exports are resolved after OS detection; a Win98 process can load
    // the unified installer without any static dependency on these entrypoints.
    {
        r = initial(s);
        platform = 1;
        Win32Ops unsupported(s);
        assert(!unsupported.open(true));
        platform = 2;
    }
    {
        r = initial(s);
        missing_export = true;
        Win32Ops unavailable(s);
        assert(!unavailable.open(true));
        missing_export = false;
    }
    r = initial(s);
    Win32Ops ops(s);
    assert(ops.open(false));
    Queue q(ops, r);
    assert(q.resume() == Result::invalid);
    std::printf("PASS boot queue actual Win32 adapter: %u registration points, before/after "
                "failures, partial boot, cancel, restore, foreign state\n",
                points);
}
