/* SPDX-License-Identifier: GPL-2.0-or-later
 * Validate the complete copied user batch before any record reaches hardware.
 * ULONG must be a 32-bit unsigned type. Kept freestanding for adversarial tests.
 */
#ifndef DG_GL_VALIDATE_H
#define DG_GL_VALIDATE_H
#include "dg-gl-limits.h"
static int DgValidateUserGl(ULONG *words, ULONG bytes, ULONG token, ULONG generation,
                            const DG_GL_LIMITS *limits, ULONG (*function_words)(void *, ULONG),
                            void *context) {
    ULONG offset = 0, records = 0;
    if (!limits->Initialized || limits->Generation != generation || !limits->MaxRecords ||
        limits->MaxRecords > DG_GL_MAX_RECORDS || !bytes || bytes > limits->MaxBytes ||
        bytes > DG_ESCAPE_MAX_BYTES || (bytes & 3))
        return 0;
    while (offset < bytes) {
        ULONG *record = words + offset / 4, op, size, expected = 0;
        if (bytes - offset < DG_GL_HEADER_BYTES || ++records > limits->MaxRecords)
            return 0;
        op = record[0];
        size = record[1];
        if (size < DG_GL_HEADER_BYTES || (size & 3) || size > bytes - offset || record[6] ||
            record[5])
            return 0;
        switch (op) {
            case DG_GL_CREATE_CONTEXT:
                expected = 36;
                break;
            case DG_GL_CREATE_DRAWABLE:
                expected = 40;
                break;
            case DG_GL_DESTROY_CONTEXT:
            case DG_GL_DESTROY_DRAWABLE:
            case DG_GL_MAKE_CURRENT:
            case DG_GL_PRESENT:
                expected = 32;
                break;
            case DG_GL_CALL:
                if (size < 36)
                    return 0;
                expected = function_words(context, record[8]);
                if (expected == (ULONG)-1 || (expected & DG_GL_FUNCTION_KIND_MASK) ||
                    expected > (DG_ESCAPE_MAX_BYTES - 36) / 4)
                    return 0;
                expected = 36 + expected * 4;
                break;
            case DG_GL_DATA_CALL: {
                ULONG args, data_bytes, payload, padding, i;
                if (size < DG_GL_DATA_ARGS)
                    return 0;
                args = function_words(context, record[8]);
                if (args == (ULONG)-1 ||
                    (args & DG_GL_FUNCTION_KIND_MASK) != DG_GL_FUNCTION_INLINE_DATA)
                    return 0;
                args &= ~DG_GL_FUNCTION_KIND_MASK;
                if (args > (size - DG_GL_DATA_ARGS) / 4)
                    return 0;
                payload = DG_GL_DATA_ARGS + args * 4;
                data_bytes = record[9];
                if (data_bytes > size - payload)
                    return 0;
                padding = (4 - (data_bytes & 3)) & 3;
                if (size - payload - data_bytes != padding)
                    return 0;
                for (i = 0; i < padding; ++i)
                    if (((const unsigned char *)record)[payload + data_bytes + i])
                        return 0;
                expected = size;
                break;
            }
            case DG_GL_QUERY: {
                ULONG args, i;
                if (size != DG_GL_QUERY_BYTES || bytes != DG_GL_QUERY_BYTES)
                    return 0;
                args = function_words(context, record[8]);
                if (args == (ULONG)-1 || (args & DG_GL_FUNCTION_KIND_MASK) != DG_GL_FUNCTION_QUERY)
                    return 0;
                args &= ~DG_GL_FUNCTION_KIND_MASK;
                if (args > 3)
                    return 0;
                for (i = args; i < 3; ++i)
                    if (record[9 + i])
                        return 0;
                expected = DG_GL_QUERY_BYTES;
                break;
            }
            default:
                /* CLOSE_CLIENT is a kernel lifetime operation. DESKTOP, retained
                 * image ownership and exclusive presentation are privileged. */
                return 0;
        }
        if (size != expected)
            return 0;
        if (op == DG_GL_CREATE_DRAWABLE &&
            (!record[8] || !record[9] || record[8] > DG_GL_MAX_DIMENSION ||
             record[9] > DG_GL_MAX_DIMENSION))
            return 0;
        record[2] = token;
        record[7] = generation;
        offset += size;
    }
    return 1;
}
#endif
