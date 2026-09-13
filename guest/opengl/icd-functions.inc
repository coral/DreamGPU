/* SPDX-License-Identifier: GPL-2.0-or-later
 * Numeric aliases for the diagnostic ICD use the existing scalar
 * implementation. These do not add protocol opcodes or claim missing GL feature
 * families. */
static void APIENTRY AliasTexCoord1d(GLdouble a0) {
    glTexCoord1f(static_cast<GLfloat>(a0));
}
static void APIENTRY AliasTexCoord1dv(const GLdouble *a0) {
    if (!a0) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    glTexCoord1f(static_cast<GLfloat>(a0[0]));
}
static void APIENTRY AliasTexCoord1i(GLint a0) {
    glTexCoord1f(static_cast<GLfloat>(a0));
}
static void APIENTRY AliasTexCoord1iv(const GLint *a0) {
    if (!a0) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    glTexCoord1f(static_cast<GLfloat>(a0[0]));
}
static void APIENTRY AliasTexCoord1s(GLshort a0) {
    glTexCoord1f(static_cast<GLfloat>(a0));
}
static void APIENTRY AliasTexCoord1sv(const GLshort *a0) {
    if (!a0) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    glTexCoord1f(static_cast<GLfloat>(a0[0]));
}
static void APIENTRY AliasTexCoord2d(GLdouble a0, GLdouble a1) {
    glTexCoord2f(static_cast<GLfloat>(a0), static_cast<GLfloat>(a1));
}
static void APIENTRY AliasTexCoord2dv(const GLdouble *a0) {
    if (!a0) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    glTexCoord2f(static_cast<GLfloat>(a0[0]), static_cast<GLfloat>(a0[1]));
}
static void APIENTRY AliasTexCoord2i(GLint a0, GLint a1) {
    glTexCoord2f(static_cast<GLfloat>(a0), static_cast<GLfloat>(a1));
}
static void APIENTRY AliasTexCoord2iv(const GLint *a0) {
    if (!a0) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    glTexCoord2f(static_cast<GLfloat>(a0[0]), static_cast<GLfloat>(a0[1]));
}
static void APIENTRY AliasTexCoord2s(GLshort a0, GLshort a1) {
    glTexCoord2f(static_cast<GLfloat>(a0), static_cast<GLfloat>(a1));
}
static void APIENTRY AliasTexCoord3d(GLdouble a0, GLdouble a1, GLdouble a2) {
    glTexCoord3f(static_cast<GLfloat>(a0), static_cast<GLfloat>(a1), static_cast<GLfloat>(a2));
}
static void APIENTRY AliasTexCoord3dv(const GLdouble *a0) {
    if (!a0) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    glTexCoord3f(static_cast<GLfloat>(a0[0]), static_cast<GLfloat>(a0[1]),
                 static_cast<GLfloat>(a0[2]));
}
static void APIENTRY AliasTexCoord3i(GLint a0, GLint a1, GLint a2) {
    glTexCoord3f(static_cast<GLfloat>(a0), static_cast<GLfloat>(a1), static_cast<GLfloat>(a2));
}
static void APIENTRY AliasTexCoord3iv(const GLint *a0) {
    if (!a0) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    glTexCoord3f(static_cast<GLfloat>(a0[0]), static_cast<GLfloat>(a0[1]),
                 static_cast<GLfloat>(a0[2]));
}
static void APIENTRY AliasTexCoord3s(GLshort a0, GLshort a1, GLshort a2) {
    glTexCoord3f(static_cast<GLfloat>(a0), static_cast<GLfloat>(a1), static_cast<GLfloat>(a2));
}
static void APIENTRY AliasTexCoord3sv(const GLshort *a0) {
    if (!a0) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    glTexCoord3f(static_cast<GLfloat>(a0[0]), static_cast<GLfloat>(a0[1]),
                 static_cast<GLfloat>(a0[2]));
}
static void APIENTRY AliasTexCoord4d(GLdouble a0, GLdouble a1, GLdouble a2, GLdouble a3) {
    glTexCoord4f(static_cast<GLfloat>(a0), static_cast<GLfloat>(a1), static_cast<GLfloat>(a2),
                 static_cast<GLfloat>(a3));
}
static void APIENTRY AliasTexCoord4dv(const GLdouble *a0) {
    if (!a0) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    glTexCoord4f(static_cast<GLfloat>(a0[0]), static_cast<GLfloat>(a0[1]),
                 static_cast<GLfloat>(a0[2]), static_cast<GLfloat>(a0[3]));
}
static void APIENTRY AliasTexCoord4i(GLint a0, GLint a1, GLint a2, GLint a3) {
    glTexCoord4f(static_cast<GLfloat>(a0), static_cast<GLfloat>(a1), static_cast<GLfloat>(a2),
                 static_cast<GLfloat>(a3));
}
static void APIENTRY AliasTexCoord4iv(const GLint *a0) {
    if (!a0) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    glTexCoord4f(static_cast<GLfloat>(a0[0]), static_cast<GLfloat>(a0[1]),
                 static_cast<GLfloat>(a0[2]), static_cast<GLfloat>(a0[3]));
}
static void APIENTRY AliasTexCoord4s(GLshort a0, GLshort a1, GLshort a2, GLshort a3) {
    glTexCoord4f(static_cast<GLfloat>(a0), static_cast<GLfloat>(a1), static_cast<GLfloat>(a2),
                 static_cast<GLfloat>(a3));
}
static void APIENTRY AliasVertex2d(GLdouble a0, GLdouble a1) {
    glVertex2f(static_cast<GLfloat>(a0), static_cast<GLfloat>(a1));
}
static void APIENTRY AliasVertex2dv(const GLdouble *a0) {
    if (!a0) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    glVertex2f(static_cast<GLfloat>(a0[0]), static_cast<GLfloat>(a0[1]));
}
static void APIENTRY AliasVertex2iv(const GLint *a0) {
    if (!a0) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    glVertex2f(static_cast<GLfloat>(a0[0]), static_cast<GLfloat>(a0[1]));
}
static void APIENTRY AliasVertex2s(GLshort a0, GLshort a1) {
    glVertex2f(static_cast<GLfloat>(a0), static_cast<GLfloat>(a1));
}
static void APIENTRY AliasVertex3d(GLdouble a0, GLdouble a1, GLdouble a2) {
    glVertex3f(static_cast<GLfloat>(a0), static_cast<GLfloat>(a1), static_cast<GLfloat>(a2));
}
static void APIENTRY AliasVertex3dv(const GLdouble *a0) {
    if (!a0) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    glVertex3f(static_cast<GLfloat>(a0[0]), static_cast<GLfloat>(a0[1]),
               static_cast<GLfloat>(a0[2]));
}
static void APIENTRY AliasVertex3i(GLint a0, GLint a1, GLint a2) {
    glVertex3f(static_cast<GLfloat>(a0), static_cast<GLfloat>(a1), static_cast<GLfloat>(a2));
}
static void APIENTRY AliasVertex3iv(const GLint *a0) {
    if (!a0) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    glVertex3f(static_cast<GLfloat>(a0[0]), static_cast<GLfloat>(a0[1]),
               static_cast<GLfloat>(a0[2]));
}
static void APIENTRY AliasVertex3s(GLshort a0, GLshort a1, GLshort a2) {
    glVertex3f(static_cast<GLfloat>(a0), static_cast<GLfloat>(a1), static_cast<GLfloat>(a2));
}
static void APIENTRY AliasVertex3sv(const GLshort *a0) {
    if (!a0) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    glVertex3f(static_cast<GLfloat>(a0[0]), static_cast<GLfloat>(a0[1]),
               static_cast<GLfloat>(a0[2]));
}
static void APIENTRY AliasVertex4d(GLdouble a0, GLdouble a1, GLdouble a2, GLdouble a3) {
    glVertex4f(static_cast<GLfloat>(a0), static_cast<GLfloat>(a1), static_cast<GLfloat>(a2),
               static_cast<GLfloat>(a3));
}
static void APIENTRY AliasVertex4dv(const GLdouble *a0) {
    if (!a0) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    glVertex4f(static_cast<GLfloat>(a0[0]), static_cast<GLfloat>(a0[1]),
               static_cast<GLfloat>(a0[2]), static_cast<GLfloat>(a0[3]));
}
static void APIENTRY AliasVertex4i(GLint a0, GLint a1, GLint a2, GLint a3) {
    glVertex4f(static_cast<GLfloat>(a0), static_cast<GLfloat>(a1), static_cast<GLfloat>(a2),
               static_cast<GLfloat>(a3));
}
static void APIENTRY AliasVertex4iv(const GLint *a0) {
    if (!a0) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    glVertex4f(static_cast<GLfloat>(a0[0]), static_cast<GLfloat>(a0[1]),
               static_cast<GLfloat>(a0[2]), static_cast<GLfloat>(a0[3]));
}
static void APIENTRY AliasVertex4sv(const GLshort *a0) {
    if (!a0) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    glVertex4f(static_cast<GLfloat>(a0[0]), static_cast<GLfloat>(a0[1]),
               static_cast<GLfloat>(a0[2]), static_cast<GLfloat>(a0[3]));
}
static void APIENTRY AliasRotated(GLdouble a0, GLdouble a1, GLdouble a2, GLdouble a3) {
    glRotatef(static_cast<GLfloat>(a0), static_cast<GLfloat>(a1), static_cast<GLfloat>(a2),
              static_cast<GLfloat>(a3));
}
static void APIENTRY AliasScaled(GLdouble a0, GLdouble a1, GLdouble a2) {
    glScalef(static_cast<GLfloat>(a0), static_cast<GLfloat>(a1), static_cast<GLfloat>(a2));
}
static void APIENTRY AliasTranslated(GLdouble a0, GLdouble a1, GLdouble a2) {
    glTranslatef(static_cast<GLfloat>(a0), static_cast<GLfloat>(a1), static_cast<GLfloat>(a2));
}
