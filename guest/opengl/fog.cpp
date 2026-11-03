/* SPDX-License-Identifier: GPL-2.0-or-later
 * EXT_fog_coord coordinates. Every core or EXT-named variant submits the one
 * FogCoordf record: a single float word. The shared scalar path rejects it
 * without DG_CAP_GL_FOG_COORD and admits it inside Begin/End and lists.
 * FogCoordf itself is a generated scalar wrapper (scalar.inc); the fog
 * coordinate array is client array state (arrays.cpp).
 */
#include "internal.h"
#include "packing.h"

extern "C" {

/* The vector is copied as bits and never loaded: an x87 load would quiet a
 * signaling NaN and change status flags (packing.h). A null vector submits
 * nothing, like the other float vector aliases. */
JGL_PACKING_ONLY void APIENTRY glFogCoordfv(const GLfloat *v) {
    if (v)
        JglScalarVector(FEnum_glFogCoordf, 1, v);
}
JGL_PACKING_ONLY void APIENTRY glFogCoordfvEXT(const GLfloat *v) {
    if (v)
        JglScalarVector(FEnum_glFogCoordf, 1, v);
}
/* Doubles convert to the float record, like the other double coordinates. */
static void FogCoordinate(GLdouble value) {
    const GLfloat coordinate = (GLfloat)value;
    JglScalarVector(FEnum_glFogCoordf, 1, &coordinate);
}
void APIENTRY glFogCoordd(GLdouble value) {
    FogCoordinate(value);
}
void APIENTRY glFogCoorddEXT(GLdouble value) {
    FogCoordinate(value);
}
void APIENTRY glFogCoorddv(const GLdouble *v) {
    if (v)
        FogCoordinate(*v);
}
void APIENTRY glFogCoorddvEXT(const GLdouble *v) {
    if (v)
        FogCoordinate(*v);
}

} /* extern C */
