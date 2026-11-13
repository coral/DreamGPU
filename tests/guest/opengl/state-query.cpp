/* SPDX-License-Identifier: GPL-2.0-or-later
 * Local answers of IsEnabled/Get* (state.h JGL_STATE_CACHE, JglStateQuery)
 * through the actual frontend, query and display-list sources, checked
 * against a reference model of native GL that executes every submitted
 * record: enables (per texture unit where GL has them), the viewport with
 * native clamping, the attribute stack, display lists in both modes,
 * CallList(s) and several contexts. A local answer must equal the model's
 * answer at that point of the command stream; anything else must reach it. */
#include <map>
#include <vector>
#define main existing_frontend_suite_not_run
#include "frontend-harness.cpp"
#undef main
#include "arrays.cpp"
#include "query.cpp"
typedef void GLvoid;
typedef signed char GLbyte;
#ifndef GL_2_BYTES
#define GL_2_BYTES 0x1407
#define GL_3_BYTES 0x1408
#define GL_4_BYTES 0x1409
#endif
namespace dg_icd_textures {
static bool Span(const void *, ULONG, ULONG, bool) {
    return true;
}
} // namespace dg_icd_textures
#include "icd-lists.inc"

#define CAP_LIGHTING 0x0b50U
#define CAP_FOG 0x0b60U
#define CAP_DITHER 0x0bd0U
#define CAP_BLEND 0x0be2U
#define CAP_TEXTURE_2D 0x0de1U
#define CAP_AUTO_NORMAL 0x0d80U /* a valid capability the frontend does not shadow */
#define CAP_COLOR_SUM 0x8458U
#define VIEWPORT 0x0ba2U
#define ENABLE_BIT 0x2000U
#define VIEWPORT_BIT 0x0800U
#define TEXTURE_BIT 0x40000U
#define ALL_BITS 0x000fffffU
#define TEXTURE0 0x84c0U

