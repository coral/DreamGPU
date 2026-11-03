/* SPDX-License-Identifier: GPL-2.0-or-later
 * ARB_multitexture through the actual frontend, client arrays, queries, client
 * stack, procedure lookup and coordinate wrappers on the strict transport. */
#define main existing_frontend_suite_not_run
#include "frontend-harness.cpp"
#undef main
#include "arrays.cpp"
#include "query.cpp"
#include "multitexture.cpp"
using GLvoid = void;
#include "icd-client.inc"
typedef void (*PROC)(void);
#define GL_FOG_INDEX 0x0b61
#define GL_FOG_DENSITY 0x0b62
#define GL_FOG_START 0x0b63
#define GL_FOG_END 0x0b64
#define GL_FOG_MODE 0x0b65
#define GL_FOG_COLOR 0x0b66
static void Marker(void) {}
extern "C" PROC GetProcAddress(HINSTANCE, LPCSTR name) {
    return name[0] == 'g' && name[1] == 'l' ? Marker : nullptr;
}
#include "compatibility.cpp"

#define TEXTURE0 0x84c0U
#define TEXTURE_GEN_S 0x0c60U
#define BLEND 0x0be2U
static ULONG Bits(GLfloat value) {
    ULONG bits;
    memcpy(&bits, &value, 4);
    return bits;
}
static void Clear(JGL_CONTEXT *c) {
    assert(!c->InBegin);
    c->Used = c->Records = 0;
    c->Error = 0;
}
static const ULONG *Nth(JGL_CONTEXT *c, unsigned index) {
    const ULONG *r = c->Packet.Words;
    for (unsigned n = 0; n < index; ++n)
        r += r[1] / 4;
    return r;
}
static void Expect(JGL_CONTEXT *c, unsigned index, ULONG function,
                   std::initializer_list<ULONG> words) {
    const ULONG *r = Nth(c, index);
    assert(r[0] == DG_GL_CALL && r[8] == function && r[1] == (9 + words.size()) * 4);
    unsigned n = 9;
    for (ULONG word : words)
        assert(r[n++] == word);
}

