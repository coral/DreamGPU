// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Windows exposes one untyped procedure lookup. Preserve pointer representation
// without reading an inactive C++ union member or passing through an object pointer.
template <class Function> Function Entry(HMODULE module, const char *name) {
    const FARPROC raw = GetProcAddress(module, name);
    Function result;
    static_assert(sizeof(result) == sizeof(raw));
    CopyMemory(&result, &raw, sizeof(result));
    return result;
}
