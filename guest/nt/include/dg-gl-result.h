/* SPDX-License-Identifier: GPL-2.0-or-later
 * Result metadata is meaningful only after matching successful completion.
 * This helper validates the bounded byte representation before exposing it to
 * the user caller. It must never authorize copying previous query data.
 */
#ifndef DG_GL_RESULT_H
#define DG_GL_RESULT_H
static int DgValidateGlResult(ULONG type, ULONG bytes, ULONG capacity, const unsigned char *data) {
    ULONG i;
    if (!data || !bytes || bytes > capacity || capacity > DG_ESCAPE_MAX_RESULT_BYTES)
        return 0;
    switch (type) {
        case DG_GL_RESULT_BOOL:
            for (i = 0; i < bytes; ++i)
                if (data[i] > 1)
                    return 0;
            return 1;
        case DG_GL_RESULT_INT:
        case DG_GL_RESULT_FLOAT:
            return !(bytes & 3);
        case DG_GL_RESULT_DOUBLE:
            return !(bytes & 7);
        case DG_GL_RESULT_STRING:
            if (data[bytes - 1])
                return 0;
            for (i = 0; i + 1 < bytes; ++i)
                if (data[i] < 32 || data[i] > 126)
                    return 0;
            return 1;
        default:
            return 0;
    }
}
#endif
