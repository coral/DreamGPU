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
        case 0x1f5: /* the buffer draw (DrawRangeElements) */
        case 0x1f7: /* the BGRA buffer draw (DrawRangeElementsEXT) */
            return DG_GL_FUNCTION_INLINE_DATA | 10;
        case 20:
            return DG_GL_FUNCTION_INLINE_DATA | 8;
        /* Framebuffer objects (DG_CAP_GL_FRAMEBUFFER_OBJECT), core numbers. */
        case 0x3c: /* BindFramebuffer */
        case 0x48: /* BindRenderbuffer */
            return 2;
        case 0x7b2: /* RenderbufferStorage */
        case 0x277: /* FramebufferRenderbuffer */
            return 4;
        case 0x27d: /* FramebufferTexture1D */
        case 0x27f: /* FramebufferTexture2D */
            return 5;
        case 0x281: /* FramebufferTexture3D */
            return 6;
        case 0x295: /* GenerateMipmap */
            return 1;
        case 0x83: /* BlitFramebuffer */
            return 10;
        case 0x197: /* DeleteFramebuffers */
        case 0x1a9: /* DeleteRenderbuffers */
            return DG_GL_FUNCTION_INLINE_DATA | 1;
        case 0x94: /* CheckFramebufferStatus */
            return DG_GL_FUNCTION_QUERY | 1;
        case 0x3f3: /* GetRenderbufferParameteriv */
            return DG_GL_FUNCTION_QUERY | 2;
        case 0x30e: /* GetFramebufferAttachmentParameteriv */
            return DG_GL_FUNCTION_QUERY | 3;
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
        /* The buffer draw: vertices 5-7, the position (FLOAT 3) from buffer 9 at
         * offset 8 with stride 12, a client colour section (UNSIGNED_BYTE 4) at
         * payload byte 140. Arguments at word 10, descriptors at word 20, the
         * reference at 52, the section at 55. */
        ULONG draw[64] = {DG_GL_DATA_CALL,     232, 0, 1, 1, 0, 0, 0, 0x1f5, 152, 4, 5, 3,
                          DG_GL_ARRAY_RAW | 3, 0,   1, 3, 0, 0, 0};
        draw[20] = 0x1406 | (3U << 16);
        draw[21] = 0x1401 | (4U << 16);
        draw[52] = 9;
        draw[53] = 8;
        draw[54] = 12;
        draw[55] = draw[56] = draw[57] = 0xff0000ffU;
        assert(DgValidateUserGl(draw, 232, 42, 7, &limits, FunctionWords, NULL));
        draw[52] = 0; /* a reference names a buffer */
        assert(!DgValidateUserGl(draw, 232, 42, 7, &limits, FunctionWords, NULL));
        draw[52] = 9;
        draw[53] = 6; /* offsets are component aligned */
        assert(!DgValidateUserGl(draw, 232, 42, 7, &limits, FunctionWords, NULL));
        draw[53] = 8;
        draw[54] = DG_GL_MAX_BUFFER_STRIDE + 4; /* bounded strides */
        assert(!DgValidateUserGl(draw, 232, 42, 7, &limits, FunctionWords, NULL));
        draw[54] = 12;
        draw[15] = 1 | 1U << 2; /* a buffer slot that is not enabled */
        assert(!DgValidateUserGl(draw, 232, 42, 7, &limits, FunctionWords, NULL));
        draw[15] = 1;
        draw[16] = 2; /* client sections hold count vertices */
        assert(!DgValidateUserGl(draw, 232, 42, 7, &limits, FunctionWords, NULL));
        draw[16] = 3;
        draw[18] = 4; /* no element buffer without an index type */
        assert(!DgValidateUserGl(draw, 232, 42, 7, &limits, FunctionWords, NULL));
        draw[18] = 0;
        draw[22] = 0x1406 | (3U << 16); /* a stray disabled descriptor */
        assert(!DgValidateUserGl(draw, 232, 42, 7, &limits, FunctionWords, NULL));
        draw[22] = 0;
        /* Indexed from element buffer 4 at offset 6 (UNSIGNED_SHORT), the
         * colour section of vertices 5-7: no inline indices. */
        draw[17] = 0x1403;
        draw[18] = 4;
        draw[19] = 6;
        draw[12] = 300;
        assert(DgValidateUserGl(draw, 232, 42, 7, &limits, FunctionWords, NULL));
        draw[19] = 5; /* element offsets are index aligned */
        assert(!DgValidateUserGl(draw, 232, 42, 7, &limits, FunctionWords, NULL));
        /* Inline indices: two UNSIGNED_SHORT after the section, then padding. */
        draw[1] = 236;
        draw[9] = 156;
        draw[12] = 2;
        draw[18] = draw[19] = 0;
        draw[58] = 5 | 7U << 16;
        assert(DgValidateUserGl(draw, 236, 42, 7, &limits, FunctionWords, NULL));
        draw[9] = 154; /* the record size must match the padded payload */
        assert(!DgValidateUserGl(draw, 236, 42, 7, &limits, FunctionWords, NULL));
        draw[9] = 156;
        draw[12] = 1; /* one index, nonzero padding */
        assert(!DgValidateUserGl(draw, 236, 42, 7, &limits, FunctionWords, NULL));
        draw[58] = 5;
        draw[1] = 236;
        assert(DgValidateUserGl(draw, 236, 42, 7, &limits, FunctionWords, NULL));
        draw[10] = 10; /* beyond GL_POLYGON */
        assert(!DgValidateUserGl(draw, 236, 42, 7, &limits, FunctionWords, NULL));
        draw[10] = 4;
        draw[8] = 0x1f7; /* every buffer draw is a BGRA buffer draw too */
        assert(DgValidateUserGl(draw, 236, 42, 7, &limits, FunctionWords, NULL));
    }
    {
        /* The BGRA buffer draw (DG_CAP_GL_VERTEX_ARRAY_BGRA): vertices 5-7 from
         * buffers only, the position (FLOAT 3) from buffer 9 at 8, BGRA colour
         * and secondary colour at 20 and 24 (stride 20), generic 2 BGRA from
         * tight buffer 10. References at words 52-63. */
        const ULONG bgra = 0x1401 | (4U << 16) | DG_GL_ARRAY_BGRA;
        ULONG draw[64] = {DG_GL_DATA_CALL,
                          256,
                          0,
                          1,
                          1,
                          0,
                          0,
                          0,
                          0x1f7,
                          176,
                          4,
                          5,
                          3,
                          DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION | DG_GL_ARRAY_COLOR |
                              DG_GL_ARRAY_SECONDARY,
                          1U << 2,
                          1 | 1U << 1 | 1U << 4 | 1U << 18,
                          0,
                          0,
                          0,
                          0};
        const ULONG references[12] = {9, 8, 20, 9, 20, 20, 9, 24, 20, 10, 0, 0};
        draw[20] = 0x1406 | (3U << 16);
        draw[21] = draw[24] = bgra;
        draw[20 + 18] = bgra | DG_GL_ATTRIB_NORMALIZED;
        memcpy(draw + 52, references, sizeof(references));
        assert(DgValidateUserGl(draw, 256, 42, 7, &limits, FunctionWords, NULL));
        draw[8] = 0x1f5; /* older kernels' buffer draw keeps refusing BGRA */
        assert(!DgValidateUserGl(draw, 256, 42, 7, &limits, FunctionWords, NULL));
        draw[8] = 0x1f7;
        const struct {
            ULONG word, value;
        } bad[] = {
            {20, 0x1406 | (3U << 16) | DG_GL_ARRAY_BGRA}, /* never a position */
            {21, 0x1401 | (3U << 16) | DG_GL_ARRAY_BGRA}, /* four components */
            {21, 0x1406 | (4U << 16) | DG_GL_ARRAY_BGRA}, /* UNSIGNED_BYTE only */
            {21, bgra | DG_GL_ATTRIB_NORMALIZED},         /* colours carry no flag */
            {24, bgra | (1U << 26)},                      /* no other bit */
            {38, bgra},                                   /* generic ones normalize */
            {38, 0x1403 | (4U << 16) | DG_GL_ARRAY_BGRA | DG_GL_ATTRIB_NORMALIZED},
        };
        for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
            const ULONG saved = draw[bad[i].word];
            draw[bad[i].word] = bad[i].value;
            assert(!DgValidateUserGl(draw, 256, 42, 7, &limits, FunctionWords, NULL));
            draw[bad[i].word] = saved;
        }
        assert(DgValidateUserGl(draw, 256, 42, 7, &limits, FunctionWords, NULL));
        /* Only colour, secondary colour and generic slots, and only exactly. */
        for (unsigned slot = 0; slot < DG_GL_ATTRIB_SLOTS; ++slot) {
            const int role = slot == 1 || slot == 4 || slot >= DG_GL_ARRAY_EXT_SLOTS;
            const ULONG d = bgra | (slot >= DG_GL_ARRAY_EXT_SLOTS ? DG_GL_ATTRIB_NORMALIZED : 0);
            assert(DgRawBgraDescriptor(slot, d) == role);
            assert(!DgRawBgraDescriptor(slot, d & ~DG_GL_ARRAY_BGRA));
        }
        /* A client section stays RGBA: generic 2 from client memory (3 vertices
         * of four bytes after three references) cannot be BGRA. */
        draw[15] = 1 | 1U << 1 | 1U << 4;
        draw[16] = 3;
        draw[61] = draw[62] = draw[63] = 0x80402010U;
        assert(!DgValidateUserGl(draw, 256, 42, 7, &limits, FunctionWords, NULL));
        draw[20 + 18] = 0x1401 | (4U << 16) | DG_GL_ATTRIB_NORMALIZED;
        assert(DgValidateUserGl(draw, 256, 42, 7, &limits, FunctionWords, NULL));
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
    {
        /* Framebuffer object CALL records carry exactly their words: a bind,
         * a 3D attachment and a blit with signed coordinates, in one batch. */
        ULONG batch[45] = {DG_GL_CALL, 44, 0, 1, 1, 0, 0, 0, 0x3c, 0x8d40, 5};
        ULONG *attach = batch + 11, *blit = batch + 26;
        const ULONG attach_words[7] = {0x281, 0x8d40, 0x8ce0, 0x806f, 7, 3, 255};
        const ULONG blit_words[11] = {0x83, 0xffffffffU, 0, 8, 8, 0, 0, 8, 8, 0x4100, 0x2600};
        attach[0] = blit[0] = DG_GL_CALL;
        attach[1] = 60;
        blit[1] = 76;
        attach[3] = attach[4] = blit[3] = blit[4] = 1;
        memcpy(attach + 8, attach_words, sizeof(attach_words));
        memcpy(blit + 8, blit_words, sizeof(blit_words));
        assert(DgValidateUserGl(batch, sizeof(batch), 42, 7, &limits, FunctionWords, NULL));
        assert(batch[2] == 42 && attach[2] == 42 && blit[7] == 7);
        assert(DgValidateUserGl(blit, 76, 42, 7, &limits, FunctionWords, NULL));
        blit[1] = 72; /* one word short */
        assert(!DgValidateUserGl(blit, 72, 42, 7, &limits, FunctionWords, NULL));
        blit[1] = 76;
        attach[8] = 0x27f; /* the 2D attachment has five words */
        assert(!DgValidateUserGl(batch, sizeof(batch), 42, 7, &limits, FunctionWords, NULL));
        attach[8] = 0x281;
        batch[8] = 0x197; /* DeleteFramebuffers is DATA, never a CALL */
        assert(!DgValidateUserGl(batch, sizeof(batch), 42, 7, &limits, FunctionWords, NULL));
        batch[8] = 0x3c;
        batch[5] = 1; /* header flags stay zero */
        assert(!DgValidateUserGl(batch, sizeof(batch), 42, 7, &limits, FunctionWords, NULL));
        batch[5] = 0;
        assert(DgValidateUserGl(batch, sizeof(batch), 42, 7, &limits, FunctionWords, NULL));
    }
    {
        /* DeleteFramebuffers and DeleteRenderbuffers: a count, then names. */
        ULONG deleted[13] = {DG_GL_DATA_CALL, 52, 0, 1, 1, 0, 0, 0, 0x197, 8, 2, 3, 9};
        assert(DgValidateUserGl(deleted, sizeof(deleted), 42, 7, &limits, FunctionWords, NULL));
        deleted[8] = 0x1a9;
        assert(DgValidateUserGl(deleted, sizeof(deleted), 42, 7, &limits, FunctionWords, NULL));
        deleted[9] = 12; /* more names than the record holds */
        assert(!DgValidateUserGl(deleted, sizeof(deleted), 42, 7, &limits, FunctionWords, NULL));
        deleted[9] = 4; /* surplus words are not padding */
        assert(!DgValidateUserGl(deleted, sizeof(deleted), 42, 7, &limits, FunctionWords, NULL));
        deleted[9] = 8;
        deleted[0] = DG_GL_CALL; /* a DATA function as a CALL */
        assert(!DgValidateUserGl(deleted, sizeof(deleted), 42, 7, &limits, FunctionWords, NULL));
        deleted[0] = DG_GL_DATA_CALL;
        deleted[8] = 0x3c; /* a CALL function as DATA */
        assert(!DgValidateUserGl(deleted, sizeof(deleted), 42, 7, &limits, FunctionWords, NULL));
    }
    {
        /* Framebuffer queries: one, two or three arguments, alone in a batch. */
        ULONG query[24] = {DG_GL_QUERY, 48, 0, 1, 1, 0, 0, 0, 0x94, 0x8ca9, 0, 0};
        assert(DgValidateUserGl(query, 48, 42, 7, &limits, FunctionWords, NULL));
        query[10] = 0x8ce0; /* CheckFramebufferStatus has one */
        assert(!DgValidateUserGl(query, 48, 42, 7, &limits, FunctionWords, NULL));
        query[8] = 0x3f3; /* GetRenderbufferParameteriv: RENDERBUFFER, pname */
        query[9] = 0x8d41;
        query[10] = 0x8d42;
        assert(DgValidateUserGl(query, 48, 42, 7, &limits, FunctionWords, NULL));
        query[11] = 0x8cd0;
        assert(!DgValidateUserGl(query, 48, 42, 7, &limits, FunctionWords, NULL));
        query[8] = 0x30e; /* GetFramebufferAttachmentParameteriv: all three */
        query[9] = 0x8d40;
        query[10] = 0x8d00;
        assert(DgValidateUserGl(query, 48, 42, 7, &limits, FunctionWords, NULL));
        memcpy(query + 12, query, 48); /* never beside another record */
        assert(!DgValidateUserGl(query, 96, 42, 7, &limits, FunctionWords, NULL));
        query[8] = 0x83; /* a CALL function as a QUERY */
        assert(!DgValidateUserGl(query, 48, 42, 7, &limits, FunctionWords, NULL));
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
