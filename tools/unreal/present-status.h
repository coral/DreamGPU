/* SPDX-License-Identifier: GPL-2.0-or-later
 * Read only this fixture's fresh OpenGLide swap failure, bounded to 16 KiB. */
static char GlidePresentError[16385];
static BOOL GlidePresentFailed(void) {
    OwnedHandle file{CreateFileA("C:\\UT99\\System\\OpenGLid.err", GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL)};
    DWORD bytes = 0;
    BOOL read;
    GlidePresentError[0] = 0;
    if (!file)
        return FALSE;
    read = ReadFile(file.get(), GlidePresentError, sizeof(GlidePresentError) - 1, &bytes, NULL);
    file.reset();
    if (!read) {
        GlidePresentError[0] = 0;
        return FALSE;
    }
    GlidePresentError[bytes] = 0;
    return Contains(GlidePresentError, "DG_PRESENT_FAILED");
}