namespace model {
/* Native GL: capabilities of the context and of each texture unit. */
static const ULONG Global[] = {
    0x0be2, 0x0b71, 0x0b90, 0x0c11, 0x0b44, 0x0b50, 0x0b60, 0x0bc0, 0x0bd0, 0x0ba1, 0x8037,
    0x2a02, 0x2a01, 0x0b41, 0x0b42, 0x0b20, 0x0b24, 0x0b10, 0x0b57, 0x4000, 0x4001, 0x4002,
    0x4003, 0x4004, 0x4005, 0x4006, 0x4007, 0x3000, 0x3001, 0x3002, 0x3003, 0x3004, 0x3005,
    0x8458, 0x0d80};
static const ULONG Unit[] = {0x0de0, 0x0de1, 0x0c60, 0x0c61, 0x0c62, 0x0c63};
#define UNITS 8
#define MAX_VIEWPORT 16384 /* a native MAX_VIEWPORT_DIMS */
#define BOUNDS 32768       /* a native VIEWPORT_BOUNDS_RANGE */
struct State {
    std::map<ULONG, bool> global;
    bool unit[UNITS][6];
    ULONG active;
    LONG viewport[4];
};
struct Saved {
    State state;
    ULONG mask;
};
struct Context {
    bool exists, initialized;
    ULONG drawable;
    State state;
    std::vector<Saved> stack;
    ULONG compiling, mode;
    std::map<ULONG, std::vector<std::vector<ULONG>>> lists;
};
static Context Contexts[128];
static ULONG Drawables[128][2];
static ULONG Executed, Answered;

static int UnitIndex(ULONG cap) {
    for (int i = 0; i < 6; ++i)
        if (Unit[i] == cap)
            return i;
    return -1;
}
static bool GlobalCap(ULONG cap) {
    for (ULONG g : Global)
        if (g == cap)
            return true;
    return false;
}
static void Defaults(Context &m) {
    m.state.global.clear();
    for (ULONG g : Global)
        m.state.global[g] = g == CAP_DITHER;
    memset(m.state.unit, 0, sizeof(m.state.unit));
    m.state.active = 0;
    memset(m.state.viewport, 0, sizeof(m.state.viewport));
    m.stack.clear();
    m.compiling = m.mode = 0;
    m.lists.clear();
    m.initialized = false;
}
/* The host sets the viewport when the context first runs a record. */
static void Bind(Context &m, ULONG drawable) {
    if (!m.initialized) {
        m.state.viewport[0] = m.state.viewport[1] = 0;
        m.state.viewport[2] = (LONG)Drawables[drawable][0];
        m.state.viewport[3] = (LONG)Drawables[drawable][1];
        m.initialized = true;
    }
}
static void Run(Context &m, const std::vector<ULONG> &call, ULONG depth);
static void Call(Context &m, ULONG name, ULONG depth) {
    auto list = m.lists.find(name);
    if (depth > 64 || list == m.lists.end())
        return;
    const auto calls = list->second; /* a list may redefine itself? not here */
    for (const auto &call : calls)
        Run(m, call, depth + 1);
}
static LONG Clamp(LONG value, LONG low, LONG high) {
    return value < low ? low : value > high ? high : value;
}
/* One CALL (function, arguments...) or CallLists (FEnum, count, offsets...). */
static void Run(Context &m, const std::vector<ULONG> &call, ULONG depth) {
    const ULONG f = call[0];
    State &s = m.state;
    ++Executed;
    if (f == FEnum_glEnable || f == FEnum_glDisable) {
        const bool on = f == FEnum_glEnable;
        const int unit = UnitIndex(call[1]);
        if (unit >= 0)
            s.unit[s.active][unit] = on;
        else if (GlobalCap(call[1]))
            s.global[call[1]] = on;
        /* anything else: GL_INVALID_ENUM, no change */
    } else if (f == FEnum_glActiveTexture) {
        if (call[1] - TEXTURE0 < UNITS)
            s.active = call[1] - TEXTURE0;
    } else if (f == FEnum_glViewport) {
        const LONG x = (LONG)call[1], y = (LONG)call[2], w = (LONG)call[3], h = (LONG)call[4];
        if (w < 0 || h < 0)
            return; /* GL_INVALID_VALUE */
        s.viewport[0] = Clamp(x, -BOUNDS, BOUNDS - 1);
        s.viewport[1] = Clamp(y, -BOUNDS, BOUNDS - 1);
        s.viewport[2] = Clamp(w, 0, MAX_VIEWPORT);
        s.viewport[3] = Clamp(h, 0, MAX_VIEWPORT);
    } else if (f == FEnum_glPushAttrib) {
        if (m.stack.size() < 16)
            m.stack.push_back({s, call[1]});
    } else if (f == FEnum_glPopAttrib) {
        if (m.stack.empty())
            return; /* GL_STACK_UNDERFLOW */
        const Saved saved = m.stack.back();
        m.stack.pop_back();
        if (saved.mask & ENABLE_BIT) {
            s.global = saved.state.global;
            memcpy(s.unit, saved.state.unit, sizeof(s.unit));
        }
        if (saved.mask & TEXTURE_BIT) {
            /* Every unit's texture and coordinate generation enables. */
            memcpy(s.unit, saved.state.unit, sizeof(s.unit));
            s.active = saved.state.active;
        }
        if (saved.mask & VIEWPORT_BIT)
            memcpy(s.viewport, saved.state.viewport, sizeof(s.viewport));
    } else if (f == FEnum_glCallList) {
        Call(m, call[1], depth);
    } else if (f == FEnum_glCallLists) {
        for (ULONG i = 0; i < call[1]; ++i)
            Call(m, call[2 + i], depth);
    }
}
static void Record(const ULONG *r) {
    const ULONG context = r[3], drawable = r[4];
    switch (r[0]) {
        case DG_GL_CREATE_CONTEXT:
            Contexts[context].exists = true;
            Defaults(Contexts[context]);
            return;
        case DG_GL_DESTROY_CONTEXT:
            Contexts[context].exists = false;
            return;
        case DG_GL_CREATE_DRAWABLE:
            Drawables[drawable][0] = r[8];
            Drawables[drawable][1] = r[9];
            return;
        case DG_GL_MAKE_CURRENT:
        case DG_GL_QUERY:
            Bind(Contexts[context], drawable);
            return;
        case DG_GL_CALL:
        case DG_GL_DATA_CALL: {
            Context &m = Contexts[context];
            std::vector<ULONG> call;
            Bind(m, drawable);
            if (r[0] == DG_GL_CALL) {
                call.assign(r + 8, r + r[1] / 4);
            } else if (r[8] == FEnum_glCallLists) {
                call.push_back(FEnum_glCallLists);
                call.push_back(r[10]);
                call.insert(call.end(), r + 11, r + 11 + r[10]);
            } else {
                return;
            }
            if (m.compiling) {
                m.lists[m.compiling].push_back(call);
                if (m.mode != GL_COMPILE_AND_EXECUTE)
                    return;
            }
            Run(m, call, 0);
            return;
        }
        default:
            return;
    }
}
/* The model's answer as native GL converts it, or false for an enum that is
 * no state of that query (the host's DG_GL_ERROR_UNSUPPORTED). */
static bool Value(const Context &m, ULONG function, ULONG pname, LONG *values, ULONG *count) {
    const State &s = m.state;
    if (pname == VIEWPORT && function != FEnum_glIsEnabled) {
        memcpy(values, s.viewport, sizeof(s.viewport));
        *count = 4;
        return true;
    }
    *count = 1;
    const int unit = UnitIndex(pname);
    if (unit >= 0) {
        values[0] = s.unit[s.active][unit];
        return true;
    }
    if (GlobalCap(pname)) {
        values[0] = s.global.at(pname);
        return true;
    }
    return false;
}
static BOOL Answer(const ULONG *r, DG_ESCAPE_REPLY *reply, ULONG capacity) {
    Context &m = Contexts[r[3]];
    BYTE *data = (BYTE *)(reply + 1);
    LONG values[4];
    ULONG count, kind;
    switch (r[8]) {
        case FEnum_glNewList:
            assert(!m.compiling && r[9]);
            m.compiling = r[9];
            m.mode = r[10];
            m.lists[r[9]].clear();
            memset(data, 0, 4);
            reply->ResultBytes = 4;
            reply->ResultType = DG_GL_RESULT_INT;
            return TRUE;
        case FEnum_glEndList:
            assert(m.compiling);
            m.compiling = 0;
            memset(data, 0, 4);
            reply->ResultBytes = 4;
            reply->ResultType = DG_GL_RESULT_INT;
            return TRUE;
        case FEnum_glIsEnabled:
            kind = DG_GL_RESULT_BOOL;
            break;
        case FEnum_glGetBooleanv:
            kind = DG_GL_RESULT_BOOL;
            break;
        case FEnum_glGetIntegerv:
            kind = DG_GL_RESULT_INT;
            break;
        case FEnum_glGetFloatv:
            kind = DG_GL_RESULT_FLOAT;
            break;
        case FEnum_glGetDoublev:
            kind = DG_GL_RESULT_DOUBLE;
            break;
        default:
            return FALSE; /* the fixture's own answers (GetError) */
    }
    ++Answered;
    if (!Value(m, r[8], r[9], values, &count)) {
        reply->Status = DG_ESCAPE_HOST;
        reply->DeviceError = DG_GL_ERROR_UNSUPPORTED;
        return TRUE;
    }
    for (ULONG i = 0; i < count; ++i) {
        if (kind == DG_GL_RESULT_BOOL) {
            data[i] = values[i] != 0;
        } else if (kind == DG_GL_RESULT_INT) {
            memcpy(data + i * 4, &values[i], 4);
        } else if (kind == DG_GL_RESULT_FLOAT) {
            GLfloat v = (GLfloat)values[i];
            memcpy(data + i * 4, &v, 4);
        } else {
            GLdouble v = (GLdouble)values[i];
            memcpy(data + i * 8, &v, 8);
        }
    }
    reply->ResultType = kind;
    reply->ResultBytes = count * (kind == DG_GL_RESULT_BOOL     ? 1
                                  : kind == DG_GL_RESULT_DOUBLE ? 8
                                                                : 4);
    assert(reply->ResultBytes <= capacity);
    return TRUE;
}
static void Install(void) {
    memset(Drawables, 0, sizeof(Drawables));
    for (auto &m : Contexts) {
        m.exists = false;
        Defaults(m);
    }
    SubmittedRecord = Record;
    AnswerQuery = Answer;
    /* Lists may draw to the window front: releasing a context publishes it. */
    BindCapabilities = DG_WINDOW_CAP_FRONT_ONLY;
}
} // namespace model

