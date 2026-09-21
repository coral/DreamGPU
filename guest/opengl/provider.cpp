/* SPDX-License-Identifier: GPL-2.0-or-later
 * Wine loads core functions directly from dgpugl.dll rather than the system
 * ICD dispatch table. Reuse the ICD implementation fragments so these exports
 * have exactly the same client state, validation and immutable image packing.
 */
#include "internal.h"

/* These fragments also contain other ICD entrypoints. Their unused aliases
 * are deliberately omitted from this provider's export surface. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "icd-client.inc"
#include "icd-pixels.inc"
#include "icd-raster.inc"
#include "icd-images.inc"
#pragma GCC diagnostic pop

extern "C" {
void APIENTRY glPushClientAttrib(GLbitfield mask) {
    AliasPushClientAttrib(mask);
}
void APIENTRY glPopClientAttrib(void) {
    AliasPopClientAttrib();
}
void APIENTRY glPixelTransferf(GLenum pname, GLfloat value) {
    AliasPixelTransferf(pname, value);
}
void APIENTRY glPixelTransferi(GLenum pname, GLint value) {
    AliasPixelTransferi(pname, value);
}
void APIENTRY glPixelZoom(GLfloat x, GLfloat y) {
    AliasPixelZoom(x, y);
}
void APIENTRY glRasterPos3f(GLfloat x, GLfloat y, GLfloat z) {
    AliasRasterPos3f(x, y, z);
}
void APIENTRY glDrawPixels(GLsizei width, GLsizei height, GLenum format, GLenum type,
                           const GLvoid *pixels) {
    AliasDrawPixels(width, height, format, type, pixels);
}
}
