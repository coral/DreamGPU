/* SPDX-License-Identifier: GPL-2.0-or-later
 * ARB_occlusion_query (target SAMPLES_PASSED) and NV_fence (condition
 * ALL_COMPLETED_NV) under their extension names (DG_CAP_GL_OCCLUSION_QUERY).
 * Without the capability every entry point fails locally with
 * GL_INVALID_OPERATION and wglGetProcAddress hides them.
 *
 * Query and fence names and objects belong to one context and live on the
 * host: Gen, Is and every state query are QUERY records, deletion is a DATA
 * record, and BeginQuery, EndQuery and SetFenceNV are CALL records, compiled
 * into display lists (their errors are deferred while compiling). Records
 * carry the core function numbers for queries and only shapes the host
 * admits: a target, condition or pname it would reject as malformed is a local
 * GL_INVALID_ENUM; what depends on GL state (an active query, a name without
 * an object) is the host's GL error.
 *
 * Results are asynchronous. One GetQueryObject round trip answers both the
 * availability and the result and carries the results of earlier queries
 * that completed meanwhile (gl.h); the frontend keeps them, and fences known
 * signalled, and answers repeated polls locally until the name is begun, set
 * or deleted or a display list executes. So Wine's availability-then-result
 * poll costs one round trip, and polling several ended queries usually one.
 */
#include "internal.h"

