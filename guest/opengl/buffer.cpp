/* SPDX-License-Identifier: GPL-2.0-or-later
 * ARB_vertex_buffer_object (DG_CAP_GL_VERTEX_BUFFER_OBJECT): buffer objects in
 * the share group's namespace, their ARRAY_BUFFER/ELEMENT_ARRAY_BUFFER
 * bindings, contents, mapping and queries. Without the capability every entry
 * point fails locally with GL_INVALID_OPERATION and wglGetProcAddress hides
 * them. ARB names only.
 *
 * The guest has no access to host memory, so each object keeps a frontend copy
 * of its contents that every record keeps equal to the host's: BufferData and
 * BufferSubData store into it and submit the same bytes (split into bounded
 * BufferSubData slabs), MapBuffer returns it and UnmapBuffer submits it all
 * again unless the map was READ_ONLY. Reads (GetBufferSubData, READ maps,
 * ArrayElement and display list compilation) never wait for the host.
 * Bindings and mappings are frontend state; the host holds neither. Buffer
 * commands execute immediately, never compiled into display lists.
 */
#include "internal.h"
#include "buffers.h"

extern "C" {

#define JGL_BUFFER_SIZE 0x8764
#define JGL_BUFFER_USAGE 0x8765
#define JGL_READ_ONLY 0x88b8
#define JGL_WRITE_ONLY 0x88b9
#define JGL_READ_WRITE 0x88ba
#define JGL_BUFFER_ACCESS 0x88bb
#define JGL_BUFFER_MAPPED 0x88bc
#define JGL_BUFFER_MAP_POINTER 0x88bd
#define JGL_STATIC_DRAW 0x88e4
/* Mapped pointers keep this alignment (WineD3D requires sixteen). */
#define JGL_BUFFER_ALIGNMENT 16
static void RemoveBuffer(JGL_BUFFERS *table, JGL_BUFFER_ENTRY *entry) {
    ULONG hole = (ULONG)(entry - table->Entries), at = hole;
    entry->Name = 0;
    entry->Object = NULL;
    --table->Count;
    for (;;) {
        at = (at + 1) & (JGL_BUFFER_SLOTS - 1);
        JGL_BUFFER_ENTRY *e = &table->Entries[at];
        if (!e->Name)
            return;
        const ULONG origin = JglBufferSlot(e->Name);
        if (((hole - origin) & (JGL_BUFFER_SLOTS - 1)) < ((at - origin) & (JGL_BUFFER_SLOTS - 1))) {
            table->Entries[hole] = *e;
            e->Name = 0;
            e->Object = NULL;
            hole = at;
        }
    }
}
static void FreeBuffers(void *opaque);
/* The current share group's table, created on first use (under the lock). */
static JGL_BUFFERS *BufferTable(BOOL create) {
    void **slot = JglSharedBuffers(FreeBuffers);
    if (!slot)
        return NULL;
    if (!*slot && create) {
        JGL_BUFFERS *table = (JGL_BUFFERS *)JglAllocate(sizeof(JGL_BUFFERS));
        if (table) {
            ZeroMemory(table, sizeof(*table));
            table->Next = 1;
        }
        *slot = table;
    }
    return (JGL_BUFFERS *)*slot;
}
/* Contents storage of at least one byte at the mapping alignment. */
static BOOL StoreContents(JGL_BUFFER *b, ULONG size) {
    void *allocation = JglAllocate(size + JGL_BUFFER_ALIGNMENT);
    if (!allocation)
        return FALSE;
    JglFree(b->Allocation);
    b->Allocation = allocation;
    b->Data = (BYTE *)(((ULONG_PTR)allocation + JGL_BUFFER_ALIGNMENT - 1) &
                       ~(ULONG_PTR)(JGL_BUFFER_ALIGNMENT - 1));
    b->Size = size;
    return TRUE;
}
static void DestroyBuffer(JGL_BUFFER *b) {
    if (b) {
        JglFree(b->Allocation);
        JglFree(b);
    }
}
/* The share group's last context is gone. */
static void FreeBuffers(void *opaque) {
    JGL_BUFFERS *table = (JGL_BUFFERS *)opaque;
    if (!table)
        return;
    for (ULONG i = 0; i < JGL_BUFFER_SLOTS; ++i)
        DestroyBuffer(table->Entries[i].Object);
    JglFree(table);
}

/* Legal outside Begin/End with the capability. FALSE has raised the error. */
static BOOL BufferReady(void) {
    if (!JglReady())
        return FALSE;
    if (!JglVertexBufferObjectAvailable()) {
        JglSetError(GL_INVALID_OPERATION);
        return FALSE;
    }
    return TRUE;
}
static JGL_BUFFER_BINDING *BufferBinding(GLenum target) {
    JGL_ARRAY_STATE *state = JglArrays();
    if (target == JGL_ARRAY_BUFFER)
        return &state->ArrayBuffer;
    if (target == JGL_ELEMENT_ARRAY_BUFFER)
        return &state->ElementBuffer;
    JglSetError(GL_INVALID_ENUM);
    return NULL;
}
/* The object bound to target, under the lock; NULL has raised the error and
 * released the lock. A binding whose object is gone binds nothing. */
static JGL_BUFFER *BoundBuffer(GLenum target) {
    if (!BufferReady())
        return NULL;
    JGL_BUFFER_BINDING *binding = BufferBinding(target);
    if (!binding)
        return NULL;
    JglLockShared();
    JGL_BUFFER *b = JglBufferObject(binding->Name, binding->Serial);
    if (!b) {
        JglUnlockShared();
        JglSetError(GL_INVALID_OPERATION);
    }
    return b;
}

/* Contents travel as BufferSubData slabs of at most one packet each. */
static BOOL SubmitContents(GLuint name, ULONG offset, const BYTE *bytes, ULONG size) {
    const ULONG slab = JglMaxDataBytes(2) & ~3UL;
    while (size) {
        const ULONG n = size < slab ? size : slab;
        const ULONG args[2] = {name, offset};
        if (!slab || !JglData(FEnum_glBufferSubDataARB, args, 2, bytes, n))
            return FALSE;
        offset += n;
        bytes += n;
        size -= n;
    }
    return TRUE;
}

void APIENTRY glGenBuffersARB(GLsizei count, GLuint *buffers) {
    if (!BufferReady())
        return;
    if (count < 0) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    if (!count || !buffers)
        return;
    JglLockShared();
    JGL_BUFFERS *table = BufferTable(TRUE);
    if (!table || (ULONG)count > DG_GL_MAX_BUFFERS - table->Count) {
        JglUnlockShared();
        JglSetError(GL_OUT_OF_MEMORY);
        return;
    }
    for (GLsizei i = 0; i < count;) {
        const GLuint candidate = table->Next++;
        JGL_BUFFER_ENTRY *e;
        if (!candidate || (e = JglFindBuffer(table, candidate))->Name)
            continue;
        e->Name = candidate;
        e->Object = NULL;
        ++table->Count;
        buffers[i++] = candidate;
    }
    JglUnlockShared();
}
void APIENTRY glBindBufferARB(GLenum target, GLuint buffer) {
    if (!BufferReady())
        return;
    JGL_BUFFER_BINDING *binding = BufferBinding(target);
    if (!binding)
        return;
    if (!buffer) {
        binding->Name = 0;
        binding->Serial = 0;
        return;
    }
    JglLockShared();
    JGL_BUFFERS *table = BufferTable(TRUE);
    JGL_BUFFER_ENTRY *e = table ? JglFindBuffer(table, buffer) : NULL;
    /* A first bind creates the object, generated or not. */
    if (e && !e->Object && (e->Name || table->Count < DG_GL_MAX_BUFFERS)) {
        JGL_BUFFER *b = (JGL_BUFFER *)JglAllocate(sizeof(JGL_BUFFER));
        if (b) {
            ZeroMemory(b, sizeof(*b));
            b->Name = buffer;
            b->Serial = ++table->Serial ? table->Serial : ++table->Serial;
            b->Usage = JGL_STATIC_DRAW;
            b->Access = JGL_READ_WRITE;
            if (!StoreContents(b, 0)) {
                JglFree(b);
                b = NULL;
            }
        }
        if (b) {
            if (!e->Name)
                ++table->Count;
            e->Name = buffer;
            e->Object = b;
        }
    }
    if (!e || !e->Object) {
        JglUnlockShared();
        JglSetError(GL_OUT_OF_MEMORY);
        return;
    }
    binding->Name = buffer;
    binding->Serial = e->Object->Serial;
    JglUnlockShared();
}
void APIENTRY glDeleteBuffersARB(GLsizei count, const GLuint *buffers) {
    if (!BufferReady())
        return;
    if (count < 0) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    if (!buffers)
        return;
    /* Deleted names reach the host in bounded batches. */
    const ULONG batch =
        (JglMaxDataBytes(1) / 4) < DG_GL_MAX_BUFFERS ? JglMaxDataBytes(1) / 4 : DG_GL_MAX_BUFFERS;
    GLuint names[256];
    const ULONG limit = batch < 256 ? batch : 256;
    ULONG pending = 0;
    for (GLsizei i = 0; i < count; ++i) {
        const GLuint name = buffers[i];
        if (!name)
            continue;
        JglLockShared();
        JGL_BUFFERS *table = BufferTable(FALSE);
        JGL_BUFFER_ENTRY *e = table ? JglFindBuffer(table, name) : NULL;
        JGL_BUFFER *b = e && e->Name ? e->Object : NULL;
        const ULONG serial = b ? b->Serial : 0;
        if (e && e->Name) {
            RemoveBuffer(table, e);
            DestroyBuffer(b);
        }
        JglUnlockShared();
        if (!b)
            continue;
        /* Bindings of this context revert to zero; a mapping ends with it. */
        JglForgetBuffer(name, serial);
        names[pending++] = name;
        if (pending == limit) {
            const ULONG args[1] = {pending};
            JglData(FEnum_glDeleteBuffersARB, args, 1, names, pending * 4);
            pending = 0;
        }
    }
    if (pending) {
        const ULONG args[1] = {pending};
        JglData(FEnum_glDeleteBuffersARB, args, 1, names, pending * 4);
    }
}
GLboolean APIENTRY glIsBufferARB(GLuint buffer) {
    if (!BufferReady() || !buffer)
        return GL_FALSE;
    JglLockShared();
    JGL_BUFFERS *table = BufferTable(FALSE);
    const GLboolean result =
        table && JglFindBuffer(table, buffer)->Object ? (GLboolean)1 : GL_FALSE;
    JglUnlockShared();
    return result;
}
void JglForgetBuffer(GLuint name, ULONG serial) {
    JGL_ARRAY_STATE *state = JglArrays();
    if (!state)
        return;
    JGL_BUFFER_BINDING *bindings[2] = {&state->ArrayBuffer, &state->ElementBuffer};
    for (JGL_BUFFER_BINDING *b : bindings)
        if (b->Name == name && b->Serial == serial)
            b->Name = b->Serial = 0;
    for (ULONG i = 0; i < JGL_ARRAY_SLOTS + DG_GL_MAX_VERTEX_ATTRIBS; ++i) {
        JGL_ARRAY *a =
            i < JGL_ARRAY_SLOTS ? &state->Attribute[i] : &state->Generic[i - JGL_ARRAY_SLOTS];
        if (a->Buffer == name && a->Serial == serial)
            a->Buffer = a->Serial = 0;
    }
}

static BOOL BufferUsage(GLenum usage) {
    /* STREAM_DRAW..STREAM_COPY, STATIC_DRAW..STATIC_COPY, DYNAMIC_DRAW..DYNAMIC_COPY */
    return (usage >= 0x88e0 && usage <= 0x88e2) || (usage >= 0x88e4 && usage <= 0x88e6) ||
           (usage >= 0x88e8 && usage <= 0x88ea);
}
void APIENTRY glBufferDataARB(GLenum target, JGL_SIZEIPTR size, const void *data, GLenum usage) {
    if (!BufferReady() || !BufferBinding(target))
        return;
    if (size < 0) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    if (!BufferUsage(usage)) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    JGL_BUFFER *b = BoundBuffer(target);
    if (!b)
        return;
    /* Replacing the store unmaps it (the old pointer is invalid). */
    b->Mapped = FALSE;
    b->Access = JGL_READ_WRITE;
    b->Usage = usage;
    const GLuint name = b->Name;
    const BOOL stored = (unsigned long long)size <= DG_GL_MAX_BUFFER_BYTES &&
                        (b->Size == (ULONG)size || StoreContents(b, (ULONG)size));
    if (!stored)
        b->Size = 0;
    const ULONG bytes = b->Size;
    if (stored) {
        if (data)
            CopyMemory(b->Data, data, bytes);
        else
            ZeroMemory(b->Data, bytes);
    }
    JglUnlockShared();
    if (!stored)
        JglSetError(GL_OUT_OF_MEMORY);
    /* One record when the contents fit, else zero-defined storage and slabs. */
    const ULONG args[3] = {name, bytes, usage};
    const BOOL inline_data = data && bytes <= JglMaxDataBytes(3);
    if (!JglData(FEnum_glBufferDataARB, args, 3, inline_data ? data : NULL,
                 inline_data ? bytes : 0) ||
        inline_data || !data)
        return;
    SubmitContents(name, 0, (const BYTE *)data, bytes);
}
void APIENTRY glBufferSubDataARB(GLenum target, JGL_SIZEIPTR offset, JGL_SIZEIPTR size,
                                 const void *data) {
    if (!BufferReady() || !BufferBinding(target))
        return;
    if (offset < 0 || size < 0) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    JGL_BUFFER *b = BoundBuffer(target);
    if (!b)
        return;
    GLenum error = 0;
    if ((unsigned long long)offset + (unsigned long long)size > b->Size)
        error = GL_INVALID_VALUE;
    else if (b->Mapped)
        error = GL_INVALID_OPERATION;
    else if (size && data)
        CopyMemory(b->Data + offset, data, (ULONG)size);
    const GLuint name = b->Name;
    JglUnlockShared();
    if (error)
        JglSetError(error);
    else if (size && data)
        SubmitContents(name, (ULONG)offset, (const BYTE *)data, (ULONG)size);
}
void APIENTRY glGetBufferSubDataARB(GLenum target, JGL_SIZEIPTR offset, JGL_SIZEIPTR size,
                                    void *data) {
    if (!BufferReady() || !BufferBinding(target))
        return;
    if (offset < 0 || size < 0) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    JGL_BUFFER *b = BoundBuffer(target);
    if (!b)
        return;
    GLenum error = 0;
    if ((unsigned long long)offset + (unsigned long long)size > b->Size)
        error = GL_INVALID_VALUE;
    else if (b->Mapped)
        error = GL_INVALID_OPERATION;
    else if (size && data)
        CopyMemory(data, b->Data + offset, (ULONG)size);
    JglUnlockShared();
    if (error)
        JglSetError(error);
}
void *APIENTRY glMapBufferARB(GLenum target, GLenum access) {
    if (!BufferReady() || !BufferBinding(target))
        return NULL;
    if (access != JGL_READ_ONLY && access != JGL_WRITE_ONLY && access != JGL_READ_WRITE) {
        JglSetError(GL_INVALID_ENUM);
        return NULL;
    }
    JGL_BUFFER *b = BoundBuffer(target);
    if (!b)
        return NULL;
    void *result = NULL;
    if (!b->Mapped) {
        b->Mapped = TRUE;
        b->Access = access;
        result = b->Data;
    }
    JglUnlockShared();
    if (!result)
        JglSetError(GL_INVALID_OPERATION);
    return result;
}
GLboolean APIENTRY glUnmapBufferARB(GLenum target) {
    if (!BufferReady() || !BufferBinding(target))
        return GL_FALSE;
    JGL_BUFFER *b = BoundBuffer(target);
    if (!b)
        return GL_FALSE;
    const BOOL mapped = b->Mapped;
    b->Mapped = FALSE;
    /* Any byte may have changed: the host receives the whole store. The lock
     * keeps the copy alive while it is submitted (another context of the
     * share group could otherwise delete or replace it); no transport path
     * takes the lock. */
    if (mapped && b->Access != JGL_READ_ONLY)
        SubmitContents(b->Name, 0, b->Data, b->Size);
    JglUnlockShared();
    if (!mapped) {
        JglSetError(GL_INVALID_OPERATION);
        return GL_FALSE;
    }
    return (GLboolean)1;
}
void APIENTRY glGetBufferParameterivARB(GLenum target, GLenum pname, GLint *params) {
    if (!BufferReady() || !BufferBinding(target))
        return;
    if (pname != JGL_BUFFER_SIZE && pname != JGL_BUFFER_USAGE && pname != JGL_BUFFER_ACCESS &&
        pname != JGL_BUFFER_MAPPED) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    JGL_BUFFER *b = BoundBuffer(target);
    if (!b)
        return;
    const GLint value = pname == JGL_BUFFER_SIZE     ? (GLint)b->Size
                        : pname == JGL_BUFFER_USAGE  ? (GLint)b->Usage
                        : pname == JGL_BUFFER_ACCESS ? (GLint)b->Access
                                                     : (GLint)b->Mapped;
    JglUnlockShared();
    if (params)
        *params = value;
}
void APIENTRY glGetBufferPointervARB(GLenum target, GLenum pname, void **params) {
    if (!BufferReady() || !BufferBinding(target))
        return;
    if (pname != JGL_BUFFER_MAP_POINTER) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    JGL_BUFFER *b = BoundBuffer(target);
    if (!b)
        return;
    void *value = b->Mapped ? b->Data : NULL;
    JglUnlockShared();
    if (params)
        *params = value;
}

} /* extern C */
