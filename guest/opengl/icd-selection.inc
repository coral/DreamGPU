/* SPDX-License-Identifier: GPL-2.0-or-later
 * GL1.1 sections 5.2/5.3. Client pointers never cross the transport. Native
 * RenderMode executes once; only its completed prefix is staged and published.
 */
namespace dg_icd_capture {
static bool Buffer(GLsizei size, const void *pointer) {
    if (size < 0) {
        JglSetError(GL_INVALID_VALUE);
        return false;
    }
    if ((ULONG)size > DG_GL_MAX_CAPTURE_VALUES) {
        JglSetError(GL_OUT_OF_MEMORY);
        return false;
    }
    if (size && !dg_icd_textures::Span(pointer, (ULONG)size, 4))
        return false;
    return true;
}
static bool Configure(ULONG fn, GLsizei size, GLenum type) {
    ULONG args[] = {(ULONG)size, type, 0}, error = 0, bytes = 0;
    if (!JglQuery(fn, args, DG_GL_RESULT_INT, &error, 4, &bytes))
        return false;
    if (bytes != 4) {
        JglSetError(GL_INVALID_OPERATION);
        return false;
    }
    if (error) {
        JglSetError(error);
        return false;
    }
    return true;
}
} // namespace dg_icd_capture
static void APIENTRY AliasSelectBuffer(GLsizei size, GLuint *buffer) {
    if (!JglReady() || !dg_icd_capture::Buffer(size, buffer))
        return;
    if (!dg_icd_capture::Configure(FEnum_glSelectBuffer, size, 0))
        return;
    auto *state = JglArrays();
    state->SelectPointer = buffer;
    state->SelectSize = (ULONG)size;
}
static void APIENTRY AliasFeedbackBuffer(GLsizei size, GLenum type, GLfloat *buffer) {
    if (!JglReady())
        return;
    if (type != GL_2D && type != GL_3D && type != GL_3D_COLOR && type != GL_3D_COLOR_TEXTURE &&
        type != GL_4D_COLOR_TEXTURE) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    if (!dg_icd_capture::Buffer(size, buffer) ||
        !dg_icd_capture::Configure(FEnum_glFeedbackBuffer, size, type))
        return;
    auto *state = JglArrays();
    state->FeedbackPointer = buffer;
    state->FeedbackSize = (ULONG)size;
}
static GLint APIENTRY AliasRenderMode(GLenum mode) {
    if (!JglReady())
        return 0;
    if (mode != GL_RENDER && mode != GL_SELECT && mode != GL_FEEDBACK) {
        JglSetError(GL_INVALID_ENUM);
        return 0;
    }
    auto *state = JglArrays();
    ULONG args[] = {mode, 0, 0}, result[3] = {}, bytes = 0;
    if (!JglQuery(FEnum_glRenderMode, args, DG_GL_RESULT_INT, result, sizeof(result), &bytes))
        return 0;
    if (bytes != sizeof(result)) {
        JglSetError(GL_INVALID_OPERATION);
        return 0;
    }
    if (result[0]) {
        JglSetError(result[0]);
        return 0;
    }
    ULONG previous = state->CaptureMode;
    state->CaptureMode = mode;
    const ULONG size = previous == GL_SELECT     ? state->SelectSize
                       : previous == GL_FEEDBACK ? state->FeedbackSize
                                                 : 0;
    void *pointer = previous == GL_SELECT ? state->SelectPointer : state->FeedbackPointer;
    const ULONG count = result[2];
    static_assert(DG_GL_MAX_CAPTURE_VALUES * 4 <= sizeof(state->Scratch));
    if (count > size || count > DG_GL_MAX_CAPTURE_VALUES || (count && !pointer)) {
        JglSetError(GL_INVALID_OPERATION);
        return 0;
    }
    for (ULONG offset = 0; offset < count;) {
        const ULONG part = count - offset > 256 ? 256 : count - offset;
        ULONG fetch[] = {0, offset, part};
        bytes = 0;
        if (!JglQuery(FEnum_glRenderMode, fetch, DG_GL_RESULT_INT, state->Scratch + offset * 4,
                      part * 4, &bytes))
            return 0;
        if (bytes != part * 4) {
            JglSetError(GL_INVALID_OPERATION);
            return 0;
        }
        offset += part;
    }
    if (count)
        CopyMemory(pointer, state->Scratch, count * 4);
    GLint returned;
    CopyMemory(&returned, result + 1, 4);
    return returned;
}
static void APIENTRY AliasInitNames(void) {
    if (JglCommandReady())
        JglScalarVector(FEnum_glInitNames, 0, nullptr);
}
static void APIENTRY AliasPopName(void) {
    if (JglCommandReady())
        JglScalarVector(FEnum_glPopName, 0, nullptr);
}
static void APIENTRY AliasLoadName(GLuint name) {
    if (JglCommandReady())
        JglScalarVector(FEnum_glLoadName, 1, &name);
}
static void APIENTRY AliasPushName(GLuint name) {
    if (JglCommandReady())
        JglScalarVector(FEnum_glPushName, 1, &name);
}
static void APIENTRY AliasPassThrough(GLfloat token) {
    if (JglCommandReady())
        JglScalarVector(FEnum_glPassThrough, 1, &token);
}
