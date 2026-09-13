/* SPDX-License-Identifier: GPL-2.0-or-later
 * Context-local, validated scalar state. Unknown/invalid values always reach
 * GL so repeated errors remain observable. Float values are compared as bits.
 */
typedef struct {
    ULONG EnableKnown[2], EnableValue[2];
    ULONG Blend[2], Offset[2];
    BOOL BlendKnown, OffsetKnown;
} JGL_STATE_CACHE;

JGL_INLINE int StateCapability(ULONG cap) {
    /* The fixed-function capabilities supported by the public frontend. */
    if (cap >= 0x4000 && cap <= 0x4007)
        return 25 + cap - 0x4000; /* LIGHT0..7 */
    if (cap >= 0x3000 && cap <= 0x3005)
        return 33 + cap - 0x3000; /* CLIP_PLANE0..5 */
    switch (cap) {
        case 0x0be2:
            return 0; /* BLEND */
        case 0x0b71:
            return 1; /* DEPTH_TEST */
        case 0x0b90:
            return 2; /* STENCIL_TEST */
        case 0x0c11:
            return 3; /* SCISSOR_TEST */
        case 0x0de0:
            return 4; /* TEXTURE_1D */
        case 0x0de1:
            return 5; /* TEXTURE_2D */
        case 0x0b44:
            return 6; /* CULL_FACE */
        case 0x0b50:
            return 7; /* LIGHTING */
        case 0x0b60:
            return 8; /* FOG */
        case 0x0bc0:
            return 9; /* ALPHA_TEST */
        case 0x0bd0:
            return 10; /* DITHER */
        case 0x0ba1:
            return 11; /* NORMALIZE */
        case 0x8037:
            return 12; /* POLYGON_OFFSET_FILL */
        case 0x2a02:
            return 13; /* POLYGON_OFFSET_LINE */
        case 0x2a01:
            return 14; /* POLYGON_OFFSET_POINT */
        case 0x0b41:
            return 15; /* POLYGON_SMOOTH */
        case 0x0b42:
            return 16; /* POLYGON_STIPPLE */
        case 0x0b20:
            return 17; /* LINE_SMOOTH */
        case 0x0b24:
            return 18; /* LINE_STIPPLE */
        case 0x0b10:
            return 19; /* POINT_SMOOTH */
        case 0x0b57:
            return 20; /* COLOR_MATERIAL */
        case 0x0c60:
            return 21; /* TEXTURE_GEN_S */
        case 0x0c61:
            return 22; /* TEXTURE_GEN_T */
        case 0x0c62:
            return 23; /* TEXTURE_GEN_R */
        case 0x0c63:
            return 24; /* TEXTURE_GEN_Q */
        case 0x8458:
            return 39; /* COLOR_SUM_EXT (only advertised after native negotiation) */
        default:
            return -1;
    }
}

JGL_INLINE BOOL BlendFactor(ULONG factor, BOOL source) {
    if (factor <= 1 || (factor >= 0x0302 && factor <= 0x0305))
        return TRUE;
    return source ? factor >= 0x0306 && factor <= 0x0308 : factor == 0x0300 || factor == 0x0301;
}

JGL_INLINE BOOL StateUnchanged(JGL_STATE_CACHE *cache, ULONG function, const void *arguments) {
    const ULONG *args = (const ULONG *)arguments;
    if (function == FEnum_glEnable || function == FEnum_glDisable) {
        int bit = StateCapability(args[0]);
        ULONG word, mask, value;
        if (bit < 0)
            return FALSE;
        word = (ULONG)bit / 32;
        mask = 1UL << ((ULONG)bit & 31);
        value = function == FEnum_glEnable ? mask : 0;
        if ((cache->EnableKnown[word] & mask) && (cache->EnableValue[word] & mask) == value)
            return TRUE;
        cache->EnableKnown[word] |= mask;
        cache->EnableValue[word] = (cache->EnableValue[word] & ~mask) | value;
    } else if (function == FEnum_glBlendFunc && BlendFactor(args[0], TRUE) &&
               BlendFactor(args[1], FALSE)) {
        if (cache->BlendKnown && cache->Blend[0] == args[0] && cache->Blend[1] == args[1])
            return TRUE;
        cache->Blend[0] = args[0];
        cache->Blend[1] = args[1];
        cache->BlendKnown = TRUE;
    } else if (function == FEnum_glPolygonOffset && (args[0] & 0x7f800000UL) != 0x7f800000UL &&
               (args[1] & 0x7f800000UL) != 0x7f800000UL) {
        if (cache->OffsetKnown && cache->Offset[0] == args[0] && cache->Offset[1] == args[1])
            return TRUE;
        cache->Offset[0] = args[0];
        cache->Offset[1] = args[1];
        cache->OffsetKnown = TRUE;
    }
    return FALSE;
}
