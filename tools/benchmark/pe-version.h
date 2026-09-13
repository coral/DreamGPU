/* SPDX-License-Identifier: GPL-2.0-or-later
 * Bounded PE32 launch check for fixed fixture executables. Win9x can create
 * detached OS loader dialogs for a newer subsystem even when CreateProcess
 * reports failure, so reject that known incompatibility before invoking it.
 */
static unsigned DgPe16(const unsigned char *p) {
    return p[0] | (unsigned(p[1]) << 8);
}
static unsigned DgPe32(const unsigned char *p) {
    return DgPe16(p) | (DgPe16(p + 2) << 16);
}
static bool DgCompatiblePe(const unsigned char *data, unsigned size, unsigned major,
                           unsigned minor) {
    if (size < 64 || data[0] != 'M' || data[1] != 'Z')
        return false;
    unsigned offset = DgPe32(data + 60);
    if (offset > size || size - offset < 96)
        return false;
    const unsigned char *pe = data + offset;
    if (DgPe32(pe) != 0x4550 || DgPe16(pe + 4) != 0x14c || DgPe16(pe + 20) < 72 ||
        DgPe16(pe + 24) != 0x10b)
        return false;
    unsigned required_major = DgPe16(pe + 72), required_minor = DgPe16(pe + 74);
    return required_major < major || (required_major == major && required_minor <= minor);
}
