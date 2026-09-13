/* SPDX-License-Identifier: GPL-2.0-or-later
 * Freestanding i686 platform declarations for the actual compatibility.c.
 * Unused arithmetic/transport entrypoints are removed by section GC.
 */
typedef unsigned int ULONG, GLenum, GLuint;
typedef int GLsizei, GLint, BOOL;
typedef unsigned char GLubyte;
typedef float GLfloat;
typedef void *HINSTANCE, *HGLRC;
typedef const char *LPCSTR;
typedef void (*PROC)(void);
#define NULL nullptr
#define APIENTRY __attribute__((stdcall))
#define WINAPI __attribute__((stdcall))
#define GL_INVALID_ENUM 0x500
#define GL_INVALID_VALUE 0x501
#define GL_OUT_OF_MEMORY 0x505
#define GL_FOG_INDEX 0x0b61
#define GL_FOG_DENSITY 0x0b62
#define GL_FOG_START 0x0b63
#define GL_FOG_END 0x0b64
#define GL_FOG_MODE 0x0b65
#define GL_FOG_COLOR 0x0b66
#include "gl.h"
#include "gl-funcs.h"

#ifdef __cplusplus
extern "C" {
#endif
BOOL JglReady(void);
void JglSetError(GLenum);
void JglScalarVector(ULONG, ULONG, const void *);
BOOL JglData(ULONG, const ULONG *, ULONG, const void *, ULONG);
ULONG JglMaxDataBytes(ULONG);
HINSTANCE JglModule(void);
HGLRC WINAPI wglGetCurrentContext(void);
PROC WINAPI GetProcAddress(HINSTANCE, LPCSTR);
void APIENTRY glColor3f(GLfloat, GLfloat, GLfloat);
void APIENTRY glColor4f(GLfloat, GLfloat, GLfloat, GLfloat);
void APIENTRY glFogf(GLenum, GLfloat);

void JglForgetTextures(ULONG count, const GLuint *textures);

typedef short GLshort;
typedef unsigned short GLushort;
void APIENTRY glVertex2f(GLfloat, GLfloat);
void APIENTRY glVertex4f(GLfloat, GLfloat, GLfloat, GLfloat);
void APIENTRY glTexCoord2f(GLfloat, GLfloat);
void APIENTRY glTexCoord4f(GLfloat, GLfloat, GLfloat, GLfloat);
void APIENTRY glLightModelf(GLenum, GLfloat);

#ifdef __cplusplus
}
#endif

/* These isolated arithmetic tests model ordinary execution, outside lists. */
#define JglCommandError JglSetError
#define JglCommandReady JglReady
