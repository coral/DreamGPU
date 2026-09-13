/* SPDX-License-Identifier: GPL-2.0-or-later
 * Retail432 Window.dll: WLog::OnCommand(40004/ID_LogFileExit) invokes
 * Core::appRequestExit(0). This numeric route is admitted only for exact bytes.
 * See target/follow-through/win98-graceful-exit-v1/engine/receipt.json and
 * log-exit-disassembly.txt; no private code is injected into the engine.
 */
#ifndef DREAMGPU_UT_ENGINE_EXIT_H
#define DREAMGPU_UT_ENGINE_EXIT_H
#include "../setup/sha256.h"
static const char RetailExitModule[] = "C:\\UT99\\System\\Window.dll";
static const char RetailExitSha[] = "bb60585ebcfc1b16e4424c5ce20bdea29512d16f1b770b4da7e9502110f8754c";
static OwnedHandle LockRetailExitModule(void) {
    OwnedHandle file{CreateFileA(RetailExitModule, GENERIC_READ, FILE_SHARE_READ, NULL,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)};
    if (!file)
        return OwnedHandle{};
    DWORD high = 0, size = GetFileSize(file.get(), &high), total = 0;
    if (high || size == INVALID_FILE_SIZE || size > 2 * 1024 * 1024)
        return OwnedHandle{};
    setup::Sha256 hash;
    BYTE block[4096];
    while (total < size) {
        DWORD count = 0, wanted = size - total;
        if (wanted > sizeof(block))
            wanted = sizeof(block);
        if (!ReadFile(file.get(), block, wanted, &count, NULL) || count != wanted)
            return OwnedHandle{};
        hash.update(block, count);
        total += count;
    }
    char digest[65];
    hash.finish(digest);
    if (!Equal(digest, RetailExitSha))
        return OwnedHandle{};
    Text("ENGINE_EXIT_MODULE_SHA256 ");
    Text(digest);
    Text("\r\n");
    return file; /* No write/delete sharing until the owned process exits. */
}
static BOOL RetailLogClass(HWND window) {
    char name[128] = {};
    if (!GetClassNameA(window, name, sizeof(name)))
        return FALSE;
    name[sizeof(name) - 1] = 0;
    const DWORD length = Length(name);
    return length >= 4 && Equal(name + length - 4, "WLog");
}
#endif
