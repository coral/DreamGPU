/* SPDX-License-Identifier: GPL-2.0-or-later
 * Retail HL build742 waits in a custom launcher dialog without its original
 * CD. Require its volume and fixed setup files before launching a timedemo.
 */
static BOOL DgRetailMediaAvailable(void) {
    char root[4] = "D:\\", label[32], marker[] = "D:\\SIERRA.INF", data[] = "D:\\DATA1.CAB";
    for (; root[0] <= 'Z'; root[0]++) {
        if (GetDriveTypeA(root) != DRIVE_CDROM)
            continue;
        label[0] = 0;
        if (!GetVolumeInformationA(root, label, sizeof(label), NULL, NULL, NULL, NULL, 0))
            continue;
        label[sizeof(label) - 1] = 0;
        if (lstrcmpiA(label, "HALF_LIFE"))
            continue;
        marker[0] = data[0] = root[0];
        DWORD attributes = GetFileAttributesA(marker);
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        attributes = GetFileAttributesA(data);
        if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY))
            return TRUE;
    }
    return FALSE;
}