/* Every coordinate variant under both names: canonical words and defaults. */
static const GLfloat F[4] = {.5f, -.25f, 2, 3};
static const GLdouble D[4] = {.5, -.25, 2, 3};
static const GLint I[4] = {-7, 8, 2147483647, -3};
static const GLshort S[4] = {-32768, 32767, 1, -1};
struct Variant {
    void (*core)(void), (*arb)(void);
    ULONG count;
    char kind;
};
#define VARIANT(n, k, ...)                                                                         \
    {[] { glMultiTexCoord##n##k(__VA_ARGS__); }, [] { glMultiTexCoord##n##k##ARB(__VA_ARGS__); },  \
     n, #k[0]}
static const Variant Variants[] = {
    VARIANT(1, f, TEXTURE0 + 1, F[0]),
    VARIANT(2, f, TEXTURE0 + 2, F[0], F[1]),
    VARIANT(3, f, TEXTURE0 + 3, F[0], F[1], F[2]),
    VARIANT(4, f, TEXTURE0 + 4, F[0], F[1], F[2], F[3]),
    VARIANT(1, fv, TEXTURE0 + 1, F),
    VARIANT(2, fv, TEXTURE0 + 2, F),
    VARIANT(3, fv, TEXTURE0 + 3, F),
    VARIANT(4, fv, TEXTURE0 + 4, F),
    VARIANT(1, d, TEXTURE0 + 1, D[0]),
    VARIANT(2, d, TEXTURE0 + 2, D[0], D[1]),
    VARIANT(3, d, TEXTURE0 + 3, D[0], D[1], D[2]),
    VARIANT(4, d, TEXTURE0 + 4, D[0], D[1], D[2], D[3]),
    VARIANT(1, dv, TEXTURE0 + 1, D),
    VARIANT(2, dv, TEXTURE0 + 2, D),
    VARIANT(3, dv, TEXTURE0 + 3, D),
    VARIANT(4, dv, TEXTURE0 + 4, D),
    VARIANT(1, i, TEXTURE0 + 1, I[0]),
    VARIANT(2, i, TEXTURE0 + 2, I[0], I[1]),
    VARIANT(3, i, TEXTURE0 + 3, I[0], I[1], I[2]),
    VARIANT(4, i, TEXTURE0 + 4, I[0], I[1], I[2], I[3]),
    VARIANT(1, iv, TEXTURE0 + 1, I),
    VARIANT(2, iv, TEXTURE0 + 2, I),
    VARIANT(3, iv, TEXTURE0 + 3, I),
    VARIANT(4, iv, TEXTURE0 + 4, I),
    VARIANT(1, s, TEXTURE0 + 1, S[0]),
    VARIANT(2, s, TEXTURE0 + 2, S[0], S[1]),
    VARIANT(3, s, TEXTURE0 + 3, S[0], S[1], S[2]),
    VARIANT(4, s, TEXTURE0 + 4, S[0], S[1], S[2], S[3]),
    VARIANT(1, sv, TEXTURE0 + 1, S),
    VARIANT(2, sv, TEXTURE0 + 2, S),
    VARIANT(3, sv, TEXTURE0 + 3, S),
    VARIANT(4, sv, TEXTURE0 + 4, S),
};
#undef VARIANT

int main() {
    const GLfloat two[2] = {.5f, .25f};
    GLint value = 0;

    /* Without DG_CAP_GL_MULTITEXTURE nothing new reaches the host: the entry
     * points fail locally, lookup hides them and draws keep the legacy record. */
    Reset();
    HGLRC context = Create();
    JGL_CONTEXT *c = Lookup(context);
    assert(!JglMultitextureAvailable());
    Clear(c);
    glActiveTextureARB(TEXTURE0 + 1);
    glActiveTexture(TEXTURE0);
    glMultiTexCoord4fARB(TEXTURE0, 1, 2, 3, 4);
    glMultiTexCoord2fvARB(TEXTURE0, two);
    glMultiTexCoord2svARB(TEXTURE0, S);
    glMultiTexCoord1d(TEXTURE0, 1);
    assert(!c->Records && !c->Used && c->Error == GL_INVALID_OPERATION);
    c->Error = 0;
    glClientActiveTextureARB(TEXTURE0 + 1);
    assert(c->Error == GL_INVALID_OPERATION && !c->Arrays.ClientTexture);
    c->Error = 0;
    assert(!strcmp((const char *)glGetString(GL_EXTENSIONS), ""));
    for (const char *name : {"glActiveTexture", "glActiveTextureARB", "glClientActiveTextureARB",
                             "glMultiTexCoord4fARB", "glMultiTexCoord2svARB", "glMultiTexCoord1d"})
        assert(!wglGetProcAddress(name));
    assert(wglGetProcAddress("glBegin") == Marker && wglGetProcAddress("glActiveTextur") == Marker);
    {
        // Not a local client enum here: the host validates it as before.
        ULONG queries = QueryCalls;
        glGetIntegerv(0x84e1, &value);
        assert(QueryCalls == queries + 1 && LastQueryFunction == FEnum_glGetIntegerv &&
               LastQueryArgument == 0x84e1);
    }
    {
        const GLfloat positions[3][2] = {{0, 0}, {1, 0}, {0, 1}};
        glVertexPointer(2, GL_FLOAT, 0, positions);
        glEnableClientState(GL_VERTEX_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, 0, positions);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        Clear(c);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        const ULONG *r = Nth(c, 0);
        assert(c->Records == 1 && r[0] == DG_GL_DATA_CALL && r[8] == FEnum_glDrawArrays &&
               r[13] == (DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION | DG_GL_ARRAY_TEXCOORD) &&
               r[9] == 28 + 24 + 24);
        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }
    // Inside a list the existing deferred-error record carries the rejection.
    assert(JglListMode(GL_COMPILE));
    Clear(c);
    glActiveTextureARB(TEXTURE0 + 1);
    assert(c->Records == 1 && !c->Error);
    Expect(c, 0, DG_GL_RECORD_ERROR, {GL_INVALID_OPERATION});
    assert(JglListMode(0));
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext(context));

    /* Negotiated: the full contract. */
    Reset();
    HostCaps = DG_CAP_GL_MULTITEXTURE;
    HostSecondaryWords = 3;
    BindCapabilities = DG_WINDOW_CAP_FRONT_ONLY;
    context = Create();
    c = Lookup(context);
    assert(JglMultitextureAvailable());
    assert(!strcmp((const char *)glGetString(GL_EXTENSIONS),
                   "GL_ARB_multitexture GL_ARB_texture_env_combine GL_EXT_texture_env_combine "
                   "GL_ARB_texture_env_dot3 GL_EXT_secondary_color "
                   "GL_EXT_separate_specular_color"));
    assert(!strcmp((const char *)glGetString(GL_VERSION), "1.1 DreamGPU"));
    for (const char *name : {"glActiveTexture", "glActiveTextureARB", "glClientActiveTextureARB",
                             "glMultiTexCoord4fARB", "glMultiTexCoord2svARB"})
        assert(wglGetProcAddress(name) == Marker);

    // ActiveTexture: one word; the unit range is local; illegal inside Begin.
    Clear(c);
    glActiveTextureARB(TEXTURE0 - 1);
    glActiveTexture(TEXTURE0 + 8);
    assert(!c->Records && c->Error == GL_INVALID_ENUM);
    Clear(c);
    glActiveTexture(TEXTURE0 + 7);
    glActiveTextureARB(TEXTURE0);
    assert(c->Records == 2 && !c->Error);
    Expect(c, 0, FEnum_glActiveTexture, {TEXTURE0 + 7});
    Expect(c, 1, FEnum_glActiveTexture, {TEXTURE0});
    Clear(c);
    glBegin(GL_TRIANGLES);
    glActiveTextureARB(TEXTURE0 + 1);
    assert(c->Error == GL_INVALID_OPERATION && c->Records == 1);
    c->Error = 0;
    glMultiTexCoord2fARB(TEXTURE0 + 1, .5f, .25f);
    glMultiTexCoord2fARB(TEXTURE0 + 8, .5f, .25f);
    assert(c->Error == GL_INVALID_ENUM && c->Records == 2);
    c->Error = 0;
    glEnd();
    Expect(c, 1, FEnum_glMultiTexCoord4f, {TEXTURE0 + 1, Bits(.5f), Bits(.25f), 0, Bits(1)});

    // Every variant and both names: one five-word record, exact bits/defaults.
    for (const Variant &v : Variants) {
        ULONG expected[5] = {TEXTURE0 + v.count, 0, 0, 0, Bits(1)};
        for (ULONG n = 0; n < v.count; ++n)
            expected[1 + n] = Bits(v.kind == 'f'   ? F[n]
                                   : v.kind == 'd' ? (GLfloat)D[n]
                                   : v.kind == 'i' ? (GLfloat)I[n]
                                                   : (GLfloat)S[n]);
        for (void (*call)(void) : {v.core, v.arb}) {
            Clear(c);
            call();
            assert(c->Records == 1 && !c->Error);
            Expect(c, 0, FEnum_glMultiTexCoord4f,
                   {expected[0], expected[1], expected[2], expected[3], expected[4]});
        }
    }
    {
        // Vector bits are never loaded; null vectors and bad units send nothing.
        const ULONG nan[4] = {0x7f800001, 0xff800123, 0x00000001, 0x80000000};
        Clear(c);
        glMultiTexCoord4fvARB(TEXTURE0 + 7, (const GLfloat *)nan);
        Expect(c, 0, FEnum_glMultiTexCoord4f, {TEXTURE0 + 7, nan[0], nan[1], nan[2], nan[3]});
        Clear(c);
        glMultiTexCoord2fv(TEXTURE0, NULL);
        glMultiTexCoord3dvARB(TEXTURE0, NULL);
        glMultiTexCoord4svARB(TEXTURE0, NULL);
        glMultiTexCoord1iv(TEXTURE0, NULL);
        assert(!c->Records && !c->Error);
        glMultiTexCoord2fvARB(TEXTURE0 + 8, two);
        assert(!c->Records && c->Error == GL_INVALID_ENUM);
        Clear(c);
        glMultiTexCoord1s(0, 1);
        assert(!c->Records && c->Error == GL_INVALID_ENUM);
    }
    // Lists are recorded on the host; local errors become deferred records.
    assert(JglListMode(GL_COMPILE));
    Clear(c);
    glActiveTextureARB(TEXTURE0 + 9);
    glActiveTextureARB(TEXTURE0 + 1);
    glBegin(GL_TRIANGLES);
    glMultiTexCoord1fARB(TEXTURE0 + 1, .5f);
    glEnd();
    assert(c->Records == 5 && !c->Error);
    Expect(c, 0, DG_GL_RECORD_ERROR, {GL_INVALID_ENUM});
    Expect(c, 1, FEnum_glActiveTexture, {TEXTURE0 + 1});
    Expect(c, 3, FEnum_glMultiTexCoord4f, {TEXTURE0 + 1, Bits(.5f), 0, 0, Bits(1)});
    assert(JglListMode(0));

    /* Texture enables and coordinate generation are per unit: a cached value
     * of one unit is never applied to another; global enables stay cached. */
    Clear(c);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_TEXTURE_2D);
    glActiveTextureARB(TEXTURE0 + 1);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_TEXTURE_2D);
    glEnable(BLEND);
    glActiveTextureARB(TEXTURE0);
    glEnable(BLEND);
    glEnable(GL_TEXTURE_2D);
    glEnable(TEXTURE_GEN_S);
    glActiveTextureARB(TEXTURE0 + 8);
    glEnable(TEXTURE_GEN_S);
    glEnable(GL_TEXTURE_2D);
    assert(c->Error == GL_INVALID_ENUM);
    c->Error = 0;
    glActiveTexture(TEXTURE0 + 1);
    glEnable(TEXTURE_GEN_S);
    glEnable(GL_TEXTURE_1D);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_TEXTURE_2D);
    assert(c->Records == 11 && !c->Error);
    Expect(c, 0, FEnum_glDisable, {GL_TEXTURE_2D});
    Expect(c, 1, FEnum_glActiveTexture, {TEXTURE0 + 1});
    Expect(c, 2, FEnum_glDisable, {GL_TEXTURE_2D});
    Expect(c, 3, FEnum_glEnable, {BLEND});
    Expect(c, 4, FEnum_glActiveTexture, {TEXTURE0});
    Expect(c, 5, FEnum_glEnable, {GL_TEXTURE_2D});
    Expect(c, 6, FEnum_glEnable, {TEXTURE_GEN_S});
    Expect(c, 7, FEnum_glActiveTexture, {TEXTURE0 + 1});
    Expect(c, 8, FEnum_glEnable, {TEXTURE_GEN_S});
    Expect(c, 9, FEnum_glEnable, {GL_TEXTURE_1D});
    Expect(c, 10, FEnum_glEnable, {GL_TEXTURE_2D});
    {
        // The per-unit server state is answered by the host on every query;
        // ACTIVE_TEXTURE/MAX_TEXTURE_UNITS too. CLIENT_ACTIVE_TEXTURE is local.
        ULONG queries = QueryCalls;
        HostEnabled = 1;
        assert(glIsEnabled(GL_TEXTURE_2D) && LastQueryFunction == FEnum_glIsEnabled &&
               LastQueryArgument == GL_TEXTURE_2D);
        HostEnabled = 0;
        assert(!glIsEnabled(GL_TEXTURE_2D) && QueryCalls == queries + 2);
        HostInteger = DG_GL_MAX_TEXTURE_UNITS;
        glGetIntegerv(0x84e2, &value);
        assert(value == DG_GL_MAX_TEXTURE_UNITS && LastQueryArgument == 0x84e2);
        HostInteger = TEXTURE0 + 1;
        glGetIntegerv(0x84e0, &value);
        assert(value == (GLint)TEXTURE0 + 1 && LastQueryArgument == 0x84e0);
        queries = QueryCalls;
        glClientActiveTextureARB(TEXTURE0 + 5);
        GLfloat number = 0;
        GLboolean flag = 0;
        glGetIntegerv(0x84e1, &value);
        glGetFloatv(0x84e1, &number);
        glGetBooleanv(0x84e1, &flag);
        assert(value == (GLint)TEXTURE0 + 5 && number == (GLfloat)(TEXTURE0 + 5) && flag);
        assert(QueryCalls == queries && !c->Records);
        glClientActiveTexture(TEXTURE0);
    }

    {
        /* Client arrays per unit: Push/PopClientAttrib restores each unit and
         * the client-active selector; the pixel-store group leaves both. */
        const GLfloat positions[4][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}};
        const GLfloat uv0[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
        const GLshort uv1[4][3] = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}, {10, 11, 12}};
        const GLfloat uv7[4] = {.125f, .25f, .375f, .5f};
        glVertexPointer(3, GL_FLOAT, 0, positions);
        glEnableClientState(GL_VERTEX_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, 0, uv0);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glClientActiveTextureARB(TEXTURE0 + 1);
        glTexCoordPointer(3, GL_SHORT, 0, uv1);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glClientActiveTextureARB(TEXTURE0 + 7);
        glTexCoordPointer(1, GL_FLOAT, 0, uv7);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        assert(glIsEnabled(GL_TEXTURE_COORD_ARRAY));
        JGL_ARRAY saved[JGL_ARRAY_SLOTS];
        memcpy(saved, c->Arrays.Attribute, sizeof(saved));
        AliasPushClientAttrib(2); // CLIENT_VERTEX_ARRAY_BIT
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(4, GL_FLOAT, 0, positions);
        glClientActiveTexture(TEXTURE0 + 1);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glClientActiveTexture(TEXTURE0);
        glTexCoordPointer(2, GL_SHORT, 0, uv1);
        assert(glIsEnabled(GL_TEXTURE_COORD_ARRAY) && c->Arrays.Attribute[3].Pointer == uv1);
        AliasInterleavedArrays(0x2a27 /* GL_T2F_V3F */, 0, positions);
        assert(c->Arrays.Attribute[3].Pointer == positions && !c->Arrays.Attribute[13].Enabled);
        AliasPopClientAttrib();
        assert(!memcmp(saved, c->Arrays.Attribute, sizeof(saved)));
        assert(c->Arrays.ClientTexture == 7 && glIsEnabled(GL_TEXTURE_COORD_ARRAY));
        AliasPushClientAttrib(1); // CLIENT_PIXEL_STORE_BIT
        glClientActiveTexture(TEXTURE0 + 2);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        AliasPopClientAttrib();
        assert(c->Arrays.ClientTexture == 2 && c->Arrays.Attribute[8].Enabled);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        assert(!c->Error);

        /* Draws through the real DATA path: DrawArraysEXT with sixteen
         * descriptors, front-buffer publication, zero padding (fixture). */
        glDrawBuffer(GL_FRONT);
        Clear(c);
        c->FrontDirty = FALSE;
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        const ULONG *r = Nth(c, 0);
        const ULONG mask = DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION | DG_GL_ARRAY_TEXCOORD |
                           DG_GL_ARRAY_TEXCOORD1 | DG_GL_ARRAY_TEXCOORD1 << 6;
        // 64 descriptors; positions 64+48; uv0 112+32; uv1 144+24; uv7 168+16.
        assert(c->Records == 1 && r[0] == DG_GL_DATA_CALL && r[8] == FEnum_glDrawArraysEXT);
        assert(r[9] == 184 && r[1] == (8 + 2 + 4 + 184 / 4) * 4);
        assert(r[10] == GL_TRIANGLE_STRIP && !r[11] && r[12] == 4 && r[13] == mask);
        const ULONG descriptors[16] = {GL_FLOAT | 3U << 16,
                                       0,
                                       0,
                                       GL_FLOAT | 2U << 16,
                                       0,
                                       0,
                                       0,
                                       GL_SHORT | 3U << 16,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       GL_FLOAT | 1U << 16,
                                       0,
                                       0};
        const BYTE *payload = (const BYTE *)(r + 14);
        assert(!memcmp(payload, descriptors, 64) && !memcmp(payload + 64, positions, 48) &&
               !memcmp(payload + 112, uv0, 32) && !memcmp(payload + 144, uv1, 24) &&
               !memcmp(payload + 168, uv7, 16));
        assert(c->FrontDirty && !c->Error);
        // The fixture checks the submitted record framing and zero tail.
        glFlush();
        assert(!c->Failed && !c->Records && !c->FrontDirty);
        // Each unit's descriptor follows its own client array.
        glDrawBuffer(GL_BACK);
        glDrawElements(GL_POINTS, 1, GL_UNSIGNED_BYTE, "\3");
        r = Nth(c, 1);
        assert(c->Records == 2 && r[8] == FEnum_glDrawArraysEXT && r[12] == 1 && !c->FrontDirty);
        payload = (const BYTE *)(r + 14);
        assert(!memcmp(payload + 64, positions[3], 12) && !memcmp(payload + 76, uv0[3], 8) &&
               !memcmp(payload + 84, uv1[3], 6) && !memcmp(payload + 92, uv7 + 3, 4));
        // ArrayElement inside Begin: unit 0 TexCoord4f, units 1/7 MultiTexCoord4f.
        Clear(c);
        glBegin(GL_POINTS);
        AliasArrayElement(2);
        glEnd();
        assert(c->Records == 6 && !c->Error);
        Expect(c, 1, FEnum_glTexCoord4f, {0, Bits(1), 0, Bits(1)});
        Expect(c, 2, FEnum_glMultiTexCoord4f, {TEXTURE0 + 1, Bits(7), Bits(8), Bits(9), Bits(1)});
        Expect(c, 3, FEnum_glMultiTexCoord4f, {TEXTURE0 + 7, Bits(.375f), 0, 0, Bits(1)});
        Expect(c, 4, FEnum_glVertex4f, {0, Bits(1), 0, Bits(1)});
        // Unit 0 alone returns to the unchanged legacy record.
        for (GLenum unit : {TEXTURE0 + 1, TEXTURE0 + 7}) {
            glClientActiveTexture(unit);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        }
        Clear(c);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        r = Nth(c, 0);
        assert(r[8] == FEnum_glDrawArrays && r[9] == 28 + 48 + 32 &&
               r[13] == (DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION | DG_GL_ARRAY_TEXCOORD));
        glFlush();
        assert(!c->Failed && !c->Error);
    }
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext(context));
    BindCapabilities = 0;
    Reset();
    assert(!Allocations);
    puts("PASS multitexture actual frontend: unnegotiated hosts unchanged; unit range/Begin "
         "errors; all 64 coordinate entry points exact; per-unit enable cache; host-only "
         "unit state; per-unit client arrays/stack; exact DrawArraysEXT and ArrayElement");
    return 0;
}
