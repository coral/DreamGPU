/* SPDX-License-Identifier: GPL-2.0-or-later
 * GL1.1 state aliases over existing validated vector/client-state paths.
 * Semantics: https://registry.khronos.org/OpenGL/specs/gl/glspec11.pdf
 * Sections2.13.1 (integer lighting colors),2.10 (texgen),3.6.1 (pixel storage).
 */
namespace icd_state {
static unsigned LightingCount(bool material, GLenum target, GLenum pname) {
    if (material ? (target != GL_FRONT && target != GL_BACK && target != GL_FRONT_AND_BACK)
                 : (target < GL_LIGHT0 || target > GL_LIGHT7))
        return 0;
    switch (pname) {
        case GL_AMBIENT:
        case GL_DIFFUSE:
        case GL_SPECULAR:
            return 4;
        case GL_EMISSION:
        case GL_AMBIENT_AND_DIFFUSE:
            return material ? 4 : 0;
        case GL_POSITION:
            return material ? 0 : 4;
        case GL_SPOT_DIRECTION:
            return material ? 0 : 3;
        case GL_COLOR_INDEXES:
            return material ? 3 : 0;
        case GL_SHININESS:
            return material ? 1 : 0;
        case GL_SPOT_EXPONENT:
        case GL_SPOT_CUTOFF:
        case GL_CONSTANT_ATTENUATION:
        case GL_LINEAR_ATTENUATION:
        case GL_QUADRATIC_ATTENUATION:
            return material ? 0 : 1;
        default:
            return 0;
    }
}
static bool LightingColor(GLenum pname) {
    return pname == GL_AMBIENT || pname == GL_DIFFUSE || pname == GL_SPECULAR ||
           pname == GL_EMISSION || pname == GL_AMBIENT_AND_DIFFUSE;
}
static void Lighting(bool material, GLenum target, GLenum pname, const GLint *values, bool scalar) {
    // Material is explicitly legal inside Begin/End; the existing vector path
    // enforces that exception. Other state must reject before reading values.
    if (!material && !JglReady())
        return;
    unsigned count = LightingCount(material, target, pname);
    if (!count || (scalar && count != 1)) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    if (!values) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    GLfloat converted[4];
    for (unsigned i = 0; i < count; ++i)
        converted[i] = LightingColor(pname) ? (GLfloat)((2.0 * values[i] + 1.0) / 4294967295.0)
                                            : (GLfloat)values[i];
    if (material)
        glMaterialfv(target, pname, converted);
    else
        glLightfv(target, pname, converted);
}
static unsigned TexGenCount(GLenum target, GLenum pname) {
    if (target != GL_S && target != GL_T && target != GL_R && target != GL_Q)
        return 0;
    return pname == GL_TEXTURE_GEN_MODE                        ? 1
           : pname == GL_OBJECT_PLANE || pname == GL_EYE_PLANE ? 4
                                                               : 0;
}
} // namespace icd_state
static void APIENTRY AliasLighti(GLenum light, GLenum pname, GLint value) {
    icd_state::Lighting(false, light, pname, &value, true);
}
static void APIENTRY AliasLightiv(GLenum light, GLenum pname, const GLint *values) {
    icd_state::Lighting(false, light, pname, values, false);
}
static void APIENTRY AliasMateriali(GLenum face, GLenum pname, GLint value) {
    icd_state::Lighting(true, face, pname, &value, true);
}
static void APIENTRY AliasMaterialiv(GLenum face, GLenum pname, const GLint *values) {
    icd_state::Lighting(true, face, pname, values, false);
}
static void APIENTRY AliasTexGend(GLenum coordinate, GLenum pname, GLdouble value) {
    if (!JglReady())
        return;
    if (icd_state::TexGenCount(coordinate, pname) != 1) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    glTexGendv(coordinate, pname, &value);
}
static void APIENTRY AliasTexGeniv(GLenum coordinate, GLenum pname, const GLint *values) {
    if (!JglReady())
        return;
    unsigned count = icd_state::TexGenCount(coordinate, pname);
    if (!count) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    if (!values) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    GLdouble converted[4];
    for (unsigned i = 0; i < count; ++i)
        converted[i] = values[i];
    glTexGendv(coordinate, pname, converted);
}
static void APIENTRY AliasPixelStoref(GLenum pname, GLfloat value) {
    if (!JglReady())
        return;
    switch (pname) {
        case GL_PACK_SWAP_BYTES:
        case GL_PACK_LSB_FIRST:
        case GL_UNPACK_SWAP_BYTES:
        case GL_UNPACK_LSB_FIRST:
            // GLboolean conversion tests zero, without integer rounding.
            glPixelStorei(pname, value != 0);
            return;
        case GL_PACK_ALIGNMENT:
        case GL_PACK_ROW_LENGTH:
        case GL_PACK_SKIP_ROWS:
        case GL_PACK_SKIP_PIXELS:
        case GL_UNPACK_ALIGNMENT:
        case GL_UNPACK_ROW_LENGTH:
        case GL_UNPACK_SKIP_ROWS:
        case GL_UNPACK_SKIP_PIXELS:
            break;
        default:
            JglSetError(GL_INVALID_ENUM);
            return;
    }
    // Round in double precision before checked conversion. This avoids i386
    // float-to-int overflow, NaN/Inf UB, and loss of the half unit near2^31.
    GLdouble number = value;
    if (!(number >= -2147483648.0 && number <= 2147483647.0)) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    GLint rounded = (GLint)number;
    GLdouble fraction = number - rounded;
    if (fraction >= 0.5 && rounded < 2147483647)
        ++rounded;
    else if (fraction <= -0.5 && rounded > (-2147483647 - 1))
        --rounded;
    glPixelStorei(pname, rounded);
}
