// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace setup {
// Optional observer. Installation and /silent have identical transaction semantics.
// The interactive boundary installs this before starting the worker and removes
// it only after joining it. Observers must preserve the caller's Win32 last error.
using ProgressObserver = void (*)(const char *operation, const char *detail);
inline ProgressObserver progress_observer = nullptr;
inline void progress(const char *operation, const char *detail = "") {
    if (progress_observer)
        progress_observer(operation, detail);
}
// Capture errors at the failing syscall, before handle cleanup or UI calls can
// replace GetLastError. Zero means a validation failure, not a Windows error.
inline unsigned failure_error = 0;
inline bool failure(const char *operation, const char *detail = "", unsigned error = 0) {
    failure_error = error;
    progress(operation, detail);
    return false;
}
} // namespace setup
