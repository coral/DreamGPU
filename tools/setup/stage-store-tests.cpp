// SPDX-License-Identifier: GPL-2.0-or-later
#define DG_SETUP_ADAPTER_TEST
#include "test-win32.h"
#include <cassert>
constexpr DWORD ERROR_NO_MORE_FILES = 18, ERROR_PATH_NOT_FOUND = 3;
struct WIN32_FIND_DATAA {
    DWORD dwFileAttributes;
    char cFileName[MAX_PATH];
};
static DWORD GetWindowsDirectoryA(char *out, DWORD size) {
    constexpr char path[] = "C:\\WINDOWS";
    if (size < sizeof(path))
        return size;
    strcpy(out, path);
    return sizeof(path) - 1;
}
struct Scan {
    std::vector<std::string> paths;
    size_t position = 0;
};
static std::map<HANDLE, Scan> scans;
static void found(Scan &s, WIN32_FIND_DATAA *out) {
    const auto &path = s.paths.at(s.position);
    strcpy(out->cFileName, path.substr(path.rfind('\\') + 1).c_str());
    const auto &file = fake_win32::files.at(path);
    out->dwFileAttributes = file.attributes | (file.directory ? FILE_ATTRIBUTE_DIRECTORY : 0);
}
static HANDLE FindFirstFileA(const char *pattern, WIN32_FIND_DATAA *out) {
    auto root = fake_win32::canon(pattern);
    root.resize(root.size() - 1);
    Scan scan;
    for (const auto &[path, unused] : fake_win32::files)
        if (path.starts_with(root) && path.find('\\', root.size()) == std::string::npos)
            scan.paths.push_back(path);
    if (scan.paths.empty()) {
        fake_win32::error = ERROR_FILE_NOT_FOUND;
        return INVALID_HANDLE_VALUE;
    }
    HANDLE id = fake_win32::next++;
    scans[id] = scan;
    found(scans[id], out);
    return id;
}
static bool FindNextFileA(HANDLE id, WIN32_FIND_DATAA *out) {
    auto &s = scans.at(id);
    if (++s.position == s.paths.size()) {
        fake_win32::error = ERROR_NO_MORE_FILES;
        return false;
    }
    found(s, out);
    return true;
}
static bool FindClose(HANDLE id) {
    return scans.erase(id) != 0;
}
static bool stage_move(const char *from, const char *to) {
    auto a = fake_win32::canon(from), b = fake_win32::canon(to);
    if (!MoveFileA(from, to))
        return false;
    std::vector<std::pair<std::string, decltype(fake_win32::files)::mapped_type>> children;
    for (auto it = fake_win32::files.begin(); it != fake_win32::files.end();) {
        if (it->first.starts_with(a + "\\")) {
            children.emplace_back(b + it->first.substr(a.size()), it->second);
            it = fake_win32::files.erase(it);
        } else
            ++it;
    }
    for (auto &item : children)
        fake_win32::files.insert(item);
    return true;
}
static bool RemoveDirectoryA(const char *path) {
    auto key = fake_win32::canon(path);
    auto it = fake_win32::files.find(key);
    if (it == fake_win32::files.end() || !it->second.directory)
        return false;
    for (const auto &[name, unused] : fake_win32::files)
        if (name.starts_with(key + "\\"))
            return false;
    if (fake_win32::fault())
        return false;
    fake_win32::files.erase(it);
    return true;
}
static DWORD stage_attributes(const char *path) {
    DWORD attrs = GetFileAttributesA(path);
    if (attrs != INVALID_FILE_ATTRIBUTES)
        return attrs;
    auto key = fake_win32::canon(path);
    auto slash = key.rfind('\\');
    if (slash != std::string::npos && !fake_win32::files.count(key.substr(0, slash)))
        fake_win32::error = ERROR_PATH_NOT_FOUND;
    return attrs;
}
#define GetFileAttributesA stage_attributes
#define MoveFileA stage_move
#include "stage-store.h"
#undef MoveFileA
#undef GetFileAttributesA
using setup::staging::Store;
constexpr char Intent[] = "C:\\WINDOWS\\DGSETUP.JRN";
constexpr char Root[] = "C:\\WINDOWS\\DGSETUP.NEW";
constexpr char Final[] = "C:\\WINDOWS\\DreamGPU";
constexpr char Hash[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static std::vector<BYTE> payload(70001, 0x39);
static Store store;
static void configure(const char *hash = Hash, setup::Os os = setup::Os::win98) {
    assert(store.reset(os, hash));
    assert(store.add("drivers/file.dll", payload.data(), DWORD(payload.size())));
    assert(store.add("RESULT.json", reinterpret_cast<const BYTE *>("done"), 4));
}
static void initial() {
    fake_win32::reset();
    scans.clear();
    fake_win32::files[fake_win32::canon("C:\\WINDOWS")].directory = true;
    configure();
}
static bool execute() {
    return store.begin() && store.copy() && store.commit() && store.finish();
}
static void clean_handles() {
    assert(fake_win32::handles.empty() && scans.empty());
}
static std::vector<std::pair<std::string, std::string>> activity;
static bool fail_reported_file = false;
static void observe(const char *operation, const char *path) {
    activity.emplace_back(operation, path);
    if (fail_reported_file && !strcmp(operation, "Extracting file"))
        fake_win32::fail = fake_win32::mutation + 1;
}
static void progress_reporting() {
    initial();
    assert(execute());
    const auto mutations = fake_win32::mutation;
    initial();
    setup::progress_observer = observe;
    assert(execute() && fake_win32::mutation == mutations);
    assert(activity.size() == 6);
    assert(activity[0].first == "Preparing staging receipt" && activity[0].second == Intent);
    assert(activity[1].first == "Preparing staging directory" && activity[1].second == Root);
    assert(activity[2].first == "Checking staging directory contents");
    assert(activity[3].first == "Creating directory");
    assert(activity[4].first == "Extracting file" &&
           activity[4].second == std::string(Root) + "\\drivers\\file.dll");
    assert(activity[5].second == std::string(Root) + "\\RESULT.json");
    clean_handles();
    initial();
    activity.clear();
    fail_reported_file = true;
    assert(!execute());
    // An I/O failure retains the failing filename, not the next file or a
    // premature success message. Observing progress adds no disk mutations.
    assert(activity.back().first == "Could not open installation file" &&
           activity.back().second == std::string(Root) + "\\drivers\\file.dll");
    assert(setup::failure_error == ERROR_ACCESS_DENIED);
    clean_handles();
    setup::progress_observer = nullptr;
    fail_reported_file = false;
    activity.clear();
    initial();
    assert(execute() && activity.empty()); // /silent leaves the observer unset.
    puts("PASS file progress, failing filename retention and silent installation");
}
static void faults() {
    initial();
    assert(execute());
    auto total = fake_win32::mutation;
    for (unsigned fail = 1; fail <= total; ++fail) {
        initial();
        fake_win32::fail = fail;
        assert(!execute());
        clean_handles();
        fake_win32::fail = 0;
        configure();
        assert(execute());
        clean_handles();
        assert(!fake_win32::files.count(fake_win32::canon(Intent)) &&
               !fake_win32::files.count(fake_win32::canon(Root)));
        assert(fake_win32::files.at(fake_win32::canon("C:\\WINDOWS\\DreamGPU\\drivers\\file.dll"))
                   .bytes == payload);
    }
    printf("PASS staging: %u syscall failure/restart points\n", total);
}
static void crashes_and_foreign() {
    initial();
    assert(store.begin());
    // Simulate a process dying during either resource chunk, without cleanup.
    assert(CreateDirectoryA("C:\\WINDOWS\\DGSETUP.NEW\\drivers", nullptr));
    auto file = fake_win32::canon("C:\\WINDOWS\\DGSETUP.NEW\\drivers\\file.dll");
    for (unsigned length : {0u, 1u, 65535u, 65536u, 70001u}) {
        fake_win32::files[file].bytes.assign(payload.begin(), payload.begin() + length);
        configure();
        assert(store.begin() && store.copy());
    }
    configure();
    assert(store.begin() && store.commit()); // lost rename acknowledgement
    configure();
    assert(execute());
    assert(
        fake_win32::files.at(fake_win32::canon("C:\\WINDOWS\\DreamGPU\\drivers\\file.dll")).bytes ==
        payload);

    initial();
    assert(store.begin());
    auto original = fake_win32::files;
    for (const auto *extra : {"foreign.dll", "drivers", "foreign-dir"}) {
        fake_win32::files = original;
        auto path = fake_win32::canon((std::string(Root) + "\\" + extra).c_str());
        fake_win32::files[path].bytes = {1};
        configure();
        auto writes = fake_win32::mutation;
        assert(!store.begin() && fake_win32::mutation == writes);
    }
    fake_win32::files = original;
    fake_win32::files[fake_win32::canon(std::string(Root).append("\\drivers").c_str())].directory =
        true;
    fake_win32::files[file].bytes = {0x40};
    configure();
    assert(!store.begin() && fake_win32::files[file].bytes == std::vector<BYTE>{0x40});

    initial();
    assert(store.begin());
    auto writes = fake_win32::mutation;
    configure("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    assert(!store.begin() && fake_win32::mutation == writes);
    configure(Hash, setup::Os::nt5);
    assert(!store.begin() && fake_win32::mutation == writes);
    configure();
    payload[0] ^= 1;
    assert(!store.begin() && fake_win32::mutation == writes);
    payload[0] ^= 1;

    initial();
    fake_win32::files[fake_win32::canon(Root)].directory = true;
    auto foreign = fake_win32::files;
    assert(!store.begin() && fake_win32::mutation == 0);
    assert(fake_win32::files.size() == foreign.size());
    initial();
    fake_win32::files[fake_win32::canon(Final)].directory = true;
    assert(!store.begin() && fake_win32::mutation == 0);
    initial();
    fake_win32::files[fake_win32::canon(Intent)].bytes = {}; // unbound metadata
    assert(!store.begin() && fake_win32::mutation == 0);
    initial();
    assert(store.begin());
    fake_win32::files[fake_win32::canon(Intent)].bytes.resize(20); // binding never complete
    configure();
    writes = fake_win32::mutation;
    assert(!store.begin() && fake_win32::mutation == writes);
    initial();
    assert(store.begin() && store.copy());
    fake_win32::files[fake_win32::canon(std::string(Root).append("\\foreign.dll").c_str())]
        .bytes = {1};
    assert(!store.commit()); // verify again immediately before publication
    clean_handles();
}
static void cancellation_setup(bool renamed) {
    initial();
    assert(store.begin() && store.copy());
    if (renamed)
        assert(store.commit());
    configure();
    assert(store.begin(true));
    assert(!store.copy() && !store.commit() && !store.finish());
    fake_win32::mutation = 0;
}
static void cancellation() {
    unsigned cases = 0;
    for (bool renamed : {false, true}) {
        cancellation_setup(renamed);
        assert(store.cancel());
        const auto operations = fake_win32::mutation;
        for (unsigned fail = 1; fail <= operations; ++fail) {
            cancellation_setup(renamed);
            fake_win32::fail = fail;
            assert(!store.cancel());
            clean_handles();
            fake_win32::fail = 0;
            if (setup::staging::cancelling() == setup::staging::Presence::present) {
                configure();
                assert(!store.begin()); // forward replay never resurrects cancellation
            }
            configure();
            assert(store.begin(true) && store.cancel());
            assert(setup::staging::pending() == setup::staging::Presence::absent);
            assert(fake_win32::files.size() == 1);
            ++cases;
        }
    }
    initial();
    assert(store.begin()); // no payloads exist: cancellation never creates them
    configure();
    assert(store.begin(true) && store.cancel() && fake_win32::files.size() == 1);
    cancellation_setup(false);
    fake_win32::files[fake_win32::canon("C:\\WINDOWS\\DGSETUP.NEW\\foreign.dll")].bytes = {1};
    auto writes = fake_win32::mutation;
    assert(!store.cancel() && fake_win32::mutation == writes);
    cancellation_setup(true);
    fake_win32::files[fake_win32::canon("C:\\WINDOWS\\DreamGPU\\P1.JRN")].bytes = {1};
    writes = fake_win32::mutation;
    assert(!store.cancel() &&
           fake_win32::mutation == writes); // component files prohibit tree cancellation
    clean_handles();
    printf(
        "PASS staging cancellation: %u cleanup failure/restart points, durable reverse direction\n",
        cases);
}
static void replacement_staging() {
    auto abandoned = [] {
        initial();
        assert(store.begin() && store.copy());
        // A different build's catalog, corrupt receipt, readonly file, and
        // cancellation marker all belong to disposable extraction state.
        fake_win32::files[fake_win32::canon(Intent)].bytes = {'o', 'l', 'd'};
        fake_win32::files[fake_win32::canon("C:\\WINDOWS\\DGSETUP.CAN")].bytes = {'x'};
        auto &obsolete = fake_win32::files[fake_win32::canon("C:\\WINDOWS\\DGSETUP.NEW\\old.txt")];
        obsolete.bytes = {'o'};
        obsolete.attributes = FILE_ATTRIBUTE_READONLY;
        fake_win32::files[fake_win32::canon("C:\\WINDOWS\\keep.txt")].bytes = {'k'};
        fake_win32::mutation = 0;
    };
    abandoned();
    assert(Store::discard_pending());
    const unsigned mutations = fake_win32::mutation;
    for (unsigned fault = 0; fault <= mutations; ++fault) {
        abandoned();
        fake_win32::fail = fault;
        assert(Store::discard_pending() == (fault == 0));
        clean_handles();
        fake_win32::fail = 0;
        assert(Store::discard_pending());
        assert(fake_win32::files.size() == 2); // Windows and unrelated keep.txt
        assert(fake_win32::files.at(fake_win32::canon("C:\\WINDOWS\\keep.txt")).bytes ==
               std::vector<BYTE>{'k'});
        configure("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
        assert(store.add("LICENSES.txt", reinterpret_cast<const BYTE *>("all notices"), 11));
        assert(execute());
        clean_handles();
    }
    // A completed installation and its Windows before-images are not scratch
    // space. The existing automatic upgrade path owns replacing that install.
    const auto installed = fake_win32::files;
    assert(Store::discard_pending());
    assert(fake_win32::files.size() == installed.size());
    for (const auto &[name, node] : installed)
        assert(fake_win32::files.at(name).bytes == node.bytes);
    // Orphaned extraction without a receipt is also disposable.
    abandoned();
    fake_win32::files.erase(fake_win32::canon(Intent));
    fake_win32::files.erase(fake_win32::canon("C:\\WINDOWS\\DGSETUP.CAN"));
    assert(Store::discard_pending());
    assert(fake_win32::files.size() == 2);
    clean_handles();
    printf("PASS new installer replaces stale staging: %u cleanup failure/retry points\n",
           mutations);
}
int main() {
    replacement_staging();
    progress_reporting();
    cancellation();
    faults();
    crashes_and_foreign();
    puts("PASS exact installer/OS/catalog binding, partial prefix resume, rename recovery and "
         "foreign refusal");
}
