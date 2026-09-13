/* SPDX-License-Identifier: GPL-2.0-or-later
 * GL 1.1 client arrays. Only guest-local descriptors retain client pointers;
 * immutable draw packets contain packed vertices, never an address. */
#include "internal.h"

extern "C" {

static GLint Attribute(GLenum cap) {
    switch (cap) {
        case GL_VERTEX_ARRAY:
            return 0;
        case GL_COLOR_ARRAY:
            return 1;
        case GL_NORMAL_ARRAY:
            return 2;
        case GL_TEXTURE_COORD_ARRAY:
            return 3;
        case GL_INDEX_ARRAY:
            return 5;
        case GL_EDGE_FLAG_ARRAY:
            return 6;
        case 0x845e:
            return JglSupportsSecondary() ? 4 : -1;
        default:
            return -1;
    }
}
static ULONG TypeBytes(GLenum type) {
    switch (type) {
        case GL_BYTE:
        case GL_UNSIGNED_BYTE:
            return 1;
        case GL_SHORT:
        case GL_UNSIGNED_SHORT:
            return 2;
        case GL_INT:
        case GL_UNSIGNED_INT:
        case GL_FLOAT:
            return 4;
        case GL_DOUBLE:
            return 8;
        default:
            return 0;
    }
}
static void Pointer(ULONG index, GLint size, GLenum type, GLsizei stride, const void *pointer) {
    JGL_ARRAY *a;
    if (!JglReady())
        return;
    if (index == 4 && !JglSupportsSecondary()) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    if (index == 4 && size != 3) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    if (stride < 0 || size < (index == 0 ? 2 : index == 1 ? 3 : 1) || size > 4) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    if (index == 5 || index == 6) {
        if ((index == 5 && type != GL_SHORT && type != GL_INT && type != GL_FLOAT &&
             type != GL_DOUBLE) ||
            (index == 6 && type != GL_UNSIGNED_BYTE)) {
            JglSetError(GL_INVALID_ENUM);
            return;
        }
    } else if (!TypeBytes(type) ||
               (index != 1 && index != 4 &&
                (type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_SHORT ||
                 type == GL_UNSIGNED_INT)) ||
               (index != 1 && index != 2 && index != 4 && type == GL_BYTE)) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    a = &JglArrays()->Attribute[index];
    a->Size = size;
    a->Type = type;
    a->Stride = stride;
    a->Pointer = pointer;
}
void APIENTRY glIndexPointer(GLenum type, GLsizei stride, const void *pointer) {
    Pointer(5, 1, type, stride, pointer);
}
void APIENTRY glEdgeFlagPointer(GLsizei stride, const void *pointer) {
    Pointer(6, 1, GL_UNSIGNED_BYTE, stride, pointer);
}
void APIENTRY glVertexPointer(GLint size, GLenum type, GLsizei stride, const void *pointer) {
    Pointer(0, size, type, stride, pointer);
}
void APIENTRY glColorPointer(GLint size, GLenum type, GLsizei stride, const void *pointer) {
    Pointer(1, size, type, stride, pointer);
}
void APIENTRY glNormalPointer(GLenum type, GLsizei stride, const void *pointer) {
    Pointer(2, 3, type, stride, pointer);
}
void APIENTRY glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *pointer) {
    Pointer(3, size, type, stride, pointer);
}
void APIENTRY glSecondaryColorPointerEXT(GLint size, GLenum type, GLsizei stride,
                                         const void *pointer) {
    Pointer(4, size, type, stride, pointer);
}
static void Enable(GLenum cap, BOOL enabled) {
    GLint index;
    if (!JglReady())
        return;
    index = Attribute(cap);
    if (index < 0) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    JglArrays()->Attribute[index].Enabled = enabled;
}
void APIENTRY glEnableClientState(GLenum cap) {
    Enable(cap, TRUE);
}
void APIENTRY glDisableClientState(GLenum cap) {
    Enable(cap, FALSE);
}
void APIENTRY glGetPointerv(GLenum pname, void **output) {
    GLint index;
    if (!JglReady() || !output)
        return;
    switch (pname) {
        case GL_SELECTION_BUFFER_POINTER:
            *output = JglArrays()->SelectPointer;
            return;
        case GL_FEEDBACK_BUFFER_POINTER:
            *output = JglArrays()->FeedbackPointer;
            return;
        case GL_INDEX_ARRAY_POINTER:
            index = 5;
            break;
        case GL_EDGE_FLAG_ARRAY_POINTER:
            index = 6;
            break;
        case GL_VERTEX_ARRAY_POINTER:
            index = 0;
            break;
        case GL_COLOR_ARRAY_POINTER:
            index = 1;
            break;
        case GL_NORMAL_ARRAY_POINTER:
            index = 2;
            break;
        case GL_TEXTURE_COORD_ARRAY_POINTER:
            index = 3;
            break;
        case 0x845d:
            if (!JglSupportsSecondary()) {
                JglSetError(GL_INVALID_ENUM);
                return;
            }
            index = 4;
            break;
        default:
            JglSetError(GL_INVALID_ENUM);
            return;
    }
    *output = (void *)JglArrays()->Attribute[index].Pointer;
}
BOOL JglArrayQuery(GLenum pname, GLint *value) {
    JGL_ARRAY_STATE *state = JglArrays();
    GLint index = Attribute(pname);
    if (!state)
        return FALSE;
    if (index >= 0) {
        *value = state->Attribute[index].Enabled;
        return TRUE;
    }
#define GET(token, index, field)                                                                   \
    case token:                                                                                    \
        *value = state->Attribute[index].field;                                                    \
        return TRUE
    switch (pname) {
        GET(GL_INDEX_ARRAY_STRIDE, 5, Stride);
        GET(GL_INDEX_ARRAY_TYPE, 5, Type);
        GET(GL_EDGE_FLAG_ARRAY_STRIDE, 6, Stride);
        case GL_CLIENT_ATTRIB_STACK_DEPTH:
            *value = state->ClientDepth;
            return TRUE;
        case GL_MAX_CLIENT_ATTRIB_STACK_DEPTH:
            *value = 16;
            return TRUE;
        case GL_ATTRIB_STACK_DEPTH:
            if (state->ListAware)
                return FALSE;
            *value = state->ServerDepth;
            return TRUE;
        case GL_MAX_ATTRIB_STACK_DEPTH:
            *value = 16;
            return TRUE;
            GET(GL_VERTEX_ARRAY_SIZE, 0, Size);
            GET(GL_VERTEX_ARRAY_TYPE, 0, Type);
            GET(GL_VERTEX_ARRAY_STRIDE, 0, Stride);
            GET(GL_COLOR_ARRAY_SIZE, 1, Size);
            GET(GL_COLOR_ARRAY_TYPE, 1, Type);
            GET(GL_COLOR_ARRAY_STRIDE, 1, Stride);
            GET(GL_NORMAL_ARRAY_TYPE, 2, Type);
            GET(GL_NORMAL_ARRAY_STRIDE, 2, Stride);
            GET(GL_TEXTURE_COORD_ARRAY_SIZE, 3, Size);
            GET(GL_TEXTURE_COORD_ARRAY_TYPE, 3, Type);
            GET(GL_TEXTURE_COORD_ARRAY_STRIDE, 3, Stride);
        case 0x845a:
        case 0x845b:
        case 0x845c:
            if (!JglSupportsSecondary())
                return FALSE;
            *value = pname == 0x845a   ? state->Attribute[4].Size
                     : pname == 0x845b ? (GLint)state->Attribute[4].Type
                                       : state->Attribute[4].Stride;
            return TRUE;
        default:
            return FALSE;
    }
#undef GET
}
/* memcpy handles unaligned client data and preserves FLOAT payload bits.
 * Integer normals and colors are normalized; positions/coordinates are not. */
