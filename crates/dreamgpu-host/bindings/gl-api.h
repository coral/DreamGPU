/* SPDX-License-Identifier: GPL-2.0-or-later
 * DreamGPU-authored integration table; not a copied upstream implementation.
 * Declaration sources: Apple SDK OpenGL/{gl.h,glext.h} or libepoxy epoxy/gl.h,
 * plus the canonical DreamGPU protocol headers mirrored into QEMU below.
 * The copied GL vocabulary provenance is recorded in guest/include/gl-funcs.h.
 * Function types come from the platform OpenGL header. The conditional expression
 * decays Apple function symbols and preserves Linux epoxy function pointers.
 */
#ifndef DREAMGPU_GL_API_H
#define DREAMGPU_GL_API_H
#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#define DG_API_GenFramebuffers glGenFramebuffersEXT
#define DG_API_DeleteFramebuffers glDeleteFramebuffersEXT
#define DG_API_BindFramebuffer glBindFramebufferEXT
#define DG_API_FramebufferTexture2D glFramebufferTexture2DEXT
#define DG_API_CheckFramebufferStatus glCheckFramebufferStatusEXT
#define DG_API_BlitFramebuffer glBlitFramebufferEXT
#define DG_API_GenRenderbuffers glGenRenderbuffersEXT
#define DG_API_DeleteRenderbuffers glDeleteRenderbuffersEXT
#define DG_API_BindRenderbuffer glBindRenderbufferEXT
#define DG_API_RenderbufferStorage glRenderbufferStorageEXT
#define DG_API_FramebufferRenderbuffer glFramebufferRenderbufferEXT
#else
#include <epoxy/gl.h>
#define DG_API_GenFramebuffers glGenFramebuffers
#define DG_API_DeleteFramebuffers glDeleteFramebuffers
#define DG_API_BindFramebuffer glBindFramebuffer
#define DG_API_FramebufferTexture2D glFramebufferTexture2D
#define DG_API_CheckFramebufferStatus glCheckFramebufferStatus
#define DG_API_BlitFramebuffer glBlitFramebuffer
#define DG_API_GenRenderbuffers glGenRenderbuffers
#define DG_API_DeleteRenderbuffers glDeleteRenderbuffers
#define DG_API_BindRenderbuffer glBindRenderbuffer
#define DG_API_RenderbufferStorage glRenderbufferStorage
#define DG_API_FramebufferRenderbuffer glFramebufferRenderbuffer
#endif
#include "../../../vendor/qemu/include/standard-headers/dreamgpu/gl-funcs.h"
#include "../../../vendor/qemu/include/standard-headers/dreamgpu/gl.h"
#include "../../../vendor/qemu/include/standard-headers/dreamgpu/transport.h"
#include "../../../vendor/qemu/include/standard-headers/dreamgpu/gpu.h"
#include "../../../vendor/qemu/include/standard-headers/dreamgpu/cursor.h"
typedef struct DreamGpuGlApi {
    __typeof__(1 ? glListBase : glListBase) dg_glListBase;
    __typeof__(1 ? glSelectBuffer : glSelectBuffer) dg_glSelectBuffer;
    __typeof__(1 ? glFeedbackBuffer : glFeedbackBuffer) dg_glFeedbackBuffer;
    __typeof__(1 ? glRenderMode : glRenderMode) dg_glRenderMode;
    __typeof__(1 ? glInitNames : glInitNames) dg_glInitNames;
    __typeof__(1 ? glLoadName : glLoadName) dg_glLoadName;
    __typeof__(1 ? glPushName : glPushName) dg_glPushName;
    __typeof__(1 ? glPopName : glPopName) dg_glPopName;
    __typeof__(1 ? glPassThrough : glPassThrough) dg_glPassThrough;

    __typeof__(1 ? glMap1d : glMap1d) dg_glMap1d;
    __typeof__(1 ? glMap1f : glMap1f) dg_glMap1f;
    __typeof__(1 ? glMap2d : glMap2d) dg_glMap2d;
    __typeof__(1 ? glMap2f : glMap2f) dg_glMap2f;
    __typeof__(1 ? glMapGrid1d : glMapGrid1d) dg_glMapGrid1d;
    __typeof__(1 ? glMapGrid2d : glMapGrid2d) dg_glMapGrid2d;
    __typeof__(1 ? glEvalCoord1d : glEvalCoord1d) dg_glEvalCoord1d;
    __typeof__(1 ? glEvalCoord2d : glEvalCoord2d) dg_glEvalCoord2d;
    __typeof__(1 ? glEvalPoint1 : glEvalPoint1) dg_glEvalPoint1;
    __typeof__(1 ? glEvalPoint2 : glEvalPoint2) dg_glEvalPoint2;
    __typeof__(1 ? glEvalMesh1 : glEvalMesh1) dg_glEvalMesh1;
    __typeof__(1 ? glEvalMesh2 : glEvalMesh2) dg_glEvalMesh2;
    __typeof__(1 ? glGetMapdv : glGetMapdv) dg_glGetMapdv;
    __typeof__(1 ? glGetMapfv : glGetMapfv) dg_glGetMapfv;
    __typeof__(1 ? glGetMapiv : glGetMapiv) dg_glGetMapiv;

    __typeof__(1 ? glEdgeFlag : glEdgeFlag) dg_glEdgeFlag;
    __typeof__(1 ? glIndexd : glIndexd) dg_glIndexd;
    __typeof__(1 ? glIndexPointer : glIndexPointer) dg_glIndexPointer;
    __typeof__(1 ? glEdgeFlagPointer : glEdgeFlagPointer) dg_glEdgeFlagPointer;
    __typeof__(1 ? glPolygonStipple : glPolygonStipple) dg_glPolygonStipple;
    __typeof__(1 ? glGetPolygonStipple : glGetPolygonStipple) dg_glGetPolygonStipple;
    __typeof__(1 ? glClearAccum : glClearAccum) dg_glClearAccum;
    __typeof__(1 ? glClearIndex : glClearIndex) dg_glClearIndex;
    __typeof__(1 ? glIndexMask : glIndexMask) dg_glIndexMask;
    __typeof__(1 ? glAccum : glAccum) dg_glAccum;
    __typeof__(1 ? glLogicOp : glLogicOp) dg_glLogicOp;

    __typeof__(1 ? glAreTexturesResident : glAreTexturesResident) dg_glAreTexturesResident;
    __typeof__(1 ? glPrioritizeTextures : glPrioritizeTextures) dg_glPrioritizeTextures;
    __typeof__(1 ? glCopyTexImage1D : glCopyTexImage1D) dg_glCopyTexImage1D;
    __typeof__(1 ? glCopyTexSubImage1D : glCopyTexSubImage1D) dg_glCopyTexSubImage1D;
    __typeof__(1 ? glBitmap : glBitmap) dg_glBitmap;
    __typeof__(1 ? glDrawPixels : glDrawPixels) dg_glDrawPixels;
    __typeof__(1 ? glCopyPixels : glCopyPixels) dg_glCopyPixels;
    __typeof__(1 ? glPixelZoom : glPixelZoom) dg_glPixelZoom;
    __typeof__(1 ? glPixelTransferf : glPixelTransferf) dg_glPixelTransferf;
    __typeof__(1 ? glPixelTransferi : glPixelTransferi) dg_glPixelTransferi;
    __typeof__(1 ? glPixelMapfv : glPixelMapfv) dg_glPixelMapfv;
    __typeof__(1 ? glPixelMapuiv : glPixelMapuiv) dg_glPixelMapuiv;
    __typeof__(1 ? glPixelMapusv : glPixelMapusv) dg_glPixelMapusv;
    __typeof__(1 ? glGetPixelMapfv : glGetPixelMapfv) dg_glGetPixelMapfv;
    __typeof__(1 ? glGetPixelMapuiv : glGetPixelMapuiv) dg_glGetPixelMapuiv;
    __typeof__(1 ? glGetPixelMapusv : glGetPixelMapusv) dg_glGetPixelMapusv;

    __typeof__(1 ? glAlphaFunc : glAlphaFunc) dg_glAlphaFunc;
    __typeof__(1 ? glBegin : glBegin) dg_glBegin;
    __typeof__(1 ? glBindTexture : glBindTexture) dg_glBindTexture;
    __typeof__(1 ? glBlendFunc : glBlendFunc) dg_glBlendFunc;
    __typeof__(1 ? glClear : glClear) dg_glClear;
    __typeof__(1 ? glClearColor : glClearColor) dg_glClearColor;
    __typeof__(1 ? glClearDepth : glClearDepth) dg_glClearDepth;
    __typeof__(1 ? glClearStencil : glClearStencil) dg_glClearStencil;
    __typeof__(1 ? glClientWaitSync : glClientWaitSync) dg_glClientWaitSync;
    __typeof__(1 ? glClipPlane : glClipPlane) dg_glClipPlane;
    __typeof__(1 ? glColor3f : glColor3f) dg_glColor3f;
    __typeof__(1 ? glColor4f : glColor4f) dg_glColor4f;
    __typeof__(1 ? glColor4fv : glColor4fv) dg_glColor4fv;
    __typeof__(1 ? glColorMask : glColorMask) dg_glColorMask;
    __typeof__(1 ? glColorMaterial : glColorMaterial) dg_glColorMaterial;
    __typeof__(1 ? glColorPointer : glColorPointer) dg_glColorPointer;
    __typeof__(1 ? glCopyTexImage2D : glCopyTexImage2D) dg_glCopyTexImage2D;
    __typeof__(1 ? glCopyTexSubImage2D : glCopyTexSubImage2D) dg_glCopyTexSubImage2D;
    __typeof__(1 ? glCullFace : glCullFace) dg_glCullFace;
    __typeof__(1 ? glDeleteSync : glDeleteSync) dg_glDeleteSync;
    __typeof__(1 ? glDeleteTextures : glDeleteTextures) dg_glDeleteTextures;
    __typeof__(1 ? glDepthFunc : glDepthFunc) dg_glDepthFunc;
    __typeof__(1 ? glDepthMask : glDepthMask) dg_glDepthMask;
    __typeof__(1 ? glDepthRange : glDepthRange) dg_glDepthRange;
    __typeof__(1 ? glDisable : glDisable) dg_glDisable;
    __typeof__(1 ? glDisableClientState : glDisableClientState) dg_glDisableClientState;
    __typeof__(1 ? glDrawArrays : glDrawArrays) dg_glDrawArrays;
    __typeof__(1 ? glDrawBuffer : glDrawBuffer) dg_glDrawBuffer;
    __typeof__(1 ? glDrawBuffers : glDrawBuffers) dg_glDrawBuffers;
    __typeof__(1 ? glDrawElements : glDrawElements) dg_glDrawElements;
    __typeof__(1 ? glEnable : glEnable) dg_glEnable;
    __typeof__(1 ? glEnableClientState : glEnableClientState) dg_glEnableClientState;
    __typeof__(1 ? glEnd : glEnd) dg_glEnd;
    __typeof__(1 ? glFenceSync : glFenceSync) dg_glFenceSync;
    __typeof__(1 ? glFinish : glFinish) dg_glFinish;
    __typeof__(1 ? glFlush : glFlush) dg_glFlush;
    __typeof__(1 ? glFogf : glFogf) dg_glFogf;
    __typeof__(1 ? glFogfv : glFogfv) dg_glFogfv;
    __typeof__(1 ? glFrontFace : glFrontFace) dg_glFrontFace;
    __typeof__(1 ? glFrustum : glFrustum) dg_glFrustum;
    __typeof__(1 ? glGenTextures : glGenTextures) dg_glGenTextures;
    __typeof__(1 ? glGetBooleanv : glGetBooleanv) dg_glGetBooleanv;
    __typeof__(1 ? glGetClipPlane : glGetClipPlane) dg_glGetClipPlane;
    __typeof__(1 ? glGetDoublev : glGetDoublev) dg_glGetDoublev;
    __typeof__(1 ? glGetError : glGetError) dg_glGetError;
    __typeof__(1 ? glGetFloatv : glGetFloatv) dg_glGetFloatv;
    __typeof__(1 ? glGetIntegerv : glGetIntegerv) dg_glGetIntegerv;
    __typeof__(1 ? glGetLightfv : glGetLightfv) dg_glGetLightfv;
    __typeof__(1 ? glGetLightiv : glGetLightiv) dg_glGetLightiv;
    __typeof__(1 ? glGetMaterialfv : glGetMaterialfv) dg_glGetMaterialfv;
    __typeof__(1 ? glGetMaterialiv : glGetMaterialiv) dg_glGetMaterialiv;
    __typeof__(1 ? glGetTexEnvfv : glGetTexEnvfv) dg_glGetTexEnvfv;
    __typeof__(1 ? glGetTexEnviv : glGetTexEnviv) dg_glGetTexEnviv;
    __typeof__(1 ? glGetTexGendv : glGetTexGendv) dg_glGetTexGendv;
    __typeof__(1 ? glGetTexGenfv : glGetTexGenfv) dg_glGetTexGenfv;
    __typeof__(1 ? glGetTexGeniv : glGetTexGeniv) dg_glGetTexGeniv;
    __typeof__(1 ? glGetTexImage : glGetTexImage) dg_glGetTexImage;
    __typeof__(1 ? glGetTexLevelParameterfv : glGetTexLevelParameterfv) dg_glGetTexLevelParameterfv;
    __typeof__(1 ? glGetTexLevelParameteriv : glGetTexLevelParameteriv) dg_glGetTexLevelParameteriv;
    __typeof__(1 ? glGetTexParameterfv : glGetTexParameterfv) dg_glGetTexParameterfv;
    __typeof__(1 ? glGetTexParameteriv : glGetTexParameteriv) dg_glGetTexParameteriv;
    __typeof__(1 ? glHint : glHint) dg_glHint;
    __typeof__(1 ? glIsEnabled : glIsEnabled) dg_glIsEnabled;
    __typeof__(1 ? glLightModelf : glLightModelf) dg_glLightModelf;
    __typeof__(1 ? glLightModelfv : glLightModelfv) dg_glLightModelfv;
    __typeof__(1 ? glLightf : glLightf) dg_glLightf;
    __typeof__(1 ? glLightfv : glLightfv) dg_glLightfv;
    __typeof__(1 ? glLineStipple : glLineStipple) dg_glLineStipple;
    __typeof__(1 ? glLineWidth : glLineWidth) dg_glLineWidth;
    __typeof__(1 ? glLoadIdentity : glLoadIdentity) dg_glLoadIdentity;
    __typeof__(1 ? glLoadMatrixd : glLoadMatrixd) dg_glLoadMatrixd;
    __typeof__(1 ? glLoadMatrixf : glLoadMatrixf) dg_glLoadMatrixf;
    __typeof__(1 ? glMaterialf : glMaterialf) dg_glMaterialf;
    __typeof__(1 ? glMaterialfv : glMaterialfv) dg_glMaterialfv;
    __typeof__(1 ? glMatrixMode : glMatrixMode) dg_glMatrixMode;
    __typeof__(1 ? glMultMatrixd : glMultMatrixd) dg_glMultMatrixd;
    __typeof__(1 ? glMultMatrixf : glMultMatrixf) dg_glMultMatrixf;
    __typeof__(1 ? glNormal3f : glNormal3f) dg_glNormal3f;
    __typeof__(1 ? glNormal3fv : glNormal3fv) dg_glNormal3fv;
    __typeof__(1 ? glNormalPointer : glNormalPointer) dg_glNormalPointer;
    __typeof__(1 ? glOrtho : glOrtho) dg_glOrtho;
    __typeof__(1 ? glPixelStorei : glPixelStorei) dg_glPixelStorei;
    __typeof__(1 ? glPointSize : glPointSize) dg_glPointSize;
    __typeof__(1 ? glPolygonMode : glPolygonMode) dg_glPolygonMode;
    __typeof__(1 ? glPolygonOffset : glPolygonOffset) dg_glPolygonOffset;
    __typeof__(1 ? glPopAttrib : glPopAttrib) dg_glPopAttrib;
    __typeof__(1 ? glPopClientAttrib : glPopClientAttrib) dg_glPopClientAttrib;
    __typeof__(1 ? glPopMatrix : glPopMatrix) dg_glPopMatrix;
    __typeof__(1 ? glPushAttrib : glPushAttrib) dg_glPushAttrib;
    __typeof__(1 ? glPushClientAttrib : glPushClientAttrib) dg_glPushClientAttrib;
    __typeof__(1 ? glPushMatrix : glPushMatrix) dg_glPushMatrix;
    __typeof__(1 ? glReadBuffer : glReadBuffer) dg_glReadBuffer;
    __typeof__(1 ? glReadPixels : glReadPixels) dg_glReadPixels;
    __typeof__(1 ? glRotatef : glRotatef) dg_glRotatef;
    __typeof__(1 ? glScalef : glScalef) dg_glScalef;
    __typeof__(1 ? glScissor : glScissor) dg_glScissor;
    __typeof__(1 ? glSecondaryColor3f : glSecondaryColor3f) dg_glSecondaryColor3f;
    __typeof__(1 ? glSecondaryColor3fv : glSecondaryColor3fv) dg_glSecondaryColor3fv;
    __typeof__(1 ? glSecondaryColorPointer : glSecondaryColorPointer) dg_glSecondaryColorPointer;
    __typeof__(1 ? glShadeModel : glShadeModel) dg_glShadeModel;
    __typeof__(1 ? glStencilFunc : glStencilFunc) dg_glStencilFunc;
    __typeof__(1 ? glStencilMask : glStencilMask) dg_glStencilMask;
    __typeof__(1 ? glStencilOp : glStencilOp) dg_glStencilOp;
    __typeof__(1 ? glTexCoord2f : glTexCoord2f) dg_glTexCoord2f;
    __typeof__(1 ? glTexCoord4f : glTexCoord4f) dg_glTexCoord4f;
    __typeof__(1 ? glTexCoord4fv : glTexCoord4fv) dg_glTexCoord4fv;
    __typeof__(1 ? glTexCoordPointer : glTexCoordPointer) dg_glTexCoordPointer;
    __typeof__(1 ? glTexEnvf : glTexEnvf) dg_glTexEnvf;
    __typeof__(1 ? glTexEnvfv : glTexEnvfv) dg_glTexEnvfv;
    __typeof__(1 ? glTexEnvi : glTexEnvi) dg_glTexEnvi;
    __typeof__(1 ? glTexEnviv : glTexEnviv) dg_glTexEnviv;
    __typeof__(1 ? glTexGendv : glTexGendv) dg_glTexGendv;
    __typeof__(1 ? glTexGenf : glTexGenf) dg_glTexGenf;
    __typeof__(1 ? glTexGenfv : glTexGenfv) dg_glTexGenfv;
    __typeof__(1 ? glTexImage1D : glTexImage1D) dg_glTexImage1D;
    __typeof__(1 ? glTexImage2D : glTexImage2D) dg_glTexImage2D;
    __typeof__(1 ? glTexParameterf : glTexParameterf) dg_glTexParameterf;
    __typeof__(1 ? glTexParameterfv : glTexParameterfv) dg_glTexParameterfv;
    __typeof__(1 ? glTexParameteri : glTexParameteri) dg_glTexParameteri;
    __typeof__(1 ? glTexParameteriv : glTexParameteriv) dg_glTexParameteriv;
    __typeof__(1 ? glTexSubImage1D : glTexSubImage1D) dg_glTexSubImage1D;
    __typeof__(1 ? glTexSubImage2D : glTexSubImage2D) dg_glTexSubImage2D;
    __typeof__(1 ? glTranslatef : glTranslatef) dg_glTranslatef;
    __typeof__(1 ? glVertex2f : glVertex2f) dg_glVertex2f;
    __typeof__(1 ? glVertex3f : glVertex3f) dg_glVertex3f;
    __typeof__(1 ? glVertex4f : glVertex4f) dg_glVertex4f;
    __typeof__(1 ? glRasterPos4d : glRasterPos4d) dg_glRasterPos4d;
    __typeof__(1 ? glVertexPointer : glVertexPointer) dg_glVertexPointer;
    __typeof__(1 ? glViewport : glViewport) dg_glViewport;
    __typeof__(1 ? glWaitSync : glWaitSync) dg_glWaitSync;
    __typeof__(1 ? DG_API_GenFramebuffers : DG_API_GenFramebuffers) dg_glGenFramebuffers;
    __typeof__(1 ? DG_API_DeleteFramebuffers : DG_API_DeleteFramebuffers) dg_glDeleteFramebuffers;
    __typeof__(1 ? DG_API_BindFramebuffer : DG_API_BindFramebuffer) dg_glBindFramebuffer;
    __typeof__(1 ? DG_API_FramebufferTexture2D
                 : DG_API_FramebufferTexture2D) dg_glFramebufferTexture2D;
    __typeof__(1 ? DG_API_CheckFramebufferStatus
                 : DG_API_CheckFramebufferStatus) dg_glCheckFramebufferStatus;
    __typeof__(1 ? DG_API_BlitFramebuffer : DG_API_BlitFramebuffer) dg_glBlitFramebuffer;
    __typeof__(1 ? DG_API_GenRenderbuffers : DG_API_GenRenderbuffers) dg_glGenRenderbuffers;
    __typeof__(1 ? DG_API_DeleteRenderbuffers
                 : DG_API_DeleteRenderbuffers) dg_glDeleteRenderbuffers;
    __typeof__(1 ? DG_API_BindRenderbuffer : DG_API_BindRenderbuffer) dg_glBindRenderbuffer;
    __typeof__(1 ? DG_API_RenderbufferStorage
                 : DG_API_RenderbufferStorage) dg_glRenderbufferStorage;
    __typeof__(1 ? DG_API_FramebufferRenderbuffer
                 : DG_API_FramebufferRenderbuffer) dg_glFramebufferRenderbuffer;
} DreamGpuGlApi;
#define DREAMGPU_GL_API_INIT                                                                       \
    .dg_glListBase = glListBase, .dg_glSelectBuffer = glSelectBuffer,                              \
    .dg_glFeedbackBuffer = glFeedbackBuffer, .dg_glRenderMode = glRenderMode,                      \
    .dg_glInitNames = glInitNames, .dg_glLoadName = glLoadName, .dg_glPushName = glPushName,       \
    .dg_glPopName = glPopName, .dg_glPassThrough = glPassThrough, .dg_glMap1d = glMap1d,           \
    .dg_glMap1f = glMap1f, .dg_glMap2d = glMap2d, .dg_glMap2f = glMap2f,                           \
    .dg_glMapGrid1d = glMapGrid1d, .dg_glMapGrid2d = glMapGrid2d,                                  \
    .dg_glEvalCoord1d = glEvalCoord1d, .dg_glEvalCoord2d = glEvalCoord2d,                          \
    .dg_glEvalPoint1 = glEvalPoint1, .dg_glEvalPoint2 = glEvalPoint2,                              \
    .dg_glEvalMesh1 = glEvalMesh1, .dg_glEvalMesh2 = glEvalMesh2, .dg_glGetMapdv = glGetMapdv,     \
    .dg_glGetMapfv = glGetMapfv, .dg_glGetMapiv = glGetMapiv, .dg_glEdgeFlag = glEdgeFlag,         \
    .dg_glIndexd = glIndexd, .dg_glIndexPointer = glIndexPointer,                                  \
    .dg_glEdgeFlagPointer = glEdgeFlagPointer, .dg_glPolygonStipple = glPolygonStipple,            \
    .dg_glGetPolygonStipple = glGetPolygonStipple, .dg_glClearAccum = glClearAccum,                \
    .dg_glClearIndex = glClearIndex, .dg_glIndexMask = glIndexMask, .dg_glAccum = glAccum,         \
    .dg_glLogicOp = glLogicOp, .dg_glAlphaFunc = glAlphaFunc, .dg_glBegin = glBegin,               \
    .dg_glBindTexture = glBindTexture, .dg_glBlendFunc = glBlendFunc, .dg_glClear = glClear,       \
    .dg_glClearColor = glClearColor, .dg_glClearDepth = glClearDepth,                              \
    .dg_glClearStencil = glClearStencil, .dg_glClientWaitSync = glClientWaitSync,                  \
    .dg_glClipPlane = glClipPlane, .dg_glColor3f = glColor3f, .dg_glColor4f = glColor4f,           \
    .dg_glColor4fv = glColor4fv, .dg_glColorMask = glColorMask,                                    \
    .dg_glColorMaterial = glColorMaterial, .dg_glAreTexturesResident = glAreTexturesResident,      \
    .dg_glPrioritizeTextures = glPrioritizeTextures, .dg_glCopyTexImage1D = glCopyTexImage1D,      \
    .dg_glCopyTexSubImage1D = glCopyTexSubImage1D, .dg_glColorPointer = glColorPointer,            \
    .dg_glCopyTexImage2D = glCopyTexImage2D, .dg_glCopyTexSubImage2D = glCopyTexSubImage2D,        \
    .dg_glCullFace = glCullFace, .dg_glDeleteSync = glDeleteSync,                                  \
    .dg_glDeleteTextures = glDeleteTextures, .dg_glDepthFunc = glDepthFunc,                        \
    .dg_glDepthMask = glDepthMask, .dg_glDepthRange = glDepthRange, .dg_glDisable = glDisable,     \
    .dg_glDisableClientState = glDisableClientState, .dg_glDrawArrays = glDrawArrays,              \
    .dg_glDrawBuffer = glDrawBuffer, .dg_glDrawBuffers = glDrawBuffers,                            \
    .dg_glDrawElements = glDrawElements, .dg_glEnable = glEnable,                                  \
    .dg_glEnableClientState = glEnableClientState, .dg_glEnd = glEnd,                              \
    .dg_glFenceSync = glFenceSync, .dg_glFinish = glFinish, .dg_glFlush = glFlush,                 \
    .dg_glFogf = glFogf, .dg_glFogfv = glFogfv, .dg_glFrontFace = glFrontFace,                     \
    .dg_glFrustum = glFrustum, .dg_glGenTextures = glGenTextures,                                  \
    .dg_glGetBooleanv = glGetBooleanv, .dg_glGetClipPlane = glGetClipPlane,                        \
    .dg_glGetDoublev = glGetDoublev, .dg_glGetError = glGetError, .dg_glGetFloatv = glGetFloatv,   \
    .dg_glGetIntegerv = glGetIntegerv, .dg_glGetLightfv = glGetLightfv,                            \
    .dg_glGetLightiv = glGetLightiv, .dg_glGetMaterialfv = glGetMaterialfv,                        \
    .dg_glGetMaterialiv = glGetMaterialiv, .dg_glGetTexEnvfv = glGetTexEnvfv,                      \
    .dg_glGetTexEnviv = glGetTexEnviv, .dg_glGetTexGendv = glGetTexGendv,                          \
    .dg_glGetTexGenfv = glGetTexGenfv, .dg_glGetTexGeniv = glGetTexGeniv,                          \
    .dg_glGetTexImage = glGetTexImage, .dg_glGetTexLevelParameterfv = glGetTexLevelParameterfv,    \
    .dg_glGetTexLevelParameteriv = glGetTexLevelParameteriv,                                       \
    .dg_glGetTexParameterfv = glGetTexParameterfv, .dg_glGetTexParameteriv = glGetTexParameteriv,  \
    .dg_glHint = glHint, .dg_glIsEnabled = glIsEnabled, .dg_glLightModelf = glLightModelf,         \
    .dg_glLightModelfv = glLightModelfv, .dg_glLightf = glLightf, .dg_glLightfv = glLightfv,       \
    .dg_glLineStipple = glLineStipple, .dg_glLineWidth = glLineWidth,                              \
    .dg_glLoadIdentity = glLoadIdentity, .dg_glLoadMatrixd = glLoadMatrixd,                        \
    .dg_glLoadMatrixf = glLoadMatrixf, .dg_glMaterialf = glMaterialf,                              \
    .dg_glMaterialfv = glMaterialfv, .dg_glMatrixMode = glMatrixMode,                              \
    .dg_glMultMatrixd = glMultMatrixd, .dg_glMultMatrixf = glMultMatrixf,                          \
    .dg_glNormal3f = glNormal3f, .dg_glNormal3fv = glNormal3fv,                                    \
    .dg_glNormalPointer = glNormalPointer, .dg_glOrtho = glOrtho, .dg_glPixelZoom = glPixelZoom,   \
    .dg_glCopyPixels = glCopyPixels, .dg_glBitmap = glBitmap, .dg_glDrawPixels = glDrawPixels,     \
    .dg_glPixelTransferf = glPixelTransferf, .dg_glPixelTransferi = glPixelTransferi,              \
    .dg_glPixelMapfv = glPixelMapfv, .dg_glPixelMapuiv = glPixelMapuiv,                            \
    .dg_glPixelMapusv = glPixelMapusv, .dg_glGetPixelMapfv = glGetPixelMapfv,                      \
    .dg_glGetPixelMapuiv = glGetPixelMapuiv, .dg_glGetPixelMapusv = glGetPixelMapusv,              \
    .dg_glPixelStorei = glPixelStorei, .dg_glPointSize = glPointSize,                              \
    .dg_glPolygonMode = glPolygonMode, .dg_glPolygonOffset = glPolygonOffset,                      \
    .dg_glPopAttrib = glPopAttrib, .dg_glPopClientAttrib = glPopClientAttrib,                      \
    .dg_glPopMatrix = glPopMatrix, .dg_glPushAttrib = glPushAttrib,                                \
    .dg_glPushClientAttrib = glPushClientAttrib, .dg_glPushMatrix = glPushMatrix,                  \
    .dg_glReadBuffer = glReadBuffer, .dg_glReadPixels = glReadPixels, .dg_glRotatef = glRotatef,   \
    .dg_glScalef = glScalef, .dg_glScissor = glScissor,                                            \
    .dg_glSecondaryColor3f = glSecondaryColor3f, .dg_glSecondaryColor3fv = glSecondaryColor3fv,    \
    .dg_glSecondaryColorPointer = glSecondaryColorPointer, .dg_glShadeModel = glShadeModel,        \
    .dg_glStencilFunc = glStencilFunc, .dg_glStencilMask = glStencilMask,                          \
    .dg_glStencilOp = glStencilOp, .dg_glTexCoord2f = glTexCoord2f,                                \
    .dg_glTexCoord4f = glTexCoord4f, .dg_glTexCoord4fv = glTexCoord4fv,                            \
    .dg_glTexCoordPointer = glTexCoordPointer, .dg_glTexEnvf = glTexEnvf,                          \
    .dg_glTexEnvfv = glTexEnvfv, .dg_glTexEnvi = glTexEnvi, .dg_glTexEnviv = glTexEnviv,           \
    .dg_glTexGendv = glTexGendv, .dg_glTexGenf = glTexGenf, .dg_glTexGenfv = glTexGenfv,           \
    .dg_glTexImage1D = glTexImage1D, .dg_glTexImage2D = glTexImage2D,                              \
    .dg_glTexParameterf = glTexParameterf, .dg_glTexParameterfv = glTexParameterfv,                \
    .dg_glTexParameteri = glTexParameteri, .dg_glTexParameteriv = glTexParameteriv,                \
    .dg_glTexSubImage1D = glTexSubImage1D, .dg_glTexSubImage2D = glTexSubImage2D,                  \
    .dg_glTranslatef = glTranslatef, .dg_glVertex2f = glVertex2f, .dg_glVertex3f = glVertex3f,     \
    .dg_glVertex4f = glVertex4f, .dg_glRasterPos4d = glRasterPos4d,                                \
    .dg_glVertexPointer = glVertexPointer, .dg_glViewport = glViewport,                            \
    .dg_glWaitSync = glWaitSync, .dg_glGenFramebuffers = DG_API_GenFramebuffers,                   \
    .dg_glDeleteFramebuffers = DG_API_DeleteFramebuffers,                                          \
    .dg_glBindFramebuffer = DG_API_BindFramebuffer,                                                \
    .dg_glFramebufferTexture2D = DG_API_FramebufferTexture2D,                                      \
    .dg_glCheckFramebufferStatus = DG_API_CheckFramebufferStatus,                                  \
    .dg_glBlitFramebuffer = DG_API_BlitFramebuffer,                                                \
    .dg_glGenRenderbuffers = DG_API_GenRenderbuffers,                                              \
    .dg_glDeleteRenderbuffers = DG_API_DeleteRenderbuffers,                                        \
    .dg_glBindRenderbuffer = DG_API_BindRenderbuffer,                                              \
    .dg_glRenderbufferStorage = DG_API_RenderbufferStorage,                                        \
    .dg_glFramebufferRenderbuffer = DG_API_FramebufferRenderbuffer
#endif
