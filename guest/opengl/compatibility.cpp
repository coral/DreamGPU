/* SPDX-License-Identifier: GPL-2.0-or-later
 * Legacy entrypoints expressed through the implemented scalar/DATA ABI.
 * Caller arrays are consumed synchronously; no guest pointer crosses the ABI.
 */
#include "internal.h"
#include "packing.h"

extern "C" {

PROC WINAPI wglGetProcAddress(LPCSTR name) {
    if (!name || !wglGetCurrentContext())
        return NULL;
    return GetProcAddress(JglModule(), name);
}

/* The vectors already contain the scalar ABI's immutable argument words.
 * Submit them directly: floating-point loads can quiet NaNs or change x87
 * status, and passing through scalar wrappers adds another stack copy. */
JGL_PACKING_ONLY void APIENTRY glVertex2fv(const GLfloat *v) {
    if (v)
        JglScalarVector(FEnum_glVertex2f, 2, v);
}
JGL_PACKING_ONLY void APIENTRY glVertex4fv(const GLfloat *v) {
    if (v)
        JglScalarVector(FEnum_glVertex4f, 4, v);
}
JGL_PACKING_ONLY void APIENTRY glTexCoord4fv(const GLfloat *v) {
    if (v)
        JglScalarVector(FEnum_glTexCoord4f, 4, v);
}
JGL_PACKING_ONLY void APIENTRY glVertex3fv(const GLfloat *v) {
    if (v)
        JglScalarVector(FEnum_glVertex3f, 3, v);
}
JGL_PACKING_ONLY void APIENTRY glColor3fv(const GLfloat *v) {
    if (v)
        JglScalarVector(FEnum_glColor3f, 3, v);
}
JGL_PACKING_ONLY void APIENTRY glColor4fv(const GLfloat *v) {
    if (v)
        JglScalarVector(FEnum_glColor4f, 4, v);
}
JGL_PACKING_ONLY void APIENTRY glTexCoord2fv(const GLfloat *v) {
    if (v)
        JglScalarVector(FEnum_glTexCoord2f, 2, v);
}
JGL_PACKING_ONLY void APIENTRY glNormal3fv(const GLfloat *v) {
    if (v)
        JglScalarVector(FEnum_glNormal3f, 3, v);
}
void APIENTRY glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a) {
    glColor4f(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}
void APIENTRY glColor3ubv(const GLubyte *v) {
    if (v)
        glColor3f(v[0] / 255.0f, v[1] / 255.0f, v[2] / 255.0f);
}
void APIENTRY glColor4ubv(const GLubyte *v) {
    if (v)
        glColor4ub(v[0], v[1], v[2], v[3]);
}

static ULONG FogCount(GLenum pname) {
    switch (pname) {
        case GL_FOG_MODE:
        case GL_FOG_DENSITY:
        case GL_FOG_START:
        case GL_FOG_END:
        case GL_FOG_INDEX:
            return 1;
        case GL_FOG_COLOR:
            return 4;
        default:
            return 0;
    }
}
void APIENTRY glFogfv(GLenum pname, const GLfloat *values) {
    ULONG count, args[1] = {pname};
    if (!JglCommandReady())
        return;
    count = FogCount(pname);
    if (!count) {
        JglCommandError(GL_INVALID_ENUM);
        return;
    }
    if (!values) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    JglData(FEnum_glFogfv, args, 1, values, count * sizeof(*values));
}
void APIENTRY glFogi(GLenum pname, GLint value) {
    if (!JglCommandReady())
        return;
    if (FogCount(pname) != 1) {
        JglCommandError(GL_INVALID_ENUM);
        return;
    }
    glFogf(pname, (GLfloat)value);
}
void APIENTRY glDeleteTextures(GLsizei count, const GLuint *textures) {
    ULONG capacity, batch;
    if (!JglReady())
        return;
    if (count < 0 || (count && !textures)) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    capacity = JglMaxDataBytes(1) / sizeof(*textures);
    if (capacity > DG_GL_MAX_TEXTURES)
        capacity = DG_GL_MAX_TEXTURES;
    if (count && !capacity) {
        JglSetError(GL_OUT_OF_MEMORY);
        return;
    }
    while (count) {
        batch = (ULONG)count < capacity ? (ULONG)count : capacity;
        if (!JglData(FEnum_glDeleteTextures, &batch, 1, textures, batch * sizeof(*textures)))
            return;
        JglForgetTextures(batch, textures);
        textures += batch;
        count -= batch;
    }
}

/* Older fixed-function games/Wine draw paths use typed coordinate aliases. */
void APIENTRY glVertex2i(GLint x, GLint y) {
    glVertex2f((GLfloat)x, (GLfloat)y);
}
void APIENTRY glVertex2sv(const GLshort *v) {
    if (v)
        glVertex2f(v[0], v[1]);
}
void APIENTRY glVertex4s(GLshort x, GLshort y, GLshort z, GLshort w) {
    glVertex4f(x, y, z, w);
}
void APIENTRY glTexCoord1f(GLfloat s) {
    glTexCoord2f(s, 0);
}
void APIENTRY glTexCoord3f(GLfloat s, GLfloat t, GLfloat r) {
    glTexCoord4f(s, t, r, 1);
}
JGL_PACKING_ONLY void APIENTRY glTexCoord1fv(const GLfloat *v) {
    ULONG words[2] = {0, 0};
    if (v) {
        __builtin_memcpy(words, v, 4);
        JglScalarVector(FEnum_glTexCoord2f, 2, words);
    }
}
JGL_PACKING_ONLY void APIENTRY glTexCoord3fv(const GLfloat *v) {
    ULONG words[4];
    if (v) {
        __builtin_memcpy(words, v, 12);
        words[3] = 0x3f800000;
        JglScalarVector(FEnum_glTexCoord4f, 4, words);
    }
}
void APIENTRY glTexCoord2sv(const GLshort *v) {
    if (v)
        glTexCoord2f(v[0], v[1]);
}
void APIENTRY glTexCoord4sv(const GLshort *v) {
    if (v)
        glTexCoord4f(v[0], v[1], v[2], v[3]);
}
void APIENTRY glColor4sv(const GLshort *v) {
    if (v)
        glColor4f((2.0f * v[0] + 1) / 65535.0f, (2.0f * v[1] + 1) / 65535.0f,
                  (2.0f * v[2] + 1) / 65535.0f, (2.0f * v[3] + 1) / 65535.0f);
}
void APIENTRY glColor4usv(const GLushort *v) {
    if (v)
        glColor4f(v[0] / 65535.0f, v[1] / 65535.0f, v[2] / 65535.0f, v[3] / 65535.0f);
}
void APIENTRY glFogiv(GLenum pname, const GLint *values) {
    ULONG i, count;
    GLfloat converted[4];
    if (!JglCommandReady())
        return;
    count = FogCount(pname);
    if (!count) {
        JglCommandError(GL_INVALID_ENUM);
        return;
    }
    if (!values) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    for (i = 0; i < count; ++i)
        converted[i] = pname == GL_FOG_COLOR ? (GLfloat)((2.0 * values[i] + 1) / 4294967295.0)
                                             : (GLfloat)values[i];
    glFogfv(pname, converted);
}
void APIENTRY glLightModeli(GLenum pname, GLint value) {
    glLightModelf(pname, (GLfloat)value);
}

} /* extern C */
