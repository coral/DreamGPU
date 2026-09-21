// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Bounded, allocation-free JSON output. The sink must consume the whole buffer
// or return false; a failed sink is never retried as if its completion were known.
class CapabilityJson {
public:
    using Sink = bool (*)(void *, const char *, unsigned);
    CapabilityJson(Sink sink, void *context) : sink_(sink), context_(context) {}
    bool good() const { return !failed_; }
    bool flush() {
        if (!failed_ && used_ && !sink_(context_, buffer_, used_))
            failed_ = true;
        used_ = 0;
        return !failed_;
    }
    void text(const char *value) {
        while (*value)
            byte(*value++);
    }
    void number(unsigned value) {
        char digits[10];
        unsigned count = 0;
        do {
            digits[count++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value);
        while (count)
            byte(digits[--count]);
    }
    void hex(unsigned value) {
        text("\"0x");
        for (unsigned n = 8; n; --n)
            byte(digit((value >> ((n - 1) * 4)) & 15));
        byte('"');
    }
    // Preserve ANSI identity bytes without emitting invalid UTF-8. Bytes above
    // ASCII are represented as U+00xx, not interpreted using the host code page.
    void string(const char *value, unsigned limit = ~0u) {
        byte('"');
        for (unsigned n = 0; n < limit && value[n]; ++n) {
            const unsigned c = static_cast<unsigned char>(value[n]);
            if (c == '"' || c == '\\') {
                byte('\\');
                byte(static_cast<char>(c));
            } else if (c < 32 || c >= 127) {
                text("\\u00");
                byte(digit(c >> 4));
                byte(digit(c & 15));
            } else {
                byte(static_cast<char>(c));
            }
        }
        byte('"');
    }
private:
    static char digit(unsigned value) { return "0123456789abcdef"[value]; }
    void byte(char value) {
        if (failed_)
            return;
        if (used_ == sizeof(buffer_) && !flush())
            return;
        buffer_[used_++] = value;
    }
    Sink sink_;
    void *context_;
    char buffer_[4096];
    unsigned used_ = 0;
    bool failed_ = false;
};