/* Every query form of one name: each answer equals the model's at that
 * point of the command stream. Returns how many reached the host; with none,
 * nothing was submitted either (the queued records are still queued). */
static ULONG Check(GLenum pname) {
    JGL_CONTEXT *c = CurrentContext();
    const model::Context &m = model::Contexts[c->Id];
    const ULONG before = QueryCalls, records = c->Records;
    LONG expected[4];
    ULONG count, reached;
    GLint integers[4];
    GLboolean booleans[4], enabled = 0;
    GLfloat floats[4];
    GLdouble doubles[4];
    glGetIntegerv(pname, integers);
    glGetBooleanv(pname, booleans);
    glGetFloatv(pname, floats);
    glGetDoublev(pname, doubles);
    if (pname != VIEWPORT)
        enabled = glIsEnabled(pname);
    assert(!c->Error && !c->Failed);
    reached = QueryCalls - before;
    if (!reached)
        assert(c->Records == records);
    /* Local answers were given before the queued records ran: the model
     * reaches the same point once they have. */
    assert(Flush(c));
    assert(model::Value(m, FEnum_glGetIntegerv, pname, expected, &count));
    for (ULONG i = 0; i < count; ++i) {
        assert(integers[i] == expected[i]);
        assert(booleans[i] == (expected[i] != 0));
        assert(floats[i] == (GLfloat)expected[i]);
        assert(doubles[i] == (GLdouble)expected[i]);
    }
    if (pname != VIEWPORT) {
        assert(model::Value(m, FEnum_glIsEnabled, pname, expected, &count));
        assert(enabled == (expected[0] != 0));
    }
    return reached;
}