static GLfloat Component(const BYTE *p, GLenum type, ULONG normalized) {
#define LOAD(T)                                                                                    \
    T v;                                                                                           \
    CopyMemory(&v, p, sizeof(v))
    switch (type) {
        case GL_FLOAT: {
            LOAD(GLfloat);
            return v;
        }
        case GL_DOUBLE: {
            LOAD(GLdouble);
            return (GLfloat)v;
        }
        case GL_BYTE: {
            LOAD(signed char);
            return normalized ? (2.0f * v + 1.0f) / 255.0f : v;
        }
        case GL_SHORT: {
            LOAD(short);
            return normalized ? (2.0f * v + 1.0f) / 65535.0f : v;
        }
        case GL_INT: {
            LOAD(GLint);
            return normalized ? (GLfloat)((2.0 * v + 1.0) / 4294967295.0) : (GLfloat)v;
        }
        case GL_UNSIGNED_BYTE: {
            LOAD(GLubyte);
            return normalized ? v / 255.0f : v;
        }
        case GL_UNSIGNED_SHORT: {
            LOAD(GLushort);
            return normalized ? v / 65535.0f : v;
        }
        case GL_UNSIGNED_INT: {
            LOAD(GLuint);
            return normalized ? (GLfloat)((double)v / 4294967295.0) : (GLfloat)v;
        }
        default:
            return 0;
    }
#undef LOAD
}
static ULONG Index(const BYTE *indices, ULONG bytes, ULONG first, ULONG offset) {
    if (!indices)
        return first + offset;
    if (bytes == 1)
        return indices[offset];
    if (bytes == 2) {
        GLushort v;
        CopyMemory(&v, indices + offset * 2, 2);
        return v;
    }
    {
        GLuint v;
        CopyMemory(&v, indices + offset * 4, 4);
        return v;
    }
}
GLfloat JglColorComponent(const BYTE *value, GLenum type) {
    return Component(value, type, 1);
}
static BOOL ArrayRange(const JGL_ARRAY *a, ULONG index) {
    ULONG size = TypeBytes(a->Type) * a->Size;
    unsigned long long end =
        (unsigned long long)index * (a->Stride ? (ULONG)a->Stride : size) + size;
    return a->Pointer && end <= ~(ULONG_PTR)0 && (ULONG_PTR)a->Pointer <= ~(ULONG_PTR)0 - end;
}
static GLdouble IndexComponent(const BYTE *p, GLenum type) {
#define DG_INDEX_LOAD(T)                                                                           \
    {                                                                                              \
        T v;                                                                                       \
        CopyMemory(&v, p, sizeof(v));                                                              \
        return (GLdouble)v;                                                                        \
    }
    switch (type) {
        case GL_SHORT:
            DG_INDEX_LOAD(GLshort);
        case GL_INT:
            DG_INDEX_LOAD(GLint);
        case GL_FLOAT:
            DG_INDEX_LOAD(GLfloat);
        case GL_DOUBLE:
            DG_INDEX_LOAD(GLdouble);
        default:
            return 0;
    }
#undef DG_INDEX_LOAD
}
void JglArrayElement(GLint index) {
    // ArrayElement is legal inside Begin/End; JglReady intentionally is not.
    JGL_ARRAY_STATE *state = JglArrays();
    if (!state)
        return;
    if (index < 0) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    GLfloat values[5][4] = {};
    // Validate and snapshot every enabled client range before any scalar can
    // flush the transport. An invalid later range must not partially set state.
    for (ULONG i = 0; i < 7; ++i) {
        const JGL_ARRAY *a = &state->Attribute[i];
        if (!a->Enabled)
            continue;
        if (!ArrayRange(a, (ULONG)index)) {
            JglCommandError(GL_INVALID_VALUE);
            return;
        }
    }
    for (ULONG i = 0; i < 5; ++i) {
        const JGL_ARRAY *a = &state->Attribute[i];
        if (!a->Enabled)
            continue;
        ULONG unit = TypeBytes(a->Type);
        ULONG stride = a->Stride ? (ULONG)a->Stride : unit * a->Size;
        const BYTE *p = (const BYTE *)a->Pointer + (ULONG_PTR)(ULONG)index * stride;
        values[i][3] = 1.0f;
        for (GLint n = 0; n < a->Size; ++n)
            values[i][n] = Component(p + n * unit, a->Type, i == 1 || i == 2 || i == 4);
    }
    GLdouble color_index = 0;
    ULONG edge = 0;
    if (state->Attribute[5].Enabled) {
        const JGL_ARRAY *a = &state->Attribute[5];
        color_index = IndexComponent((const BYTE *)a->Pointer +
                                         (ULONG_PTR)(ULONG)index *
                                             (a->Stride ? (ULONG)a->Stride : TypeBytes(a->Type)),
                                     a->Type);
    }
    if (state->Attribute[6].Enabled) {
        const JGL_ARRAY *a = &state->Attribute[6];
        edge = *((const BYTE *)a->Pointer +
                 (ULONG_PTR)(ULONG)index * (a->Stride ? (ULONG)a->Stride : 1)) != 0;
    }
    const ULONG functions[5] = {FEnum_glVertex4f, FEnum_glColor4f, FEnum_glNormal3f,
                                FEnum_glTexCoord4f, FEnum_glSecondaryColor3f};
    // Vertex last: the preceding setters establish this vertex's attributes.
    for (ULONG n = 1; n <= 5; ++n) {
        if (n == 5) {
            if (state->Attribute[5].Enabled)
                JglScalarVector(FEnum_glIndexd, 2, &color_index);
            if (state->Attribute[6].Enabled)
                JglScalarVector(FEnum_glEdgeFlag, 1, &edge);
        }
        ULONG i = n % 5;
        if (state->Attribute[i].Enabled)
            JglScalarVector(functions[i], i == 2 || i == 4 ? 3 : 4, values[i]);
    }
}
void JglInterleavedArrays(GLenum format, GLsizei stride, const void *pointer) {
    if (!JglReady())
        return;
    if (stride < 0) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    // Table2.5; float=4, ubyte=1, c=4 on this explicitly32-bit GL ABI.
    static_assert(sizeof(GLfloat) == 4 && sizeof(GLubyte) == 1);
    struct Layout {
        BYTE texture, color, normal, vertex, color_offset, normal_offset, vertex_offset, bytes;
        GLenum color_type;
    };
    static const Layout layouts[] = {{0, 0, 0, 2, 0, 0, 0, 8, 0},  // GL_V2F
                                     {0, 0, 0, 3, 0, 0, 0, 12, 0}, // GL_V3F
                                     {0, 4, 0, 2, 0, 0, 4, 12, GL_UNSIGNED_BYTE},
                                     {0, 4, 0, 3, 0, 0, 4, 16, GL_UNSIGNED_BYTE},
                                     {0, 3, 0, 3, 0, 0, 12, 24, GL_FLOAT},
                                     {0, 0, 3, 3, 0, 0, 12, 24, 0},
                                     {0, 4, 3, 3, 0, 16, 28, 40, GL_FLOAT},
                                     {2, 0, 0, 3, 0, 0, 8, 20, 0},
                                     {4, 0, 0, 4, 0, 0, 16, 32, 0},
                                     {2, 4, 0, 3, 8, 0, 12, 24, GL_UNSIGNED_BYTE},
                                     {2, 3, 0, 3, 8, 0, 20, 32, GL_FLOAT},
                                     {2, 0, 3, 3, 0, 8, 20, 32, 0},
                                     {2, 4, 3, 3, 8, 24, 36, 48, GL_FLOAT},
                                     {4, 4, 3, 4, 16, 32, 44, 60, GL_FLOAT}};
    // GL_V2F..GL_T4F_C4F_N3F_V4F are contiguous tokens0x2A20..0x2A2D.
    if (format < 0x2a20 || format > 0x2a2d) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    const Layout &layout = layouts[format - 0x2a20];
    if ((ULONG_PTR)pointer > ~(ULONG_PTR)0 - layout.vertex_offset) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    JGL_ARRAY_STATE *state = JglArrays();
    JGL_ARRAY next[7];
    CopyMemory(next, state->Attribute, sizeof(next));
    const GLint sizes[4] = {layout.vertex, layout.color, layout.normal, layout.texture};
    const ULONG offsets[4] = {layout.vertex_offset, layout.color_offset, layout.normal_offset, 0};
    for (ULONG i = 0; i < 4; ++i) {
        next[i].Enabled = sizes[i] != 0;
        // Disabled descriptors retain their original pointer/type/stride.
        if (!next[i].Enabled)
            continue;
        next[i].Size = sizes[i];
        next[i].Type = i == 1 ? layout.color_type : GL_FLOAT;
        next[i].Stride = stride ? stride : layout.bytes;
        next[i].Pointer = (const void *)((ULONG_PTR)pointer + offsets[i]);
    }
    next[5].Enabled = next[6].Enabled = FALSE;
    // Secondary color is an independent EXT array, unchanged by this GL1.1 call.
    CopyMemory(state->Attribute, next, sizeof(next));
}

