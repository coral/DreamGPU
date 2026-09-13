/* SPDX-License-Identifier: GPL-2.0-or-later
 * OpenGL 1.1 section 2.12: raster coordinates follow vertex transformation,
 * clipping and associated state. Native GL owns this pipeline; every numeric
 * source type converts exactly to double, including the full GLint domain.
 * https://registry.khronos.org/OpenGL/specs/gl/glspec11.pdf
 */
namespace dg_icd_raster {
inline void position(GLdouble x, GLdouble y, GLdouble z, GLdouble w) {
    if (!JglCommandReady())
        return;
    const GLdouble values[4] = {x, y, z, w};
    JglScalarVector(FEnum_glRasterPos4d, 8, values);
}
} // namespace dg_icd_raster
static void APIENTRY AliasRasterPos2d(GLdouble c0, GLdouble c1) {
    dg_icd_raster::position(c0, c1, 0.0, 1.0);
}
static void APIENTRY AliasRasterPos2dv(const GLdouble *v) {
    if (!JglCommandReady())
        return;
    if (!v) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    dg_icd_raster::position(v[0], v[1], 0.0, 1.0);
}
static void APIENTRY AliasRasterPos2f(GLfloat c0, GLfloat c1) {
    dg_icd_raster::position(c0, c1, 0.0, 1.0);
}
static void APIENTRY AliasRasterPos2fv(const GLfloat *v) {
    if (!JglCommandReady())
        return;
    if (!v) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    dg_icd_raster::position(v[0], v[1], 0.0, 1.0);
}
static void APIENTRY AliasRasterPos2i(GLint c0, GLint c1) {
    dg_icd_raster::position(c0, c1, 0.0, 1.0);
}
static void APIENTRY AliasRasterPos2iv(const GLint *v) {
    if (!JglCommandReady())
        return;
    if (!v) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    dg_icd_raster::position(v[0], v[1], 0.0, 1.0);
}
static void APIENTRY AliasRasterPos2s(GLshort c0, GLshort c1) {
    dg_icd_raster::position(c0, c1, 0.0, 1.0);
}
static void APIENTRY AliasRasterPos2sv(const GLshort *v) {
    if (!JglCommandReady())
        return;
    if (!v) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    dg_icd_raster::position(v[0], v[1], 0.0, 1.0);
}
static void APIENTRY AliasRasterPos3d(GLdouble c0, GLdouble c1, GLdouble c2) {
    dg_icd_raster::position(c0, c1, c2, 1.0);
}
static void APIENTRY AliasRasterPos3dv(const GLdouble *v) {
    if (!JglCommandReady())
        return;
    if (!v) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    dg_icd_raster::position(v[0], v[1], v[2], 1.0);
}
static void APIENTRY AliasRasterPos3f(GLfloat c0, GLfloat c1, GLfloat c2) {
    dg_icd_raster::position(c0, c1, c2, 1.0);
}
static void APIENTRY AliasRasterPos3fv(const GLfloat *v) {
    if (!JglCommandReady())
        return;
    if (!v) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    dg_icd_raster::position(v[0], v[1], v[2], 1.0);
}
static void APIENTRY AliasRasterPos3i(GLint c0, GLint c1, GLint c2) {
    dg_icd_raster::position(c0, c1, c2, 1.0);
}
static void APIENTRY AliasRasterPos3iv(const GLint *v) {
    if (!JglCommandReady())
        return;
    if (!v) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    dg_icd_raster::position(v[0], v[1], v[2], 1.0);
}
static void APIENTRY AliasRasterPos3s(GLshort c0, GLshort c1, GLshort c2) {
    dg_icd_raster::position(c0, c1, c2, 1.0);
}
static void APIENTRY AliasRasterPos3sv(const GLshort *v) {
    if (!JglCommandReady())
        return;
    if (!v) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    dg_icd_raster::position(v[0], v[1], v[2], 1.0);
}
static void APIENTRY AliasRasterPos4d(GLdouble c0, GLdouble c1, GLdouble c2, GLdouble c3) {
    dg_icd_raster::position(c0, c1, c2, c3);
}
static void APIENTRY AliasRasterPos4dv(const GLdouble *v) {
    if (!JglCommandReady())
        return;
    if (!v) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    dg_icd_raster::position(v[0], v[1], v[2], v[3]);
}
static void APIENTRY AliasRasterPos4f(GLfloat c0, GLfloat c1, GLfloat c2, GLfloat c3) {
    dg_icd_raster::position(c0, c1, c2, c3);
}
static void APIENTRY AliasRasterPos4fv(const GLfloat *v) {
    if (!JglCommandReady())
        return;
    if (!v) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    dg_icd_raster::position(v[0], v[1], v[2], v[3]);
}
static void APIENTRY AliasRasterPos4i(GLint c0, GLint c1, GLint c2, GLint c3) {
    dg_icd_raster::position(c0, c1, c2, c3);
}
static void APIENTRY AliasRasterPos4iv(const GLint *v) {
    if (!JglCommandReady())
        return;
    if (!v) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    dg_icd_raster::position(v[0], v[1], v[2], v[3]);
}
static void APIENTRY AliasRasterPos4s(GLshort c0, GLshort c1, GLshort c2, GLshort c3) {
    dg_icd_raster::position(c0, c1, c2, c3);
}
static void APIENTRY AliasRasterPos4sv(const GLshort *v) {
    if (!JglCommandReady())
        return;
    if (!v) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    dg_icd_raster::position(v[0], v[1], v[2], v[3]);
}