/* Exact scripted cases: each one fails if a single invalidation is missing. */
static void Scripted(void) {
    Reset();
    model::Install();
    HostCaps = DG_CAP_GL_MULTITEXTURE;
    HGLRC context = Create();
    JGL_CONTEXT *c = CurrentContext();

    /* GL defaults: known at creation, DITHER the only enabled capability;
     * the viewport is the host's (drawable size at its first record). */
    assert(Check(CAP_DITHER) == 0 && Check(CAP_LIGHTING) == 0 && Check(0x4007) == 0 &&
           Check(0x3005) == 0 && Check(CAP_TEXTURE_2D) == 0);
    GLint viewport[4] = {-1, -1, -1, -1};
    ULONG queries = QueryCalls;
    glGetIntegerv(VIEWPORT, viewport);
    assert(QueryCalls == queries + 1 && viewport[0] == 0 && viewport[1] == 0 &&
           viewport[2] == 320 && viewport[3] == 240);
    /* A capability the frontend does not shadow, and COLOR_SUM without
     * EXT_secondary_color (never enabled here), always reach the host. */
    assert(Check(CAP_AUTO_NORMAL) == 5);
    assert(Check(CAP_COLOR_SUM) == 5);
    /* VIEWPORT is no capability: the host's GL_INVALID_ENUM, even known. */
    glViewport(1, 2, 3, 4);
    queries = QueryCalls;
    assert(!glIsEnabled(VIEWPORT) && QueryCalls == queries + 1);
    assert(glGetError() == GL_INVALID_ENUM && glGetError() == GL_NO_ERROR);
    assert(Check(VIEWPORT) == 0);

    /* Setters make values known; a query costs no submission at all. */
    glEnable(CAP_LIGHTING);
    glDisable(CAP_DITHER);
    glViewport(-5, 7, 640, 480);
    assert(Check(CAP_LIGHTING) == 0 && Check(CAP_DITHER) == 0 && Check(VIEWPORT) == 0);

    /* Inside Begin/End a query is GL_INVALID_OPERATION and writes nothing. */
    glBegin(GL_TRIANGLES);
    GLint untouched = 77;
    glGetIntegerv(CAP_LIGHTING, &untouched);
    assert(untouched == 77 && !glIsEnabled(CAP_LIGHTING));
    assert(c->Error == GL_INVALID_OPERATION);
    c->Error = 0;
    glEnd();

    /* PopAttrib (before any list: the frontend's own attribute depth). */
    glPushAttrib(ENABLE_BIT | VIEWPORT_BIT);
    glDisable(CAP_LIGHTING);
    glViewport(0, 0, 10, 10);
    assert(Check(CAP_LIGHTING) == 0 && Check(VIEWPORT) == 0);
    glPopAttrib();
    assert(Check(CAP_LIGHTING) == 5 && Check(VIEWPORT) == 4);
    assert(Check(CAP_DITHER) == 5);
    /* Underflow changes nothing and is the frontend's own error. */
    glPopAttrib();
    assert(glGetError() == 0x0504);

    /* Viewports native GL would clamp, or rejects, are unknown afterwards. */
    glViewport(0, 0, 20000, 100);
    assert(Check(VIEWPORT) == 4);
    glViewport(0, 0, 50, 60);
    assert(Check(VIEWPORT) == 0);
    glViewport(0, 0, -1, 60);
    assert(Check(VIEWPORT) == 4);
    glViewport(-40000, 0, 8, 8);
    assert(Check(VIEWPORT) == 4);
    glViewport(4096, -4096, 4096, 4096);
    assert(Check(VIEWPORT) == 0);

    /* ActiveTexture: the new unit's enables are the host's; global ones
     * stay known. */
    glEnable(CAP_TEXTURE_2D);
    glEnable(CAP_FOG);
    assert(Check(CAP_TEXTURE_2D) == 0);
    glActiveTextureARB(TEXTURE0 + 1);
    assert(Check(CAP_TEXTURE_2D) == 5 && Check(CAP_FOG) == 0);
    glEnable(CAP_TEXTURE_2D);
    assert(Check(CAP_TEXTURE_2D) == 0);
    glActiveTextureARB(TEXTURE0);
    assert(Check(CAP_TEXTURE_2D) == 5);

    /* Display lists. GL_COMPILE records but executes nothing; nothing is
     * known while a list is open, and EndList forgets everything. */
    glEnable(CAP_LIGHTING);
    assert(Check(CAP_LIGHTING) == 0);
    AliasNewList(1, GL_COMPILE);
    glDisable(CAP_LIGHTING);
    glViewport(0, 0, 11, 12);
    assert(Check(CAP_LIGHTING) == 5 && Check(VIEWPORT) == 4);
    AliasEndList();
    assert(Check(CAP_LIGHTING) == 5); /* still enabled: compiled only */
    glEnable(CAP_LIGHTING);
    glViewport(0, 0, 1, 1);
    assert(Check(CAP_LIGHTING) == 0 && Check(VIEWPORT) == 0);
    /* CallList executes state the frontend does not see. */
    AliasCallList(1);
    assert(Check(CAP_LIGHTING) == 5 && Check(VIEWPORT) == 4);
    glEnable(CAP_LIGHTING);
    assert(Check(CAP_LIGHTING) == 0);
    {
        const ULONG offsets[2] = {7, 1};
        assert(JglCallLists(2, offsets));
    }
    assert(Check(CAP_LIGHTING) == 5);
    /* GL_COMPILE_AND_EXECUTE executes as it compiles. */
    glEnable(CAP_BLEND);
    assert(Check(CAP_BLEND) == 0);
    AliasNewList(2, GL_COMPILE_AND_EXECUTE);
    glDisable(CAP_BLEND);
    assert(Check(CAP_BLEND) == 5);
    AliasEndList();
    assert(Check(CAP_BLEND) == 5);
    glEnable(CAP_BLEND);
    /* A list's PopAttrib, executed through CallList. */
    AliasNewList(3, GL_COMPILE);
    glPopAttrib();
    AliasEndList();
    glPushAttrib(ENABLE_BIT);
    glDisable(CAP_BLEND);
    assert(Check(CAP_BLEND) == 0);
    AliasCallList(3);
    assert(Check(CAP_BLEND) == 5);
    /* PopAttrib once lists are in use (the host owns the attribute depth). */
    glPushAttrib(ALL_BITS);
    glDisable(CAP_BLEND);
    glViewport(9, 9, 9, 9);
    assert(Check(CAP_BLEND) == 0 && Check(VIEWPORT) == 0);
    glPopAttrib();
    assert(Check(CAP_BLEND) == 5 && Check(VIEWPORT) == 4);

    /* Contexts never see each other's state; each starts with defaults. */
    glEnable(CAP_FOG);
    HGLRC other = wglCreateContext((HDC)2);
    assert(other && wglMakeCurrent((HDC)2, other));
    assert(Check(CAP_FOG) == 0 && Check(CAP_DITHER) == 0);
    glDisable(CAP_FOG);
    assert(Check(CAP_FOG) == 0);
    assert(wglMakeCurrent((HDC)1, context));
    assert(Check(CAP_FOG) == 0);
    GLboolean fog = 0;
    glGetBooleanv(CAP_FOG, &fog);
    assert(fog == 1);

    /* A failure another thread reported is the host query's to report. */
    glEnable(CAP_LIGHTING);
    c->DeferredFault = 1;
    queries = QueryCalls;
    assert(!glIsEnabled(CAP_LIGHTING) && c->Failed && QueryCalls == queries);
    assert(wglMakeCurrent(NULL, NULL));
}

