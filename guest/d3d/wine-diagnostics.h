/* SPDX-License-Identifier: GPL-2.0-or-later
 * Bounded failure-only descriptor evidence; successful draws perform no I/O.
 * The serial game helper owns deletion and collection of this fixed log.
 */
static HRESULT dg_surface_failure(unsigned int line, const DDSURFACEDESC2 *desc,
                                  unsigned int version, HRESULT result) {
    DWORD saved, size, written;
    HANDLE file;
    char text[768];
    int count;
    if (SUCCEEDED(result))
        return result;
    saved = GetLastError();
    file = CreateFileA("C:\\DGDDRAW.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        size = GetFileSize(file, NULL);
        if (size != INVALID_FILE_SIZE && size < 16384) {
            count = wsprintfA(text,
                              "SURFACE_ERROR line=%u version=%u hr=%08lx size=%lu flags=%08lx "
                              "caps=%08lx caps2=%08lx width=%lu height=%lu pfsize=%lu "
                              "pfflags=%08lx bits=%lu stencil=%lu mask=%08lx stencilmask=%08lx\r\n",
                              line, version, (DWORD)result, desc->dwSize, desc->dwFlags,
                              desc->ddsCaps.dwCaps, desc->ddsCaps.dwCaps2, desc->dwWidth,
                              desc->dwHeight, desc->u4.ddpfPixelFormat.dwSize,
                              desc->u4.ddpfPixelFormat.dwFlags,
                              desc->u4.ddpfPixelFormat.u1.dwZBufferBitDepth,
                              desc->u4.ddpfPixelFormat.u2.dwStencilBitDepth,
                              desc->u4.ddpfPixelFormat.u3.dwZBitMask,
                              desc->u4.ddpfPixelFormat.u4.dwStencilBitMask);
            SetFilePointer(file, 0, NULL, FILE_END);
            WriteFile(file, text, count, &written, NULL);
        }
        CloseHandle(file);
    }
    SetLastError(saved);
    return result;
}