extern "C" {

#define JGL_SAMPLES_PASSED 0x8914
#define JGL_QUERY_COUNTER_BITS 0x8864
#define JGL_CURRENT_QUERY 0x8865
#define JGL_QUERY_RESULT 0x8866
#define JGL_QUERY_RESULT_AVAILABLE 0x8867
#define JGL_ALL_COMPLETED_NV 0x84f2
#define JGL_FENCE_STATUS_NV 0x84f3
#define JGL_FENCE_CONDITION_NV 0x84f4
/* Answer words of GetQueryObjectuiv with every report (gl.h). */
#define JGL_ANSWER_WORDS (2 + 2 * DG_GL_QUERY_REPORTS)

/* Legal outside Begin/End with the capability; FALSE has raised the error. */
static BOOL OcclusionReady(void) {
    if (!JglReady())
        return FALSE;
    if (!JglOcclusionQueryAvailable()) {
        JglSetError(GL_INVALID_OPERATION);
        return FALSE;
    }
    return TRUE;
}
/* The compiled commands defer their errors while compiling a list. */
static BOOL OcclusionCommandReady(void) {
    if (!JglCommandReady())
        return FALSE;
    if (!JglOcclusionQueryAvailable()) {
        JglCommandError(GL_INVALID_OPERATION);
        return FALSE;
    }
    return TRUE;
}

/* Settled entries: what the host reported for a name. */
static JGL_SETTLED *SettledOf(BOOL fences) {
    JGL_QUERY_STATE *state = JglQueryState();
    return state ? (fences ? &state->Fences : &state->Results) : NULL;
}
static BOOL SettledFind(JGL_SETTLED *s, GLuint name, GLuint *value) {
    if (!s || !name)
        return FALSE;
    for (ULONG i = 0; i < s->Count; ++i)
        if (s->Names[i] == name) {
            if (value)
                *value = s->Values[i];
            return TRUE;
        }
    return FALSE;
}
static void SettledForget(JGL_SETTLED *s, GLuint name) {
    if (!s || !name)
        return;
    for (ULONG i = 0; i < s->Count; ++i)
        if (s->Names[i] == name) {
            --s->Count;
            s->Names[i] = s->Names[s->Count];
            s->Values[i] = s->Values[s->Count];
            return;
        }
}
/* Keep a reported value; a full table replaces its entries in turn. */
static void SettledKeep(JGL_SETTLED *s, GLuint name, GLuint value) {
    ULONG at;
    if (!s || !name)
        return;
    for (at = 0; at < s->Count && s->Names[at] != name; ++at) {
    }
    if (at == s->Count) {
        if (s->Count < JGL_SETTLED_SLOTS) {
            ++s->Count;
        } else {
            at = s->Next;
            s->Next = (s->Next + 1) % JGL_SETTLED_SLOTS;
        }
    }
    s->Names[at] = name;
    s->Values[at] = value;
}

/* Gen: host-reserved names in batches of at most DG_GL_QUERY_NAME_BATCH. The
 * host answers zeros (and keeps the error) when it cannot reserve them. */
static void GenerateQueryNames(ULONG function, GLsizei count, GLuint *names) {
    if (!OcclusionReady())
        return;
    if (count < 0) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    for (GLsizei done = 0; names && done < count;) {
        GLuint batch[DG_GL_QUERY_NAME_BATCH];
        const ULONG n = (ULONG)(count - done) < DG_GL_QUERY_NAME_BATCH ? (ULONG)(count - done)
                                                                       : DG_GL_QUERY_NAME_BATCH;
        const ULONG args[3] = {n, 0, 0};
        ULONG bytes = 0;
        if (!JglQuery(function, args, DG_GL_RESULT_INT, batch, n * 4, &bytes) || bytes != n * 4)
            return;
        if (!batch[0]) /* the host's error; the names stay untouched */
            return;
        CopyMemory(names + done, batch, n * 4);
        done += (GLsizei)n;
    }
}
/* Delete: nonzero names in DATA batches; the host ignores unknown ones. */
static void DeleteQueryNames(ULONG function, BOOL fences, GLsizei count, const GLuint *names) {
    if (!OcclusionReady())
        return;
    if (count < 0) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    if (!names)
        return;
    const ULONG fit = JglMaxDataBytes(1) / 4;
    ULONG limit = fit < 256 ? fit : 256, used = 0;
    GLuint batch[256];
    if (!limit) /* never with a negotiated packet of at least 32 words */
        limit = 1;
    for (GLsizei i = 0; i < count; ++i) {
        if (!names[i])
            continue;
        SettledForget(SettledOf(fences), names[i]);
        batch[used++] = names[i];
        if (used == limit) {
            const ULONG args[1] = {used};
            JglData(function, args, 1, batch, used * 4);
            used = 0;
        }
    }
    if (used) {
        const ULONG args[1] = {used};
        JglData(function, args, 1, batch, used * 4);
    }
}
static GLboolean IsQueryObject(ULONG function, BOOL fences, GLuint name) {
    const ULONG args[3] = {name, 0, 0};
    GLboolean result = GL_FALSE;
    ULONG bytes = 0;
    if (!OcclusionReady() || !name)
        return GL_FALSE;
    if (SettledFind(SettledOf(fences), name, NULL))
        return (GLboolean)1;
    JglQuery(function, args, DG_GL_RESULT_BOOL, &result, sizeof(result), &bytes);
    return result ? (GLboolean)1 : GL_FALSE;
}

void APIENTRY glGenQueriesARB(GLsizei count, GLuint *ids) {
    GenerateQueryNames(FEnum_glGenQueries, count, ids);
}
void APIENTRY glDeleteQueriesARB(GLsizei count, const GLuint *ids) {
    DeleteQueryNames(FEnum_glDeleteQueries, FALSE, count, ids);
}
GLboolean APIENTRY glIsQueryARB(GLuint id) {
    return IsQueryObject(FEnum_glIsQuery, FALSE, id);
}
/* Begin and End compile into display lists; the host owns the active query. */
void APIENTRY glBeginQueryARB(GLenum target, GLuint id) {
    if (!OcclusionCommandReady())
        return;
    if (target != JGL_SAMPLES_PASSED) {
        JglCommandError(GL_INVALID_ENUM);
        return;
    }
    SettledForget(SettledOf(FALSE), id);
    const ULONG args[2] = {target, id};
    JglScalarVector(FEnum_glBeginQuery, 2, args);
}
void APIENTRY glEndQueryARB(GLenum target) {
    if (!OcclusionCommandReady())
        return;
    if (target != JGL_SAMPLES_PASSED) {
        JglCommandError(GL_INVALID_ENUM);
        return;
    }
    const ULONG args[1] = {target};
    JglScalarVector(FEnum_glEndQuery, 1, args);
}
void APIENTRY glGetQueryivARB(GLenum target, GLenum pname, GLint *params) {
    const ULONG args[3] = {target, pname, 0};
    GLint value = 0;
    ULONG bytes = 0;
    if (!OcclusionReady())
        return;
    if (target != JGL_SAMPLES_PASSED ||
        (pname != JGL_CURRENT_QUERY && pname != JGL_QUERY_COUNTER_BITS)) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    if (params &&
        JglQuery(FEnum_glGetQueryiv, args, DG_GL_RESULT_INT, &value, sizeof(value), &bytes) &&
        bytes == sizeof(value))
        *params = value;
}
/* QUERY_RESULT or QUERY_RESULT_AVAILABLE into value; FALSE leaves params
 * unchanged (a local or host error). */
static BOOL QueryObjectValue(GLuint id, GLenum pname, GLuint *value) {
    if (!OcclusionReady())
        return FALSE;
    if (pname != JGL_QUERY_RESULT && pname != JGL_QUERY_RESULT_AVAILABLE) {
        JglSetError(GL_INVALID_ENUM);
        return FALSE;
    }
    JGL_SETTLED *results = SettledOf(FALSE);
    GLuint result;
    if (!SettledFind(results, id, &result)) {
        const ULONG args[3] = {id, pname, DG_GL_QUERY_REPORTS};
        ULONG answer[JGL_ANSWER_WORDS], bytes = 0;
        if (!JglQuery(FEnum_glGetQueryObjectuiv, args, DG_GL_RESULT_INT, answer, sizeof(answer),
                      &bytes) ||
            bytes < 8 || bytes % 8)
            return FALSE; /* a single zero word: the host raised the error */
        for (ULONG i = 2; i < bytes / 4; i += 2)
            SettledKeep(results, answer[i], answer[i + 1]);
        if (answer[0] != DG_GL_QUERY_AVAILABLE) {
            if (pname == JGL_QUERY_RESULT_AVAILABLE)
                *value = GL_FALSE;
            return pname == JGL_QUERY_RESULT_AVAILABLE;
        }
        result = answer[1];
        SettledKeep(results, id, result);
    }
    *value = pname == JGL_QUERY_RESULT ? result : (GLuint)1;
    return TRUE;
}
void APIENTRY glGetQueryObjectuivARB(GLuint id, GLenum pname, GLuint *params) {
    GLuint value;
    if (QueryObjectValue(id, pname, &value) && params)
        *params = value;
}
/* Counts beyond INT_MAX clamp to it. */
void APIENTRY glGetQueryObjectivARB(GLuint id, GLenum pname, GLint *params) {
    GLuint value;
    if (QueryObjectValue(id, pname, &value) && params)
        *params = value > 0x7fffffffU ? 0x7fffffff : (GLint)value;
}

void APIENTRY glGenFencesNV(GLsizei count, GLuint *fences) {
    GenerateQueryNames(FEnum_glGenFencesNV, count, fences);
}
void APIENTRY glDeleteFencesNV(GLsizei count, const GLuint *fences) {
    DeleteQueryNames(FEnum_glDeleteFencesNV, TRUE, count, fences);
}
GLboolean APIENTRY glIsFenceNV(GLuint fence) {
    return IsQueryObject(FEnum_glIsFenceNV, TRUE, fence);
}
/* SetFenceNV compiles into display lists. */
void APIENTRY glSetFenceNV(GLuint fence, GLenum condition) {
    if (!OcclusionCommandReady())
        return;
    if (condition != JGL_ALL_COMPLETED_NV) {
        JglCommandError(GL_INVALID_ENUM);
        return;
    }
    SettledForget(SettledOf(TRUE), fence);
    const ULONG args[2] = {fence, condition};
    JglScalarVector(FEnum_glSetFenceNV, 2, args);
}
/* One, then the status or condition (gl.h); FALSE for a name that is not a
 * fence, after the host's error. A TRUE status is kept. */
static BOOL FenceAnswer(ULONG function, GLuint fence, GLenum pname, GLuint *value) {
    const ULONG args[3] = {fence, pname, 0};
    ULONG answer[2] = {0, 0}, bytes = 0;
    if (!JglQuery(function, args, DG_GL_RESULT_INT, answer, sizeof(answer), &bytes) ||
        bytes != sizeof(answer) || answer[0] != 1)
        return FALSE;
    if (pname != JGL_FENCE_CONDITION_NV && answer[1])
        SettledKeep(SettledOf(TRUE), fence, 1);
    *value = answer[1];
    return TRUE;
}
/* TRUE for a name that is not a fence, as NV_fence requires. */
GLboolean APIENTRY glTestFenceNV(GLuint fence) {
    GLuint status = 1;
    if (!OcclusionReady() || SettledFind(SettledOf(TRUE), fence, NULL))
        return (GLboolean)1;
    FenceAnswer(FEnum_glTestFenceNV, fence, 0, &status);
    return status ? (GLboolean)1 : GL_FALSE;
}
void APIENTRY glFinishFenceNV(GLuint fence) {
    GLuint status;
    if (!OcclusionReady() || SettledFind(SettledOf(TRUE), fence, NULL))
        return;
    FenceAnswer(FEnum_glFinishFenceNV, fence, 0, &status);
}
void APIENTRY glGetFenceivNV(GLuint fence, GLenum pname, GLint *params) {
    GLuint value;
    if (!OcclusionReady())
        return;
    if (pname != JGL_FENCE_STATUS_NV && pname != JGL_FENCE_CONDITION_NV) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    if (SettledFind(SettledOf(TRUE), fence, NULL))
        value = pname == JGL_FENCE_STATUS_NV ? 1 : JGL_ALL_COMPLETED_NV;
    else if (!FenceAnswer(FEnum_glGetFenceivNV, fence, pname, &value))
        return;
    if (params)
        *params = (GLint)value;
}

} /* extern C */
