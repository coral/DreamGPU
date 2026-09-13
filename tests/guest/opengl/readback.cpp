#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal.h"
static JGL_UNPACK pack = {4, 0, 0, 0, 0, 0};
static ULONG capacity = 65536, calls, error, scratch[16384];
static GLint texture_width = 1024, texture_height = 1024;
BOOL JglReady(void) {
    return TRUE;
}
void JglSetError(GLenum e) {
    error = e;
}
JGL_UNPACK *JglPack(void) {
    return &pack;
}
ULONG JglReadbackCapacity(void) {
    return capacity;
}
ULONG *JglReadbackBuffer(void) {
    return scratch;
}
void JglDrawableSize(ULONG *w, ULONG *h) {
    *w = 640;
    *h = 480;
}
static ULONG pixel(ULONG at) {
    return (at & 255) | (((at >> 8) & 255) << 8) | 0xff550000;
}
BOOL JglQuery(ULONG fn, const ULONG args[3], ULONG kind, void *out, ULONG bytes, ULONG *written) {
    ULONG i;
    assert(kind == DG_GL_RESULT_INT && bytes <= capacity);
    ++calls;
    *written = bytes;
    if (fn == FEnum_glGetTexLevelParameteriv) {
        assert(bytes == 4);
        *(GLint *)out = args[2] == GL_TEXTURE_WIDTH ? texture_width : texture_height;
        return TRUE;
    }
    if (fn == FEnum_glGetTexImage) {
        ULONG count = args[1] >> 16;
        assert((args[1] & 65535) == 0);
        if (!count)
            count = 128;
        assert(count * 4 == bytes);
        for (i = 0; i < count; ++i)
            ((ULONG *)out)[i] = pixel(args[2] + i);
        return TRUE;
    }
    assert(fn == FEnum_glReadPixels);
    {
        ULONG w = args[2] & 65535, h = args[2] >> 16;
        assert(w * h * 4 == bytes && args[0] + w <= 640 && args[1] + h <= 480);
        for (i = 0; i < w * h; ++i)
            ((ULONG *)out)[i] = pixel((args[1] + i / w) * 640 + args[0] + i % w);
    }
    return TRUE;
}
#include "readback.cpp"
int main(void) {
    ULONG *out = (ULONG *)malloc(1024 * 1024 * 4 + 8);
    ULONG i, bulk_calls;
    assert(out);
    out[0] = out[1024 * 1024 + 1] = 0x12345678;
    calls = 0;
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, out + 1);
    assert(!error && calls == 66);
    bulk_calls = calls;
    for (i = 0; i < 1024 * 1024; ++i)
        assert(out[i + 1] == pixel(i));
    assert(out[0] == 0x12345678 && out[1024 * 1024 + 1] == 0x12345678);
    capacity = 512;
    calls = 0;
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, out + 1);
    assert(!error && calls == 8194 && calls > bulk_calls * 100);
    capacity = 65536;
    calls = 0;
    glReadPixels(0, 0, 640, 480, GL_RGBA, GL_UNSIGNED_BYTE, out + 1);
    assert(!error && calls == 20);
    for (i = 0; i < 640 * 480; ++i)
        assert(out[i + 1] == pixel(i));
    capacity = 512;
    calls = 0;
    glReadPixels(0, 0, 640, 480, GL_RGBA, GL_UNSIGNED_BYTE, out + 1);
    assert(!error && calls == 2400);
    for (i = 0; i < 640 * 480; ++i)
        assert(out[i + 1] == pixel(i));
    capacity = 65536;
    pack.RowLength = 8;
    pack.SkipRows = 1;
    pack.SkipPixels = 1;
    memset(out, 0xcc, 8 * 8 * 4);
    glReadPixels(-1, -1, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, out);
    for (i = 0; i < 16; ++i) {
        ULONG row = i / 4, col = i % 4;
        assert(out[(row + 1) * 8 + col + 1] == (row && col ? pixel((row - 1) * 640 + col - 1) : 0));
    }
    assert(out[0] == 0xcccccccc && out[8] == 0xcccccccc);
    free(out);
    puts("PASS bulk readback: 128x fewer texture RPCs,120x fewer framebuffer RPCs,legacy512 "
         "compatibility,clipping,packing and guards");
    return 0;
}
