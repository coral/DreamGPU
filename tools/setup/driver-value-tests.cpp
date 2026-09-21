// SPDX-License-Identifier: GPL-2.0-or-later
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <strings.h>
#include <vector>
using DWORD = uint32_t;
using LONG = int32_t;
using BYTE = unsigned char;
using HKEY = int;
constexpr DWORD REG_SZ = 1, ERROR_INVALID_DATA = 13;
static std::vector<BYTE> value;
static DWORD kind, error, reported, last_error, lines;
static LONG RegQueryValueExA(HKEY, const char *, void *, DWORD *type, BYTE *out, DWORD *size) {
    if (!value.empty())
        std::memcpy(out, value.data(), std::min<size_t>(*size, value.size()));
    *size = reported;
    *type = kind;
    return LONG(error);
}
static void SetLastError(DWORD code) { last_error = code; }
static int lstrcmpiA(const char *a, const char *b) { return strcasecmp(a, b); }
static bool Fail(const char *) { return false; }
static void Line(const char *) { ++lines; }
// The Python gate extracts this function unchanged from driver32.cpp.
#include "driver-value.inc"
static void check(const std::vector<BYTE> &bytes, bool expected, const char *identity = "dgpumini.drv",
                  DWORD type = REG_SZ, DWORD result = 0, DWORD size = UINT32_MAX) {
    value = bytes;
    kind = type;
    error = result;
    reported = size == UINT32_MAX ? DWORD(value.size()) : size;
    last_error = lines = 0;
    assert(Value(1, "drv", identity) == expected);
    assert(lines == (expected ? 2u : 0u));
    assert(last_error == (expected ? 0u : result ? result : ERROR_INVALID_DATA));
}
int main() {
    const std::string name = "dgpumini.drv";
    std::vector<BYTE> bytes(name.begin(), name.end());
    check(bytes, true);
    check(bytes, true, "DGPUMINI.DRV");
    bytes.push_back(0);
    check(bytes, true);
    check(bytes, false, "qemumini.drv");
    bytes.push_back('x');
    check(bytes, false);
    bytes.back() = 0;
    check(bytes, false);
    check({}, false);
    check({0}, false);
    check({'a', 0, 'b'}, false);
    check({'a'}, false, "a", 4);
    check({'a'}, false, "a", REG_SZ, 5);
    check({'a'}, false, "a", REG_SZ, 234);
    const std::string full(255, 'a');
    bytes.assign(full.begin(), full.end());
    check(bytes, true, full.c_str());
    bytes.push_back(0);
    check(bytes, true, full.c_str());
    bytes.back() = 'a';
    check(bytes, false, full.c_str());
    check(bytes, false, full.c_str(), REG_SZ, 0, 257);
    puts("PASS actual Win98 driver helper registry reader: bounded strings, aliases, errors and malformed bytes");
}