static void Vertex(JGL_ARRAY_STATE *state, ULONG vertex, BYTE *out, ULONG bytes) {
    static const ULONG offsets[5] = {0, 16, 32, 44, DG_GL_VERTEX_SECONDARY};
    ULONG i, component;
    ZeroMemory(out, bytes);
    for (i = 0; i < 5; ++i) {
        const JGL_ARRAY *a = &state->Attribute[i];
        ULONG unit, stride;
        const BYTE *p;
        GLfloat *v;
        if (!a->Enabled)
            continue;
        unit = TypeBytes(a->Type);
        stride = a->Stride ? (ULONG)a->Stride : unit * a->Size;
        p = (const BYTE *)a->Pointer + (ULONG_PTR)vertex * stride;
        v = (GLfloat *)(out + offsets[i]);
        if (i != 2 && i != 4)
            v[3] = 1;
        if (a->Type == GL_FLOAT)
            CopyMemory(v, p, a->Size * 4);
        else
            for (component = 0; component < (ULONG)a->Size; ++component)
                v[component] = Component(p + component * unit, a->Type,
                                         i == 4             ? 1
                                         : i == 1 || i == 2 ? i
                                                            : 0);
    }
    if (state->Attribute[5].Enabled) {
        const JGL_ARRAY *a = &state->Attribute[5];
        const BYTE *p = (const BYTE *)a->Pointer +
                        (ULONG_PTR)vertex * (a->Stride ? (ULONG)a->Stride : TypeBytes(a->Type));
        GLdouble v = IndexComponent(p, a->Type);
        CopyMemory(out + DG_GL_VERTEX_INDEX, &v, 8);
    }
    if (state->Attribute[6].Enabled) {
        const JGL_ARRAY *a = &state->Attribute[6];
        out[DG_GL_VERTEX_EDGE] = *((const BYTE *)a->Pointer +
                                   (ULONG_PTR)vertex * (a->Stride ? (ULONG)a->Stride : 1)) != 0;
    }
}
static void Draw(GLenum mode, GLint first, GLsizei count, GLenum type, const void *index_pointer,
                 BOOL elements) {
    JGL_ARRAY_STATE *state;
    ULONG i, max_index = 0, index_size = 0, capacity, at = 0, mask = 0, vertex_bytes;
    const BYTE *indices = static_cast<const BYTE *>(index_pointer);
    if (!JglCommandReady())
        return;
    if (mode > GL_POLYGON) {
        JglCommandError(GL_INVALID_ENUM);
        return;
    }
    if (first < 0 || count < 0) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    if (elements) {
        if (type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT) {
            JglCommandError(GL_INVALID_ENUM);
            return;
        }
        index_size = TypeBytes(type);
    }
    if (!count)
        return;
    state = JglArrays();
    if (!state->Attribute[0].Enabled)
        return;
    if (elements &&
        (!indices || (ULONG)count * (unsigned long long)index_size > ~(ULONG_PTR)0 ||
         (ULONG_PTR)indices > ~(ULONG_PTR)0 - (ULONG)count * (unsigned long long)index_size)) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    if (elements)
        for (i = 0; i < (ULONG)count; ++i) {
            ULONG index = Index(indices, index_size, 0, i);
            if (index > max_index)
                max_index = index;
        }
    else {
        unsigned long long end = (unsigned long long)(ULONG)first + (ULONG)count - 1;
        if (end > 0xffffffffUL) {
            JglCommandError(GL_INVALID_VALUE);
            return;
        }
        max_index = (ULONG)end;
    }
    /* Validate all ranges before emitting any piece of the draw. Caller
     * address validity itself remains the application's GL responsibility. */
    for (i = 0; i < 7; ++i)
        if (state->Attribute[i].Enabled) {
            const JGL_ARRAY *a = &state->Attribute[i];
            if (!ArrayRange(a, max_index)) {
                JglCommandError(GL_INVALID_VALUE);
                return;
            }
            mask |= 1U << i;
        }
    vertex_bytes = DG_GL_VERTEX_SIZE(mask);
    capacity = JglMaxDataBytes(4) / vertex_bytes;
    if (capacity > sizeof(state->Scratch) / vertex_bytes)
        capacity = sizeof(state->Scratch) / vertex_bytes;
    if (capacity < 4 || ((mode == GL_LINE_LOOP || mode == GL_POLYGON) && (ULONG)count > capacity)) {
        JglCommandError(GL_OUT_OF_MEMORY);
        return;
    }
    /* A split line strip would restart the stipple counter. Preserve that
     * contract by rejecting only this resource-limited stippled case. */
    if (mode == GL_LINE_STRIP && (ULONG)count > capacity) {
        ULONG args[3] = {GL_LINE_STIPPLE, 0, 0}, bytes;
        GLboolean enabled;
        if (!JglQuery(FEnum_glIsEnabled, args, DG_GL_RESULT_BOOL, &enabled, sizeof(enabled),
                      &bytes))
            return;
        if (enabled) {
            JglCommandError(GL_OUT_OF_MEMORY);
            return;
        }
    }
    while (at < (ULONG)count) {
        ULONG n = (ULONG)count - at, args[4] = {mode, 0, 0, mask};
        BOOL fan = mode == GL_TRIANGLE_FAN && at != 0;
        if (fan)
            n += 2;
        if (n > capacity) {
            n = capacity;
            if (mode == GL_LINES || mode == GL_TRIANGLE_STRIP || mode == GL_QUAD_STRIP)
                n &= ~1U;
            if (mode == GL_TRIANGLES)
                n -= n % 3;
            if (mode == GL_QUADS)
                n -= n % 4;
        }
        args[2] = n;
        for (i = 0; i < n; ++i) {
            ULONG offset = fan ? (i == 0 ? 0 : i == 1 ? at - 1 : at + i - 2) : at + i;
            Vertex(state, Index(indices, index_size, (ULONG)first, offset),
                   state->Scratch + i * vertex_bytes, vertex_bytes);
        }
        if (!JglData(FEnum_glDrawArrays, args, 4, state->Scratch, n * vertex_bytes))
            return;
        at += fan ? n - 2 : n;
        if (at >= (ULONG)count)
            break;
        if (mode == GL_LINE_STRIP)
            --at;
        if (mode == GL_TRIANGLE_STRIP || mode == GL_QUAD_STRIP)
            at -= 2;
    }
}
void APIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    Draw(mode, first, count, 0, NULL, FALSE);
}
void APIENTRY glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices) {
    Draw(mode, 0, count, type, indices, TRUE);
}

} /* extern C */
