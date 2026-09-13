/* SPDX-License-Identifier: GPL-2.0-or-later
 * GL1.1 edge/index and fixed pixel state. The offered pixel format is RGBA
 * without accumulation storage; native error/no-effect semantics stay visible.
 */
static void APIENTRY AliasEdgeFlag(GLboolean value) {
    ULONG v = value != 0;
    JglScalarVector(FEnum_glEdgeFlag, 1, &v);
}
static void APIENTRY AliasEdgeFlagv(const GLboolean *v) {
    if (!JglArrays())
        return;
    if (!v) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    AliasEdgeFlag(*v);
}
static void APIENTRY AliasIndexd(GLdouble value) {
    JglScalarVector(FEnum_glIndexd, 2, &value);
}
static void APIENTRY AliasIndexdv(const GLdouble *v) {
    if (!JglArrays())
        return;
    if (!v) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    AliasIndexd((GLdouble)*v);
}
static void APIENTRY AliasIndexf(GLfloat value) {
    AliasIndexd((GLdouble)value);
}
static void APIENTRY AliasIndexfv(const GLfloat *v) {
    if (!JglArrays())
        return;
    if (!v) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    AliasIndexd((GLdouble)*v);
}
static void APIENTRY AliasIndexi(GLint value) {
    AliasIndexd((GLdouble)value);
}
static void APIENTRY AliasIndexiv(const GLint *v) {
    if (!JglArrays())
        return;
    if (!v) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    AliasIndexd((GLdouble)*v);
}
static void APIENTRY AliasIndexs(GLshort value) {
    AliasIndexd((GLdouble)value);
}
static void APIENTRY AliasIndexsv(const GLshort *v) {
    if (!JglArrays())
        return;
    if (!v) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    AliasIndexd((GLdouble)*v);
}
static void APIENTRY AliasIndexub(GLubyte value) {
    AliasIndexd((GLdouble)value);
}
static void APIENTRY AliasIndexubv(const GLubyte *v) {
    if (!JglArrays())
        return;
    if (!v) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    AliasIndexd((GLdouble)*v);
}
static void APIENTRY AliasIndexPointer(GLenum type, GLsizei stride, const void *p) {
    glIndexPointer(type, stride, p);
}
static void APIENTRY AliasEdgeFlagPointer(GLsizei stride, const void *p) {
    glEdgeFlagPointer(stride, p);
}
static void APIENTRY AliasClearAccum(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    if (!JglCommandReady())
        return;
    const GLfloat v[] = {r, g, b, a};
    JglScalarVector(FEnum_glClearAccum, 4, v);
}
static void APIENTRY AliasClearIndex(GLfloat value) {
    if (JglCommandReady())
        JglScalarVector(FEnum_glClearIndex, 1, &value);
}
static void APIENTRY AliasIndexMask(GLuint mask) {
    if (JglCommandReady())
        JglScalarVector(FEnum_glIndexMask, 1, &mask);
}
static void APIENTRY AliasAccum(GLenum op, GLfloat value) {
    if (!JglCommandReady())
        return;
    ULONG v[] = {op, 0};
    CopyMemory(v + 1, &value, 4);
    JglScalarVector(FEnum_glAccum, 2, v);
}
static void APIENTRY AliasLogicOp(GLenum op) {
    if (JglCommandReady())
        JglScalarVector(FEnum_glLogicOp, 1, &op);
}
static void APIENTRY AliasPolygonStipple(const GLubyte *mask) {
    if (!JglCommandReady())
        return;
    dg_icd_images::Layout l;
    if (!dg_icd_images::Describe(32, 32, GL_COLOR_INDEX, GL_BITMAP, mask, &l))
        return;
    BYTE pattern[128];
    dg_icd_images::Pack(l, 32, 0, pattern, 128);
    JglData(FEnum_glPolygonStipple, nullptr, 0, pattern, 128);
}
static void APIENTRY AliasGetPolygonStipple(GLubyte *mask) {
    if (!JglReady())
        return;
    dg_icd_images::Layout l;
    if (!dg_icd_images::DescribeStore(32, 32, GL_COLOR_INDEX, GL_BITMAP, mask, &l, JglPack(),
                                      false))
        return;
    BYTE pattern[128];
    ULONG args[3] = {}, bytes = 0;
    if (!JglQuery(FEnum_glGetPolygonStipple, args, DG_GL_RESULT_INT, pattern, 128, &bytes))
        return;
    if (bytes != 128) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    for (ULONG y = 0; y < 32; ++y)
        for (ULONG x = 0; x < 32; ++x) {
            ULONG bit = (ULONG)l.Skip + x;
            BYTE *dst = const_cast<BYTE *>(l.Source) + (ULONG_PTR)y * l.Stride + bit / 8;
            BYTE flag = (BYTE)(1U << (l.Lsb ? (bit & 7) : 7 - (bit & 7)));
            if (pattern[y * 4 + x / 8] & (1U << (7 - (x & 7))))
                *dst |= flag;
            else
                *dst &= (BYTE)~flag;
        }
}
