/* SPDX-License-Identifier: GPL-2.0-or-later
 * EXT_framebuffer_object with EXT_framebuffer_blit and EXT_packed_depth_stencil
 * (DG_CAP_GL_FRAMEBUFFER_OBJECT), under EXT names only. Without the capability
 * every entry point fails locally with GL_INVALID_OPERATION and
 * wglGetProcAddress hides them.
 *
 * Framebuffer names belong to one context, renderbuffer names to its share
 * group. Gen reserves names and Is answers from those tables; an object exists
 * from its first bind, which sends the bind record that creates it on the
 * host. The draw, read and renderbuffer bindings are tracked here for the
 * binding queries, DrawBuffer/ReadBuffer validation, front-buffer publication
 * and ReadPixels clipping. Records carry the core function numbers and only
 * shapes the host admits: every enum, level and size it would reject as a
 * malformed record is a local error; what depends on GL state (the window
 * bound, missing objects, the texture's target) is the host's GL error.
 * Everything executes immediately, even while compiling a display list (its
 * errors are immediate too), except BlitFramebuffer, which compiles.
 */
#include "internal.h"

extern "C" {

#define JGL_DEPTH_ATTACHMENT 0x8d00
#define JGL_STENCIL_ATTACHMENT 0x8d20
#define JGL_NEAREST 0x2600
#define JGL_LINEAR 0x2601
#define JGL_BLIT_BUFFERS (GL_COLOR_BUFFER_BIT | 0x100 /* DEPTH */ | 0x400 /* STENCIL */)
#define JGL_FRAMEBUFFER_SLOTS (DG_GL_MAX_FRAMEBUFFERS * 2)
#define JGL_RENDERBUFFER_SLOTS (DG_GL_MAX_RENDERBUFFERS * 2)
#if (JGL_FRAMEBUFFER_SLOTS & (JGL_FRAMEBUFFER_SLOTS - 1)) ||                                       \
    (JGL_RENDERBUFFER_SLOTS & (JGL_RENDERBUFFER_SLOTS - 1))
#error Framebuffer name tables require power-of-two capacities
#endif

/* A name table: open addressing with backward-shift deletion over Mask + 1
 * slots, at most half of them used. Object is set by the first bind. */
typedef struct {
    GLuint Name;
    BOOL Object;
} JGL_OBJECT_NAME;
typedef struct {
    ULONG Count, Next, Mask;
    JGL_OBJECT_NAME Entries[];
} JGL_OBJECT_NAMES;

/* The table in slot, created on first use when create is set; one allocation
 * its owner frees with HeapFree. */
static JGL_OBJECT_NAMES *NameTable(void **slot, ULONG slots, BOOL create) {
    if (!slot)
        return NULL;
    if (!*slot && create) {
        const ULONG bytes = sizeof(JGL_OBJECT_NAMES) + slots * sizeof(JGL_OBJECT_NAME);
        JGL_OBJECT_NAMES *table = (JGL_OBJECT_NAMES *)JglAllocate(bytes);
        if (table) {
            ZeroMemory(table, bytes);
            table->Next = 1;
            table->Mask = slots - 1;
        }
        *slot = table;
    }
    return (JGL_OBJECT_NAMES *)*slot;
}
static ULONG NameSlot(const JGL_OBJECT_NAMES *table, GLuint name) {
    return (name * 2654435761u) & table->Mask;
}
/* The entry of name, or the empty slot ending its probe chain. */
static JGL_OBJECT_NAME *FindObjectName(JGL_OBJECT_NAMES *table, GLuint name) {
    for (ULONG at = NameSlot(table, name);; at = (at + 1) & table->Mask) {
        JGL_OBJECT_NAME *e = &table->Entries[at];
        if (!e->Name || e->Name == name)
            return e;
    }
}
static void RemoveObjectName(JGL_OBJECT_NAMES *table, JGL_OBJECT_NAME *entry) {
    ULONG hole = (ULONG)(entry - table->Entries), at = hole;
    entry->Name = 0;
    entry->Object = FALSE;
    --table->Count;
    for (;;) {
        at = (at + 1) & table->Mask;
        JGL_OBJECT_NAME *e = &table->Entries[at];
        if (!e->Name)
            return;
        const ULONG origin = NameSlot(table, e->Name);
        if (((hole - origin) & table->Mask) < ((at - origin) & table->Mask)) {
            table->Entries[hole] = *e;
            e->Name = 0;
            e->Object = FALSE;
            hole = at;
        }
    }
}
/* Reserves count unused nonzero names, all or none (GL_OUT_OF_MEMORY). */
static void GenerateNames(JGL_OBJECT_NAMES *table, ULONG limit, GLsizei count, GLuint *names) {
    if (!table || (ULONG)count > limit - table->Count) {
        JglSetError(GL_OUT_OF_MEMORY);
        return;
    }
    for (GLsizei i = 0; i < count;) {
        const GLuint candidate = table->Next++;
        JGL_OBJECT_NAME *e;
        if (!candidate || (e = FindObjectName(table, candidate))->Name)
            continue;
        e->Name = candidate;
        e->Object = FALSE;
        ++table->Count;
        names[i++] = candidate;
    }
}
/* The object of a nonzero name exists from here on; FALSE without room. */
static BOOL CreateObject(JGL_OBJECT_NAMES *table, ULONG limit, GLuint name) {
    if (!table)
        return FALSE;
    JGL_OBJECT_NAME *e = FindObjectName(table, name);
    if (!e->Name) {
        if (table->Count >= limit)
            return FALSE;
        e->Name = name;
        ++table->Count;
    }
    e->Object = TRUE;
    return TRUE;
}
static BOOL ObjectExists(JGL_OBJECT_NAMES *table, GLuint name) {
    return table && name && FindObjectName(table, name)->Object;
}
/* Frees name; TRUE when it named an object, which the host has. */
static BOOL ForgetName(JGL_OBJECT_NAMES *table, GLuint name) {
    if (!table || !name)
        return FALSE;
    JGL_OBJECT_NAME *e = FindObjectName(table, name);
    if (!e->Name)
        return FALSE;
    const BOOL object = e->Object;
    RemoveObjectName(table, e);
    return object;
}

/* Deleted names reach the host in DATA batches of at most limit names. */
typedef struct {
    ULONG Function, Limit, Count;
    GLuint Names[256];
} JGL_DELETION;
static void StartDeletion(JGL_DELETION *d, ULONG function, ULONG maximum) {
    const ULONG fit = JglMaxDataBytes(1) / 4;
    d->Function = function;
    d->Limit = fit < maximum ? fit : maximum;
    if (d->Limit > 256)
        d->Limit = 256;
    if (!d->Limit) /* never with a negotiated packet of at least 32 words */
        d->Limit = 1;
    d->Count = 0;
}
static void SendDeletion(JGL_DELETION *d) {
    if (!d->Count)
        return;
    const ULONG args[1] = {d->Count};
    JglData(d->Function, args, 1, d->Names, d->Count * 4);
    d->Count = 0;
}
static void QueueDeletion(JGL_DELETION *d, GLuint name) {
    d->Names[d->Count++] = name;
    if (d->Count == d->Limit)
        SendDeletion(d);
}

/* Legal outside Begin/End with the capability. FALSE has raised the error. */
static BOOL FramebufferReady(void) {
    if (!JglReady())
        return FALSE;
    if (!JglFramebufferObjectAvailable()) {
        JglSetError(GL_INVALID_OPERATION);
        return FALSE;
    }
    return TRUE;
}
static BOOL FramebufferTarget(GLenum target) {
    return target == JGL_FRAMEBUFFER || target == JGL_DRAW_FRAMEBUFFER ||
           target == JGL_READ_FRAMEBUFFER;
}
static BOOL AttachmentPoint(GLenum attachment) {
    return JglColorAttachment(attachment) || attachment == JGL_DEPTH_ATTACHMENT ||
           attachment == JGL_STENCIL_ATTACHMENT;
}
/* RenderbufferStorage formats: RGB, RGBA, R3_G3_B2, RGB4..RGBA16, RGB565 once
 * negotiated, the depth and stencil formats and the packed DEPTH_STENCIL ones. */
static BOOL RenderbufferFormat(GLenum format) {
    return format == 0x1907 || format == 0x1908 || format == 0x2a10 ||
           (format >= 0x804f && format <= 0x805b) || JglRgb565Format((GLint)format) ||
           format == 0x1902 || (format >= 0x81a5 && format <= 0x81a7) || format == 0x1901 ||
           (format >= 0x8d46 && format <= 0x8d49) || format == JGL_DEPTH_STENCIL ||
           format == 0x88f0;
}
static JGL_OBJECT_NAMES *Framebuffers(BOOL create) {
    JGL_FRAMEBUFFER_STATE *state = JglFramebufferState();
    return state ? NameTable(&state->Names, JGL_FRAMEBUFFER_SLOTS, create) : NULL;
}
/* The share group's renderbuffer names, under the shared lock. */
static JGL_OBJECT_NAMES *Renderbuffers(BOOL create) {
    return NameTable(JglSharedRenderbuffers(), JGL_RENDERBUFFER_SLOTS, create);
}

void APIENTRY glGenFramebuffersEXT(GLsizei count, GLuint *framebuffers) {
    if (!FramebufferReady())
        return;
    if (count < 0) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    if (count && framebuffers)
        GenerateNames(Framebuffers(TRUE), DG_GL_MAX_FRAMEBUFFERS, count, framebuffers);
}
void APIENTRY glGenRenderbuffersEXT(GLsizei count, GLuint *renderbuffers) {
    if (!FramebufferReady())
        return;
    if (count < 0) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    if (!count || !renderbuffers)
        return;
    JglLockShared();
    JGL_OBJECT_NAMES *table = Renderbuffers(TRUE);
    if (table)
        GenerateNames(table, DG_GL_MAX_RENDERBUFFERS, count, renderbuffers);
    JglUnlockShared();
    if (!table)
        JglSetError(GL_OUT_OF_MEMORY);
}
GLboolean APIENTRY glIsFramebufferEXT(GLuint framebuffer) {
    if (!FramebufferReady())
        return GL_FALSE;
    return ObjectExists(Framebuffers(FALSE), framebuffer) ? (GLboolean)1 : GL_FALSE;
}
GLboolean APIENTRY glIsRenderbufferEXT(GLuint renderbuffer) {
    if (!FramebufferReady())
        return GL_FALSE;
    JglLockShared();
    const BOOL exists = ObjectExists(Renderbuffers(FALSE), renderbuffer);
    JglUnlockShared();
    return exists ? (GLboolean)1 : GL_FALSE;
}
/* FRAMEBUFFER binds both the draw and the read framebuffer. */
void APIENTRY glBindFramebufferEXT(GLenum target, GLuint framebuffer) {
    if (!FramebufferReady())
        return;
    if (!FramebufferTarget(target)) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    if (framebuffer && !CreateObject(Framebuffers(TRUE), DG_GL_MAX_FRAMEBUFFERS, framebuffer)) {
        JglSetError(GL_OUT_OF_MEMORY);
        return;
    }
    const ULONG args[2] = {target, framebuffer};
    JglScalarVector(FEnum_glBindFramebuffer, 2, args);
    JGL_FRAMEBUFFER_STATE *state = JglFramebufferState();
    if (target != JGL_READ_FRAMEBUFFER)
        state->Draw = framebuffer;
    if (target != JGL_DRAW_FRAMEBUFFER)
        state->Read = framebuffer;
}
void APIENTRY glBindRenderbufferEXT(GLenum target, GLuint renderbuffer) {
    if (!FramebufferReady())
        return;
    if (target != JGL_RENDERBUFFER) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    if (renderbuffer) {
        JglLockShared();
        const BOOL created =
            CreateObject(Renderbuffers(TRUE), DG_GL_MAX_RENDERBUFFERS, renderbuffer);
        JglUnlockShared();
        if (!created) {
            JglSetError(GL_OUT_OF_MEMORY);
            return;
        }
    }
    const ULONG args[2] = {target, renderbuffer};
    JglScalarVector(FEnum_glBindRenderbuffer, 2, args);
    JglFramebufferState()->Renderbuffer = renderbuffer;
}
/* Only names of objects reach the host. A deleted bound framebuffer leaves
 * the window bound in its place, as on the host. */
void APIENTRY glDeleteFramebuffersEXT(GLsizei count, const GLuint *framebuffers) {
    if (!FramebufferReady())
        return;
    if (count < 0) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    if (!framebuffers)
        return;
    JGL_FRAMEBUFFER_STATE *state = JglFramebufferState();
    JGL_OBJECT_NAMES *table = Framebuffers(FALSE);
    JGL_DELETION deletion;
    StartDeletion(&deletion, FEnum_glDeleteFramebuffers, DG_GL_MAX_FRAMEBUFFERS);
    for (GLsizei i = 0; i < count; ++i) {
        const GLuint name = framebuffers[i];
        if (!ForgetName(table, name))
            continue;
        if (state->Draw == name)
            state->Draw = 0;
        if (state->Read == name)
            state->Read = 0;
        QueueDeletion(&deletion, name);
    }
    SendDeletion(&deletion);
}
/* A deleted bound renderbuffer unbinds; other contexts keep their binding. */
void APIENTRY glDeleteRenderbuffersEXT(GLsizei count, const GLuint *renderbuffers) {
    if (!FramebufferReady())
        return;
    if (count < 0) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    if (!renderbuffers)
        return;
    JGL_FRAMEBUFFER_STATE *state = JglFramebufferState();
    JGL_DELETION deletion;
    StartDeletion(&deletion, FEnum_glDeleteRenderbuffers, DG_GL_MAX_RENDERBUFFERS);
    for (GLsizei i = 0; i < count; ++i) {
        const GLuint name = renderbuffers[i];
        JglLockShared();
        const BOOL object = ForgetName(Renderbuffers(FALSE), name);
        JglUnlockShared();
        if (!object)
            continue;
        if (state->Renderbuffer == name)
            state->Renderbuffer = 0;
        QueueDeletion(&deletion, name);
    }
    SendDeletion(&deletion);
}
void APIENTRY glRenderbufferStorageEXT(GLenum target, GLenum internalformat, GLsizei width,
                                       GLsizei height) {
    if (!FramebufferReady())
        return;
    if (target != JGL_RENDERBUFFER || !RenderbufferFormat(internalformat)) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    if (width < 0 || height < 0 || width > DG_GL_MAX_RENDERBUFFER_SIZE ||
        height > DG_GL_MAX_RENDERBUFFER_SIZE) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    const ULONG args[4] = {target, internalformat, (ULONG)width, (ULONG)height};
    JglScalarVector(FEnum_glRenderbufferStorage, 4, args);
}
/* The attachment records: target, attachment and textarget are checked even
 * to detach (texture zero), as the host checks them. */
static BOOL TextureAttachment(GLenum target, GLenum attachment, BOOL textarget, GLint level) {
    if (!FramebufferReady())
        return FALSE;
    if (!FramebufferTarget(target) || !AttachmentPoint(attachment) || !textarget) {
        JglSetError(GL_INVALID_ENUM);
        return FALSE;
    }
    if (level < 0 || level > DG_GL_MAX_TEXTURE_LEVEL) {
        JglSetError(GL_INVALID_VALUE);
        return FALSE;
    }
    return TRUE;
}
void APIENTRY glFramebufferTexture1DEXT(GLenum target, GLenum attachment, GLenum textarget,
                                        GLuint texture, GLint level) {
    if (!TextureAttachment(target, attachment, textarget == GL_TEXTURE_1D, level))
        return;
    const ULONG args[5] = {target, attachment, textarget, texture, (ULONG)level};
    JglScalarVector(FEnum_glFramebufferTexture1D, 5, args);
}
void APIENTRY glFramebufferTexture2DEXT(GLenum target, GLenum attachment, GLenum textarget,
                                        GLuint texture, GLint level) {
    /* A rectangle's nonzero level is the host's INVALID_VALUE: it depends on
     * the texture, not on the record. */
    if (!TextureAttachment(target, attachment,
                           JglImage2DTarget(textarget) || JglRectangleTarget(textarget), level))
        return;
    const ULONG args[5] = {target, attachment, textarget, texture, (ULONG)level};
    JglScalarVector(FEnum_glFramebufferTexture2D, 5, args);
}
void APIENTRY glFramebufferTexture3DEXT(GLenum target, GLenum attachment, GLenum textarget,
                                        GLuint texture, GLint level, GLint zoffset) {
    if (!TextureAttachment(target, attachment,
                           textarget == JGL_TEXTURE_3D && JglTexture3DAvailable(), level))
        return;
    if (zoffset < 0 || zoffset >= DG_GL_MAX_3D_TEXTURE_SIZE) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    const ULONG args[6] = {target, attachment, textarget, texture, (ULONG)level, (ULONG)zoffset};
    JglScalarVector(FEnum_glFramebufferTexture3D, 6, args);
}
void APIENTRY glFramebufferRenderbufferEXT(GLenum target, GLenum attachment,
                                           GLenum renderbuffertarget, GLuint renderbuffer) {
    if (!FramebufferReady())
        return;
    if (!FramebufferTarget(target) || !AttachmentPoint(attachment) ||
        renderbuffertarget != JGL_RENDERBUFFER) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    const ULONG args[4] = {target, attachment, renderbuffertarget, renderbuffer};
    JglScalarVector(FEnum_glFramebufferRenderbuffer, 4, args);
}
/* The active unit's binding of target. */
void APIENTRY glGenerateMipmapEXT(GLenum target) {
    if (!FramebufferReady())
        return;
    if (target != GL_TEXTURE_1D && target != GL_TEXTURE_2D &&
        !(target == JGL_TEXTURE_CUBE_MAP && JglTextureCubeMapAvailable()) &&
        !(target == JGL_TEXTURE_3D && JglTexture3DAvailable())) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    const ULONG args[1] = {target};
    JglScalarVector(FEnum_glGenerateMipmap, 1, args);
}
/* Compiled into display lists, so errors are deferred while compiling. An
 * empty mask copies nothing and sends nothing. */
void APIENTRY glBlitFramebufferEXT(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0,
                                   GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask,
                                   GLenum filter) {
    if (!JglCommandReady())
        return;
    if (!JglFramebufferObjectAvailable()) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    if (mask & ~(GLbitfield)JGL_BLIT_BUFFERS) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    if (filter != JGL_NEAREST && filter != JGL_LINEAR) {
        JglCommandError(GL_INVALID_ENUM);
        return;
    }
    /* LINEAR filters colour alone. */
    if (filter == JGL_LINEAR && (mask & ~(GLbitfield)GL_COLOR_BUFFER_BIT)) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    if (!mask)
        return;
    const ULONG args[10] = {(ULONG)srcX0, (ULONG)srcY0, (ULONG)srcX1, (ULONG)srcY1, (ULONG)dstX0,
                            (ULONG)dstY0, (ULONG)dstX1, (ULONG)dstY1, mask,         filter};
    JglScalarVector(FEnum_glBlitFramebuffer, 10, args);
}

/* Host queries: one RESULT_INT each. */
static BOOL QueryInteger(ULONG function, ULONG a, ULONG b, ULONG c, GLint *value) {
    const ULONG args[3] = {a, b, c};
    ULONG bytes = 0;
    return JglQuery(function, args, DG_GL_RESULT_INT, value, sizeof(*value), &bytes) &&
           bytes == sizeof(*value);
}
/* Zero, with the error kept, when the query fails. */
GLenum APIENTRY glCheckFramebufferStatusEXT(GLenum target) {
    GLint status = 0;
    if (!FramebufferReady())
        return 0;
    if (!FramebufferTarget(target)) {
        JglSetError(GL_INVALID_ENUM);
        return 0;
    }
    return QueryInteger(FEnum_glCheckFramebufferStatus, target, 0, 0, &status) ? (GLenum)status : 0;
}
/* OBJECT_TYPE, OBJECT_NAME, TEXTURE_LEVEL, TEXTURE_CUBE_MAP_FACE and
 * TEXTURE_3D_ZOFFSET. */
void APIENTRY glGetFramebufferAttachmentParameterivEXT(GLenum target, GLenum attachment,
                                                       GLenum pname, GLint *params) {
    GLint value = 0;
    if (!FramebufferReady())
        return;
    if (!FramebufferTarget(target) || !AttachmentPoint(attachment) || pname - 0x8cd0 > 4) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    if (params && QueryInteger(FEnum_glGetFramebufferAttachmentParameteriv, target, attachment,
                               pname, &value))
        *params = value;
}
/* WIDTH, HEIGHT, INTERNAL_FORMAT and RED..STENCIL_SIZE of the bound renderbuffer. */
void APIENTRY glGetRenderbufferParameterivEXT(GLenum target, GLenum pname, GLint *params) {
    GLint value = 0;
    if (!FramebufferReady())
        return;
    if (target != JGL_RENDERBUFFER || (pname - 0x8d42 > 2 && pname - 0x8d50 > 5)) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    if (params && QueryInteger(FEnum_glGetRenderbufferParameteriv, target, pname, 0, &value))
        *params = value;
}

} /* extern C */
