/* SPDX-License-Identifier: GPL-2.0-or-later
 * OpenGL 1.1 numeric entry points. Specification (4 March 1997), sections
 * 2.6.3, 2.7, 2.9 and table 2.6:
 * https://registry.khronos.org/OpenGL/specs/gl/glspec11.pdf
 * Signed components use (2*c+1)/(2^bits-1), not modern SNORM. Floating
 * components are not clamped here. The existing core owns state and packing.
 */
namespace dg_icd_numeric {
inline GLfloat component(GLbyte c) {
    return static_cast<GLfloat>((2.0 * c + 1.0) / 255.0);
}
inline GLfloat component(GLshort c) {
    return static_cast<GLfloat>((2.0 * c + 1.0) / 65535.0);
}
inline GLfloat component(GLint c) {
    return static_cast<GLfloat>((2.0 * c + 1.0) / 4294967295.0);
}
inline GLfloat component(GLubyte c) {
    return static_cast<GLfloat>(c / 255.0);
}
inline GLfloat component(GLushort c) {
    return static_cast<GLfloat>(c / 65535.0);
}
inline GLfloat component(GLuint c) {
    return static_cast<GLfloat>(c / 4294967295.0);
}
inline GLfloat component(GLdouble c) {
    return static_cast<GLfloat>(c);
}
template <class T> inline bool vector(const T *v) {
    // As in the other ICD aliases: reject null, read exactly the API's fixed
    // component count. Non-null client pointers must address readable values.
    if (!v) {
        JglSetError(GL_INVALID_OPERATION);
        return false;
    }
    return true;
}
template <class T> inline void rect(T x1, T y1, T x2, T y2) {
    // Checking first is essential: a rejected nested Begin alone would let the
    // following vertices corrupt, and End prematurely close, the caller's draw.
    if (!JglReady())
        return;
    glBegin(GL_POLYGON);
    glVertex2f(static_cast<GLfloat>(x1), static_cast<GLfloat>(y1));
    glVertex2f(static_cast<GLfloat>(x2), static_cast<GLfloat>(y1));
    glVertex2f(static_cast<GLfloat>(x2), static_cast<GLfloat>(y2));
    glVertex2f(static_cast<GLfloat>(x1), static_cast<GLfloat>(y2));
    glEnd();
}
} // namespace dg_icd_numeric
static void APIENTRY AliasColor3b(GLbyte c0, GLbyte c1, GLbyte c2) {
    glColor4f(dg_icd_numeric::component(c0), dg_icd_numeric::component(c1),
              dg_icd_numeric::component(c2), 1.0f);
}
static void APIENTRY AliasColor3bv(const GLbyte *v) {
    if (!dg_icd_numeric::vector(v))
        return;
    glColor4f(dg_icd_numeric::component(v[0]), dg_icd_numeric::component(v[1]),
              dg_icd_numeric::component(v[2]), 1.0f);
}
static void APIENTRY AliasColor3d(GLdouble c0, GLdouble c1, GLdouble c2) {
    glColor4f(dg_icd_numeric::component(c0), dg_icd_numeric::component(c1),
              dg_icd_numeric::component(c2), 1.0f);
}
static void APIENTRY AliasColor3dv(const GLdouble *v) {
    if (!dg_icd_numeric::vector(v))
        return;
    glColor4f(dg_icd_numeric::component(v[0]), dg_icd_numeric::component(v[1]),
              dg_icd_numeric::component(v[2]), 1.0f);
}
static void APIENTRY AliasColor3i(GLint c0, GLint c1, GLint c2) {
    glColor4f(dg_icd_numeric::component(c0), dg_icd_numeric::component(c1),
              dg_icd_numeric::component(c2), 1.0f);
}
static void APIENTRY AliasColor3iv(const GLint *v) {
    if (!dg_icd_numeric::vector(v))
        return;
    glColor4f(dg_icd_numeric::component(v[0]), dg_icd_numeric::component(v[1]),
              dg_icd_numeric::component(v[2]), 1.0f);
}
static void APIENTRY AliasColor3s(GLshort c0, GLshort c1, GLshort c2) {
    glColor4f(dg_icd_numeric::component(c0), dg_icd_numeric::component(c1),
              dg_icd_numeric::component(c2), 1.0f);
}
static void APIENTRY AliasColor3sv(const GLshort *v) {
    if (!dg_icd_numeric::vector(v))
        return;
    glColor4f(dg_icd_numeric::component(v[0]), dg_icd_numeric::component(v[1]),
              dg_icd_numeric::component(v[2]), 1.0f);
}
static void APIENTRY AliasColor3ub(GLubyte c0, GLubyte c1, GLubyte c2) {
    glColor4f(dg_icd_numeric::component(c0), dg_icd_numeric::component(c1),
              dg_icd_numeric::component(c2), 1.0f);
}
static void APIENTRY AliasColor3ui(GLuint c0, GLuint c1, GLuint c2) {
    glColor4f(dg_icd_numeric::component(c0), dg_icd_numeric::component(c1),
              dg_icd_numeric::component(c2), 1.0f);
}
static void APIENTRY AliasColor3uiv(const GLuint *v) {
    if (!dg_icd_numeric::vector(v))
        return;
    glColor4f(dg_icd_numeric::component(v[0]), dg_icd_numeric::component(v[1]),
              dg_icd_numeric::component(v[2]), 1.0f);
}
static void APIENTRY AliasColor3us(GLushort c0, GLushort c1, GLushort c2) {
    glColor4f(dg_icd_numeric::component(c0), dg_icd_numeric::component(c1),
              dg_icd_numeric::component(c2), 1.0f);
}
static void APIENTRY AliasColor3usv(const GLushort *v) {
    if (!dg_icd_numeric::vector(v))
        return;
    glColor4f(dg_icd_numeric::component(v[0]), dg_icd_numeric::component(v[1]),
              dg_icd_numeric::component(v[2]), 1.0f);
}
static void APIENTRY AliasColor4b(GLbyte c0, GLbyte c1, GLbyte c2, GLbyte c3) {
    glColor4f(dg_icd_numeric::component(c0), dg_icd_numeric::component(c1),
              dg_icd_numeric::component(c2), dg_icd_numeric::component(c3));
}
static void APIENTRY AliasColor4bv(const GLbyte *v) {
    if (!dg_icd_numeric::vector(v))
        return;
    glColor4f(dg_icd_numeric::component(v[0]), dg_icd_numeric::component(v[1]),
              dg_icd_numeric::component(v[2]), dg_icd_numeric::component(v[3]));
}
static void APIENTRY AliasColor4d(GLdouble c0, GLdouble c1, GLdouble c2, GLdouble c3) {
    glColor4f(dg_icd_numeric::component(c0), dg_icd_numeric::component(c1),
              dg_icd_numeric::component(c2), dg_icd_numeric::component(c3));
}
static void APIENTRY AliasColor4dv(const GLdouble *v) {
    if (!dg_icd_numeric::vector(v))
        return;
    glColor4f(dg_icd_numeric::component(v[0]), dg_icd_numeric::component(v[1]),
              dg_icd_numeric::component(v[2]), dg_icd_numeric::component(v[3]));
}
static void APIENTRY AliasColor4i(GLint c0, GLint c1, GLint c2, GLint c3) {
    glColor4f(dg_icd_numeric::component(c0), dg_icd_numeric::component(c1),
              dg_icd_numeric::component(c2), dg_icd_numeric::component(c3));
}
static void APIENTRY AliasColor4iv(const GLint *v) {
    if (!dg_icd_numeric::vector(v))
        return;
    glColor4f(dg_icd_numeric::component(v[0]), dg_icd_numeric::component(v[1]),
              dg_icd_numeric::component(v[2]), dg_icd_numeric::component(v[3]));
}
static void APIENTRY AliasColor4s(GLshort c0, GLshort c1, GLshort c2, GLshort c3) {
    glColor4f(dg_icd_numeric::component(c0), dg_icd_numeric::component(c1),
              dg_icd_numeric::component(c2), dg_icd_numeric::component(c3));
}
static void APIENTRY AliasColor4ui(GLuint c0, GLuint c1, GLuint c2, GLuint c3) {
    glColor4f(dg_icd_numeric::component(c0), dg_icd_numeric::component(c1),
              dg_icd_numeric::component(c2), dg_icd_numeric::component(c3));
}
static void APIENTRY AliasColor4uiv(const GLuint *v) {
    if (!dg_icd_numeric::vector(v))
        return;
    glColor4f(dg_icd_numeric::component(v[0]), dg_icd_numeric::component(v[1]),
              dg_icd_numeric::component(v[2]), dg_icd_numeric::component(v[3]));
}
static void APIENTRY AliasColor4us(GLushort c0, GLushort c1, GLushort c2, GLushort c3) {
    glColor4f(dg_icd_numeric::component(c0), dg_icd_numeric::component(c1),
              dg_icd_numeric::component(c2), dg_icd_numeric::component(c3));
}
static void APIENTRY AliasNormal3b(GLbyte c0, GLbyte c1, GLbyte c2) {
    glNormal3f(dg_icd_numeric::component(c0), dg_icd_numeric::component(c1),
               dg_icd_numeric::component(c2));
}
static void APIENTRY AliasNormal3bv(const GLbyte *v) {
    if (!dg_icd_numeric::vector(v))
        return;
    glNormal3f(dg_icd_numeric::component(v[0]), dg_icd_numeric::component(v[1]),
               dg_icd_numeric::component(v[2]));
}
static void APIENTRY AliasNormal3d(GLdouble c0, GLdouble c1, GLdouble c2) {
    glNormal3f(dg_icd_numeric::component(c0), dg_icd_numeric::component(c1),
               dg_icd_numeric::component(c2));
}
static void APIENTRY AliasNormal3dv(const GLdouble *v) {
    if (!dg_icd_numeric::vector(v))
        return;
    glNormal3f(dg_icd_numeric::component(v[0]), dg_icd_numeric::component(v[1]),
               dg_icd_numeric::component(v[2]));
}
static void APIENTRY AliasNormal3i(GLint c0, GLint c1, GLint c2) {
    glNormal3f(dg_icd_numeric::component(c0), dg_icd_numeric::component(c1),
               dg_icd_numeric::component(c2));
}
static void APIENTRY AliasNormal3iv(const GLint *v) {
    if (!dg_icd_numeric::vector(v))
        return;
    glNormal3f(dg_icd_numeric::component(v[0]), dg_icd_numeric::component(v[1]),
               dg_icd_numeric::component(v[2]));
}
static void APIENTRY AliasNormal3s(GLshort c0, GLshort c1, GLshort c2) {
    glNormal3f(dg_icd_numeric::component(c0), dg_icd_numeric::component(c1),
               dg_icd_numeric::component(c2));
}
static void APIENTRY AliasNormal3sv(const GLshort *v) {
    if (!dg_icd_numeric::vector(v))
        return;
    glNormal3f(dg_icd_numeric::component(v[0]), dg_icd_numeric::component(v[1]),
               dg_icd_numeric::component(v[2]));
}
static void APIENTRY AliasRectd(GLdouble x1, GLdouble y1, GLdouble x2, GLdouble y2) {
    dg_icd_numeric::rect(x1, y1, x2, y2);
}
static void APIENTRY AliasRectdv(const GLdouble *a, const GLdouble *b) {
    if (!JglReady())
        return;
    if (!dg_icd_numeric::vector(a) || !dg_icd_numeric::vector(b))
        return;
    dg_icd_numeric::rect(a[0], a[1], b[0], b[1]);
}
static void APIENTRY AliasRectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2) {
    dg_icd_numeric::rect(x1, y1, x2, y2);
}
static void APIENTRY AliasRectfv(const GLfloat *a, const GLfloat *b) {
    if (!JglReady())
        return;
    if (!dg_icd_numeric::vector(a) || !dg_icd_numeric::vector(b))
        return;
    dg_icd_numeric::rect(a[0], a[1], b[0], b[1]);
}
static void APIENTRY AliasRecti(GLint x1, GLint y1, GLint x2, GLint y2) {
    dg_icd_numeric::rect(x1, y1, x2, y2);
}
static void APIENTRY AliasRectiv(const GLint *a, const GLint *b) {
    if (!JglReady())
        return;
    if (!dg_icd_numeric::vector(a) || !dg_icd_numeric::vector(b))
        return;
    dg_icd_numeric::rect(a[0], a[1], b[0], b[1]);
}
static void APIENTRY AliasRects(GLshort x1, GLshort y1, GLshort x2, GLshort y2) {
    dg_icd_numeric::rect(x1, y1, x2, y2);
}
static void APIENTRY AliasRectsv(const GLshort *a, const GLshort *b) {
    if (!JglReady())
        return;
    if (!dg_icd_numeric::vector(a) || !dg_icd_numeric::vector(b))
        return;
    dg_icd_numeric::rect(a[0], a[1], b[0], b[1]);
}
