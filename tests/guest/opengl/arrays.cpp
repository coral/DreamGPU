/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "arrays.cpp"
#include "secondary.cpp"
static JGL_ARRAY_STATE States[2];
static ULONG Current, Error, Capacity, Calls, Counts[100], Modes[100], Masks[100];
static GLfloat Positions[100][1024];
static BYTE Packed[65536];
static BOOL Ready = TRUE, Stipple, Secondary = TRUE;
static GLfloat SecondaryValues[3];
BOOL JglSupportsSecondary(void) {
    return Secondary;
}
void JglScalarVector(ULONG fn, ULONG words, const void *p) {
    assert(fn == FEnum_glSecondaryColor3f && words == 3);
    memcpy(SecondaryValues, p, 12);
}
BOOL JglQuery(ULONG fn, const ULONG *args, ULONG type, void *out, ULONG capacity, ULONG *bytes) {
    assert(fn == FEnum_glIsEnabled && args[0] == GL_LINE_STIPPLE && type == DG_GL_RESULT_BOOL &&
           capacity == 1);
    *(GLboolean *)out = Stipple;
    *bytes = 1;
    return TRUE;
}
BOOL JglReady(void) {
    return Ready;
}
JGL_ARRAY_STATE *JglArrays(void) {
    return &States[Current];
}
void JglSetError(GLenum error) {
    if (!Error)
        Error = error;
}
ULONG JglMaxDataBytes(ULONG words) {
    assert(words == 4);
    return Capacity * 64;
}
BOOL JglData(ULONG fn, const ULONG *args, ULONG words, const void *payload, ULONG bytes) {
    ULONG i, stride = DG_GL_VERTEX_SIZE(args[3]);
    const BYTE *p = (const BYTE *)payload;
    assert(fn == FEnum_glDrawArrays && words == 4 && args[1] == 0);
    assert(bytes == args[2] * stride && bytes <= Capacity * 64 && Calls < 100);
    Counts[Calls] = args[2];
    Modes[Calls] = args[0];
    Masks[Calls] = args[3];
    for (i = 0; i < args[2]; ++i) {
        ULONG reserved;
        memcpy(&Positions[Calls][i], p + i * stride, 4);
        memcpy(&reserved, p + i * stride + 60, 4);
        assert(!reserved);
    }
    memcpy(Packed, p, bytes);
    ++Calls;
    return TRUE;
}
static void Reset(void) {
    ULONG i, j;
    memset(States, 0, sizeof(States));
    Current = Error = Calls = 0;
    Ready = TRUE;
    Capacity = 1023;
    for (j = 0; j < 2; ++j)
        for (i = 0; i < 5; ++i) {
            States[j].Attribute[i].Size = i == 2 || i == 4 ? 3 : 4;
            States[j].Attribute[i].Type = GL_FLOAT;
        }
}
static GLfloat Value(ULONG vertex, ULONG offset) {
    GLfloat value;
    memcpy(&value, Packed + vertex * DG_GL_VERTEX_SIZE(Masks[Calls - 1]) + offset, 4);
    return value;
}
int main(void) {
    GLfloat vertices[2100][3];
    ULONG i;
    GLint value;
    void *pointer;
    const GLubyte colors[3][4] = {{255, 0, 128, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}};
    const short normals[3][3] = {{-32768, 0, 32767}, {0, 0, 32767}, {0, 0, 32767}};
    const GLushort indices[] = {2, 0, 1};
    for (i = 0; i < 2100; ++i) {
        vertices[i][0] = (GLfloat)i;
        vertices[i][1] = 0;
        vertices[i][2] = 0;
    }
    Reset();
    glVertexPointer(3, GL_FLOAT, 0, vertices);
    glEnableClientState(GL_VERTEX_ARRAY);
    assert(JglArrayQuery(GL_VERTEX_ARRAY_SIZE, &value) && value == 3);
    glGetPointerv(GL_VERTEX_ARRAY_POINTER, &pointer);
    assert(pointer == vertices);
    Current = 1;
    assert(JglArrayQuery(GL_VERTEX_ARRAY_SIZE, &value) && value == 4);
    glGetPointerv(GL_VERTEX_ARRAY_POINTER, &pointer);
    assert(!pointer);
    Current = 0;
    glDrawArrays(GL_TRIANGLES, 1, 3);
    assert(Calls == 1 && Positions[0][0] == 1 && Positions[0][2] == 3);
    assert(Value(0, 12) == 1 && Masks[0] == DG_GL_ARRAY_POSITION);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, colors);
    glEnableClientState(GL_COLOR_ARRAY);
    glNormalPointer(GL_SHORT, 0, normals);
    glEnableClientState(GL_NORMAL_ARRAY);
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, indices);
    assert(Positions[1][0] == 2 && Positions[1][1] == 0 && Positions[1][2] == 1);
    assert(Value(0, 24) == 1 && Value(1, 16) == 1 && Value(1, 32) == -1 && Value(1, 40) == 1);
    assert(Value(1, 24) > 0.5019f && Value(1, 24) < 0.5020f);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    Calls = 0;
    Capacity = 5;
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 11);
    assert(Calls == 4 && Counts[0] == 4 && Counts[1] == 4 && Counts[2] == 4 && Counts[3] == 5);
    assert(Positions[1][0] == 2 && Positions[2][0] == 4 && Positions[3][0] == 6 &&
           Positions[3][4] == 10);
    Calls = 0;
    glDrawArrays(GL_TRIANGLE_FAN, 0, 11);
    assert(Calls == 3 && Positions[1][0] == 0 && Positions[1][1] == 4 && Positions[1][2] == 5);
    assert(Positions[2][0] == 0 && Positions[2][1] == 7 && Positions[2][4] == 10);
    Calls = 0;
    glDrawArrays(GL_TRIANGLES, 0, 11);
    assert(Calls == 3 && Counts[0] == 3 && Counts[1] == 3 && Counts[2] == 5);
    Calls = 0;
    glDrawArrays(GL_LINE_STRIP, 0, 11);
    assert(Calls == 3 && Positions[1][0] == 4 && Positions[2][0] == 8);
    Calls = 0;
    Stipple = TRUE;
    glDrawArrays(GL_LINE_STRIP, 0, 11);
    assert(!Calls && Error == GL_OUT_OF_MEMORY);
    Stipple = FALSE;
    Error = 0;
    Calls = 0;
    glDrawArrays(GL_POLYGON, 0, 11);
    assert(!Calls && Error == GL_OUT_OF_MEMORY);
    Error = 0;
    glVertexPointer(1, GL_FLOAT, 0, vertices);
    assert(Error == GL_INVALID_VALUE && States[0].Attribute[0].Size == 3);
    Error = 0;
    glVertexPointer(3, GL_UNSIGNED_BYTE, 0, vertices);
    assert(Error == GL_INVALID_ENUM && States[0].Attribute[0].Type == GL_FLOAT);
    Error = 0;
    glVertexPointer(3, GL_FLOAT, 0, (void *)(~(ULONG_PTR)0 - 8));
    glDrawArrays(GL_TRIANGLES, 0, 3);
    assert(!Calls && Error == GL_INVALID_VALUE);
    Error = 0;
    glVertexPointer(3, GL_FLOAT, 0, vertices);
    glDrawElements(GL_TRIANGLES, 3, GL_FLOAT, indices);
    assert(!Calls && Error == GL_INVALID_ENUM);
    Error = 0;
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, NULL);
    assert(!Calls && Error == GL_INVALID_VALUE);
    Error = 0;
    glDrawArrays(GL_TRIANGLES, 0, 0);
    assert(!Calls && !Error);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    assert(!Calls && !Error);
    glEnableClientState(GL_VERTEX_ARRAY);
    Ready = FALSE;
    glDrawArrays(GL_TRIANGLES, 0, 3);
    assert(!Calls);
    Ready = TRUE;
    Capacity = 1023;
    glDrawArrays(GL_TRIANGLES, 0, 2100);
    assert(Calls == 3 && Counts[0] == 1023 && Counts[1] == 1023 && Counts[2] == 54);
    assert(!Error);
    Reset();
    glVertexPointer(3, GL_FLOAT, 0, vertices);
    glEnableClientState(GL_VERTEX_ARRAY);
    glSecondaryColorPointerEXT(3, GL_UNSIGNED_BYTE, 4, colors);
    glEnableClientState(0x845e);
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, indices);
    assert(Masks[0] == (DG_GL_ARRAY_POSITION | DG_GL_ARRAY_SECONDARY));
    assert(Value(0, 72) == 1 && Value(1, 64) == 1 && Value(1, 76) == 0);
    assert(JglArrayQuery(0x845a, &value) && value == 3);
    glGetPointerv(0x845d, &pointer);
    assert(pointer == colors);
    Calls = 0;
    Capacity = 7;
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 3);
    assert(Calls == 1 && Counts[0] == 3);
    Calls = 0;
    glSecondaryColorPointerEXT(3, GL_FLOAT, 0, vertices);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 11);
    assert(Calls == 4 && Counts[0] == 4 && Counts[3] == 5 && Positions[3][0] == 6);
    assert(Value(4, 64) == 10 && Value(4, 76) == 0);
    glSecondaryColorPointerEXT(4, GL_FLOAT, 0, vertices);
    assert(Error == GL_INVALID_VALUE);
    Error = 0;
    glSecondaryColorPointerEXT(3, GL_FLOAT, 0, (void *)(~(ULONG_PTR)0 - 8));
    Calls = 0;
    glDrawArrays(GL_TRIANGLES, 0, 3);
    assert(!Calls && Error == GL_INVALID_VALUE);
    Error = 0;
    Secondary = FALSE;
    glEnableClientState(0x845e);
    assert(Error == GL_INVALID_ENUM);
    Error = 0;
    Secondary = TRUE;
    glSecondaryColor3ubEXT(255, 0, 128);
    assert(SecondaryValues[0] == 1 && SecondaryValues[1] == 0 && SecondaryValues[2] > .5019f);
    glSecondaryColor3sEXT(-32768, 0, 32767);
    assert(SecondaryValues[0] == -1 && SecondaryValues[2] == 1);
    {
        const GLuint values[] = {0, 0xffffffffU, 0x80000000U};
        glSecondaryColor3uivEXT(values);
        assert(SecondaryValues[0] == 0 && SecondaryValues[1] == 1 && SecondaryValues[2] == .5f);
    }
    glSecondaryColor3dEXT(.25, .5, .75);
    assert(SecondaryValues[0] == .25f && SecondaryValues[2] == .75f);
    puts("PASS actual client arrays: local state, indexed/strided snapshots, normalization, "
         "topology across bounded packets, invalid ranges/types, zero draws");
    return 0;
}
