/* SPDX-License-Identifier: GPL-2.0-or-later
 * GL 1.1 client arrays, plus negotiated per-unit texture coordinates, the
 * EXT_fog_coord array and ARB_vertex_program generic attribute arrays (state in
 * attrib.cpp). Only guest-local descriptors retain client pointers; immutable
 * draw packets contain packed vertices, never an address. ARB_vertex_array_bgra
 * arrays are reordered to RGBA while they are packed; only buffer slots of the
 * BGRA buffer draw reach the host as BGRA. */
#include "internal.h"
#include "gl-arrays.h"
#include "buffers.h"

extern "C" {

/* Texture-coordinate commands address the client-active unit's slot. */
static ULONG TextureSlot(const JGL_ARRAY_STATE *state) {
    return state->ClientTexture ? 6 + state->ClientTexture : 3;
}
static GLint Attribute(const JGL_ARRAY_STATE *state, GLenum cap) {
    switch (cap) {
        case GL_VERTEX_ARRAY:
            return 0;
        case GL_COLOR_ARRAY:
            return 1;
        case GL_NORMAL_ARRAY:
            return 2;
        case GL_TEXTURE_COORD_ARRAY:
            return (GLint)TextureSlot(state);
        case GL_INDEX_ARRAY:
            return 5;
        case GL_EDGE_FLAG_ARRAY:
            return 6;
        case 0x845e:
            return JglSupportsSecondary() ? 4 : -1;
        case 0x8457: /* FOG_COORDINATE_ARRAY_EXT */
            return JglFogCoordAvailable() ? JGL_FOG_SLOT : -1;
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
    /* ARB_vertex_array_bgra: size BGRA for colours and secondary colours;
     * any other array keeps it an invalid size. */
    const BOOL bgra =
        size == JGL_BGRA && (index == 1 || index == 4) && JglVertexArrayBgraAvailable();
    if (index == 4 && !JglSupportsSecondary()) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    if (index == 4 && size != 3 && !bgra) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    /* EXT_fog_coord: one FLOAT or DOUBLE coordinate (size is implicit). */
    if (index == JGL_FOG_SLOT) {
        if (!JglFogCoordAvailable()) {
            JglSetError(GL_INVALID_OPERATION);
            return;
        }
        if (type != GL_FLOAT && type != GL_DOUBLE) {
            JglSetError(GL_INVALID_ENUM);
            return;
        }
    }
    if (stride < 0 || (!bgra && (size < (index == 0 ? 2 : index == 1 ? 3 : 1) || size > 4))) {
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
    /* A valid colour type other than UNSIGNED_BYTE cannot be BGRA. */
    if (bgra && type != GL_UNSIGNED_BYTE) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    a = &JglArrays()->Attribute[index == 3 ? TextureSlot(JglArrays()) : index];
    a->Size = bgra ? 4 : size;
    a->Bgra = bgra;
    a->Type = type;
    a->Stride = stride;
    a->Pointer = pointer;
    /* ARB_vertex_buffer_object: the pointer is an offset into the bound buffer. */
    a->Buffer = JglArrays()->ArrayBuffer.Name;
    a->Serial = JglArrays()->ArrayBuffer.Serial;
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
void APIENTRY glFogCoordPointer(GLenum type, GLsizei stride, const void *pointer) {
    Pointer(JGL_FOG_SLOT, 1, type, stride, pointer);
}
void APIENTRY glFogCoordPointerEXT(GLenum type, GLsizei stride, const void *pointer) {
    Pointer(JGL_FOG_SLOT, 1, type, stride, pointer);
}
static void Enable(GLenum cap, BOOL enabled) {
    GLint index;
    if (!JglReady())
        return;
    index = Attribute(JglArrays(), cap);
    if (index < 0) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    JglEnableArray(JglArrays(), (ULONG)index, enabled);
}
void APIENTRY glEnableClientState(GLenum cap) {
    Enable(cap, TRUE);
}
void APIENTRY glDisableClientState(GLenum cap) {
    Enable(cap, FALSE);
}
/* Client-only selector; never recorded or sent. Unit 0 keeps slot 3. */
void APIENTRY glClientActiveTextureARB(GLenum texture) {
    if (!JglReady())
        return;
    if (!JglMultitextureAvailable()) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    if (texture - JGL_TEXTURE0 >= DG_GL_MAX_TEXTURE_UNITS) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    JglArrays()->ClientTexture = texture - JGL_TEXTURE0;
}
void APIENTRY glClientActiveTexture(GLenum texture) {
    glClientActiveTextureARB(texture);
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
            index = (GLint)TextureSlot(JglArrays());
            break;
        case 0x845d:
            if (!JglSupportsSecondary()) {
                JglSetError(GL_INVALID_ENUM);
                return;
            }
            index = 4;
            break;
        case 0x8456: /* FOG_COORDINATE_ARRAY_POINTER_EXT */
            if (!JglFogCoordAvailable()) {
                JglSetError(GL_INVALID_ENUM);
                return;
            }
            index = JGL_FOG_SLOT;
            break;
        default:
            JglSetError(GL_INVALID_ENUM);
            return;
    }
    *output = (void *)JglArrays()->Attribute[index].Pointer;
}
BOOL JglArrayQuery(GLenum pname, GLint *value) {
    JGL_ARRAY_STATE *state = JglArrays();
    GLint index;
    if (!state)
        return FALSE;
    index = Attribute(state, pname);
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
        case GL_COLOR_ARRAY_SIZE:
            *value = state->Attribute[1].Bgra ? JGL_BGRA : state->Attribute[1].Size;
            return TRUE;
            GET(GL_COLOR_ARRAY_TYPE, 1, Type);
            GET(GL_COLOR_ARRAY_STRIDE, 1, Stride);
            GET(GL_NORMAL_ARRAY_TYPE, 2, Type);
            GET(GL_NORMAL_ARRAY_STRIDE, 2, Stride);
            GET(GL_TEXTURE_COORD_ARRAY_SIZE, TextureSlot(state), Size);
            GET(GL_TEXTURE_COORD_ARRAY_TYPE, TextureSlot(state), Type);
            GET(GL_TEXTURE_COORD_ARRAY_STRIDE, TextureSlot(state), Stride);
        case 0x84e1: /* CLIENT_ACTIVE_TEXTURE: otherwise an unknown host enum */
            if (!JglMultitextureAvailable())
                return FALSE;
            *value = (GLint)(JGL_TEXTURE0 + state->ClientTexture);
            return TRUE;
        case 0x845a:
        case 0x845b:
        case 0x845c:
            if (!JglSupportsSecondary())
                return FALSE;
            *value = pname == 0x845a
                         ? (state->Attribute[4].Bgra ? JGL_BGRA : state->Attribute[4].Size)
                     : pname == 0x845b ? (GLint)state->Attribute[4].Type
                                       : state->Attribute[4].Stride;
            return TRUE;
        case 0x81a8: /* ARRAY_ELEMENT_LOCK_FIRST_EXT */
        case 0x81a9: /* ARRAY_ELEMENT_LOCK_COUNT_EXT */
            if (!JglCompiledVertexArrayAvailable())
                return FALSE;
            *value = (GLint)(pname == 0x81a8 ? state->LockFirst : state->LockCount);
            return TRUE;
        case 0x8454: /* FOG_COORDINATE_ARRAY_TYPE_EXT */
        case 0x8455: /* FOG_COORDINATE_ARRAY_STRIDE_EXT */
            if (!JglFogCoordAvailable())
                return FALSE;
            *value = pname == 0x8454 ? (GLint)state->Attribute[JGL_FOG_SLOT].Type
                                     : state->Attribute[JGL_FOG_SLOT].Stride;
            return TRUE;
        default:
            break;
    }
    /* ARB_vertex_buffer_object: ARRAY_BUFFER and ELEMENT_ARRAY_BUFFER, then
     * each array's latched buffer (VERTEX, NORMAL, COLOR, INDEX, TEXTURE_COORD
     * of the client-active unit, EDGE_FLAG, SECONDARY_COLOR, FOG_COORDINATE;
     * WEIGHT_ARRAY has no array here). */
    if (pname < 0x8894 || pname > 0x889d || !JglVertexBufferObjectAvailable() ||
        (pname == 0x889c && !JglSupportsSecondary()) ||
        (pname == 0x889d && !JglFogCoordAvailable()))
        return FALSE;
    {
        static const signed char slots[8] = {0, 2, 1, 5, -1, 6, 4, JGL_FOG_SLOT};
        const signed char slot = pname >= 0x8896 ? slots[pname - 0x8896] : 0;
        *value = pname == 0x8894 ? (GLint)state->ArrayBuffer.Name
                 : pname == 0x8895
                     ? (GLint)state->ElementBuffer.Name
                     : (GLint)state->Attribute[slot < 0 ? TextureSlot(state) : (ULONG)slot].Buffer;
    }
    return TRUE;
#undef GET
}
/* Fixed-size copies handle unaligned client data inline: the guest builds
 * without builtins, where CopyMemory is a call per element. */
#define JGL_LOAD(destination, source, bytes) __builtin_memcpy((destination), (source), (bytes))
/* One element of `bytes` bytes; the common sizes copy inline. */
static inline void CopyElement(BYTE *destination, const BYTE *source, ULONG bytes) {
    switch (bytes) {
        case 4:
            JGL_LOAD(destination, source, 4);
            return;
        case 8:
            JGL_LOAD(destination, source, 8);
            return;
        case 12:
            JGL_LOAD(destination, source, 12);
            return;
        case 16:
            JGL_LOAD(destination, source, 16);
            return;
        default:
            CopyMemory(destination, source, bytes);
    }
}
/* memcpy handles unaligned client data and preserves FLOAT payload bits.
 * Integer normals and colors are normalized; positions/coordinates are not. */
static GLfloat Component(const BYTE *p, GLenum type, ULONG normalized) {
#define LOAD(T)                                                                                    \
    T v;                                                                                           \
    JGL_LOAD(&v, p, sizeof(v))
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
        JGL_LOAD(&v, indices + offset * 2, 2);
        return v;
    }
    {
        GLuint v;
        JGL_LOAD(&v, indices + offset * 4, 4);
        return v;
    }
}
/* The smallest and largest of `count` (nonzero) indices of `bytes` bytes.
 * Pairs of indices go to two independent minimum/maximum chains. */
static void IndexBounds(const BYTE *indices, ULONG bytes, ULONG count, ULONG *low, ULONG *high) {
    ULONG l = 0xffffffffUL, h = 0, l2 = 0xffffffffUL, h2 = 0;
#define JGL_BOUNDS(T)                                                                              \
    {                                                                                              \
        ULONG i = 0;                                                                               \
        for (; i + 1 < count; i += 2) {                                                            \
            T v, w;                                                                                \
            JGL_LOAD(&v, indices + i * sizeof(T), sizeof(T));                                      \
            JGL_LOAD(&w, indices + (i + 1) * sizeof(T), sizeof(T));                                \
            l = (ULONG)v < l ? (ULONG)v : l;                                                       \
            h = (ULONG)v > h ? (ULONG)v : h;                                                       \
            l2 = (ULONG)w < l2 ? (ULONG)w : l2;                                                    \
            h2 = (ULONG)w > h2 ? (ULONG)w : h2;                                                    \
        }                                                                                          \
        if (i < count) {                                                                           \
            T v;                                                                                   \
            JGL_LOAD(&v, indices + i * sizeof(T), sizeof(T));                                      \
            l = (ULONG)v < l ? (ULONG)v : l;                                                       \
            h = (ULONG)v > h ? (ULONG)v : h;                                                       \
        }                                                                                          \
    }
    if (bytes == 1) {
        JGL_BOUNDS(GLubyte)
    } else if (bytes == 2) {
        JGL_BOUNDS(GLushort)
    } else {
        JGL_BOUNDS(GLuint)
    }
#undef JGL_BOUNDS
    *low = l < l2 ? l : l2;
    *high = h > h2 ? h : h2;
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
/* A buffer array's element range up to index `high` lies within its object. */
static BOOL BufferRange(const JGL_ARRAY *a, const JGL_BUFFER *b, ULONG high);
/* The memory position of RGBA component n: BGRA exchanges red and blue. */
static GLint BgraComponent(const JGL_ARRAY *a, GLint n) {
    return a->Bgra && !(n & 1) ? 2 - n : n;
}
/* One array's element `index` as up to four floats: FLOAT components keep
 * their bits, other types convert (normalized as given); missing components
 * keep the caller's defaults. BGRA exchanges red and blue. */
static inline void ElementValues(const JGL_ARRAY *a, ULONG index, ULONG normalized,
                                 GLfloat *values) {
    const ULONG unit = TypeBytes(a->Type);
    const ULONG stride = a->Stride ? (ULONG)a->Stride : unit * a->Size;
    const BYTE *p = (const BYTE *)a->Pointer + (ULONG_PTR)index * stride;
    if (a->Type == GL_FLOAT) {
        switch (a->Size) {
            case 4:
                JGL_LOAD(values, p, 16);
                return;
            case 3:
                JGL_LOAD(values, p, 12);
                return;
            case 2:
                JGL_LOAD(values, p, 8);
                return;
            default:
                JGL_LOAD(values, p, 4);
                return;
        }
    }
    for (GLint n = 0; n < a->Size; ++n)
        values[n] = Component(p + BgraComponent(a, n) * unit, a->Type, normalized);
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
    /* The enabled conventional slots and generic attributes, found once. */
    ULONG enabled = 0, generics = 0, buffered = 0;
    for (ULONG i = 0; i < JGL_ARRAY_SLOTS; ++i)
        if (state->Attribute[i].Enabled) {
            enabled |= 1U << i;
            buffered |= state->Attribute[i].Buffer;
        }
    for (ULONG i = 0; i < DG_GL_MAX_VERTEX_ATTRIBS; ++i)
        if (state->Generic[i].Enabled) {
            generics |= 1U << i;
            buffered |= state->Generic[i].Buffer;
        }
    /* Buffer arrays (ARB_vertex_buffer_object) read their objects' copies,
     * which must be unmapped. A copy not holding this element is never read:
     * as in a draw (LeaveUnread), its array is disabled for the element and
     * its current value applies. The shared lock keeps the copies alive until
     * every value is snapshotted. Without a buffer array (always without the
     * capability) the arrays are read in place. */
    const JGL_ARRAY *attribute = state->Attribute, *generic_arrays = state->Generic;
    if (buffered && !JglVertexBufferObjectAvailable())
        buffered = 0;
    JGL_ARRAY copies[JGL_ARRAY_SLOTS + DG_GL_MAX_VERTEX_ATTRIBS];
    if (buffered) {
        CopyMemory(copies, state->Attribute, sizeof(state->Attribute));
        CopyMemory(copies + JGL_ARRAY_SLOTS, state->Generic, sizeof(state->Generic));
        JglLockShared();
        for (ULONG i = 0; i < JGL_ARRAY_SLOTS + DG_GL_MAX_VERTEX_ATTRIBS; ++i) {
            JGL_ARRAY *a = &copies[i];
            if (!a->Enabled || !a->Buffer)
                continue;
            const JGL_BUFFER *b = JglBufferObject(a->Buffer, a->Serial);
            if (!b || b->Mapped) {
                JglUnlockShared();
                JglCommandError(GL_INVALID_OPERATION);
                return;
            }
            if (!BufferRange(a, b, (ULONG)index)) {
                if (i < JGL_ARRAY_SLOTS)
                    enabled &= ~(1U << i);
                else
                    generics &= ~(1U << (i - JGL_ARRAY_SLOTS));
                continue;
            }
            a->Pointer = b->Data + (ULONG_PTR)a->Pointer;
        }
        attribute = copies;
        generic_arrays = copies + JGL_ARRAY_SLOTS;
    }
    // Validate and snapshot every enabled client range before any scalar can
    // flush the transport. An invalid later range must not partially set state.
    for (ULONG i = 0; i < JGL_ARRAY_SLOTS + DG_GL_MAX_VERTEX_ATTRIBS; ++i) {
        if (!(i < JGL_ARRAY_SLOTS ? (enabled >> i) & 1 : (generics >> (i - JGL_ARRAY_SLOTS)) & 1))
            continue;
        if (!ArrayRange(i < JGL_ARRAY_SLOTS ? &attribute[i] : &generic_arrays[i - JGL_ARRAY_SLOTS],
                        (ULONG)index)) {
            if (buffered)
                JglUnlockShared();
            JglCommandError(GL_INVALID_VALUE);
            return;
        }
    }
    /* Each record's function and argument words, in submission order. */
    struct Call {
        ULONG Function, Words, Arguments[5];
    } calls[JGL_ARRAY_SLOTS + DG_GL_MAX_VERTEX_ATTRIBS];
    ULONG count = 0;
    GLfloat values[4];
    /* Conventional attributes: COLOR, NORMAL, TEXTURE_COORD (unit 0) and
     * SECONDARY_COLOR, each normalized but the coordinates. */
    static const ULONG functions[5] = {FEnum_glVertex4f, FEnum_glColor4f, FEnum_glNormal3f,
                                       FEnum_glTexCoord4f, FEnum_glSecondaryColor3f};
    for (ULONG i = 1; i <= 4; ++i) {
        if (!((enabled >> i) & 1))
            continue;
        values[0] = values[1] = values[2] = 0;
        values[3] = 1.0f;
        ElementValues(&attribute[i], (ULONG)index, i != 3, values);
        Call &call = calls[count++];
        call.Function = functions[i];
        call.Words = i == 2 || i == 4 ? 3 : 4;
        JGL_LOAD(call.Arguments, values, 16);
    }
    /* Generic attributes (ARB_vertex_program) normalize only when asked,
     * like VertexAttrib4N*vARB; missing components are (0, 0, 0, 1). */
    for (ULONG i = 1; i < DG_GL_MAX_VERTEX_ATTRIBS; ++i) {
        if (!((generics >> i) & 1))
            continue;
        const JGL_ARRAY *a = &generic_arrays[i];
        values[0] = values[1] = values[2] = 0;
        values[3] = 1.0f;
        ElementValues(a, (ULONG)index, a->Normalized, values);
        Call &call = calls[count++];
        call.Function = FEnum_glVertexAttrib4fARB;
        call.Words = 5;
        call.Arguments[0] = i;
        JGL_LOAD(call.Arguments + 1, values, 16);
    }
    // Units 1-7 (slots 7-13) use the canonical target-first record.
    for (ULONG i = 7; i < JGL_FOG_SLOT; ++i) {
        if (!((enabled >> i) & 1))
            continue;
        values[0] = values[1] = values[2] = 0;
        values[3] = 1.0f;
        ElementValues(&attribute[i], (ULONG)index, 0, values);
        Call &call = calls[count++];
        call.Function = FEnum_glMultiTexCoord4f;
        call.Words = 5;
        call.Arguments[0] = JGL_TEXTURE0 + i - 6;
        JGL_LOAD(call.Arguments + 1, values, 16);
    }
    /* A FLOAT fog coordinate keeps its bits; a DOUBLE converts like the
     * other attributes. */
    if ((enabled >> JGL_FOG_SLOT) & 1) {
        const JGL_ARRAY *a = &attribute[JGL_FOG_SLOT];
        const BYTE *p =
            (const BYTE *)a->Pointer +
            (ULONG_PTR)(ULONG)index * (a->Stride ? (ULONG)a->Stride : TypeBytes(a->Type));
        Call &call = calls[count++];
        call.Function = FEnum_glFogCoordf;
        call.Words = 1;
        if (a->Type == GL_FLOAT) {
            JGL_LOAD(call.Arguments, p, 4);
        } else {
            const GLfloat value = Component(p, a->Type, 0);
            JGL_LOAD(call.Arguments, &value, 4);
        }
    }
    if ((enabled >> 5) & 1) {
        const JGL_ARRAY *a = &attribute[5];
        const GLdouble color_index = IndexComponent(
            (const BYTE *)a->Pointer +
                (ULONG_PTR)(ULONG)index * (a->Stride ? (ULONG)a->Stride : TypeBytes(a->Type)),
            a->Type);
        Call &call = calls[count++];
        call.Function = FEnum_glIndexd;
        call.Words = 2;
        JGL_LOAD(call.Arguments, &color_index, 8);
    }
    if ((enabled >> 6) & 1) {
        const JGL_ARRAY *a = &attribute[6];
        Call &call = calls[count++];
        call.Function = FEnum_glEdgeFlag;
        call.Words = 1;
        call.Arguments[0] = *((const BYTE *)a->Pointer +
                              (ULONG_PTR)(ULONG)index * (a->Stride ? (ULONG)a->Stride : 1)) != 0;
    }
    // Vertex last: the preceding setters establish this vertex's attributes.
    // Generic attribute 0 is the position and supersedes the vertex array.
    if (generics & 1) {
        values[0] = values[1] = values[2] = 0;
        values[3] = 1.0f;
        ElementValues(&generic_arrays[0], (ULONG)index, generic_arrays[0].Normalized, values);
        Call &call = calls[count++];
        call.Function = FEnum_glVertexAttrib4fARB;
        call.Words = 5;
        call.Arguments[0] = 0;
        JGL_LOAD(call.Arguments + 1, values, 16);
    } else if (enabled & 1) {
        values[0] = values[1] = values[2] = 0;
        values[3] = 1.0f;
        ElementValues(&attribute[0], (ULONG)index, 0, values);
        Call &call = calls[count++];
        call.Function = FEnum_glVertex4f;
        call.Words = 4;
        JGL_LOAD(call.Arguments, values, 16);
    }
    if (buffered)
        JglUnlockShared();
    for (ULONG i = 0; i < count; ++i)
        JglScalarVector(calls[i].Function, calls[i].Words, calls[i].Arguments);
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
    JGL_ARRAY next[JGL_ARRAY_SLOTS];
    CopyMemory(next, state->Attribute, sizeof(next));
    const GLint sizes[4] = {layout.vertex, layout.color, layout.normal, layout.texture};
    const ULONG offsets[4] = {layout.vertex_offset, layout.color_offset, layout.normal_offset, 0};
    // GL1.3: the coordinate set belongs to the client-active unit only.
    const ULONG slots[4] = {0, 1, 2, TextureSlot(state)};
    for (ULONG i = 0; i < 4; ++i) {
        JGL_ARRAY &a = next[slots[i]];
        a.Enabled = sizes[i] != 0;
        // Disabled descriptors retain their original pointer/type/stride.
        if (!a.Enabled)
            continue;
        a.Size = sizes[i];
        a.Bgra = FALSE;
        a.Type = i == 1 ? layout.color_type : GL_FLOAT;
        a.Stride = stride ? stride : layout.bytes;
        a.Pointer = (const void *)((ULONG_PTR)pointer + offsets[i]);
        a.Buffer = state->ArrayBuffer.Name;
        a.Serial = state->ArrayBuffer.Serial;
    }
    next[5].Enabled = next[6].Enabled = FALSE;
    // Secondary color and fog coordinates are independent EXT arrays, unchanged
    // by this GL1.1 call; enabled, they still accompany the following draws.
    CopyMemory(state->Attribute, next, sizeof(next));
    state->EnabledArrays = JglCountEnabledArrays(state);
}

// GL1.1 Table2.6 signed normalization differs from modern SNORM zero.
// Preserve that existing arithmetic in the cold signed-normalized path while
// still writing directly to packet storage. Game FLOAT/UBYTE paths stay raw.
static GLenum ArrayWireType(ULONG index, const JGL_ARRAY *a) {
    if ((index == 1 || index == 2 || index == 4) &&
        (a->Type == GL_BYTE || a->Type == GL_SHORT || a->Type == GL_INT))
        return GL_FLOAT;
    return a->Type;
}
/* Components of a client section: a BGRA secondary colour travels as the RGB
 * its slot admits, every other array as its size. */
static GLint ArrayWireSize(ULONG index, const JGL_ARRAY *a) {
    return a->Bgra && index == 4 ? 3 : a->Size;
}
/* Seven descriptors (DrawArrays) unless a unit 1-7 or the fog coordinate
 * array is enabled; then sixteen (DrawArraysEXT), with reserved slot 15 zero.
 * Any generic attribute array selects the attribute draw: thirty-two, the
 * sixteen DrawArraysEXT slots then generic attributes 0-15. Arrays are the
 * descriptors in that slot order (slot 15 never has one). */
struct RawCapture {
    const JGL_ARRAY *const *arrays;
    const BYTE *indices;
    ULONG index_size, first, at, count, mask, generic, slots;
    unsigned bytes;
    BOOL fan;
    unsigned descriptors[DG_GL_ATTRIB_SLOTS], offsets[DG_GL_ATTRIB_SLOTS];
};
/* The enabled array of a descriptor slot, or NULL. */
static const JGL_ARRAY *CaptureSlot(const RawCapture *c, ULONG slot) {
    if (slot >= DG_GL_ARRAY_EXT_SLOTS)
        return c->generic & (1U << (slot - DG_GL_ARRAY_EXT_SLOTS)) ? c->arrays[slot] : NULL;
    return slot < JGL_ARRAY_SLOTS && (c->mask & (1U << slot)) ? c->arrays[slot] : NULL;
}
static BOOL CaptureLayout(RawCapture *c, ULONG count) {
    return c->slots == DG_GL_ATTRIB_SLOTS ? DgRawAttribLayout(c->mask, c->generic, count,
                                                              c->descriptors, c->offsets, &c->bytes)
                                          : DgRawArrayLayoutSlots(c->mask, count, c->descriptors,
                                                                  c->offsets, &c->bytes, c->slots);
}
/* CaptureSlot's slots with an array as a mask, so that draws visit only
 * those, in the same ascending order, instead of all c->slots. */
static ULONG CaptureMask(const RawCapture *c) {
    const ULONG conventional = c->slots < JGL_ARRAY_SLOTS ? c->slots : JGL_ARRAY_SLOTS;
    ULONG slots = c->mask & ((1UL << conventional) - 1);
    if (c->slots > DG_GL_ARRAY_EXT_SLOTS)
        slots |= (c->generic & DG_GL_ATTRIB_MASK) << DG_GL_ARRAY_EXT_SLOTS;
    return slots;
}
/* Each captured slot's section at its offset, zero gaps from `end` on. */
static ULONG CaptureSections(const RawCapture *c, BYTE *out, ULONG end) {
    for (ULONG slots = CaptureMask(c); slots; slots &= slots - 1) {
        const ULONG i = (ULONG)__builtin_ctz(slots);
        const JGL_ARRAY *a = c->arrays[i];
        if (!a)
            continue;
        ULONG unit = TypeBytes(a->Type);
        ULONG input_bytes = unit * a->Size;
        ULONG bytes = TypeBytes(ArrayWireType(i, a)) * ArrayWireSize(i, a);
        ULONG stride = a->Stride ? (ULONG)a->Stride : input_bytes;
        BYTE *destination = out + c->offsets[i];
        if (c->offsets[i] != end)
            ZeroMemory(out + end, c->offsets[i] - end);
        if (a->Bgra && !c->indices && !c->fan) {
            /* BGRA bytes become the section's R, G, B (and A); a range of
             * vertices (a buffer draw's client section) walks its source. */
            const BYTE *source = (const BYTE *)a->Pointer + (ULONG_PTR)(c->first + c->at) * stride;
            BYTE *vertex = destination;
            if (bytes == 4) {
                for (ULONG v = 0; v < c->count; ++v, source += stride, vertex += 4) {
                    vertex[0] = source[2];
                    vertex[1] = source[1];
                    vertex[2] = source[0];
                    vertex[3] = source[3];
                }
            } else {
                for (ULONG v = 0; v < c->count; ++v, source += stride, vertex += 3) {
                    vertex[0] = source[2];
                    vertex[1] = source[1];
                    vertex[2] = source[0];
                }
            }
        } else if (a->Bgra) {
            /* BGRA bytes become the section's R, G, B (and A). */
            for (ULONG v = 0; v < c->count; ++v) {
                ULONG offset = c->fan ? (v == 0   ? 0
                                         : v == 1 ? c->at - 1
                                                  : c->at + v - 2)
                                      : c->at + v;
                ULONG index = Index(c->indices, c->index_size, c->first, offset);
                const BYTE *source = (const BYTE *)a->Pointer + (ULONG_PTR)index * stride;
                BYTE *vertex = destination + v * bytes;
                vertex[0] = source[2];
                vertex[1] = source[1];
                vertex[2] = source[0];
                if (bytes == 4)
                    vertex[3] = source[3];
            }
        } else if (ArrayWireType(i, a) != a->Type) {
            for (ULONG v = 0; v < c->count; ++v) {
                ULONG offset = c->fan ? (v == 0   ? 0
                                         : v == 1 ? c->at - 1
                                                  : c->at + v - 2)
                                      : c->at + v;
                ULONG index = Index(c->indices, c->index_size, c->first, offset);
                const BYTE *source = (const BYTE *)a->Pointer + (ULONG_PTR)index * stride;
                for (GLint component = 0; component < a->Size; ++component) {
                    GLfloat value = Component(source + component * unit, a->Type, 1);
                    CopyMemory(destination + v * bytes + component * 4, &value, 4);
                }
            }
        } else if (!c->indices && !c->fan && stride == bytes) {
            CopyMemory(destination,
                       (const BYTE *)a->Pointer + (ULONG_PTR)(c->first + c->at) * stride,
                       c->count * bytes);
        } else if (!c->indices && !c->fan) {
            /* A strided range (interleaved or padded vertices). */
            const BYTE *source = (const BYTE *)a->Pointer + (ULONG_PTR)(c->first + c->at) * stride;
            for (ULONG v = 0; v < c->count; ++v)
                CopyElement(destination + v * bytes, source + (ULONG_PTR)v * stride, bytes);
        } else {
            for (ULONG v = 0; v < c->count; ++v) {
                ULONG offset = c->fan ? (v == 0   ? 0
                                         : v == 1 ? c->at - 1
                                                  : c->at + v - 2)
                                      : c->at + v;
                ULONG index = Index(c->indices, c->index_size, c->first, offset);
                CopyElement(destination + v * bytes,
                            (const BYTE *)a->Pointer + (ULONG_PTR)index * stride, bytes);
            }
        }
        end = c->offsets[i] + c->count * bytes;
    }
    return end;
}
static void CaptureVertices(void *opaque, BYTE *out) {
    const RawCapture *c = (const RawCapture *)opaque;
    CopyMemory(out, c->descriptors, c->slots * 4);
    ULONG end = CaptureSections(c, out, c->slots * 4);
    if (end != c->bytes)
        ZeroMemory(out + end, c->bytes - end);
}
/* Client arrays (or buffer arrays resolved into their objects' copies):
 * every index or vertex expands into packed vertices, split to the packet.
 * A caller that has scanned these same indices passes their largest as
 * `scanned`, which the draw then does not scan again. */
static void ClientDraw(GLenum mode, GLint first, GLsizei count, ULONG index_size,
                       const BYTE *indices, BOOL elements, const JGL_ARRAY *const *arrays,
                       ULONG generic, const ULONG *scanned = NULL) {
    ULONG i, max_index = 0, capacity, at = 0, mask = 0, vertex_bytes;
    if (elements &&
        (!indices || (ULONG)count * (unsigned long long)index_size > ~(ULONG_PTR)0 ||
         (ULONG_PTR)indices > ~(ULONG_PTR)0 - (ULONG)count * (unsigned long long)index_size)) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    if (elements && scanned) {
        max_index = *scanned;
    } else if (elements) {
        ULONG low;
        IndexBounds(indices, index_size, (ULONG)count, &low, &max_index);
    } else {
        unsigned long long end = (unsigned long long)(ULONG)first + (ULONG)count - 1;
        if (end > 0xffffffffUL) {
            JglCommandError(GL_INVALID_VALUE);
            return;
        }
        max_index = (ULONG)end;
    }
    /* Validate all ranges before emitting any piece of the draw. Caller
     * address validity itself remains the application's GL responsibility. */
    for (i = 0; i < DG_GL_ATTRIB_SLOTS; ++i) {
        const JGL_ARRAY *a = arrays[i];
        if (!a)
            continue;
        if (!ArrayRange(a, max_index)) {
            JglCommandError(GL_INVALID_VALUE);
            return;
        }
        if (i < JGL_ARRAY_SLOTS)
            mask |= 1U << i;
    }
    RawCapture capture = {};
    capture.arrays = arrays;
    capture.indices = indices;
    capture.index_size = index_size;
    capture.first = (ULONG)first;
    capture.mask = mask | DG_GL_ARRAY_RAW;
    capture.generic = generic;
    // Unit 0 alone keeps the legacy record that every host admits. Elements
    // and ranges expand into these same packed vertices, fog included.
    capture.slots = generic                      ? DG_GL_ATTRIB_SLOTS
                    : (mask & ~DG_GL_ARRAY_MASK) ? DG_GL_ARRAY_EXT_SLOTS
                                                 : 7;
    const ULONG function = capture.slots == DG_GL_ATTRIB_SLOTS ? FEnum_glVertexAttribPointerARB
                           : capture.slots == 7                ? FEnum_glDrawArrays
                                                               : FEnum_glDrawArraysEXT;
    const ULONG words = function == FEnum_glVertexAttribPointerARB ? 5 : 4;
    vertex_bytes = 0;
    for (i = 0; i < capture.slots; ++i) {
        const JGL_ARRAY *a = CaptureSlot(&capture, i);
        if (!a)
            continue;
        const GLenum type = ArrayWireType(i, a);
        capture.descriptors[i] = type | ((ULONG)ArrayWireSize(i, a) << 16);
        /* GL ignores normalization of floating-point generic arrays. */
        if (i >= DG_GL_ARRAY_EXT_SLOTS && a->Normalized && type != GL_FLOAT && type != GL_DOUBLE)
            capture.descriptors[i] |= DG_GL_ATTRIB_NORMALIZED;
        vertex_bytes += TypeBytes(type) * ArrayWireSize(i, a);
    }
    ULONG limit = JglMaxDataBytes(words);
    capacity = limit > capture.slots * 4 ? (limit - capture.slots * 4) / vertex_bytes : 0;
    if (capacity > DG_GL_MAX_VERTICES)
        capacity = DG_GL_MAX_VERTICES;
    // At most alignment padding needs trimming; no payload memory is touched.
    while (capacity && (!CaptureLayout(&capture, capacity) || capture.bytes > limit))
        --capacity;
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
        ULONG n = (ULONG)count - at, args[5] = {mode, 0, 0, mask | DG_GL_ARRAY_RAW, generic};
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
        capture.at = at;
        capture.count = n;
        capture.fan = fan;
        CaptureLayout(&capture, n);
        if (!JglCaptureData(function, args, words, capture.bytes, CaptureVertices, &capture))
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
/* The buffer draw (DG_CAP_GL_VERTEX_BUFFER_OBJECT): the attribute draw's
 * descriptors, a reference per buffer slot, client sections of vertices
 * base..base+vertices-1 and any inline indices. */
struct BufferCapture {
    RawCapture raw;
    ULONG args[10], reference_words;
    /* The shared layout's types (ULONG is unsigned long on Windows). */
    unsigned references[3 * DG_GL_ATTRIB_SLOTS], index_offset;
    const BYTE *indices;
};
/* Inline indices as scanned: each is held within low..high while it is
 * copied, so client memory changing during the call (which GL leaves
 * undefined) can never put an index outside the client vertices, which the
 * host would reject with the whole batch. */
static void CopyIndices(BYTE *out, const BYTE *indices, ULONG size, ULONG count, ULONG low,
                        ULONG high) {
#define JGL_CLAMP(T)                                                                               \
    for (ULONG i = 0; i < count; ++i) {                                                            \
        T v;                                                                                       \
        JGL_LOAD(&v, indices + i * sizeof(T), sizeof(T));                                          \
        v = (ULONG)v < low ? (T)low : (ULONG)v > high ? (T)high : v;                               \
        JGL_LOAD(out + i * sizeof(T), &v, sizeof(T));                                              \
    }
    if (size == 1) {
        JGL_CLAMP(GLubyte)
    } else if (size == 2) {
        JGL_CLAMP(GLushort)
    } else {
        JGL_CLAMP(GLuint)
    }
#undef JGL_CLAMP
}
static void CaptureBuffer(void *opaque, BYTE *out) {
    const BufferCapture *b = (const BufferCapture *)opaque;
    const RawCapture *c = &b->raw;
    CopyMemory(out, c->descriptors, DG_GL_ATTRIB_DESCRIPTOR_BYTES);
    CopyMemory(out + DG_GL_ATTRIB_DESCRIPTOR_BYTES, b->references, b->reference_words * 4);
    ULONG end = CaptureSections(c, out, DG_GL_ATTRIB_DESCRIPTOR_BYTES + b->reference_words * 4);
    if (b->indices) {
        const ULONG bytes = b->args[2] * c->index_size;
        if (b->index_offset != end)
            ZeroMemory(out + end, b->index_offset - end);
        if (c->count)
            CopyIndices(out + b->index_offset, b->indices, c->index_size, b->args[2], c->first,
                        c->first + c->count - 1);
        else
            CopyMemory(out + b->index_offset, b->indices, bytes);
        end = b->index_offset + bytes;
    }
    if (end != c->bytes)
        ZeroMemory(out + end, c->bytes - end);
}
/* A buffer-sourced array's contents up to vertex `high` lie within its
 * object. The offset is any pointer value: it is never added before it is
 * known to lie within the object (high * stride + element < 2^63). */
static BOOL BufferRange(const JGL_ARRAY *a, const JGL_BUFFER *b, ULONG high) {
    const unsigned long long element = (unsigned long long)TypeBytes(a->Type) * a->Size;
    const unsigned long long stride = a->Stride ? (ULONG)a->Stride : element;
    const ULONG_PTR offset = (ULONG_PTR)a->Pointer;
    return offset <= b->Size && high * stride + element <= b->Size - offset;
}
/* Leaves out of a draw the buffer slots whose object does not hold vertex
 * `high`, as the host's buffer draw does (gl.h): such an array is never read,
 * so the draw treats it as disabled and its current value applies. GL leaves
 * those reads undefined and drivers fetch only the arrays the vertex stage
 * consumes; Doom 3's ARB2 path leaves a texture coordinate array enabled on a
 * small, stale buffer. Returns whether a position or generic attribute 0 is
 * left to provoke vertices (without one the draw draws nothing, no error). */
static BOOL LeaveUnread(const JGL_ARRAY **drawn, JGL_BUFFER *const *objects, ULONG *buffered,
                        ULONG *generic, ULONG high) {
    for (ULONG slots = *buffered; slots; slots &= slots - 1) {
        const ULONG i = (ULONG)__builtin_ctz(slots);
        if (BufferRange(drawn[i], objects[i], high))
            continue;
        *buffered &= ~(1U << i);
        drawn[i] = NULL;
        if (i >= DG_GL_ARRAY_EXT_SLOTS)
            *generic &= ~(1U << (i - DG_GL_ARRAY_EXT_SLOTS));
    }
    return drawn[0] || (*generic & 1);
}
/* A buffer draw's capture before its arrays are described: no array, no
 * client section, no inline index and every descriptor zero. Offsets are
 * written by BufferLayout for every client section, the only ones
 * CaptureSections reads, and references up to reference_words; the rest of
 * the capture (over 700 bytes) is never read, so it is not cleared. */
static void ResetBufferCapture(BufferCapture *b) {
    RawCapture *c = &b->raw;
    c->arrays = NULL;
    c->indices = NULL;
    c->index_size = c->first = c->at = c->count = c->mask = c->generic = c->slots = 0;
    c->bytes = 0;
    c->fan = FALSE;
    __builtin_memset(c->descriptors, 0, sizeof(c->descriptors));
    __builtin_memset(b->args, 0, sizeof(b->args));
    b->reference_words = b->index_offset = 0;
    b->indices = NULL;
}
/* DgRawBufferLayoutOf (gl-arrays.h: the NT validator's admission of the
 * buffer draw) visiting only the enabled slots instead of all thirty-two
 * twice: for descriptors that are zero outside the enabled slots, as the
 * draws here build them, it returns the same result, index offset, length
 * and client section offsets. Offsets of other slots are left unwritten, as
 * CaptureSections never reads them. tests/guest/opengl/vertex-buffer-object.cpp
 * compares the two. */
static int BufferLayout(const unsigned *a, const unsigned *descriptors, const unsigned *references,
                        unsigned *offsets, unsigned *indices, unsigned *length, int bgra) {
    const unsigned mask = a[3], generic = a[4], buffered = a[5], vertices = a[6];
    const unsigned enabled = (mask & DG_GL_ARRAY_EXT_MASK) | (generic << DG_GL_ARRAY_EXT_SLOTS);
    const unsigned client = enabled & ~buffered, index = DgBufferIndexBytes(a[7]);
    unsigned at = DG_GL_ATTRIB_DESCRIPTOR_BYTES, n = 0;
    *indices = 0;
    if (a[0] > 9 || mask != ((mask & DG_GL_ARRAY_EXT_MASK) | DG_GL_ARRAY_RAW) ||
        (generic & ~(unsigned)DG_GL_ATTRIB_MASK) ||
        !((mask & DG_GL_ARRAY_POSITION) || (generic & 1)) || (buffered & ~enabled) ||
        index == (unsigned)-1 || a[2] > DG_GL_MAX_BUFFER_ELEMENTS)
        return 0;
    if (!index) {
        if (a[8] || a[9] || vertices != (client ? a[2] : 0) || vertices > DG_GL_MAX_VERTICES ||
            (a[2] && a[1] > 0xffffffffU - (a[2] - 1)))
            return 0;
    } else if (client ? !vertices || vertices > DG_GL_MAX_VERTICES ||
                            a[1] > 0xffffffffU - (vertices - 1)
                      : vertices || a[1]) {
        return 0;
    } else if (a[8] ? a[9] & (index - 1) : a[9]) {
        return 0;
    }
    for (unsigned slots = enabled; slots; slots &= slots - 1) {
        const unsigned slot = (unsigned)__builtin_ctz(slots), d = descriptors[slot];
        if (!(slot >= DG_GL_ARRAY_EXT_SLOTS ? DgRawAttribDescriptor(d) : DgRawArraySlot(slot, d)) &&
            !(bgra && ((buffered >> slot) & 1) && DgRawBgraDescriptor(slot, d)))
            return 0;
        if ((buffered >> slot) & 1) {
            const unsigned unit = DgArrayComponentBytes(d & 65535), *r = references + 3 * n++;
            if (!r[0] || r[2] > DG_GL_MAX_BUFFER_STRIDE || ((r[1] | r[2]) & (unit - 1)))
                return 0;
        }
    }
    at += n * DG_GL_BUFFER_REFERENCE_BYTES;
    for (unsigned slots = client; slots; slots &= slots - 1) {
        const unsigned slot = (unsigned)__builtin_ctz(slots), d = descriptors[slot];
        const unsigned unit = DgArrayComponentBytes(d & 65535);
        at = (at + unit - 1) & ~(unit - 1);
        offsets[slot] = at;
        at += vertices * ((d >> 16) & 255) * unit;
    }
    if (index && !a[8]) {
        at = (at + index - 1) & ~(index - 1);
        *indices = at;
        if (a[2] > DG_GL_MAX_INDICES)
            return 0;
        at += a[2] * index;
    }
    *length = (at + 3) & ~3U;
    return 1;
}
/* Draws reading buffer objects: every object exists and is unmapped (else
 * GL_INVALID_OPERATION), and buffer arrays too short for the drawn vertices
 * are left unread (LeaveUnread). The buffer draw carries references and only
 * the client arrays' bytes. Display list compilation, which dereferences
 * buffers at compile time, and anything the record cannot express resolve
 * buffer arrays into their objects' copies and draw them as client arrays.
 * `present` holds the slots with an array (Draw's enabled arrays); every
 * pass visits only its bits. */
static void BufferDraw(GLenum mode, GLint first, GLsizei count, ULONG index_size,
                       const void *index_pointer, BOOL elements, const JGL_ARRAY *const *arrays,
                       ULONG present, ULONG generic, ULONG buffered) {
    JGL_ARRAY_STATE *state = JglArrays();
    const JGL_BUFFER_BINDING element = elements ? state->ElementBuffer : JGL_BUFFER_BINDING{};
    /* Read only at buffer slots, each written first. */
    JGL_BUFFER *objects[DG_GL_ATTRIB_SLOTS], *index_object = NULL;
    JGL_ARRAY resolved[DG_GL_ATTRIB_SLOTS];
    /* The arrays the draw reads: unread buffer slots are left out. */
    const JGL_ARRAY *drawn[DG_GL_ATTRIB_SLOTS], *sources[DG_GL_ATTRIB_SLOTS];
    const BYTE *indices = (const BYTE *)index_pointer;
    const ULONG_PTR index_offset = (ULONG_PTR)index_pointer;
    BOOL record = !JglCompiling() && (ULONG)count <= DG_GL_MAX_BUFFER_ELEMENTS;
    ULONG low = 0, high = 0, client = present & ~buffered, i, slots;
    BOOL ranged = FALSE, scanned = FALSE;
    /* Inline indices are client memory, checked as in a client draw. */
    if (elements && !element.Name &&
        (!indices || (ULONG)count * (unsigned long long)index_size > ~(ULONG_PTR)0 ||
         (ULONG_PTR)indices > ~(ULONG_PTR)0 - (ULONG)count * (unsigned long long)index_size)) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    JglLockShared();
    JGL_LOAD(drawn, arrays, sizeof(drawn));
    for (slots = buffered; slots; slots &= slots - 1) {
        i = (ULONG)__builtin_ctz(slots);
        objects[i] = JglBufferObject(arrays[i]->Buffer, arrays[i]->Serial);
        if (!objects[i] || objects[i]->Mapped)
            goto invalid;
    }
    if (element.Name) {
        index_object = JglBufferObject(element.Name, element.Serial);
        if (!index_object || index_object->Mapped || index_offset > index_object->Size ||
            (unsigned long long)(ULONG)count * index_size > index_object->Size - index_offset)
            goto invalid;
        indices = index_object->Data + index_offset;
        record = record && !(index_offset % index_size);
    }
    /* The vertex range: always known for array draws; indexed draws scan
     * their indices only when client arrays need it (else the host bounds
     * them) or when the buffers resolve locally. */
    if (!elements) {
        if ((unsigned long long)(ULONG)first + (ULONG)count - 1 > 0xffffffffUL)
            goto value;
        low = (ULONG)first;
        high = (ULONG)first + (ULONG)count - 1;
    }
    /* Client sections hold at most DG_GL_MAX_VERTICES; inline indices travel
     * in one packet. */
    if ((!elements && client && (ULONG)count > DG_GL_MAX_VERTICES) ||
        (elements && !element.Name && (ULONG)count > DG_GL_MAX_INDICES))
        record = FALSE;
    if (elements && (client || !record)) {
        IndexBounds(indices, index_size, (ULONG)count, &low, &high);
        scanned = TRUE;
        if (client && high - low >= DG_GL_MAX_VERTICES)
            record = FALSE;
    }
    /* Client arrays hold every drawn vertex, as in a client draw (a client
     * array means high is known). */
    for (slots = client; slots; slots &= slots - 1)
        if (!ArrayRange(arrays[__builtin_ctz(slots)], high))
            goto value;
    /* Without a scan the host bounds the record's reads (and leaves its
     * unread slots out); a local draw is bounded below before it reads any
     * copy. */
    ranged = !elements || client || !record;
    if (ranged && !LeaveUnread(drawn, objects, &buffered, &generic, high)) {
        JglUnlockShared();
        return;
    }
    /* The record's references: bounded strides, aligned offsets. Without a
     * buffer slot left, an array draw is a client draw. */
    for (slots = buffered; slots && record; slots &= slots - 1) {
        const JGL_ARRAY *a = drawn[__builtin_ctz(slots)];
        const ULONG unit = TypeBytes(a->Type);
        record = (ULONG_PTR)a->Pointer <= 0xffffffffUL && a->Stride <= DG_GL_MAX_BUFFER_STRIDE &&
                 !((ULONG_PTR)a->Pointer % unit) && !((ULONG)a->Stride % unit);
    }
    if (!buffered && !element.Name)
        record = FALSE;
    if (record) {
        BufferCapture b;
        RawCapture *c = &b.raw;
        ResetBufferCapture(&b);
        /* A BGRA buffer slot needs the BGRA buffer draw (ARB_vertex_array_bgra). */
        BOOL bgra = FALSE;
        c->arrays = drawn;
        c->mask = (client & DG_GL_ARRAY_EXT_MASK) | DG_GL_ARRAY_RAW;
        c->generic = client >> DG_GL_ARRAY_EXT_SLOTS;
        c->slots = DG_GL_ATTRIB_SLOTS;
        c->first = low;
        c->count = client ? high - low + 1 : 0;
        c->index_size = index_size;
        ULONG references = 0;
        /* Each drawn slot in order; drawn arrays are client or buffer slots. */
        for (slots = client | buffered; slots; slots &= slots - 1) {
            i = (ULONG)__builtin_ctz(slots);
            const JGL_ARRAY *a = drawn[i];
            if (!a)
                continue;
            const GLenum type = buffered & (1U << i) ? a->Type : ArrayWireType(i, a);
            const GLint size = buffered & (1U << i) ? a->Size : ArrayWireSize(i, a);
            c->descriptors[i] = type | ((ULONG)size << 16);
            if (i >= DG_GL_ARRAY_EXT_SLOTS && a->Normalized && type != GL_FLOAT &&
                type != GL_DOUBLE)
                c->descriptors[i] |= DG_GL_ATTRIB_NORMALIZED;
            if ((buffered & (1U << i)) && a->Bgra) {
                c->descriptors[i] |= DG_GL_ARRAY_BGRA;
                bgra = TRUE;
            }
            if (buffered & (1U << i)) {
                b.references[references++] = a->Buffer;
                b.references[references++] = (unsigned)(ULONG_PTR)a->Pointer;
                b.references[references++] = (unsigned)a->Stride;
            }
        }
        b.reference_words = references;
        b.args[0] = mode;
        b.args[1] = client ? low : elements ? 0 : (ULONG)first;
        b.args[2] = (ULONG)count;
        b.args[3] = ((buffered | client) & DG_GL_ARRAY_EXT_MASK) | DG_GL_ARRAY_RAW;
        b.args[4] = (buffered | client) >> DG_GL_ARRAY_EXT_SLOTS;
        b.args[5] = buffered;
        b.args[6] = c->count;
        b.args[7] = elements ? (index_size == 1   ? GL_UNSIGNED_BYTE
                                : index_size == 2 ? GL_UNSIGNED_SHORT
                                                  : GL_UNSIGNED_INT)
                             : 0;
        b.args[8] = element.Name;
        b.args[9] = element.Name ? (ULONG)index_offset : 0;
        b.indices = elements && !element.Name ? indices : NULL;
        unsigned words[10], length = 0;
        for (i = 0; i < 10; ++i)
            words[i] = b.args[i];
        if (BufferLayout(words, c->descriptors, b.references, c->offsets, &b.index_offset, &length,
                         bgra) &&
            length <= JglMaxDataBytes(10)) {
            c->bytes = length;
            JglUnlockShared();
            JglCaptureData(bgra ? FEnum_glDrawRangeElementsEXT : FEnum_glDrawRangeElements, b.args,
                           10, length, CaptureBuffer, &b);
            return;
        }
    }
    if (!ranged) {
        ULONG ignored;
        IndexBounds(indices, index_size, (ULONG)count, &ignored, &high);
        scanned = TRUE;
        if (!LeaveUnread(drawn, objects, &buffered, &generic, high)) {
            JglUnlockShared();
            return;
        }
    }
    /* Compiled or beyond the record: the copies are client arrays now, kept
     * alive by the shared lock while they are read (no transport path takes
     * it). Every buffer slot left holds vertex high, the largest index when
     * the indices were scanned. */
    JGL_LOAD(sources, drawn, sizeof(sources));
    for (slots = buffered; slots; slots &= slots - 1) {
        i = (ULONG)__builtin_ctz(slots);
        resolved[i] = *drawn[i];
        resolved[i].Pointer = objects[i]->Data + (ULONG_PTR)drawn[i]->Pointer;
        resolved[i].Buffer = 0;
        sources[i] = &resolved[i];
    }
    ClientDraw(mode, first, count, index_size, indices, elements, sources, generic,
               elements && scanned ? &high : NULL);
    JglUnlockShared();
    return;
invalid:
    JglUnlockShared();
    JglCommandError(GL_INVALID_OPERATION);
    return;
value:
    JglUnlockShared();
    JglCommandError(GL_INVALID_VALUE);
}
/* Indexed client-array draws with negotiated buffer draws: the buffer draw
 * without buffer slots carries each enabled array's vertices low..high once,
 * at its wire size, then the indices inline, instead of one packed vertex per
 * index. It is chosen when it is no larger than that expansion and fits one
 * packet; the host draws natively from the client sections and bounds every
 * inline index by them at admission, so only indices scanned here travel.
 * Display list compilation keeps the expansion: lists capture client draws,
 * never the buffer draw. Returns FALSE, having done nothing, when the draw
 * remains ClientDraw's (its GL errors included); once it has scanned the
 * indices it sets *scanned and their largest, *high, for ClientDraw.
 * `present` holds the slots with an array. */
static BOOL RangedDraw(GLenum mode, GLsizei count, ULONG index_size, const BYTE *indices,
                       const JGL_ARRAY *const *arrays, ULONG present, ULONG generic, BOOL *scanned,
                       ULONG *high) {
    ULONG low, vertex_bytes = 0, i, slots;
    if (!JglVertexBufferObjectAvailable() || JglCompiling() || !indices ||
        (ULONG)count > DG_GL_MAX_INDICES ||
        (ULONG_PTR)indices > ~(ULONG_PTR)0 - (ULONG)count * index_size)
        return FALSE;
    IndexBounds(indices, index_size, (ULONG)count, &low, high);
    *scanned = TRUE;
    if (*high - low >= DG_GL_MAX_VERTICES)
        return FALSE;
    const ULONG vertices = *high - low + 1;
    for (slots = present; slots; slots &= slots - 1) {
        i = (ULONG)__builtin_ctz(slots);
        const JGL_ARRAY *a = arrays[i];
        if (!ArrayRange(a, *high))
            return FALSE;
        vertex_bytes += TypeBytes(ArrayWireType(i, a)) * ArrayWireSize(i, a);
    }
    if ((unsigned long long)vertices * vertex_bytes +
            (unsigned long long)(ULONG)count * index_size >
        (unsigned long long)(ULONG)count * vertex_bytes)
        return FALSE;
    BufferCapture b;
    RawCapture *c = &b.raw;
    ResetBufferCapture(&b);
    c->arrays = arrays;
    c->mask = (present & DG_GL_ARRAY_EXT_MASK) | DG_GL_ARRAY_RAW;
    c->generic = generic;
    c->slots = DG_GL_ATTRIB_SLOTS;
    c->first = low;
    c->count = vertices;
    c->index_size = index_size;
    for (slots = present; slots; slots &= slots - 1) {
        i = (ULONG)__builtin_ctz(slots);
        const JGL_ARRAY *a = arrays[i];
        const GLenum type = ArrayWireType(i, a);
        c->descriptors[i] = type | ((ULONG)ArrayWireSize(i, a) << 16);
        /* GL ignores normalization of floating-point generic arrays. */
        if (i >= DG_GL_ARRAY_EXT_SLOTS && a->Normalized && type != GL_FLOAT && type != GL_DOUBLE)
            c->descriptors[i] |= DG_GL_ATTRIB_NORMALIZED;
    }
    b.args[0] = mode;
    b.args[1] = low;
    b.args[2] = (ULONG)count;
    b.args[3] = c->mask;
    b.args[4] = generic;
    b.args[5] = 0;
    b.args[6] = vertices;
    b.args[7] = index_size == 1   ? GL_UNSIGNED_BYTE
                : index_size == 2 ? GL_UNSIGNED_SHORT
                                  : GL_UNSIGNED_INT;
    b.indices = indices;
    unsigned words[10], length = 0;
    for (i = 0; i < 10; ++i)
        words[i] = b.args[i];
    if (!BufferLayout(words, c->descriptors, b.references, c->offsets, &b.index_offset, &length,
                      0) ||
        length > JglMaxDataBytes(10))
        return FALSE;
    c->bytes = length;
    JglCaptureData(FEnum_glDrawRangeElements, b.args, 10, length, CaptureBuffer, &b);
    return TRUE;
}
static void Draw(GLenum mode, GLint first, GLsizei count, GLenum type, const void *index_pointer,
                 BOOL elements) {
    JGL_ARRAY_STATE *state;
    ULONG index_size = 0;
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
    /* The enabled arrays, by their mask: slot 15 never has one. */
    const ULONG present = state->EnabledArrays & ~(1UL << JGL_ARRAY_SLOTS);
    ULONG generic = present >> DG_GL_ARRAY_EXT_SLOTS, buffered = 0;
    const JGL_ARRAY *arrays[DG_GL_ATTRIB_SLOTS] = {};
    for (ULONG slots = present; slots; slots &= slots - 1) {
        const ULONG i = (ULONG)__builtin_ctz(slots);
        const JGL_ARRAY *a = JglSlotArray(state, i);
        arrays[i] = a;
        if (a->Buffer)
            buffered |= 1U << i;
    }
    // The vertex array or generic attribute array 0 provokes drawing.
    if (!(present & 1) && !(generic & 1))
        return;
    BOOL scanned = FALSE;
    ULONG high = 0;
    if (buffered || (elements && state->ElementBuffer.Name))
        BufferDraw(mode, first, count, index_size, index_pointer, elements, arrays, present,
                   generic, buffered);
    else if (!elements || !RangedDraw(mode, count, index_size, (const BYTE *)index_pointer, arrays,
                                      present, generic, &scanned, &high))
        ClientDraw(mode, first, count, index_size, (const BYTE *)index_pointer, elements, arrays,
                   generic, scanned ? &high : NULL);
}
void APIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    Draw(mode, first, count, 0, NULL, FALSE);
}
void APIENTRY glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices) {
    Draw(mode, 0, count, type, indices, TRUE);
}
/* EXT_compiled_vertex_array, offered with the buffer draw that carries
 * indexed client-array draws in bulk. The lock is client-vertex-array state
 * only: draws still read the arrays as they are, which the extension permits
 * (locked contents may be cached, never must be), so a locked draw submits
 * exactly the records of the same draw unlocked. Neither command is compiled
 * into display lists. */
void APIENTRY glLockArraysEXT(GLint first, GLsizei count) {
    if (!JglReady())
        return;
    JGL_ARRAY_STATE *state = JglArrays();
    if (!JglCompiledVertexArrayAvailable() || state->LockCount) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    if (first < 0 || count <= 0) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    state->LockFirst = (ULONG)first;
    state->LockCount = (ULONG)count;
}
void APIENTRY glUnlockArraysEXT(void) {
    if (!JglReady())
        return;
    JGL_ARRAY_STATE *state = JglArrays();
    if (!JglCompiledVertexArrayAvailable() || !state->LockCount) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    state->LockFirst = state->LockCount = 0;
}

} /* extern C */
