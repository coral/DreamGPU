/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef DREAMGPU_NT_ICD_PIXEL_FORMAT_H
#define DREAMGPU_NT_ICD_PIXEL_FORMAT_H
static LONG DgIcdDescribePixelFormat(LONG index, ULONG bytes, PIXELFORMATDESCRIPTOR *output) {
    if (!output)
        return 1;
    if (index != 1 || bytes < sizeof(*output))
        return 0;
    PIXELFORMATDESCRIPTOR format = {};
    format.nSize = sizeof(format);
    format.nVersion = 1;
    format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    format.iPixelType = PFD_TYPE_RGBA;
    format.cColorBits = 32;
    format.cRedBits = format.cGreenBits = format.cBlueBits = format.cAlphaBits = 8;
    format.cRedShift = 16;
    format.cGreenShift = 8;
    format.cAlphaShift = 24;
    format.cDepthBits = 24;
    format.cStencilBits = 8;
    format.iLayerType = PFD_MAIN_PLANE;
    memcpy(output, &format, sizeof(format));
    return 1;
}
#endif
