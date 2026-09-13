// SPDX-License-Identifier: GPL-2.0-or-later
// Host test syscall seam for the actual Win32Store. No transaction policy here.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <map>
#include <vector>
#include <algorithm>
using BYTE = unsigned char;
using DWORD = uint32_t;
using LONG = int32_t;
using HANDLE = intptr_t;
using HKEY = intptr_t;
using HRSRC = intptr_t;
using HGLOBAL = intptr_t;
constexpr HANDLE INVALID_HANDLE_VALUE = -1;
constexpr int MAX_PATH = 260;
constexpr HKEY HKEY_LOCAL_MACHINE = 1;
constexpr DWORD INVALID_FILE_ATTRIBUTES = 0xffffffff, INVALID_FILE_SIZE = 0xffffffff,
                INVALID_SET_FILE_POINTER = 0xffffffff;
constexpr DWORD ERROR_SUCCESS = 0, ERROR_FILE_NOT_FOUND = 2, ERROR_ACCESS_DENIED = 5,
                ERROR_SHARING_VIOLATION = 32, ERROR_MORE_DATA = 234;
constexpr DWORD FILE_ATTRIBUTE_DIRECTORY = 16, FILE_ATTRIBUTE_REPARSE_POINT = 1024,
                FILE_ATTRIBUTE_NORMAL = 128, FILE_SHARE_READ = 1, FILE_SHARE_WRITE = 2,
                FILE_ATTRIBUTE_READONLY = 1;
constexpr DWORD GENERIC_READ = 1, GENERIC_WRITE = 2, OPEN_EXISTING = 3, OPEN_ALWAYS = 4,
                CREATE_NEW = 1, FILE_BEGIN = 0, FILE_END = 2, KEY_READ = 1, KEY_WRITE = 2,
                KEY_QUERY_VALUE = 1, KEY_SET_VALUE = 2, REG_SZ = 1, REG_DWORD = 4;
