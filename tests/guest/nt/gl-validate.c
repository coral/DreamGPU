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
        case 20:
            return DG_GL_FUNCTION_INLINE_DATA | 8;
        default:
            return (ULONG)-1;
    }
}
int main(void) {
    const DG_GL_LIMITS limits = {7, DG_ESCAPE_MAX_BYTES, DG_GL_MAX_RECORDS, 1};
    ULONG words[1024], valid[] = {DG_GL_CALL, 52, 999, 1, 1, 0, 0, 999, 17, 0, 0, 0, 0};
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
