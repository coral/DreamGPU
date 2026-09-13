/* SPDX-License-Identifier: GPL-2.0-or-later
 * GL1.1 section 5.4. List definitions and captured data belong to the native
 * logical namespace. Client pointers are dereferenced only while compiling.
 */
namespace dg_icd_lists {
static bool Query(ULONG fn, const ULONG args[3], ULONG *result, ULONG count) {
    ULONG bytes = 0;
    if (!JglQuery(fn, args, DG_GL_RESULT_INT, result, count * 4, &bytes))
        return false;
    if (bytes != count * 4) {
        JglSetError(GL_INVALID_OPERATION);
        return false;
    }
    if (result[0]) {
        JglSetError(result[0]);
        return false;
    }
    return true;
}
} // namespace dg_icd_lists
static void APIENTRY AliasNewList(GLuint name, GLenum mode) {
    if (!JglReady())
        return;
    if (!name) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    if (mode != GL_COMPILE && mode != GL_COMPILE_AND_EXECUTE) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    if (JglCompiling()) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    if (!JglListMode(0))
        return;
    ULONG args[] = {name, mode, 0}, result = 0;
    if (dg_icd_lists::Query(FEnum_glNewList, args, &result, 1))
        JglListMode(mode);
}
static void APIENTRY AliasEndList(void) {
    if (!JglReady())
        return;
    if (!JglCompiling()) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    ULONG args[3] = {}, result = 0, bytes = 0;
    if (!JglQuery(FEnum_glEndList, args, DG_GL_RESULT_INT, &result, 4, &bytes))
        return;
    if (bytes != 4) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    if (!result || result == GL_OUT_OF_MEMORY)
        JglListMode(0);
    if (result)
        JglSetError(result);
}
static GLuint APIENTRY AliasGenLists(GLsizei count) {
    if (!JglReady())
        return 0;
    if (count < 0) {
        JglSetError(GL_INVALID_VALUE);
        return 0;
    }
    if (!count)
        return 0;
    if ((ULONG)count > DG_GL_MAX_TEXTURES) {
        JglSetError(GL_OUT_OF_MEMORY);
        return 0;
    }
    ULONG args[] = {(ULONG)count, 0, 0}, result[2] = {};
    return dg_icd_lists::Query(FEnum_glGenLists, args, result, 2) ? result[1] : 0;
}
static GLboolean APIENTRY AliasIsList(GLuint name) {
    if (!JglReady() || !name)
        return GL_FALSE;
    ULONG args[] = {name, 0, 0}, bytes = 0;
    GLboolean result = 0;
    if (!JglQuery(FEnum_glIsList, args, DG_GL_RESULT_BOOL, &result, 1, &bytes))
        return GL_FALSE;
    if (bytes != 1 || result > 1) {
        JglSetError(GL_INVALID_OPERATION);
        return GL_FALSE;
    }
    return result;
}
static void APIENTRY AliasDeleteLists(GLuint name, GLsizei count) {
    if (!JglReady())
        return;
    if (count < 0) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    if (!count)
        return;
    ULONG args[] = {name, (ULONG)count, 0}, result = 0;
    dg_icd_lists::Query(FEnum_glDeleteLists, args, &result, 1);
}
static void APIENTRY AliasListBase(GLuint base) {
    if (JglReady())
        JglScalarVector(FEnum_glListBase, 1, &base);
}
static void APIENTRY AliasCallList(GLuint name) {
    JglCallList(name);
}
static void APIENTRY AliasCallLists(GLsizei count, GLenum type, const GLvoid *values) {
    // CallList(s) are explicitly legal inside Begin. JglCallLists checks the
    // actual owning context without applying the ordinary outside-Begin guard.
    auto *state = JglArrays();
    if (!state)
        return;
    if (count < 0) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    ULONG unit = 0;
    switch (type) {
        case GL_BYTE:
        case GL_UNSIGNED_BYTE:
            unit = 1;
            break;
        case GL_SHORT:
        case GL_UNSIGNED_SHORT:
        case GL_2_BYTES:
            unit = 2;
            break;
        case GL_3_BYTES:
            unit = 3;
            break;
        case GL_INT:
        case GL_UNSIGNED_INT:
        case GL_FLOAT:
        case GL_4_BYTES:
            unit = 4;
            break;
        default:
            JglCommandError(GL_INVALID_ENUM);
            return;
    }
    if (!count)
        return;
    if ((ULONG)count > DG_GL_MAX_CAPTURE_VALUES || (ULONG)count > JglMaxDataBytes(1) / 4) {
        JglCommandError(GL_OUT_OF_MEMORY);
        return;
    }
    if (!dg_icd_textures::Span(values, (ULONG)count, unit, true))
        return;
    static_assert(DG_GL_MAX_CAPTURE_VALUES * 4 <= sizeof(state->Scratch));
    for (ULONG i = 0; i < (ULONG)count; ++i) {
        const BYTE *p = (const BYTE *)values + i * unit;
        ULONG offset = 0;
        switch (type) {
            case GL_BYTE: {
                GLbyte v;
                CopyMemory(&v, p, 1);
                offset = (ULONG)(GLint)v;
                break;
            }
            case GL_UNSIGNED_BYTE:
                offset = *p;
                break;
            case GL_SHORT: {
                GLshort v;
                CopyMemory(&v, p, 2);
                offset = (ULONG)(GLint)v;
                break;
            }
            case GL_UNSIGNED_SHORT: {
                GLushort v;
                CopyMemory(&v, p, 2);
                offset = v;
                break;
            }
            case GL_INT:
            case GL_UNSIGNED_INT:
                CopyMemory(&offset, p, 4);
                break;
            case GL_FLOAT: {
                GLfloat v;
                CopyMemory(&v, p, 4);
                double wide = v;
                if (!(wide >= -2147483648.0 && wide < 4294967296.0)) {
                    JglCommandError(GL_INVALID_VALUE);
                    return;
                }
                offset = (ULONG)(long long)wide;
                break;
            }
            default:
                for (ULONG b = 0; b < unit; ++b)
                    offset = (offset << 8) | p[b];
                break;
        }
        CopyMemory(state->Scratch + i * 4, &offset, 4);
    }
    JglCallLists((ULONG)count, (const ULONG *)state->Scratch);
}