constexpr bool TRUE = true;
#define MAKEINTRESOURCEA(i) reinterpret_cast<const char *>(uintptr_t(i))
#define RT_RCDATA reinterpret_cast<const char *>(uintptr_t(10))
namespace fake_win32 {
struct Node {
    std::vector<BYTE> bytes;
    bool directory = false;
    bool flushed = false;
    DWORD attributes = FILE_ATTRIBUTE_NORMAL;
};
struct Open {
    std::string path;
    DWORD pos = 0;
    DWORD access = 0, share = 0;
};
struct Value {
    DWORD type;
    std::vector<BYTE> bytes;
};
inline std::map<std::string, Node> files;
inline std::map<intptr_t, Open> handles;
inline std::map<std::string, std::map<std::string, Value>> keys;
inline std::map<intptr_t, std::string> key_handles;
inline std::map<intptr_t, std::vector<BYTE>> resources;
inline bool enforce_file_sharing = false;
inline intptr_t next = 10;
inline DWORD error = 0, mutation = 0, fail = 0;
inline bool fault() {
    if (++mutation == fail) {
        error = ERROR_ACCESS_DENIED;
        return true;
    }
    return false;
}
inline std::string canon(const char *p) {
    std::string r(p);
    for (auto &c : r)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return r;
}
inline void reset() {
    files.clear();
    handles.clear();
    keys.clear();
    key_handles.clear();
    resources.clear();
    next = 10;
    error = mutation = fail = 0;
}
} // namespace fake_win32
inline int lstrlenA(const char *p) {
    return int(strlen(p));
}
inline char *lstrcpyA(char *a, const char *b) {
    return strcpy(a, b);
}
inline char *lstrcatA(char *a, const char *b) {
    return strcat(a, b);
}
inline int lstrcmpA(const char *a, const char *b) {
    return strcmp(a, b);
}
inline int wsprintfA(char *out, const char *format, ...) {
    std::string f(format);
    auto p = f.find("%08lX");
    if (p != std::string::npos)
        f.replace(p, 5, "%08X");
    va_list a;
    va_start(a, format);
    int n = vsnprintf(out, 40, f.c_str(), a);
    va_end(a);
    return n;
}
inline DWORD GetLastError() {
    return fake_win32::error;
}
inline DWORD GetSystemDirectoryA(char *out, DWORD n) {
    constexpr char p[] = "C:\\WINDOWS\\SYSTEM";
    if (n < sizeof(p))
        return sizeof(p);
    strcpy(out, p);
    return sizeof(p) - 1;
}
inline DWORD GetFileAttributesA(const char *p) {
    auto i = fake_win32::files.find(fake_win32::canon(p));
    if (i == fake_win32::files.end()) {
        fake_win32::error = ERROR_FILE_NOT_FOUND;
        return INVALID_FILE_ATTRIBUTES;
    }
    return i->second.attributes | (i->second.directory ? FILE_ATTRIBUTE_DIRECTORY : 0);
}
inline bool SetFileAttributesA(const char *p, DWORD attributes) {
    using namespace fake_win32;
    auto i = files.find(canon(p));
    if (i == files.end() || fault())
        return false;
    i->second.attributes = attributes;
    return true;
}
inline HANDLE CreateFileA(const char *p, DWORD access, DWORD share, void *, DWORD mode, DWORD,
                          void *) {
    using namespace fake_win32;
    auto name = canon(p);
    auto i = files.find(name);
    if (enforce_file_sharing)
        for (const auto &[id, opened] : handles) {
            (void)id;
            if (opened.path == name &&
                (((access & GENERIC_READ) && !(opened.share & FILE_SHARE_READ)) ||
                 ((access & GENERIC_WRITE) && !(opened.share & FILE_SHARE_WRITE)) ||
                 ((opened.access & GENERIC_READ) && !(share & FILE_SHARE_READ)) ||
                 ((opened.access & GENERIC_WRITE) && !(share & FILE_SHARE_WRITE)))) {
                error = 32; // ERROR_SHARING_VIOLATION
                return INVALID_HANDLE_VALUE;
            }
        }
    if (i != files.end() && (i->second.attributes & FILE_ATTRIBUTE_READONLY) &&
        (access & GENERIC_WRITE)) {
        error = ERROR_ACCESS_DENIED;
        return INVALID_HANDLE_VALUE;
    }
    if (mode == CREATE_NEW) {
        if (i != files.end() || fault())
            return INVALID_HANDLE_VALUE;
        files[name] = {};
    } else if (mode == OPEN_ALWAYS && i == files.end()) {
        if (fault())
            return INVALID_HANDLE_VALUE;
        files[name] = {};
    } else if (i == files.end()) {
        error = ERROR_FILE_NOT_FOUND;
        return INVALID_HANDLE_VALUE;
    }
    auto id = next++;
    handles[id] = {name, 0, access, share};
    return id;
}
inline bool CloseHandle(HANDLE h) {
    return fake_win32::handles.erase(h) != 0;
}
inline DWORD GetFileSize(HANDLE h, DWORD *high) {
    if (high)
        *high = 0;
    return DWORD(fake_win32::files[fake_win32::handles[h].path].bytes.size());
}
inline bool ReadFile(HANDLE h, void *out, DWORD size, DWORD *got, void *) {
    using namespace fake_win32;
    auto &o = handles[h];
    auto &b = files[o.path].bytes;
    *got = std::min(size, DWORD(b.size() - o.pos));
    if (*got)
        memcpy(out, b.data() + o.pos, *got);
    o.pos += *got;
    return true;
}
inline bool WriteFile(HANDLE h, const void *data, DWORD size, DWORD *written, void *) {
    using namespace fake_win32;
    if (fault()) {
        *written = 0;
        return false;
    }
    auto &o = handles[h];
    auto &f = files[o.path];
    f.bytes.resize(std::max(f.bytes.size(), size_t(o.pos) + size));
    memcpy(f.bytes.data() + o.pos, data, size);
    o.pos += size;
    *written = size;
    f.flushed = false;
    return true;
}
inline bool FlushFileBuffers(HANDLE h) {
    using namespace fake_win32;
    if (fault())
        return false;
    files[handles[h].path].flushed = true;
    return true;
}
inline DWORD SetFilePointer(HANDLE h, DWORD pos, void *, DWORD from) {
    using namespace fake_win32;
    auto &o = handles[h];
    o.pos = (from == FILE_END ? DWORD(files[o.path].bytes.size()) : 0) + pos;
    return o.pos;
}
inline bool SetEndOfFile(HANDLE h) {
    using namespace fake_win32;
    if (fault())
        return false;
    auto &o = handles[h];
    files[o.path].bytes.resize(o.pos);
    return true;
}
inline bool CopyFileA(const char *a, const char *b, bool exclusive) {
    using namespace fake_win32;
    auto src = files.find(canon(a));
    if (src == files.end() || (exclusive && files.count(canon(b))) || fault())
        return false;
    files[canon(b)] = src->second;
    files[canon(b)].flushed = false;
    return true;
}
inline bool MoveFileA(const char *a, const char *b) {
    using namespace fake_win32;
    auto src = files.find(canon(a));
    if (src == files.end() || files.count(canon(b)) || fault())
        return false;
    files[canon(b)] = src->second;
    files.erase(src);
    return true;
}
inline bool DeleteFileA(const char *p) {
    using namespace fake_win32;
    if (fault())
        return false;
    return files.erase(canon(p)) != 0;
}
inline bool CreateDirectoryA(const char *p, void *) {
    using namespace fake_win32;
    if (files.count(canon(p)) || fault())
        return false;
    files[canon(p)].directory = true;
    return true;
}
inline LONG RegOpenKeyExA(HKEY, const char *p, DWORD, DWORD, HKEY *out) {
    using namespace fake_win32;
    auto path = canon(p);
    if (!keys.count(path))
        return ERROR_FILE_NOT_FOUND;
    *out = next++;
    key_handles[*out] = path;
    return ERROR_SUCCESS;
}
inline LONG RegCloseKey(HKEY k) {
    fake_win32::key_handles.erase(k);
    return ERROR_SUCCESS;
}
inline LONG RegQueryValueExA(HKEY k, const char *n, void *, DWORD *t, BYTE *out, DWORD *size) {
    using namespace fake_win32;
    auto &key = keys[key_handles[k]];
    auto i = key.find(canon(n));
    if (i == key.end())
        return ERROR_FILE_NOT_FOUND;
    if (*size < i->second.bytes.size())
        return ERROR_MORE_DATA;
    *size = DWORD(i->second.bytes.size());
    *t = i->second.type;
    if (*size)
        memcpy(out, i->second.bytes.data(), *size);
    return ERROR_SUCCESS;
}
inline LONG RegSetValueExA(HKEY k, const char *n, DWORD, DWORD type, const BYTE *data, DWORD size) {
    using namespace fake_win32;
    if (fault())
        return ERROR_ACCESS_DENIED;
    keys[key_handles[k]][canon(n)] = {type, {data, data + size}};
    return ERROR_SUCCESS;
}
inline LONG RegDeleteValueA(HKEY k, const char *n) {
    using namespace fake_win32;
    if (fault())
        return ERROR_ACCESS_DENIED;
    return keys[key_handles[k]].erase(canon(n)) ? ERROR_SUCCESS : ERROR_FILE_NOT_FOUND;
}
inline LONG RegFlushKey(HKEY) {
    return fake_win32::fault() ? ERROR_ACCESS_DENIED : ERROR_SUCCESS;
}
inline LONG RegCreateKeyExA(HKEY, const char *p, DWORD, void *, DWORD, DWORD, void *, HKEY *out,
                            DWORD *disposition) {
    using namespace fake_win32;
    if (fault())
        return ERROR_ACCESS_DENIED;
    auto path = canon(p);
    *disposition = keys.count(path) ? 2 : 1;
    keys[path];
    *out = next++;
    key_handles[*out] = path;
    return ERROR_SUCCESS;
}
inline LONG RegQueryInfoKeyA(HKEY key, char *, DWORD *, DWORD *, DWORD *subkeys, DWORD *, DWORD *,
                             DWORD *values, DWORD *, DWORD *, DWORD *, void *) {
    using namespace fake_win32;
    const auto &path = key_handles[key];
    if (!keys.count(path))
        return ERROR_FILE_NOT_FOUND;
    *values = DWORD(keys[path].size());
    *subkeys = 0;
    for (const auto &entry : keys)
        if (entry.first.starts_with(path + "\\"))
            ++*subkeys;
    return ERROR_SUCCESS;
}
inline LONG RegDeleteKeyA(HKEY, const char *p) {
    using namespace fake_win32;
    auto i = keys.find(canon(p));
    if (i == keys.end())
        return ERROR_FILE_NOT_FOUND;
    if (!i->second.empty() || fault())
        return ERROR_ACCESS_DENIED;
    keys.erase(i);
    return ERROR_SUCCESS;
}
inline HRSRC FindResourceA(void *, const char *p, const char *) {
    auto id = intptr_t(p);
    return fake_win32::resources.count(id) ? id : 0;
}
inline DWORD SizeofResource(void *, HRSRC r) {
    return DWORD(fake_win32::resources[r].size());
}
inline HGLOBAL LoadResource(void *, HRSRC r) {
    return r;
}
inline const void *LockResource(HGLOBAL r) {
    return fake_win32::resources[r].data();
}

inline HANDLE GetProcessHeap() {
    return 1;
}
inline void *HeapAlloc(HANDLE, DWORD, size_t bytes) {
    return std::malloc(bytes);
}
inline bool HeapFree(HANDLE, DWORD, void *p) {
    std::free(p);
    return true;
}
