/* SPDX-License-Identifier: GPL-2.0-or-later
 * Context-local, validated scalar state. Unknown/invalid values always reach
 * GL so repeated errors remain observable. Float values are compared as bits.
 *
 * What is known is exactly the host's native state: repeated setters are
 * dropped and IsEnabled/Get* of the enables and VIEWPORT are answered from it
 * without a synchronous host query (StateQuery). Knowledge comes only from
 * the context's creation (GL defaults, StateInitial) and from setters this
 * context executed outside display lists. Everything that may change state
 * the frontend does not model forgets all of it: PopAttrib, CallList(s),
 * NewList/EndList (no setter updates it while a list is compiled, nor
 * while GL_COMPILE_AND_EXECUTE executes it), and ActiveTexture the per-unit
 * enables. The host changes none of it on its own (its private blits save
 * and restore their scissor; it sets the viewport only before the context's
 * first record runs), so nothing else invalidates it.
 */
typedef struct {
    ULONG EnableKnown[2], EnableValue[2];
    ULONG Blend[2], Offset[2];
    BOOL BlendKnown, OffsetKnown;
    /* The last executed Viewport, when native GL keeps it unclamped. */
    LONG Viewport[4];
    BOOL ViewportKnown;
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

/* TEXTURE_1D/2D and TEXTURE_GEN_S..Q are per texture unit (bits 4-5, 21-24). */
#define JGL_UNIT_ENABLES 0x01e00030UL
/* Every capability bit: 0-38, and 39 (COLOR_SUM) with EXT_secondary_color. */
#define JGL_ENABLES_LOW 0xffffffffUL
#define JGL_ENABLES_HIGH 0x7fUL
#define JGL_COLOR_SUM_ENABLE 0x80UL
#define JGL_DITHER_ENABLE (1UL << 10)
#define JGL_VIEWPORT 0x0ba2

/* A new context's native state (GL 1.x tables 6.x): every capability is
 * disabled except DITHER, on every texture unit. The host creates a fresh
 * native context for each one and enables none of these itself. The
 * viewport stays unknown: the host sets it to the drawable when the context
 * first runs a record, from a size the frontend does not control. */
JGL_INLINE void StateInitial(JGL_STATE_CACHE *cache, BOOL color_sum) {
    ZeroMemory(cache, sizeof(*cache));
    cache->EnableKnown[0] = JGL_ENABLES_LOW;
    cache->EnableKnown[1] = JGL_ENABLES_HIGH | (color_sum ? JGL_COLOR_SUM_ENABLE : 0);
    cache->EnableValue[0] = JGL_DITHER_ENABLE;
}
/* Native GL clamps the viewport size to its MAX_VIEWPORT_DIMS and, with
 * ARB_viewport_array, the origin to its VIEWPORT_BOUNDS_RANGE; no native
 * limit is below DG_GL_MAX_DIMENSION. A negative size is GL_INVALID_VALUE.
 * Only values inside all of those are known afterwards. */
JGL_INLINE BOOL ViewportExact(const LONG *v) {
    const LONG limit = DG_GL_MAX_DIMENSION;
    return v[0] >= -limit && v[0] <= limit && v[1] >= -limit && v[1] <= limit && v[2] >= 0 &&
           v[2] <= limit && v[3] >= 0 && v[3] <= limit;
}
/* The known value of an enable or of VIEWPORT: the number of values (one or
 * four), or zero when only the host knows. The active unit's enables only. */
JGL_INLINE ULONG StateQuery(const JGL_STATE_CACHE *cache, ULONG pname, LONG *values) {
    int bit;
    ULONG word, mask;
    if (pname == JGL_VIEWPORT) {
        if (!cache->ViewportKnown)
            return 0;
        values[0] = cache->Viewport[0];
        values[1] = cache->Viewport[1];
        values[2] = cache->Viewport[2];
        values[3] = cache->Viewport[3];
        return 4;
    }
    bit = StateCapability(pname);
    if (bit < 0)
        return 0;
    word = (ULONG)bit / 32;
    mask = 1UL << ((ULONG)bit & 31);
    if (!(cache->EnableKnown[word] & mask))
        return 0;
    values[0] = (cache->EnableValue[word] & mask) != 0;
    return 1;
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
    } else if (function == FEnum_glBlendFunc) {
        /* Native GL may accept factors this table does not model (constant
         * colors, SRC_COLOR as a source): after one, the state is unknown. */
        if (!BlendFactor(args[0], TRUE) || !BlendFactor(args[1], FALSE)) {
            cache->BlendKnown = FALSE;
            return FALSE;
        }
        if (cache->BlendKnown && cache->Blend[0] == args[0] && cache->Blend[1] == args[1])
            return TRUE;
        cache->Blend[0] = args[0];
        cache->Blend[1] = args[1];
        cache->BlendKnown = TRUE;
    } else if (function == FEnum_glBlendFuncSeparate) {
        /* Separate factors replace both pairs; BlendEquation leaves them. */
        cache->BlendKnown = FALSE;
    } else if (function == FEnum_glPolygonOffset && (args[0] & 0x7f800000UL) != 0x7f800000UL &&
               (args[1] & 0x7f800000UL) != 0x7f800000UL) {
        if (cache->OffsetKnown && cache->Offset[0] == args[0] && cache->Offset[1] == args[1])
            return TRUE;
        cache->Offset[0] = args[0];
        cache->Offset[1] = args[1];
        cache->OffsetKnown = TRUE;
    } else if (function == FEnum_glViewport) {
        /* Always sent: only remembered for queries. */
        cache->ViewportKnown = ViewportExact((const LONG *)arguments);
        if (cache->ViewportKnown)
            __builtin_memcpy(cache->Viewport, arguments, sizeof(cache->Viewport));
    } else if (function == FEnum_glActiveTexture) {
        /* Only the previous unit's enables were known; never apply them to
         * another unit. Callers pass only validated unit enums. */
        cache->EnableKnown[0] &= ~JGL_UNIT_ENABLES;
    }
    return FALSE;
}
