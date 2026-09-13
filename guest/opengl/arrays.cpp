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
    if (!TypeBytes(type) ||
        (index != 1 && index != 4 &&
         (type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_SHORT || type == GL_UNSIGNED_INT)) ||
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
        case GL_ATTRIB_STACK_DEPTH:
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
            return normalized == 1 ? (2.0f * v + 1.0f) / 255.0f
                   : normalized    ? (v == -128 ? -1.0f : v / 127.0f)
                                   : v;
        }
        case GL_SHORT: {
            LOAD(short);
            return normalized == 1 ? (2.0f * v + 1.0f) / 65535.0f
                   : normalized    ? (v == -32768 ? -1.0f : v / 32767.0f)
                                   : v;
        }
        case GL_INT: {
            LOAD(GLint);
            return normalized == 1 ? (GLfloat)((2.0 * v + 1.0) / 4294967295.0)
                   : normalized    ? (GLfloat)((double)v / 2147483647.0)
                                   : (GLfloat)v;
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
}
static void Draw(GLenum mode, GLint first, GLsizei count, GLenum type, const void *index_pointer,
                 BOOL elements) {
    JGL_ARRAY_STATE *state;
    ULONG i, max_index = 0, index_size = 0, capacity, at = 0, mask = 0, vertex_bytes;
    const BYTE *indices = static_cast<const BYTE *>(index_pointer);
    if (!JglReady())
        return;
    if (mode > GL_POLYGON) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    if (first < 0 || count < 0) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    if (elements) {
        if (type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT) {
            JglSetError(GL_INVALID_ENUM);
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
        JglSetError(GL_INVALID_VALUE);
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
            JglSetError(GL_INVALID_VALUE);
            return;
        }
        max_index = (ULONG)end;
    }
    /* Validate all ranges before emitting any piece of the draw. Caller
     * address validity itself remains the application's GL responsibility. */
    for (i = 0; i < 5; ++i)
        if (state->Attribute[i].Enabled) {
            const JGL_ARRAY *a = &state->Attribute[i];
            ULONG size = TypeBytes(a->Type) * a->Size;
            unsigned long long end =
                (unsigned long long)max_index * (a->Stride ? (ULONG)a->Stride : size) + size;
            if (!a->Pointer || end > ~(ULONG_PTR)0 || (ULONG_PTR)a->Pointer > ~(ULONG_PTR)0 - end) {
                JglSetError(GL_INVALID_VALUE);
                return;
            }
            mask |= 1U << i;
        }
    vertex_bytes = DG_GL_VERTEX_SIZE(mask);
    capacity = JglMaxDataBytes(4) / vertex_bytes;
    if (capacity > sizeof(state->Scratch) / vertex_bytes)
        capacity = sizeof(state->Scratch) / vertex_bytes;
    if (capacity < 4 || ((mode == GL_LINE_LOOP || mode == GL_POLYGON) && (ULONG)count > capacity)) {
        JglSetError(GL_OUT_OF_MEMORY);
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
            JglSetError(GL_OUT_OF_MEMORY);
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
