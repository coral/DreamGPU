/* SPDX-License-Identifier: GPL-2.0-or-later
 * Numeric aliases for the diagnostic ICD use the existing scalar
 * implementation. These do not add protocol opcodes or claim missing GL feature
 * families. */
static void APIENTRY MissingNewList(GLuint, GLenum) {
    Unsupported(0);
}
static void APIENTRY MissingEndList(void) {
    Unsupported(1);
}
static void APIENTRY MissingCallList(GLuint) {
    Unsupported(2);
}
static void APIENTRY MissingCallLists(GLsizei, GLenum, const GLvoid *) {
    Unsupported(3);
}
static void APIENTRY MissingDeleteLists(GLuint, GLsizei) {
    Unsupported(4);
}
static GLuint APIENTRY MissingGenLists(GLsizei) {
    Unsupported(5);
    return {};
}
static void APIENTRY MissingListBase(GLuint) {
    Unsupported(6);
}
static void APIENTRY MissingEdgeFlag(GLboolean) {
    Unsupported(41);
}
static void APIENTRY MissingEdgeFlagv(const GLboolean *) {
    Unsupported(42);
}
static void APIENTRY MissingIndexd(GLdouble) {
    Unsupported(44);
}
static void APIENTRY MissingIndexdv(const GLdouble *) {
    Unsupported(45);
}
static void APIENTRY MissingIndexf(GLfloat) {
    Unsupported(46);
}
static void APIENTRY MissingIndexfv(const GLfloat *) {
    Unsupported(47);
}
static void APIENTRY MissingIndexi(GLint) {
    Unsupported(48);
}
static void APIENTRY MissingIndexiv(const GLint *) {
    Unsupported(49);
}
static void APIENTRY MissingIndexs(GLshort) {
    Unsupported(50);
}
static void APIENTRY MissingIndexsv(const GLshort *) {
    Unsupported(51);
}
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
static void APIENTRY MissingPolygonStipple(const GLubyte *) {
    Unsupported(175);
}
static void APIENTRY MissingFeedbackBuffer(GLsizei, GLenum, GLfloat *) {
    Unsupported(194);
}
static void APIENTRY MissingSelectBuffer(GLsizei, GLuint *) {
    Unsupported(195);
}
static GLint APIENTRY MissingRenderMode(GLenum) {
    Unsupported(196);
    return {};
}
static void APIENTRY MissingInitNames(void) {
    Unsupported(197);
}
static void APIENTRY MissingLoadName(GLuint) {
    Unsupported(198);
}
static void APIENTRY MissingPassThrough(GLfloat) {
    Unsupported(199);
}
static void APIENTRY MissingPopName(void) {
    Unsupported(200);
}
static void APIENTRY MissingPushName(GLuint) {
    Unsupported(201);
}
static void APIENTRY MissingClearAccum(GLfloat, GLfloat, GLfloat, GLfloat) {
    Unsupported(204);
}
static void APIENTRY MissingClearIndex(GLfloat) {
    Unsupported(205);
}
static void APIENTRY MissingIndexMask(GLuint) {
    Unsupported(212);
}
static void APIENTRY MissingAccum(GLenum, GLfloat) {
    Unsupported(213);
}
static void APIENTRY MissingMap1d(GLenum, GLdouble, GLdouble, GLint, GLint, const GLdouble *) {
    Unsupported(220);
}
static void APIENTRY MissingMap1f(GLenum, GLfloat, GLfloat, GLint, GLint, const GLfloat *) {
    Unsupported(221);
}
static void APIENTRY MissingMap2d(GLenum, GLdouble, GLdouble, GLint, GLint, GLdouble, GLdouble,
                                  GLint, GLint, const GLdouble *) {
    Unsupported(222);
}
static void APIENTRY MissingMap2f(GLenum, GLfloat, GLfloat, GLint, GLint, GLfloat, GLfloat, GLint,
                                  GLint, const GLfloat *) {
    Unsupported(223);
}
static void APIENTRY MissingMapGrid1d(GLint, GLdouble, GLdouble) {
    Unsupported(224);
}
static void APIENTRY MissingMapGrid1f(GLint, GLfloat, GLfloat) {
    Unsupported(225);
}
static void APIENTRY MissingMapGrid2d(GLint, GLdouble, GLdouble, GLint, GLdouble, GLdouble) {
    Unsupported(226);
}
static void APIENTRY MissingMapGrid2f(GLint, GLfloat, GLfloat, GLint, GLfloat, GLfloat) {
    Unsupported(227);
}
static void APIENTRY MissingEvalCoord1d(GLdouble) {
    Unsupported(228);
}
static void APIENTRY MissingEvalCoord1dv(const GLdouble *) {
    Unsupported(229);
}
static void APIENTRY MissingEvalCoord1f(GLfloat) {
    Unsupported(230);
}
static void APIENTRY MissingEvalCoord1fv(const GLfloat *) {
    Unsupported(231);
}
static void APIENTRY MissingEvalCoord2d(GLdouble, GLdouble) {
    Unsupported(232);
}
static void APIENTRY MissingEvalCoord2dv(const GLdouble *) {
    Unsupported(233);
}
static void APIENTRY MissingEvalCoord2f(GLfloat, GLfloat) {
    Unsupported(234);
}
static void APIENTRY MissingEvalCoord2fv(const GLfloat *) {
    Unsupported(235);
}
static void APIENTRY MissingEvalMesh1(GLenum, GLint, GLint) {
    Unsupported(236);
}
static void APIENTRY MissingEvalPoint1(GLint) {
    Unsupported(237);
}
static void APIENTRY MissingEvalMesh2(GLenum, GLint, GLint, GLint, GLint) {
    Unsupported(238);
}
static void APIENTRY MissingEvalPoint2(GLint, GLint) {
    Unsupported(239);
}
static void APIENTRY MissingLogicOp(GLenum) {
    Unsupported(242);
}
static void APIENTRY MissingGetMapdv(GLenum, GLenum, GLdouble *) {
    Unsupported(266);
}
static void APIENTRY MissingGetMapfv(GLenum, GLenum, GLfloat *) {
    Unsupported(267);
}
static void APIENTRY MissingGetMapiv(GLenum, GLenum, GLint *) {
    Unsupported(268);
}
static void APIENTRY MissingGetPolygonStipple(GLubyte *) {
    Unsupported(274);
}
static GLboolean APIENTRY MissingIsList(GLuint) {
    Unsupported(287);
    return {};
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
static void APIENTRY MissingEdgeFlagPointer(GLsizei, const GLvoid *) {
    Unsupported(312);
}
static void APIENTRY MissingIndexPointer(GLenum, GLsizei, const GLvoid *) {
    Unsupported(314);
}
static void APIENTRY MissingIndexub(GLubyte) {
    Unsupported(315);
}
static void APIENTRY MissingIndexubv(const GLubyte *) {
    Unsupported(316);
}
