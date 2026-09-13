// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "policy.h"
#include "sha256.h"
#include <span>
namespace setup::native_alias {
// Only a private captured copy is transformed. OS originals remain byte-exact
// rollback sources. The Win98 thunk recipe matches the donor-documented equal-
// length rename and the independently accepted native DLL in our fixture.
constexpr char Win98Source[] = "a278cc0f49601723f258a9699d6aef138fbf7c3abaaaf9acce2610e08911d0ac";
constexpr char Win98Alias[] = "45b201f0ec12b03b4457eb8b2e0334003b639a2746e934ed68eb7c784a99fd32";
constexpr size_t Win98Thunk = 2100;
inline bool equal(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}
inline bool pe32(std::span<const uint8_t> bytes) {
    if (bytes.size() < 64 || bytes.size() > 64u * 1024 * 1024 || bytes[0] != 'M' || bytes[1] != 'Z')
        return false;
    uint32_t pe = 0;
    for (unsigned n = 0; n < 4; n++)
        pe |= uint32_t(bytes[60 + n]) << (n * 8);
    if (pe > bytes.size() - 26)
        return false;
    return bytes[pe] == 'P' && bytes[pe + 1] == 'E' && !bytes[pe + 2] && !bytes[pe + 3] &&
           bytes[pe + 4] == 0x4c && bytes[pe + 5] == 1 && bytes[pe + 24] == 0x0b &&
           bytes[pe + 25] == 1;
}
inline void hash(std::span<const uint8_t> bytes, char out[65]) {
    Sha256 sha;
    sha.update(bytes.data(), bytes.size());
    sha.finish(out);
}
inline bool derive(Os os, const char *public_name, std::span<uint8_t> private_bytes) {
    if ((os != Os::nt5 && os != Os::win98) || !pe32(private_bytes))
        return false;
    if (!equal(public_name, "ddraw.dll") && !equal(public_name, "d3d8.dll") &&
        !equal(public_name, "d3d9.dll"))
        return false;
    if (os != Os::win98 || !equal(public_name, "ddraw.dll"))
        return true;
    char source[65];
    hash(private_bytes, source);
    if (!equal(source, Win98Source) || private_bytes.size() < Win98Thunk + 10)
        return false;
    constexpr char original[] = "DDRAW.DLL", replacement[] = "DDSYS.DLL";
    for (size_t i = 0; i < sizeof(original); i++)
        if (private_bytes[Win98Thunk + i] != uint8_t(original[i]))
            return false;
    // This checked recipe does not rewrite lowercase export/version metadata,
    // DDRAW16.DLL, or arbitrary occurrences elsewhere in an unknown binary.
    for (size_t i = 0; i < sizeof(replacement); i++)
        private_bytes[Win98Thunk + i] = uint8_t(replacement[i]);
    char derived[65];
    hash(private_bytes, derived);
    return equal(derived, Win98Alias);
}
} // namespace setup::native_alias
