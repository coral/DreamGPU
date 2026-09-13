// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "lifecycle.h"
#include "providers.h"
#include "sha256.h"
namespace setup::lifecycle {
inline bool copy_text(char *out, size_t capacity, const char *text) {
    size_t n = 0;
    for (; text[n]; n++) {
        if (n + 1 >= capacity)
            return false;
        out[n] = text[n];
    }
    out[n] = 0;
    return true;
}
inline Image string_value(const char *text) {
    Image i;
    i.exists = 1;
    i.type = 1;
    size_t n = 0;
    while (text[n] && n + 1 < sizeof(i.value)) {
        i.value[n] = uint8_t(text[n]);
        ++n;
    }
    i.value[n] = 0;
    i.size = uint32_t(n + 1);
    Sha256 h;
    h.update(i.value, i.size);
    h.finish(i.sha);
    return i;
}
inline Image number_value(uint32_t value) {
    Image i;
    i.exists = 1;
    i.type = 4;
    i.size = 4;
    for (unsigned n = 0; n < 4; n++)
        i.value[n] = uint8_t(value >> (8 * n));
    Sha256 h;
    h.update(i.value, i.size);
    h.finish(i.sha);
    return i;
}
template <class Payloads> bool shared_plan(Os os, const Payloads &payloads, Journal &j) {
    j = {};
    j.generation = 1;
    for (const char *name : shared_runtime) {
        char source[128] = "application/";
        size_t offset = sizeof("application/") - 1;
        if (!copy_text(source + offset, sizeof(source) - offset, name))
            return false;
        bool found = false;
        for (const auto &p : payloads) {
            if (p.os != unsigned(os) || !text_equal(source, p.path, sizeof(source)))
                continue;
            if (found || j.count >= max_items)
                return false;
            found = true;
            auto &i = j.items[j.count++];
            i.kind = Kind::file;
            i.resource = p.id;
            if (!copy_text(i.path, sizeof(i.path), name) ||
                !copy_text(i.desired.sha, sizeof(i.desired.sha), p.sha))
                return false;
            i.desired.exists = 1;
            i.desired.size = p.size;
        }
        if (!found)
            return false;
    }
    return true;
}
// Registration hook only. Caller must append the verified ICD file operation
// before this, and production readiness must come from acceptance descriptors.
inline bool append_icd_registration(Os os, Journal &j, bool production_ready) {
    if (!production_ready)
        return false;
    if (j.count > max_items - 5)
        return false;
    if (os == Os::nt5) {
        auto &key = j.items[j.count++];
        key.kind = Kind::registry_key;
        copy_text(key.path, sizeof(key.path), nt_icd_key);
        key.desired.exists = 1;
        Sha256 h;
        h.finish(key.desired.sha);
        constexpr const char *names[] = {"Dll", "Version", "DriverVersion", "Flags"};
        for (unsigned n = 0; n < 4; n++) {
            auto &i = j.items[j.count++];
            i.kind = Kind::registry;
            copy_text(i.path, sizeof(i.path), nt_icd_key);
            copy_text(i.name, sizeof(i.name), names[n]);
            i.desired = n ? number_value(n == 1 ? 2 : n == 2 ? 1 : 0) : string_value("dgpuicd.dll");
        }
        return true;
    }
    if (os == Os::win98) {
        auto &i = j.items[j.count++];
        i.kind = Kind::registry;
        copy_text(i.path, sizeof(i.path), win98_icd_key);
        copy_text(i.name, sizeof(i.name), "DGPUICD");
        i.desired = string_value("dgpuicd.dll");
        return true;
    }
    return false;
}
} // namespace setup::lifecycle
