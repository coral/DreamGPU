/* SPDX-License-Identifier: GPL-2.0-or-later
 * GL 1.1 texture residency/priority semantics:
 * https://registry.khronos.org/OpenGL/specs/gl/glspec11.pdf sections 3.8 and 6.1.
 * Residency comes from native queries, never from logical name existence.
 */
namespace dg_icd_textures {
static bool CopyFormat(GLenum format) {
    return format == GL_ALPHA || format == GL_LUMINANCE || format == GL_LUMINANCE_ALPHA ||
           format == GL_INTENSITY || format == GL_RGB || format == GL_RGBA ||
           format == GL_R3_G3_B2 || (format >= GL_ALPHA4 && format <= GL_LUMINANCE16_ALPHA16) ||
           (format >= GL_INTENSITY4 && format <= GL_INTENSITY16) ||
           (format >= GL_RGB4 && format <= GL_RGBA16);
}
static bool Span(const void *pointer, ULONG count, ULONG unit) {
    const ULONG_PTR max = ~(ULONG_PTR)0;
    if (!pointer || (ULONG_PTR)count > max / unit ||
        (ULONG_PTR)pointer > max - ((ULONG_PTR)count * unit - 1)) {
        JglSetError(GL_INVALID_VALUE);
        return false;
    }
    return true;
}
} // namespace dg_icd_textures
static GLboolean APIENTRY AliasAreTexturesResident(GLsizei count, const GLuint *textures,
                                                   GLboolean *residences) {
    if (!JglReady())
        return GL_FALSE;
    if (count < 0) {
        JglSetError(GL_INVALID_VALUE);
        return GL_FALSE;
    }
    if (!count)
        return GL_TRUE;
    if ((ULONG)count > DG_GL_MAX_TEXTURES) {
        JglSetError(GL_OUT_OF_MEMORY);
        return GL_FALSE;
    }
    if (!dg_icd_textures::Span(textures, (ULONG)count, sizeof(*textures)) ||
        !dg_icd_textures::Span(residences, (ULONG)count, sizeof(*residences)))
        return GL_FALSE;
    /* Generated but never-bound names are not texture objects. Validate the
     * whole input before the first native query, using the local namespace. */
    for (GLsizei i = 0; i < count; ++i)
        if (!glIsTexture(textures[i])) {
            JglSetError(GL_INVALID_VALUE);
            return GL_FALSE;
        }
    JGL_ARRAY_STATE *arrays = JglArrays();
    if (!arrays)
        return GL_FALSE;
    static_assert(DG_GL_MAX_TEXTURES <= sizeof(arrays->Scratch));
    bool all = true;
    for (ULONG at = 0; at < (ULONG)count;) {
        ULONG n = (ULONG)count - at;
        if (n > 3)
            n = 3;
        ULONG args[3];
        for (ULONG i = 0; i < 3; ++i)
            args[i] = textures[at + (i < n ? i : n - 1)];
        BYTE result[5];
        ULONG bytes = 0;
        if (!JglQuery(FEnum_glAreTexturesResident, args, DG_GL_RESULT_BOOL, result, sizeof(result),
                      &bytes))
            return GL_FALSE;
        if (bytes != sizeof(result)) {
            JglSetError(GL_INVALID_OPERATION);
            return GL_FALSE;
        }
        for (BYTE value : result)
            if (value > 1) {
                JglSetError(GL_INVALID_OPERATION);
                return GL_FALSE;
            }
        if (!result[0]) {
            JglSetError(GL_INVALID_VALUE);
            return GL_FALSE;
        }
        if (result[1] && (!result[2] || !result[3] || !result[4])) {
            JglSetError(GL_INVALID_OPERATION);
            return GL_FALSE;
        }
        all = all && result[1];
        CopyMemory(arrays->Scratch + at, result + 2, n);
        at += n;
    }
    if (!all)
        CopyMemory(residences, arrays->Scratch, (ULONG)count);
    return all ? GL_TRUE : GL_FALSE;
}
static void APIENTRY AliasPrioritizeTextures(GLsizei count, const GLuint *textures,
                                             const GLclampf *priorities) {
    if (!JglReady())
        return;
    if (count < 0) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    if (!count)
        return;
    if (!dg_icd_textures::Span(textures, (ULONG)count, sizeof(*textures)) ||
        !dg_icd_textures::Span(priorities, (ULONG)count, sizeof(*priorities)))
        return;
    JGL_ARRAY_STATE *arrays = JglArrays();
    if (!arrays)
        return;
    ULONG capacity = JglMaxDataBytes(1);
    if (capacity > sizeof(arrays->Scratch))
        capacity = sizeof(arrays->Scratch);
    capacity /= 8;
    if (capacity > DG_GL_MAX_TEXTURES)
        capacity = DG_GL_MAX_TEXTURES;
    if (!capacity) {
        JglSetError(GL_OUT_OF_MEMORY);
        return;
    }
    ULONG at = 0;
    while (at < (ULONG)count) {
        ULONG n = (ULONG)count - at;
        if (n > capacity)
            n = capacity;
        for (ULONG i = 0; i < n; ++i) {
            CopyMemory(arrays->Scratch + i * 8, textures + at + i, 4);
            CopyMemory(arrays->Scratch + i * 8 + 4, priorities + at + i, 4);
        }
        if (!JglData(FEnum_glPrioritizeTextures, &n, 1, arrays->Scratch, n * 8))
            return;
        at += n;
    }
}
static void APIENTRY AliasCopyTexImage1D(GLenum target, GLint level, GLenum internal_format,
                                         GLint x, GLint y, GLsizei width, GLint border) {
    if (!JglReady())
        return;
    if (target != GL_TEXTURE_1D) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    if (!dg_icd_textures::CopyFormat(internal_format)) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    if (level < 0 || level > DG_GL_MAX_TEXTURE_LEVEL || width < 0 || border < 0 || border > 1 ||
        width < 2 * border ||
        (ULONG)width > (DG_GL_MAX_TEXTURE_DIMENSION >> level) + 2 * (ULONG)border) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    ULONG args[] = {target,   (ULONG)level, internal_format, (ULONG)x,
                    (ULONG)y, (ULONG)width, (ULONG)border};
    JglScalarVector(FEnum_glCopyTexImage1D, 7, args);
}
static void APIENTRY AliasCopyTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLint x,
                                            GLint y, GLsizei width) {
    if (!JglReady())
        return;
    if (target != GL_TEXTURE_1D) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    if (level < 0 || level > DG_GL_MAX_TEXTURE_LEVEL || width < 0) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    GLint extent = 0, border = 0;
    ULONG query[] = {target, (ULONG)level, GL_TEXTURE_WIDTH};
    ULONG bytes = 0;
    if (!JglQuery(FEnum_glGetTexLevelParameteriv, query, DG_GL_RESULT_INT, &extent, sizeof(extent),
                  &bytes))
        return;
    if (bytes != sizeof(extent)) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    query[2] = GL_TEXTURE_BORDER;
    if (!JglQuery(FEnum_glGetTexLevelParameteriv, query, DG_GL_RESULT_INT, &border, sizeof(border),
                  &bytes))
        return;
    if (bytes != sizeof(border) || border < 0 || border > 1 || extent < 2 * border ||
        (ULONG)extent > DG_GL_MAX_TEXTURE_DIMENSION + 2) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    /* Border texel -1 is legal only for a real bordered level. Bounds are
     * checked before subtraction, so extreme GLsizei/GLint never overflow. */
    const GLint end = extent - border;
    if (xoffset < -border || xoffset > end || width > end - xoffset) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    ULONG args[] = {target, (ULONG)level, (ULONG)xoffset, (ULONG)x, (ULONG)y, (ULONG)width};
    JglScalarVector(FEnum_glCopyTexSubImage1D, 6, args);
}
