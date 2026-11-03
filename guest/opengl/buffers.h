/* SPDX-License-Identifier: GPL-2.0-or-later
 * The share group's ARB_vertex_buffer_object table (buffer.cpp owns it), and
 * the lookup draws use. Callers hold the shared lock (JglLockShared).
 */
#ifndef DG_OPENGL_BUFFERS_H
#define DG_OPENGL_BUFFERS_H
#include "internal.h"
#define JGL_BUFFER_SLOTS (DG_GL_MAX_BUFFERS * 2)
#if JGL_BUFFER_SLOTS & (JGL_BUFFER_SLOTS - 1)
#error Buffer namespace table requires a power-of-two capacity
#endif

/* Open addressing with backward-shift deletion. A name with a NULL object was
 * generated but never bound. */
typedef struct {
    GLuint Name;
    JGL_BUFFER *Object;
} JGL_BUFFER_ENTRY;
typedef struct {
    ULONG Count, Next, Serial;
    JGL_BUFFER_ENTRY Entries[JGL_BUFFER_SLOTS];
} JGL_BUFFERS;

static inline ULONG JglBufferSlot(GLuint name) {
    return (name * 2654435761u) & (JGL_BUFFER_SLOTS - 1);
}
/* The entry of name, or the empty slot ending its probe chain. */
static inline JGL_BUFFER_ENTRY *JglFindBuffer(JGL_BUFFERS *table, GLuint name) {
    ULONG at = JglBufferSlot(name);
    for (;;) {
        JGL_BUFFER_ENTRY *e = &table->Entries[at];
        if (!e->Name || e->Name == name)
            return e;
        at = (at + 1) & (JGL_BUFFER_SLOTS - 1);
    }
}
/* The object an array or binding latched, or NULL once that object is gone
 * (deleted, or its name reused). */
static inline JGL_BUFFER *JglBufferObject(GLuint name, ULONG serial) {
    void **slot = name ? JglSharedBuffers(NULL) : NULL;
    JGL_BUFFERS *table = slot ? (JGL_BUFFERS *)*slot : NULL;
    JGL_BUFFER *b = table ? JglFindBuffer(table, name)->Object : NULL;
    return b && b->Serial == serial ? b : NULL;
}
#endif
