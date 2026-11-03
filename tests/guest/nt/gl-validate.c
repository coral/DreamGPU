/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t ULONG;
#include "gl.h"
#include "dg-escape.h"
#include "dg-gl-validate.h"
#include "dg-gl-result.h"
static ULONG FunctionWords(void *context, ULONG function) {
    (void)context;
    switch (function) {
        case 17:
            return 4;
        case 18:
            return DG_GL_FUNCTION_INLINE_DATA | 2;
        case 19:
            return DG_GL_FUNCTION_QUERY | 1;
        case 469:
        case 0x1d6: /* DrawArraysEXT */
            return DG_GL_FUNCTION_INLINE_DATA | 4;
        case 0xaec: /* the attribute draw (VertexAttribPointerARB) */
            return DG_GL_FUNCTION_INLINE_DATA | 5;
        case 20:
            return DG_GL_FUNCTION_INLINE_DATA | 8;
        default:
            return (ULONG)-1;
    }
}
int main(void) {
    const DG_GL_LIMITS limits = {7, DG_ESCAPE_MAX_BYTES, DG_GL_MAX_RECORDS, 1};
    ULONG words[1024], valid[] = {DG_GL_CALL, 52, 999, 1, 1, 0, 0, 999, 17, 0, 0, 0, 0};
    {
        ULONG compact[32] = {DG_GL_DATA_CALL,    128, 0, 1, 1, 0, 0, 0, 469, 72, 4, 0, 2,
                             DG_GL_ARRAY_RAW | 3};
        compact[14] = 0x140a | (2U << 16); // double2 position at offset32
        compact[15] = 0x1401 | (4U << 16); // byte4 color at offset64
        assert(DgValidateUserGl(compact, sizeof(compact), 42, 7, &limits, FunctionWords, NULL));
        compact[21] = 1; // alignment padding before double data
        assert(!DgValidateUserGl(compact, sizeof(compact), 42, 7, &limits, FunctionWords, NULL));
        compact[21] = 0;
        compact[15] |= 0x80000000U; // reserved descriptor bits
        assert(!DgValidateUserGl(compact, sizeof(compact), 42, 7, &limits, FunctionWords, NULL));
        compact[15] &= ~0x80000000U;
        compact[12] = DG_GL_MAX_VERTICES + 1;
        assert(!DgValidateUserGl(compact, sizeof(compact), 42, 7, &limits, FunctionWords, NULL));
        compact[12] = 2;
        compact[16] = 0x1406 | (3U << 16); // disabled descriptor must be zero
        assert(!DgValidateUserGl(compact, sizeof(compact), 42, 7, &limits, FunctionWords, NULL));
    }
    {
        /* Sixteen-slot multitexture arrays: float2 position, unit 3 short3. */
        ULONG ext[37] = {DG_GL_DATA_CALL,
                         148,
                         0,
                         1,
                         1,
                         0,
                         0,
                         0,
                         0x1d6,
                         92,
                         4,
                         0,
                         2,
                         DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION | (DG_GL_ARRAY_TEXCOORD1 << 2)};
        ext[14] = 0x1406 | (2U << 16);
        ext[14 + 9] = 0x1402 | (3U << 16);
        assert(DgValidateUserGl(ext, sizeof(ext), 42, 7, &limits, FunctionWords, NULL));
        ext[13] |= DG_GL_ARRAY_FOG_COORD; /* fog slot enabled without a descriptor */
        assert(!DgValidateUserGl(ext, sizeof(ext), 42, 7, &limits, FunctionWords, NULL));
        ext[13] &= ~DG_GL_ARRAY_FOG_COORD;
        ext[14 + 14] = 0x1406 | (1U << 16); /* descriptor of the disabled fog slot */
        assert(!DgValidateUserGl(ext, sizeof(ext), 42, 7, &limits, FunctionWords, NULL));
        ext[14 + 14] = 0;
        ext[13] |= 1U << 15; /* reserved slot 15 bit */
        assert(!DgValidateUserGl(ext, sizeof(ext), 42, 7, &limits, FunctionWords, NULL));
        ext[13] &= ~(1U << 15);
        ext[14 + 15] = 0x1406 | (1U << 16); /* reserved slot 15 descriptor */
        assert(!DgValidateUserGl(ext, sizeof(ext), 42, 7, &limits, FunctionWords, NULL));
        ext[14 + 15] = 0;
        ext[14 + 9] = 0x1403 | (3U << 16); /* unsigned coordinates follow slot 3 */
        assert(!DgValidateUserGl(ext, sizeof(ext), 42, 7, &limits, FunctionWords, NULL));
        ext[14 + 9] = 0x1402 | (3U << 16);
        ext[13] &= ~DG_GL_ARRAY_RAW; /* the multitexture form is always compact */
        assert(!DgValidateUserGl(ext, sizeof(ext), 42, 7, &limits, FunctionWords, NULL));
        ext[13] |= DG_GL_ARRAY_RAW;
        ext[36] = 1; /* nonzero final section byte is data, not padding: still valid */
        assert(DgValidateUserGl(ext, sizeof(ext), 42, 7, &limits, FunctionWords, NULL));
    }
    {
        /* The attribute draw: two vertices of generic attribute 0 (FLOAT 2, at
         * 128) and generic 3 (UNSIGNED_BYTE 4 normalized, at 144), no vertex
         * array; descriptors at word 15. */
        ULONG attrib[53] = {DG_GL_DATA_CALL, 212,        0, 1, 1, 0, 0, 0, 0xaec, 152, 5, 0, 2,
                            DG_GL_ARRAY_RAW, 1 | 1U << 3};
        attrib[15 + 16] = 0x1406 | (2U << 16);
        attrib[15 + 19] = 0x1401 | (4U << 16) | DG_GL_ATTRIB_NORMALIZED;
        assert(DgValidateUserGl(attrib, sizeof(attrib), 42, 7, &limits, FunctionWords, NULL));
        attrib[14] = 1U << 3; /* neither a position nor generic attribute 0 */
        assert(!DgValidateUserGl(attrib, sizeof(attrib), 42, 7, &limits, FunctionWords, NULL));
        attrib[14] = 1 | 1U << 3 | 1U << 16; /* reserved generic mask bit */
        assert(!DgValidateUserGl(attrib, sizeof(attrib), 42, 7, &limits, FunctionWords, NULL));
        attrib[14] = 1 | 1U << 3;
        attrib[15 + 16] |= DG_GL_ATTRIB_NORMALIZED; /* normalized floats */
        assert(!DgValidateUserGl(attrib, sizeof(attrib), 42, 7, &limits, FunctionWords, NULL));
        attrib[15 + 16] = 0x1406 | (5U << 16); /* five components */
        assert(!DgValidateUserGl(attrib, sizeof(attrib), 42, 7, &limits, FunctionWords, NULL));
        attrib[15 + 16] = 0x1406 | (2U << 16) | 1U << 25; /* reserved descriptor bit */
        assert(!DgValidateUserGl(attrib, sizeof(attrib), 42, 7, &limits, FunctionWords, NULL));
        attrib[15 + 16] = 0x1406 | (2U << 16);
        attrib[15 + 20] = 0x1406 | (1U << 16); /* a disabled generic descriptor */
        assert(!DgValidateUserGl(attrib, sizeof(attrib), 42, 7, &limits, FunctionWords, NULL));
        attrib[15 + 20] = 0;
        attrib[11] = 1; /* first is always zero */
        assert(!DgValidateUserGl(attrib, sizeof(attrib), 42, 7, &limits, FunctionWords, NULL));
        attrib[11] = 0;
        attrib[13] |= DG_GL_ARRAY_POSITION; /* a position without its descriptor */
        assert(!DgValidateUserGl(attrib, sizeof(attrib), 42, 7, &limits, FunctionWords, NULL));
        attrib[13] = DG_GL_ARRAY_RAW;
        attrib[10] = 10; /* beyond GL_POLYGON */
        assert(!DgValidateUserGl(attrib, sizeof(attrib), 42, 7, &limits, FunctionWords, NULL));
        attrib[10] = 0;
        assert(DgValidateUserGl(attrib, sizeof(attrib), 42, 7, &limits, FunctionWords, NULL));
        attrib[8] = 0x1d6; /* the sixteen-slot layout has no generic slots */
        assert(!DgValidateUserGl(attrib, sizeof(attrib), 42, 7, &limits, FunctionWords, NULL));
    }
    {
        /* Fog coordinates (slot 14): float2 positions at 64, one FLOAT each at 80. */
        ULONG fog[36] = {DG_GL_DATA_CALL,
                         144,
                         0,
                         1,
                         1,
                         0,
                         0,
                         0,
                         0x1d6,
                         88,
                         4,
                         0,
                         2,
                         DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION | DG_GL_ARRAY_FOG_COORD};
        fog[14] = 0x1406 | (2U << 16);
        fog[14 + 14] = 0x1406 | (1U << 16);
        assert(DgValidateUserGl(fog, sizeof(fog), 42, 7, &limits, FunctionWords, NULL));
        fog[14 + 14] = 0x1406 | (2U << 16); /* exactly one component */
        assert(!DgValidateUserGl(fog, sizeof(fog), 42, 7, &limits, FunctionWords, NULL));
        {
            /* The same with a payload sized for two components per vertex. */
            ULONG pair[38];
            memcpy(pair, fog, sizeof(fog));
            pair[36] = pair[37] = 0;
            pair[1] = 152;
            pair[9] = 96;
            assert(!DgValidateUserGl(pair, sizeof(pair), 42, 7, &limits, FunctionWords, NULL));
            pair[14 + 14] = 0x1404 | (2U << 16);
            assert(!DgValidateUserGl(pair, sizeof(pair), 42, 7, &limits, FunctionWords, NULL));
            pair[13] = DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION | DG_GL_ARRAY_TEXCOORD1;
            pair[14 + 14] = 0;
            pair[14 + 7] = 0x1406 | (2U << 16); /* the same bytes as a unit-1 coordinate */
            assert(DgValidateUserGl(pair, sizeof(pair), 42, 7, &limits, FunctionWords, NULL));
        }
        static const ULONG others[] = {0x1400, 0x1401, 0x1402, 0x1403, 0x1404, 0x1405};
        for (unsigned i = 0; i < sizeof(others) / sizeof(others[0]); ++i) {
            fog[14 + 14] = others[i] | (1U << 16); /* only FLOAT or DOUBLE */
            assert(!DgValidateUserGl(fog, sizeof(fog), 42, 7, &limits, FunctionWords, NULL));
        }
        fog[14 + 14] = 0x1406 | (1U << 16);
        fog[8] = 469; /* the seven-slot DrawArrays layout never has the fog slot */
        assert(!DgValidateUserGl(fog, sizeof(fog), 42, 7, &limits, FunctionWords, NULL));
        fog[8] = 0x1d6;
        assert(DgValidateUserGl(fog, sizeof(fog), 42, 7, &limits, FunctionWords, NULL));
    }
    {
        /* One DOUBLE fog coordinate after one float3 position: 8-byte aligned at
         * 80 behind four zero padding bytes. */
        ULONG fog[36] = {DG_GL_DATA_CALL,
                         144,
                         0,
                         1,
                         1,
                         0,
                         0,
                         0,
                         0x1d6,
                         88,
                         0,
                         0,
                         1,
                         DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION | DG_GL_ARRAY_FOG_COORD};
        fog[14] = 0x1406 | (3U << 16);
        fog[14 + 14] = 0x140a | (1U << 16);
        fog[14 + 16] = 0x3f800000;
        fog[14 + 20] = 0x00000000;
        fog[14 + 21] = 0x3ff00000;
        assert(DgValidateUserGl(fog, sizeof(fog), 42, 7, &limits, FunctionWords, NULL));
        fog[14 + 19] = 1; /* alignment padding before the double */
        assert(!DgValidateUserGl(fog, sizeof(fog), 42, 7, &limits, FunctionWords, NULL));
        fog[14 + 19] = 0;
        fog[14 + 14] = 0x140a | (1U << 16);
        fog[13] |= DG_GL_ARRAY_TEXCOORD; /* unit 0 enabled without a descriptor */
        assert(!DgValidateUserGl(fog, sizeof(fog), 42, 7, &limits, FunctionWords, NULL));
    }
    memcpy(words, valid, sizeof(valid));
    assert(DgValidateUserGl(words, 52, 42, 7, &limits, FunctionWords, NULL));
    assert(words[2] == 42 && words[7] == 7);
    memcpy(words, valid, sizeof(valid));
    words[5] = 1;
    assert(!DgValidateUserGl(words, 52, 42, 7, &limits, FunctionWords, NULL));
    memcpy(words, valid, sizeof(valid));
    words[1] = 48;
    assert(!DgValidateUserGl(words, 52, 42, 7, &limits, FunctionWords, NULL));
    memcpy(words, valid, sizeof(valid));
    words[8] = 18; /* data call cannot masquerade as scalar */
    assert(!DgValidateUserGl(words, 52, 42, 7, &limits, FunctionWords, NULL));
    memcpy(words, valid, sizeof(valid));
    words[0] = DG_GL_DESKTOP;
    assert(!DgValidateUserGl(words, 52, 42, 7, &limits, FunctionWords, NULL));
    memcpy(words, valid, sizeof(valid));
    words[0] = DG_GL_CLOSE_CLIENT;
    words[1] = 32;
    assert(!DgValidateUserGl(words, 32, 42, 7, &limits, FunctionWords, NULL));
    memcpy(words, valid, sizeof(valid));
    words[1] = 0xfffffffc;
    assert(!DgValidateUserGl(words, 52, 42, 7, &limits, FunctionWords, NULL));
    assert(!DgValidateUserGl(words, 0, 42, 7, &limits, FunctionWords, NULL));
    assert(!DgValidateUserGl(words, 31, 42, 7, &limits, FunctionWords, NULL));
    assert(!DgValidateUserGl(words, 65540, 42, 7, &limits, FunctionWords, NULL));
    memcpy(words, valid, sizeof(valid));
    words[0] = DG_GL_CREATE_DRAWABLE;
    words[1] = 40;
    words[8] = 4096;
    words[9] = 4096;
    assert(DgValidateUserGl(words, 40, 42, 7, &limits, FunctionWords, NULL));
    words[9] = 4097;
    assert(!DgValidateUserGl(words, 40, 42, 7, &limits, FunctionWords, NULL));
    /* Only WNDOBJ-owned presentation may select desktop/front/no-export
     * behavior; the public offscreen command channel accepts flags zero. */
    memset(words, 0, sizeof(words));
    words[0] = DG_GL_PRESENT;
    words[1] = 32;
    words[3] = words[4] = 1;
    assert(DgValidateUserGl(words, 32, 42, 7, &limits, FunctionWords, NULL));
    words[5] = DG_GL_PRESENT_FRONT_ONLY;
    assert(!DgValidateUserGl(words, 32, 42, 7, &limits, FunctionWords, NULL));
    words[5] = DG_GL_PRESENT_NO_EXPORT;
    assert(!DgValidateUserGl(words, 32, 42, 7, &limits, FunctionWords, NULL));
    words[5] = DG_GL_PRESENT_RETAIN;
    assert(!DgValidateUserGl(words, 32, 42, 7, &limits, FunctionWords, NULL));
    words[5] = DG_GL_PRESENT_BOUNDED;
    words[1] = 40;
    words[8] = words[9] = 64;
    assert(!DgValidateUserGl(words, 40, 42, 7, &limits, FunctionWords, NULL));
    memset(words, 0, sizeof(words));
    words[0] = DG_GL_DATA_CALL;
    words[1] = 52;
    words[3] = words[4] = 1;
    words[8] = 18;
    words[9] = 3;
    words[12] = 0x00123456;
    assert(DgValidateUserGl(words, 52, 42, 7, &limits, FunctionWords, NULL));
    assert(words[2] == 42 && words[7] == 7);
    words[12] |= 0x01000000; /* alignment padding must be zero */
    assert(!DgValidateUserGl(words, 52, 42, 7, &limits, FunctionWords, NULL));
    words[12] = 0;
    words[9] = 0xffffffff;
    assert(!DgValidateUserGl(words, 52, 42, 7, &limits, FunctionWords, NULL));
    words[9] = 5; /* declared blob exceeds record */
    assert(!DgValidateUserGl(words, 52, 42, 7, &limits, FunctionWords, NULL));
    words[9] = 0; /* surplus bytes are not arbitrary padding */
    assert(!DgValidateUserGl(words, 52, 42, 7, &limits, FunctionWords, NULL));
    words[1] = 72;
    words[8] = 20; /* allocation-only TexImage, native semantics next */
    assert(DgValidateUserGl(words, 72, 42, 7, &limits, FunctionWords, NULL));
    words[8] = 19; /* query-kind function cannot take inline data */
    assert(!DgValidateUserGl(words, 72, 42, 7, &limits, FunctionWords, NULL));
    memset(words, 0, sizeof(words));
    words[0] = DG_GL_QUERY;
    words[1] = 48;
    words[3] = words[4] = 1;
    words[8] = 19;
    assert(DgValidateUserGl(words, 48, 42, 7, &limits, FunctionWords, NULL));
    words[10] = 1; /* unused query argument */
    assert(!DgValidateUserGl(words, 48, 42, 7, &limits, FunctionWords, NULL));
    words[10] = 0;
    memcpy(words + 12, words, 48);
    assert(!DgValidateUserGl(words, 96, 42, 7, &limits, FunctionWords, NULL));
    words[8] = 18;
    assert(!DgValidateUserGl(words, 48, 42, 7, &limits, FunctionWords, NULL));
    {
        unsigned char result[512] = {0};
        assert(DgValidateGlResult(DG_GL_RESULT_INT, 4, 512, result));
        assert(DgValidateGlResult(DG_GL_RESULT_INT, 4, 65536, result));
        assert(DgValidateGlResult(DG_GL_RESULT_DOUBLE, 128, 128, result));
        assert(!DgValidateGlResult(DG_GL_RESULT_DOUBLE, 4, 512, result));
        assert(!DgValidateGlResult(DG_GL_RESULT_INT, 8, 4, result));
        assert(!DgValidateGlResult(DG_GL_RESULT_INT, 4, 65537, result));
        assert(!DgValidateGlResult(DG_GL_RESULT_INT, 0, 512, result));
        assert(!DgValidateGlResult(999, 4, 512, result));
        assert(DgValidateGlResult(DG_GL_RESULT_BOOL, 16, 512, result));
        result[0] = 2;
        assert(!DgValidateGlResult(DG_GL_RESULT_BOOL, 16, 512, result));
        memcpy(result, "DreamGPU", sizeof("DreamGPU"));
        assert(DgValidateGlResult(DG_GL_RESULT_STRING, sizeof("DreamGPU"), 512, result));
        assert(!DgValidateGlResult(DG_GL_RESULT_STRING, sizeof("DreamGPU") - 1, 512, result));
        result[1] = 0;
        assert(!DgValidateGlResult(DG_GL_RESULT_STRING, sizeof("DreamGPU"), 512, result));
    }
    puts("GL user-batch validation tests passed");
    return 0;
}
