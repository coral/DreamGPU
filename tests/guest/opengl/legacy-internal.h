/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef JGL_TEST_LEGACY_H
#define JGL_TEST_LEGACY_H
#include "texture-internal.h"
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif
#define ZeroMemory(p, n) memset((p), 0, (n))
typedef unsigned char BYTE, GLubyte;
typedef ULONG GLuint;
typedef float GLfloat;
typedef void *HINSTANCE, *HGLRC;
typedef const char *LPCSTR;
typedef void (*PROC)(void);
#define WINAPI
#define GL_FOG_INDEX 0x0b61
#define GL_FOG_DENSITY 0x0b62
#define GL_FOG_START 0x0b63
#define GL_FOG_END 0x0b64
#define GL_FOG_MODE 0x0b65
#define GL_FOG_COLOR 0x0b66
HINSTANCE JglModule(void);
HGLRC wglGetCurrentContext(void);
PROC GetProcAddress(HINSTANCE, LPCSTR);
void glVertex3f(GLfloat, GLfloat, GLfloat);
void glColor3f(GLfloat, GLfloat, GLfloat);
void glColor4f(GLfloat, GLfloat, GLfloat, GLfloat);
void glTexCoord2f(GLfloat, GLfloat);
void glNormal3f(GLfloat, GLfloat, GLfloat);
void JglScalarVector(ULONG, ULONG, const void *);
void glFogf(GLenum, GLfloat);
BOOL JglQuery(ULONG, const ULONG *, ULONG, void *, ULONG, ULONG *);
void JglDrawableSize(ULONG *, ULONG *);

void JglForgetTextures(ULONG count, const GLuint *textures);

typedef short GLshort;
typedef unsigned short GLushort;
void APIENTRY glVertex2f(GLfloat, GLfloat);
void APIENTRY glVertex4f(GLfloat, GLfloat, GLfloat, GLfloat);
void APIENTRY glTexCoord2f(GLfloat, GLfloat);
void APIENTRY glTexCoord4f(GLfloat, GLfloat, GLfloat, GLfloat);
void APIENTRY glLightModelf(GLenum, GLfloat);

#define GL_TEXTURE_HEIGHT 0x1001
#define GL_TEXTURE_WIDTH 0x1000
ULONG JglReadbackCapacity(void);
ULONG *JglReadbackBuffer(void);
#ifdef __cplusplus
}
#endif
#endif
