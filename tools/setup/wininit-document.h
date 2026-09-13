// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "boot-queue.h"
namespace setup::boot {
// Wininit's [rename] lines are destination=source; repeated NUL destinations
// represent separate deletions. Do not round-trip through profile key APIs,
// which can collapse duplicate keys or rewrite unrelated sections/comments.
struct WininitDocument {
    static constexpr unsigned capacity = 65536;
    char data[capacity]{};
    unsigned bytes = 0;
    struct Line {
        unsigned start, end, body;
        bool rename;
    };
    static bool folded(const char *a, unsigned n, const char *b) {
        unsigned i = 0;
        for (; i < n && b[i]; ++i) {
            char x = a[i], y = b[i];
            if (x >= 'a' && x <= 'z')
                x -= 32;
            if (y >= 'a' && y <= 'z')
                y -= 32;
            if (x != y)
                return false;
        }
        return i == n && !b[i];
    }
    template <class Visit> bool lines(Visit visit) const {
        if (bytes > capacity)
            return false;
        bool rename = false, seen = false;
        for (unsigned pos = 0; pos < bytes;) {
            unsigned start = pos;
            while (pos < bytes && data[pos] != '\n') {
                unsigned char c = data[pos];
                if (!c || (c < 32 && c != '\r' && c != '\t'))
                    return false;
                ++pos;
            }
            unsigned body = pos;
            if (body > start && data[body - 1] == '\r')
                --body;
            if (pos < bytes)
                ++pos;
            unsigned first = start, last = body;
            while (first < last && (data[first] == ' ' || data[first] == '\t'))
                ++first;
            while (last > first && (data[last - 1] == ' ' || data[last - 1] == '\t'))
                --last;
            if (first < last && data[first] == '[') {
                if (data[last - 1] != ']')
                    return false;
                rename = folded(data + first, last - first, "[rename]");
                if (rename && seen)
                    return false; // ambiguous duplicate section
                seen = seen || rename;
            }
            if (!visit(Line{start, pos, body, rename}))
                return false;
        }
        return true;
    }
    static bool pair(const WininitDocument &doc, const Line &line,
                     char (&destination)[path_capacity], char (&source)[path_capacity],
                     bool &is_pair) {
        is_pair = false;
        unsigned a = line.start, end = line.body;
        while (a < end && (doc.data[a] == ' ' || doc.data[a] == '\t'))
            ++a;
        if (!line.rename || a == end || doc.data[a] == ';' || doc.data[a] == '[')
            return true;
        unsigned split = a;
        while (split < end && doc.data[split] != '=')
            ++split;
        if (split == end)
            return false;
        unsigned left = split, right = split + 1;
        while (left > a && (doc.data[left - 1] == ' ' || doc.data[left - 1] == '\t'))
            --left;
        while (right < end && (doc.data[right] == ' ' || doc.data[right] == '\t'))
            ++right;
        while (end > right && (doc.data[end - 1] == ' ' || doc.data[end - 1] == '\t'))
            --end;
        if (left == a || end == right || left - a >= path_capacity || end - right >= path_capacity)
            return false;
        for (unsigned i = a; i < left; ++i)
            destination[i - a] = doc.data[i];
        destination[left - a] = 0;
        for (unsigned i = right; i < end; ++i)
            source[i - right] = doc.data[i];
        source[end - right] = 0;
        is_pair = true;
        return true;
    }
    bool append(const char *value, unsigned n) {
        if (n > capacity - bytes)
            return false;
        for (unsigned i = 0; i < n; ++i)
            data[bytes++] = value[i];
        return true;
    }
};
} // namespace setup::boot
