// SPDX-License-Identifier: GPL-2.0-or-later
// Unlike diagnostic logs, capability JSON must be returned in full. Called only
// with a fixed whitelist path after the owned process has exited. Deny concurrent
// writers and reject oversize/short/changing files instead of returning a tail.
static const char *ReadCapabilityResult(const char *path) {
    OutputBytes = 0;
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return "missing-capability-output";
    LARGE_INTEGER size = {}, after = {};
    DWORD read = 0;
    const char *error = NULL;
    if (!FileSize(file, &size) || size.QuadPart <= 0)
        error = "empty-capability-output";
    else if (size.QuadPart > OUTPUT_MAX)
        error = "capability-output-too-large";
    else if (!ReadFile(file, Output, (DWORD)size.QuadPart, &read, NULL) ||
             read != (DWORD)size.QuadPart || !FileSize(file, &after) ||
             after.QuadPart != size.QuadPart)
        error = "incomplete-capability-output";
    else
        OutputBytes = read;
    CloseHandle(file);
    return error;
}