/* EXT_secondary_color: COLOR_SUM is a known default too. */
static void Secondary(void) {
    Reset();
    model::Install();
    HostSecondaryWords = 3;
    Create();
    assert(JglSupportsSecondary());
    assert(Check(CAP_COLOR_SUM) == 0);
    glEnable(CAP_COLOR_SUM);
    assert(Check(CAP_COLOR_SUM) == 0);
    assert(wglMakeCurrent(NULL, NULL));
}

/* Random command streams over two contexts: every answer, local or not,
 * equals the model's; most known state is answered locally. */
static ULONG Seed;
static ULONG Next(ULONG n) {
    Seed = Seed * 1103515245UL + 12345UL;
    return (Seed >> 16) % n;
}
static GLenum RandomCap(void) {
    const ULONG pick = Next(sizeof(model::Global) / 4 + 6 + 1);
    if (pick < sizeof(model::Global) / 4)
        return model::Global[pick];
    if (pick < sizeof(model::Global) / 4 + 6)
        return model::Unit[pick - sizeof(model::Global) / 4];
    return VIEWPORT;
}
static void Random(ULONG seed, ULONG steps, ULONG *local, ULONG *host) {
    static const ULONG masks[] = {ENABLE_BIT, VIEWPORT_BIT, TEXTURE_BIT, ENABLE_BIT | VIEWPORT_BIT,
                                  ALL_BITS, 0x1 /* CURRENT: no shadowed state */};
    Seed = seed;
    Reset();
    model::Install();
    HostCaps = DG_CAP_GL_MULTITEXTURE;
    HostSecondaryWords = 3;
    /* Odd seeds: the queued transport (deferred release, streams). */
    HostFeatures = seed & 1 ? DG_ESCAPE_FEATURE_QUEUE : 0;
    HGLRC contexts[2] = {wglCreateContext((HDC)1), wglCreateContext((HDC)2)};
    ULONG which = 0;
    assert(contexts[0] && contexts[1] && wglMakeCurrent((HDC)1, contexts[0]));
    for (ULONG step = 0; step < steps; ++step) {
        JGL_CONTEXT *c = CurrentContext();
        const ULONG op = Next(200); /* half are queries */
        if (op < 60) {
            GLenum cap = RandomCap();
            if (cap == VIEWPORT || Next(2))
                cap = Next(2) ? CAP_LIGHTING : CAP_FOG;
            if (Next(2))
                glEnable(cap);
            else
                glDisable(cap);
        } else if (op < 66) {
            glActiveTextureARB(TEXTURE0 + Next(4));
        } else if (op < 76) {
            const ULONG kind = Next(8);
            if (kind == 0)
                glViewport(0, 0, (GLsizei)(Next(40000)), 1);
            else if (kind == 1)
                glViewport(-(GLint)Next(70000), 0, 1, 1);
            else if (kind == 2)
                glViewport(0, 0, -1, 1);
            else
                glViewport((GLint)Next(100) - 50, (GLint)Next(100), (GLsizei)Next(1024),
                           (GLsizei)Next(768));
        } else if (op < 81) {
            /* Overflow is a GL error without a change, here or natively. */
            glPushAttrib(masks[Next(sizeof(masks) / 4)]);
        } else if (op < 85) {
            glPopAttrib(); /* likewise underflow */
        } else if (op < 87) {
            if (!c->Arrays.ListMode)
                AliasNewList(1 + Next(4), Next(2) ? GL_COMPILE : GL_COMPILE_AND_EXECUTE);
        } else if (op < 89) {
            AliasEndList(); /* outside a list: GL_INVALID_OPERATION */
        } else if (op < 92) {
            /* Lists never call lists here: nested calls grow exponentially. */
            if (!c->Arrays.ListMode)
                AliasCallList(1 + Next(4));
        } else if (op < 93) {
            const ULONG offsets[2] = {1 + Next(4), 1 + Next(4)};
            if (!c->Arrays.ListMode)
                assert(JglCallLists(2, offsets));
        } else if (op < 97) {
            if (!c->Arrays.ListMode) {
                which ^= 1;
                assert(wglMakeCurrent((HDC)(ULONG_PTR)(1 + which), contexts[which]));
            }
        } else if (op < 100) {
            glBegin(GL_POINTS);
            glEnd();
        } else {
            /* Half of them the names Neverwinter Nights asks every frame. */
            static const GLenum hot[] = {CAP_LIGHTING, CAP_FOG, VIEWPORT, CAP_TEXTURE_2D};
            const GLenum pname = Next(2) ? hot[Next(4)] : RandomCap();
            const ULONG reached = Check(pname);
            *local += (pname == VIEWPORT ? 4 : 5) - reached;
            *host += reached;
        }
        c = CurrentContext();
        /* Lists stay short: a few commands each. */
        if (c->Arrays.ListMode && !Next(8))
            AliasEndList();
        assert(!c->Failed);
        c->Error = 0;
    }
    assert(wglMakeCurrent(NULL, NULL));
}

int main() {
    /* icd-lists.inc entry points this suite does not drive. */
    (void)AliasGenLists;
    (void)AliasIsList;
    (void)AliasDeleteLists;
    (void)AliasListBase;
    (void)AliasCallLists;
    Scripted();
    Secondary();
    ULONG local = 0, host = 0;
    for (ULONG seed = 1; seed <= 8; ++seed)
        Random(seed, 6000, &local, &host);
    printf("random: %lu local, %lu host answers (%lu model queries, %lu model calls)\n",
           (unsigned long)local, (unsigned long)host, (unsigned long)model::Answered,
           (unsigned long)model::Executed);
    fflush(stdout);
    assert(local > host / 4 && host > 0);
    puts("PASS local state queries: GL defaults, setters, Begin/End, PopAttrib with and "
         "without lists, clamped/rejected viewports, per-unit enables, GL_COMPILE and "
         "COMPILE_AND_EXECUTE, CallList(s), contexts and deferred faults match the native model");
    return 0;
}
