// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace setup {
// Startup callbacks do background work, but must not hide unfinished setup.
// Explicit /silent remains a promise to scripts that no UI will be opened.
constexpr bool notify_result(bool silent, bool startup, unsigned code) {
    return !silent &&
           (!startup || code == 0 || code == 11 || code == 16 || (code >= 20 && code != 28));
}
constexpr bool valid_startup(bool startup, bool staging, int action) {
    return !startup || (!staging && action == 0);
}
} // namespace setup
