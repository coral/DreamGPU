/* SPDX-License-Identifier: GPL-2.0-or-later
 * Complete GL1.1 evaluator grids, bounded by the honestly advertised order.
 * Source semantics: OpenGL1.1 section5.1; native GL evaluates curves/surfaces.
 */
namespace dg_icd_evaluator {
constexpr ULONG Coefficients = 4 * DG_GL_MAX_EVAL_ORDER * DG_GL_MAX_EVAL_ORDER;
constexpr ULONG Values = Coefficients + 4;
static ULONG Components(GLenum target, ULONG *dimension) {
    ULONG index;
    if (target >= GL_MAP1_COLOR_4 && target <= GL_MAP1_VERTEX_4) {
        *dimension = 1;
        index = target - GL_MAP1_COLOR_4;
    } else if (target >= GL_MAP2_COLOR_4 && target <= GL_MAP2_VERTEX_4) {
        *dimension = 2;
        index = target - GL_MAP2_COLOR_4;
    } else
        return 0;
    const BYTE sizes[] = {4, 1, 3, 1, 2, 3, 4, 3, 4};
    return sizes[index];
}
template <class T>
static void Map(ULONG function, GLenum target, T u1, T u2, GLint us, GLint uo, T v1, T v2, GLint vs,
                GLint vo, const T *points, ULONG expected_dimension) {
    if (!JglCommandReady())
        return;
    ULONG dimension = 0, k = Components(target, &dimension);
    if (!k || dimension != expected_dimension) {
        JglCommandError(GL_INVALID_ENUM);
        return;
    }
    if (uo < 1 || uo > DG_GL_MAX_EVAL_ORDER || us < (GLint)k || u1 == u2 ||
        (dimension == 2 && (vo < 1 || vo > DG_GL_MAX_EVAL_ORDER || vs < (GLint)k || v1 == v2))) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    GLint maximum;
    if (!dg_icd_pixels::Integer(GL_MAX_EVAL_ORDER, &maximum))
        return;
    if (maximum < 1 || maximum > DG_GL_MAX_EVAL_ORDER) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    if (uo > maximum || (dimension == 2 && vo > maximum)) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    const unsigned long long last =
        (unsigned long long)(uo - 1) * (ULONG)us +
        (dimension == 2 ? (unsigned long long)(vo - 1) * (ULONG)vs : 0) + k;
    if (!points || last > (~(ULONG_PTR)0) / sizeof(T) ||
        (ULONG_PTR)points > ~(ULONG_PTR)0 - last * sizeof(T)) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    T data[Values];
    ULONG count = 0;
    data[count++] = u1;
    data[count++] = u2;
    if (dimension == 2) {
        data[count++] = v1;
        data[count++] = v2;
    }
    for (GLint u = 0; u < uo; ++u)
        for (GLint v = 0; v < (dimension == 2 ? vo : 1); ++v)
            for (ULONG c = 0; c < k; ++c)
                CopyMemory(data + count++,
                           (const BYTE *)points +
                               ((ULONG_PTR)u * (ULONG)us + (ULONG_PTR)v * (ULONG)vs + c) *
                                   sizeof(T),
                           sizeof(T));
    ULONG args[] = {target, (ULONG)uo, (ULONG)vo};
    JglData(function, args, dimension + 1, data, count * sizeof(T));
}
static void Grid(bool two, GLint un, GLdouble u1, GLdouble u2, GLint vn, GLdouble v1, GLdouble v2) {
    if (!JglCommandReady())
        return;
    if (un < 1 || (two && vn < 1)) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    ULONG words[10] = {(ULONG)un};
    CopyMemory(words + 1, &u1, 8);
    CopyMemory(words + 3, &u2, 8);
    if (two) {
        words[5] = (ULONG)vn;
        CopyMemory(words + 6, &v1, 8);
        CopyMemory(words + 8, &v2, 8);
    }
    JglScalarVector(two ? FEnum_glMapGrid2d : FEnum_glMapGrid1d, two ? 10 : 5, words);
}
static void Coord(GLdouble u, GLdouble v, bool two) {
    const GLdouble values[] = {u, v};
    JglScalarVector(two ? FEnum_glEvalCoord2d : FEnum_glEvalCoord1d, two ? 4 : 2, values);
}
static void Mesh(GLenum mode, GLint i1, GLint i2, GLint j1, GLint j2, bool two) {
    if (!JglCommandReady())
        return;
    if (mode != GL_POINT && mode != GL_LINE && (!two || mode != GL_FILL)) {
        JglCommandError(GL_INVALID_ENUM);
        return;
    }
    long long w = (long long)i2 - i1 + 1, h = two ? (long long)j2 - j1 + 1 : 1;
    if (w <= 0 || h <= 0 || (two && mode == GL_FILL && h == 1))
        return;
    if (two && mode == GL_FILL)
        --h;
    if (w > 0 && h > 0) {
        ULONG factor = mode == GL_POINT || !two ? 1 : 2;
        unsigned long long limit = DG_GL_MAX_VERTICES / factor;
        if ((unsigned long long)w > limit ||
            (unsigned long long)h > limit / (unsigned long long)w) {
            JglCommandError(GL_OUT_OF_MEMORY);
            return;
        }
    }
    const ULONG args[] = {mode, (ULONG)i1, (ULONG)i2, (ULONG)j1, (ULONG)j2};
    JglScalarVector(two ? FEnum_glEvalMesh2 : FEnum_glEvalMesh1, two ? 5 : 3, args);
}
static void Get(ULONG function, GLenum target, GLenum pname, void *output) {
    if (!JglReady())
        return;
    ULONG dimension = 0, k = Components(target, &dimension);
    if (!k || (pname != GL_ORDER && pname != GL_DOMAIN && pname != GL_COEFF)) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    if (!output) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    ULONG count = dimension;
    if (pname == GL_DOMAIN)
        count *= 2;
    if (pname == GL_COEFF) {
        GLint orders[2] = {};
        ULONG args[] = {target, GL_ORDER, dimension}, bytes = 0;
        if (!JglQuery(FEnum_glGetMapiv, args, DG_GL_RESULT_INT, orders, dimension * 4, &bytes))
            return;
        if (bytes != dimension * 4 || orders[0] < 1 || orders[0] > DG_GL_MAX_EVAL_ORDER ||
            (dimension == 2 && (orders[1] < 1 || orders[1] > DG_GL_MAX_EVAL_ORDER))) {
            JglSetError(GL_INVALID_OPERATION);
            return;
        }
        count = k * (ULONG)orders[0] * (dimension == 2 ? (ULONG)orders[1] : 1);
    }
    ULONG unit = function == FEnum_glGetMapdv ? 8 : 4,
          kind = function == FEnum_glGetMapdv   ? DG_GL_RESULT_DOUBLE
                 : function == FEnum_glGetMapfv ? DG_GL_RESULT_FLOAT
                                                : DG_GL_RESULT_INT;
    if ((ULONG_PTR)output > ~(ULONG_PTR)0 - count * unit) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    GLdouble data[Coefficients];
    ULONG args[] = {target, pname, count}, bytes = 0;
    if (!JglQuery(function, args, kind, data, count * unit, &bytes))
        return;
    if (bytes != count * unit) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    CopyMemory(output, data, bytes);
}
} // namespace dg_icd_evaluator
static void APIENTRY AliasMap1d(GLenum t, GLdouble u1, GLdouble u2, GLint stride, GLint order,
                                const GLdouble *p) {
    dg_icd_evaluator::Map(FEnum_glMap1d, t, u1, u2, stride, order, (GLdouble)0, (GLdouble)1, 0, 1,
                          p, 1);
}
static void APIENTRY AliasMap2d(GLenum t, GLdouble u1, GLdouble u2, GLint us, GLint uo, GLdouble v1,
                                GLdouble v2, GLint vs, GLint vo, const GLdouble *p) {
    dg_icd_evaluator::Map(FEnum_glMap2d, t, u1, u2, us, uo, v1, v2, vs, vo, p, 2);
}
static void APIENTRY AliasMapGrid1d(GLint n, GLdouble u1, GLdouble u2) {
    dg_icd_evaluator::Grid(false, n, u1, u2, 1, 0, 1);
}
static void APIENTRY AliasMapGrid2d(GLint un, GLdouble u1, GLdouble u2, GLint vn, GLdouble v1,
                                    GLdouble v2) {
    dg_icd_evaluator::Grid(true, un, u1, u2, vn, v1, v2);
}
static void APIENTRY AliasEvalCoord1d(GLdouble u) {
    dg_icd_evaluator::Coord(u, 0, false);
}
static void APIENTRY AliasEvalCoord1dv(const GLdouble *p) {
    if (!JglArrays())
        return;
    if (!p) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    AliasEvalCoord1d(p[0]);
}
static void APIENTRY AliasEvalCoord2d(GLdouble u, GLdouble v) {
    dg_icd_evaluator::Coord(u, v, true);
}
static void APIENTRY AliasEvalCoord2dv(const GLdouble *p) {
    if (!JglArrays())
        return;
    if (!p) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    AliasEvalCoord2d(p[0], p[1]);
}
static void APIENTRY AliasMap1f(GLenum t, GLfloat u1, GLfloat u2, GLint stride, GLint order,
                                const GLfloat *p) {
    dg_icd_evaluator::Map(FEnum_glMap1f, t, u1, u2, stride, order, (GLfloat)0, (GLfloat)1, 0, 1, p,
                          1);
}
static void APIENTRY AliasMap2f(GLenum t, GLfloat u1, GLfloat u2, GLint us, GLint uo, GLfloat v1,
                                GLfloat v2, GLint vs, GLint vo, const GLfloat *p) {
    dg_icd_evaluator::Map(FEnum_glMap2f, t, u1, u2, us, uo, v1, v2, vs, vo, p, 2);
}
static void APIENTRY AliasMapGrid1f(GLint n, GLfloat u1, GLfloat u2) {
    dg_icd_evaluator::Grid(false, n, u1, u2, 1, 0, 1);
}
static void APIENTRY AliasMapGrid2f(GLint un, GLfloat u1, GLfloat u2, GLint vn, GLfloat v1,
                                    GLfloat v2) {
    dg_icd_evaluator::Grid(true, un, u1, u2, vn, v1, v2);
}
static void APIENTRY AliasEvalCoord1f(GLfloat u) {
    dg_icd_evaluator::Coord(u, 0, false);
}
static void APIENTRY AliasEvalCoord1fv(const GLfloat *p) {
    if (!JglArrays())
        return;
    if (!p) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    AliasEvalCoord1f(p[0]);
}
static void APIENTRY AliasEvalCoord2f(GLfloat u, GLfloat v) {
    dg_icd_evaluator::Coord(u, v, true);
}
static void APIENTRY AliasEvalCoord2fv(const GLfloat *p) {
    if (!JglArrays())
        return;
    if (!p) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    AliasEvalCoord2f(p[0], p[1]);
}
static void APIENTRY AliasEvalPoint1(GLint i) {
    JglScalarVector(FEnum_glEvalPoint1, 1, &i);
}
static void APIENTRY AliasEvalPoint2(GLint i, GLint j) {
    const GLint args[] = {i, j};
    JglScalarVector(FEnum_glEvalPoint2, 2, args);
}
static void APIENTRY AliasEvalMesh1(GLenum mode, GLint i1, GLint i2) {
    dg_icd_evaluator::Mesh(mode, i1, i2, 0, 0, false);
}
static void APIENTRY AliasEvalMesh2(GLenum mode, GLint i1, GLint i2, GLint j1, GLint j2) {
    dg_icd_evaluator::Mesh(mode, i1, i2, j1, j2, true);
}
static void APIENTRY AliasGetMapdv(GLenum target, GLenum query, GLdouble *p) {
    dg_icd_evaluator::Get(FEnum_glGetMapdv, target, query, p);
}
static void APIENTRY AliasGetMapfv(GLenum target, GLenum query, GLfloat *p) {
    dg_icd_evaluator::Get(FEnum_glGetMapfv, target, query, p);
}
static void APIENTRY AliasGetMapiv(GLenum target, GLenum query, GLint *p) {
    dg_icd_evaluator::Get(FEnum_glGetMapiv, target, query, p);
}
