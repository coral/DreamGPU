/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <epoxy/egl.h>
#include <epoxy/gl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static void check(int yes, const char *what) {
    if (!yes) {
        fprintf(stderr, "FAIL %s (GL error %#x)\n", what, glGetError());
        ++failures;
    }
}
static void error(const char *where) {
    check(glGetError() == GL_NO_ERROR, where);
}
static void pixel(const char *name, int x, int y, int r, int g, int b, int a, int tolerance) {
    unsigned char p[4];
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    int e[4] = {r, g, b, a};
    int ok = 1;
    for (int i = 0; i < 4; i++)
        if (abs((int)p[i] - e[i]) > tolerance)
            ok = 0;
    printf("%s: actual=%u,%u,%u,%u expected=%d,%d,%d,%d\n", name, p[0], p[1], p[2], p[3], r, g, b,
           a);
    check(ok, name);
}
