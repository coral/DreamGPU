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
    // Unsupported edge/index enables reject explicitly; they never become
    // enabled-but-ignored data sources in ArrayElement or interleaved draws.
    glEnableClientState(0x8077); // INDEX_ARRAY
    assert(c->Error == GL_INVALID_ENUM);
    clear(c);
    glEnableClientState(0x8079); // EDGE_FLAG_ARRAY
    assert(c->Error == GL_INVALID_ENUM);
    clear(c);
    for (GLenum cap : {0x8077, 0x8079}) {
        GLint enabled = 1;
        glDisableClientState(cap);
        assert(!c->Error && JglArrayQuery(cap, &enabled) && enabled == 0);
    }
    for (GLenum name : {0x8091, 0x8093}) {
        void *pointer = positions;
        glGetPointerv(name, &pointer);
        assert(!c->Error && !pointer);
    }
    GLint initial = -1;
    assert(JglArrayQuery(0x8085, &initial) && initial == GL_FLOAT);
    assert(JglArrayQuery(0x8086, &initial) && initial == 0);
    assert(JglArrayQuery(0x808c, &initial) && initial == 0);

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
        JGL_ARRAY previous[5];
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
        assert(!memcmp(&a[4], &previous[4], sizeof(a[4])));
        AliasInterleavedArrays(0x2a20 + n, 71, interleaved);
        for (unsigned i = 0; i < 4; ++i)
            if (a[i].Enabled)
                assert(a[i].Stride == 71);
    }
    JGL_ARRAY saved[5];
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
         "rejection; explicit edge/index gaps");
}
