/* SPDX-License-Identifier: GPL-2.0-or-later */
#define main existing_frontend_suite_not_run
#include "frontend-harness.cpp"
#undef main
#include "arrays.cpp"
using GLvoid = void;
#include "icd-client.inc"
static void clear(JGL_CONTEXT *c) {
    assert(!c->InBegin);
    c->Used = c->Records = 0;
    c->Error = 0;
}
static const ULONG *record(JGL_CONTEXT *c, unsigned index, ULONG function,
                           std::initializer_list<float> expected) {
    const ULONG *r = c->Packet.Words;
    for (unsigned n = 0; n < index; ++n)
        r += r[1] / 4;
    assert(r[0] == DG_GL_CALL && r[8] == function && r[1] == (9 + expected.size()) * 4);
    unsigned n = 9;
    for (float value : expected) {
        float actual;
        memcpy(&actual, r + n++, 4);
        assert(actual == value);
    }
    return r;
}
int main() {
    Reset();
    HGLRC context = Create();
    JGL_CONTEXT *c = Lookup(context);
    float positions[][2] = {{1, 2}, {3, 4}};
    const unsigned char colors[][3] = {{255, 0, 127}, {0, 255, 0}};
    const signed char normals[][3] = {{-128, 0, 127}, {127, -1, -128}};
    const float tex[][1] = {{0.25f}, {0.75f}};
    glVertexPointer(2, GL_FLOAT, 0, positions);
    glColorPointer(3, GL_UNSIGNED_BYTE, 0, colors);
    glNormalPointer(GL_BYTE, 0, normals);
    glTexCoordPointer(1, GL_FLOAT, 0, tex);
    for (GLenum cap : {GL_VERTEX_ARRAY, GL_COLOR_ARRAY, GL_NORMAL_ARRAY, GL_TEXTURE_COORD_ARRAY})
        glEnableClientState(cap);
    glBegin(GL_TRIANGLES);
    AliasArrayElement(0);
    assert(c->InBegin && !c->Error && c->Records == 5);
    record(c, 1, FEnum_glColor4f, {1, 0, 127.0f / 255.0f, 1});
    record(c, 2, FEnum_glNormal3f, {-1, 1.0f / 255.0f, 1});
    record(c, 3, FEnum_glTexCoord4f, {0.25f, 0, 0, 1});
    record(c, 4, FEnum_glVertex4f, {1, 2, 0, 1});
    glEnd();
    // No vertex enable is required to update current attributes.
    clear(c);
    glDisableClientState(GL_VERTEX_ARRAY);
    AliasArrayElement(1);
    assert(c->Records == 3 && !c->Error);
    record(c, 0, FEnum_glColor4f, {0, 1, 0, 1});
    record(c, 1, FEnum_glNormal3f, {1, -1.0f / 255.0f, -1});
    // Validate every array before any setter; rejected pointer arithmetic has
    // no partial color/normal update and never dereferences a wrapped address.
    clear(c);
    glTexCoordPointer(1, GL_FLOAT, 0, nullptr);
    AliasArrayElement(0);
    assert(!c->Records && c->Error == GL_INVALID_VALUE);
    clear(c);
    glTexCoordPointer(1, GL_FLOAT, 0, (void *)(~(ULONG_PTR)0 - 1));
    AliasArrayElement(1);
    assert(!c->Records && c->Error == GL_INVALID_VALUE);
    clear(c);
    AliasArrayElement(-1);
    assert(!c->Records && c->Error == GL_INVALID_VALUE);
    clear(c);
    // Both additional descriptors participate in ArrayElement and client stacks.
    for (GLenum cap : {GL_COLOR_ARRAY, GL_NORMAL_ARRAY, GL_TEXTURE_COORD_ARRAY})
        glDisableClientState(cap);
    GLdouble index_values[] = {16777217.25, -5.5};
    GLubyte edge_values[] = {0, 7};
    glIndexPointer(GL_DOUBLE, 0, index_values);
    glEdgeFlagPointer(0, edge_values);
    glEnableClientState(GL_INDEX_ARRAY);
    glEnableClientState(GL_EDGE_FLAG_ARRAY);
    glBegin(GL_TRIANGLES);
    AliasArrayElement(0);
    glEnd();
    assert(!c->Error && c->Records == 4);
    const ULONG *r = c->Packet.Words + 10;
    assert(r[8] == FEnum_glIndexd);
    GLdouble exact;
    memcpy(&exact, r + 9, 8);
    assert(exact == index_values[0]);
    r += 11;
    assert(r[8] == FEnum_glEdgeFlag && r[9] == 0);
    clear(c);
    AliasPushClientAttrib(2);
    glDisableClientState(GL_EDGE_FLAG_ARRAY);
    AliasPopClientAttrib();
    GLint enabled = 0;
    assert(JglArrayQuery(GL_EDGE_FLAG_ARRAY, &enabled) && enabled == 1);
    assert(!c->Error);
    clear(c);
    glBegin(GL_POINTS);
    GLdouble eval = 0.25;
    JglScalarVector(FEnum_glEvalCoord1d, 2, &eval);
    GLint point = 1;
    JglScalarVector(FEnum_glEvalPoint1, 1, &point);
    glEnd();
    assert(!c->Error && c->Records == 4);
    clear(c);
    c->DrawBuffer = GL_FRONT;
    c->FrontDirty = FALSE;
    ULONG mesh[] = {0x1b00, 0, 2};
    JglScalarVector(FEnum_glEvalMesh1, 3, mesh);
    assert(c->FrontDirty);
    c->FrontDirty = FALSE;
    c->Arrays.CaptureMode = GL_FEEDBACK;
    JglScalarVector(FEnum_glEvalMesh1, 3, mesh);
    assert(!c->FrontDirty);
    clear(c);
    glBegin(GL_POINTS);
    glVertex3f(0, 0, 0);
    glEnd();
    assert(!c->FrontDirty && !c->Error);
    c->Arrays.SelectPointer = &enabled;
    c->Arrays.FeedbackPointer = &eval;
    void *pointer = nullptr;
    glGetPointerv(GL_SELECTION_BUFFER_POINTER, &pointer);
    assert(pointer == &enabled);
    glGetPointerv(GL_FEEDBACK_BUFFER_POINTER, &pointer);
    assert(pointer == &eval);
    c->Arrays.CaptureMode = GL_RENDER;
    c->DrawBuffer = GL_BACK;
    clear(c);

    alignas(float) unsigned char interleaved[256] = {};
    struct Expected {
        unsigned stride, vertex_size, vertex_offset, color_size, color_offset, normal_offset,
            tex_size;
        bool normal, ubyte;
    };
    const Expected expected[] = {
        {8, 2, 0, 0, 0, 0, 0, false, false},   {12, 3, 0, 0, 0, 0, 0, false, false},
        {12, 2, 4, 4, 0, 0, 0, false, true},   {16, 3, 4, 4, 0, 0, 0, false, true},
        {24, 3, 12, 3, 0, 0, 0, false, false}, {24, 3, 12, 0, 0, 0, 0, true, false},
        {40, 3, 28, 4, 0, 16, 0, true, false}, {20, 3, 8, 0, 0, 0, 2, false, false},
        {32, 4, 16, 0, 0, 0, 4, false, false}, {24, 3, 12, 4, 8, 0, 2, false, true},
        {32, 3, 20, 3, 8, 0, 2, false, false}, {32, 3, 20, 0, 0, 8, 2, true, false},
        {48, 3, 36, 4, 8, 24, 2, true, false}, {60, 4, 44, 4, 16, 32, 4, true, false}};
    for (unsigned n = 0; n < 14; ++n) {
        const auto &e = expected[n];
        JGL_ARRAY previous[7];
        memcpy(previous, c->Arrays.Attribute, sizeof(previous));
        AliasInterleavedArrays(0x2a20 + n, 0, interleaved);
        assert(!c->Error);
        auto *a = c->Arrays.Attribute;
        assert(a[0].Enabled && a[0].Size == (GLint)e.vertex_size &&
               a[0].Pointer == interleaved + e.vertex_offset && a[0].Stride == (GLint)e.stride);
        assert(a[1].Enabled == !!e.color_size && a[2].Enabled == e.normal &&
               a[3].Enabled == !!e.tex_size);
        for (unsigned i = 1; i < 4; ++i) {
            if (a[i].Enabled)
                assert(a[i].Stride == (GLint)e.stride);
            else {
                previous[i].Enabled = FALSE;
                assert(!memcmp(&a[i], &previous[i], sizeof(a[i])));
            }
        }
        if (e.color_size)
            assert(a[1].Size == (GLint)e.color_size &&
                   a[1].Pointer == interleaved + e.color_offset &&
                   a[1].Type == (e.ubyte ? GL_UNSIGNED_BYTE : GL_FLOAT));
        if (e.normal)
            assert(a[2].Size == 3 && a[2].Pointer == interleaved + e.normal_offset);
        if (e.tex_size)
            assert(a[3].Size == (GLint)e.tex_size && a[3].Pointer == interleaved);
        assert(!a[5].Enabled && !a[6].Enabled);
        assert(!memcmp(&a[4], &previous[4], sizeof(a[4])));
        AliasInterleavedArrays(0x2a20 + n, 71, interleaved);
        for (unsigned i = 0; i < 4; ++i)
            if (a[i].Enabled)
                assert(a[i].Stride == 71);
    }
    JGL_ARRAY saved[7];
    memcpy(saved, c->Arrays.Attribute, sizeof(saved));
    for (unsigned failure = 0; failure < 4; ++failure) {
        clear(c);
        if (failure == 0)
            AliasInterleavedArrays(0xdead, 0, interleaved);
        else if (failure == 1)
            AliasInterleavedArrays(0x2a20, -1, interleaved);
        else if (failure == 2)
            AliasInterleavedArrays(0x2a2d, 0, (void *)(~(ULONG_PTR)0 - 1));
        else {
            glBegin(GL_TRIANGLES);
            AliasInterleavedArrays(0x2a20, 0, interleaved);
            glEnd();
        }
        assert(c->Error && !memcmp(saved, c->Arrays.Attribute, sizeof(saved)));
    }
    clear(c);
    JGL_UNPACK pack = {8, 7, 6, 5, 1, 1}, unpack = {2, 11, 12, 13, 0, 1};
    *JglPack() = pack;
    *JglUnpack() = unpack;
    AliasPushClientAttrib(3);
    assert(c->Arrays.ClientDepth == 1);
    AliasInterleavedArrays(0x2a20, 0, positions);
    *JglPack() = {};
    *JglUnpack() = {};
    AliasPopClientAttrib();
    assert(!c->Error && !c->Arrays.ClientDepth &&
           !memcmp(saved, c->Arrays.Attribute, sizeof(saved)));
    assert(!memcmp(JglPack(), &pack, sizeof(pack)) &&
           !memcmp(JglUnpack(), &unpack, sizeof(unpack)));
    for (unsigned mask : {0U, 1U, 2U, 0xffffffffU}) {
        AliasPushClientAttrib(mask);
        c->Arrays.Attribute[0].Pointer = positions;
        JglPack()->Alignment = 4;
        JglUnpack()->Alignment = 4;
        AliasPopClientAttrib();
        assert(c->Arrays.Attribute[0].Pointer == (mask & 2 ? saved[0].Pointer : positions));
        assert(JglPack()->Alignment == (mask & 1 ? pack.Alignment : 4));
        memcpy(c->Arrays.Attribute, saved, sizeof(saved));
        *JglPack() = pack;
        *JglUnpack() = unpack;
    }
    // Stack depth and pointer snapshots belong to the current context only.
    AliasPushClientAttrib(3);
    HGLRC other = wglCreateContext((HDC)1);
    assert(other && wglMakeCurrent((HDC)1, other));
    assert(JglArrays()->ClientDepth == 0 && JglPack()->Alignment == 4);
    AliasPopClientAttrib();
    assert(glGetError() == 0x0504);
    assert(wglMakeCurrent((HDC)1, context));
    AliasPopClientAttrib();
    assert(!c->Arrays.ClientDepth);
    for (unsigned n = 0; n < 16; ++n)
        AliasPushClientAttrib(0);
    GLint depth = -1;
    assert(JglArrayQuery(GL_CLIENT_ATTRIB_STACK_DEPTH, &depth) && depth == 16);
    assert(JglArrayQuery(GL_MAX_CLIENT_ATTRIB_STACK_DEPTH, &depth) && depth == 16);
    AliasPushClientAttrib(3);
    assert(c->Error == 0x0503 && c->Arrays.ClientDepth == 16);
    c->Error = 0;
    glBegin(GL_TRIANGLES);
    AliasPopClientAttrib();
    assert(c->Error == GL_INVALID_OPERATION && c->Arrays.ClientDepth == 16);
    glEnd();
    clear(c);
    for (unsigned n = 0; n < 16; ++n)
        AliasPopClientAttrib();
    AliasPopClientAttrib();
    assert(c->Error == 0x0504 && !c->Arrays.ClientDepth);
    // The stack borrows arrays; edits to application memory survive pop.
    clear(c);
    AliasInterleavedArrays(0x2a20, 0, positions);
    AliasPushClientAttrib(2);
    AliasInterleavedArrays(0x2a21, 0, interleaved);
    positions[0][0] = 19;
    AliasPopClientAttrib();
    glBegin(GL_TRIANGLES);
    AliasArrayElement(0);
    record(c, 1, FEnum_glVertex4f, {19, 2, 0, 1});
    glEnd();
    AliasPushClientAttrib(3); // Context deletion discards descriptors, never borrowed arrays.
    assert(wglMakeCurrent(nullptr, nullptr));
    assert(wglDeleteContext(context) && wglDeleteContext(other));
    assert(positions[0][0] == 19);
    Reset();
    assert(!Allocations);
    puts("PASS client state: actual arrays/core;14 interleaved layouts; borrowed "
         "per-context16-entry stacks;pack/unpack;vertex-last normalization; transactional "
         "rejection; exact edge/index snapshots and client-stack restoration");
}
