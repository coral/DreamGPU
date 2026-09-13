/* SPDX-License-Identifier: GPL-2.0-or-later
 * GL1.1 client attribute stack: specification section6.1.11/table6.2.
 * https://registry.khronos.org/OpenGL/specs/gl/glspec11.pdf
 * No client address or client stack mutation crosses the transport.
 */
static void APIENTRY AliasArrayElement(GLint index) {
    JglArrayElement(index);
}
static void APIENTRY AliasInterleavedArrays(GLenum format, GLsizei stride, const GLvoid *pointer) {
    JglInterleavedArrays(format, stride, pointer);
}
static void APIENTRY AliasPushClientAttrib(GLbitfield mask) {
    if (!JglReady())
        return;
    JGL_ARRAY_STATE *state = JglArrays();
    if (state->ClientDepth == 16) {
        JglSetError(0x0503); // GL_STACK_OVERFLOW
        return;
    }
    JGL_CLIENT_SNAPSHOT &entry = state->ClientStack[state->ClientDepth];
    entry.Mask = mask;
    if (mask & 2) // GL_CLIENT_VERTEX_ARRAY_BIT
        CopyMemory(entry.Attribute, state->Attribute, sizeof(entry.Attribute));
    if (mask & 1) { // GL_CLIENT_PIXEL_STORE_BIT
        entry.Pack = *JglPack();
        entry.Unpack = *JglUnpack();
    }
    ++state->ClientDepth;
}
static void APIENTRY AliasPopClientAttrib(void) {
    if (!JglReady())
        return;
    JGL_ARRAY_STATE *state = JglArrays();
    if (!state->ClientDepth) {
        JglSetError(0x0504); // GL_STACK_UNDERFLOW
        return;
    }
    JGL_CLIENT_SNAPSHOT &entry = state->ClientStack[state->ClientDepth - 1];
    if (entry.Mask & 2)
        CopyMemory(state->Attribute, entry.Attribute, sizeof(entry.Attribute));
    if (entry.Mask & 1) {
        *JglPack() = entry.Pack;
        *JglUnpack() = entry.Unpack;
    }
    // Borrowed pointers have no destructor. Clear the dead entry nevertheless
    // so context snapshots retain only live client descriptor references.
    ZeroMemory(&entry, sizeof(entry));
    --state->ClientDepth;
}
