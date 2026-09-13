// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace setup {
enum class Os : uint32_t { unsupported, win98, nt5 };
constexpr Os select_os(uint32_t platform, uint32_t major, uint32_t minor) {
    if (platform == 1 && major == 4 && minor == 10)
        return Os::win98;
    if (platform == 2 && major == 5 && minor <= 1)
        return Os::nt5;
    return Os::unsupported;
}
constexpr char upper(char c) {
    return c >= 'a' && c <= 'z' ? char(c - 32) : c;
}
inline bool pci(const char *id, size_t bytes) {
    constexpr char prefix[] = "PCI\\VEN_1234&DEV_1113";
    if (bytes <= sizeof(prefix) - 1)
        return false;
    for (size_t i = 0; i < sizeof(prefix) - 1; ++i)
        if (upper(id[i]) != prefix[i])
            return false;
    return id[sizeof(prefix) - 1] == '&' || id[sizeof(prefix) - 1] == 0;
}
inline bool safe_path(const char *name) {
    if (!name || !*name)
        return false;
    size_t component = 0, total = 0;
    char last = 0;
    for (; *name; ++name) {
        const char c = *name;
        if (++total > 180)
            return false;
        if (c == '/') {
            if (!component || last == '.')
                return false;
            component = 0;
        } else {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '.' || c == '_' || c == '-'))
                return false;
            if (!component && c == '.')
                return false;
            ++component;
        }
        last = c;
    }
    return component && last != '.';
}
// Fresh-directory transaction: never owns preexisting paths. Operations must
// report failure without acquiring ownership. Cleanup reports incomplete rollback.
template <class Store> class Transaction {
    Store &store_;
    bool owned_ = false;
    bool committed_ = false;

  public:
    explicit Transaction(Store &store) : store_(store) {}
    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;
    ~Transaction() {
        if (owned_ && !committed_)
            store_.rollback();
    }
    bool begin() {
        if (owned_)
            return false;
        owned_ = store_.begin();
        return owned_;
    }
    bool commit() {
        if (!owned_ || committed_ || !store_.commit())
            return false;
        committed_ = true;
        return true;
    }
    bool rollback() {
        if (!owned_ || committed_)
            return true;
        if (!store_.rollback())
            return false;
        owned_ = false;
        return true;
    }
};
} // namespace setup
