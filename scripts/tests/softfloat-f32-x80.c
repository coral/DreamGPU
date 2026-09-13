/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The SPDX tag covers the original test harness. Its reference function below
 * is copied with modifications (renaming) from float32_to_floatx80:
 * Source: vendor/qemu/fpu/softfloat.c
 *         @ 04c0f383f3f402a7a081b1169e2af69307fd2f07
 * Upstream: https://gitlab.com/qemu-project/qemu
 * The reference retains the upstream QEMU/SoftFloat licensing notices in the
 * production source included below; it is not relicensed by the harness tag.
 */
/* Compile the production translation unit; retain its pre-fast-path algorithm
 * as an independent reference using the real canonicalization routines. */
#include "fpu/softfloat.c"

static floatx80 canonical_f32_to_x80(float32 a, float_status *s) {
    FloatParts64 p64 = float32_unpack_canonical(a, s);
    FloatParts128 p128 = parts64_to_parts128(&p64, s);

    return floatx80_round_pack_canonical(&p128, s);
}

static uint64_t checked;

static void check(uint32_t bits, const float_status *initial) {
    float_status actual_status = *initial;
    float_status reference_status = *initial;
    floatx80 actual = float32_to_floatx80(bits, &actual_status);
    floatx80 reference = canonical_f32_to_x80(bits, &reference_status);

    if (actual.high != reference.high || actual.low != reference.low ||
        memcmp(&actual_status, &reference_status, sizeof(actual_status))) {
        fprintf(stderr,
                "f32=%08x actual=%04x:%016" PRIx64 " reference=%04x:%016" PRIx64
                " flags=%04x/%04x rounding=%u precision=%u\n",
                bits, actual.high, actual.low, reference.high, reference.low,
                actual_status.float_exception_flags, reference_status.float_exception_flags,
                initial->float_rounding_mode, initial->floatx80_rounding_precision);
        abort();
    }
    checked++;
}

int main(void) {
    static const unsigned exponents[] = {1, 127, 254};
    static const uint32_t fractions[] = {0,        1,        0x1fffff, 0x3fffff,
                                         0x400000, 0x400001, 0x7ffffe, 0x7fffff};
    float_status s = {.default_nan_pattern = 0x40};
    uint32_t random = 0x137b529d;

    /* Every significand, both signs, at low/middle/high normal exponents. */
    for (unsigned e = 0; e < ARRAY_SIZE(exponents); e++) {
        for (uint32_t frac = 0; frac < 0x800000; frac++) {
            uint32_t bits = (exponents[e] << 23) | frac;
            check(bits, &s);
            check(bits | 0x80000000u, &s);
        }
    }

    /* Every exponent, both signs, mantissa/NaN boundaries, every rounding
     * mode and x87 precision, flush/default-NaN/rebias controls and existing
     * exception flags. Check the whole status structure, not just result bits.
     */
    for (unsigned rounding = 0; rounding < 8; rounding++) {
        for (unsigned precision = 0; precision < 3; precision++) {
            for (unsigned options = 0; options < 64; options++) {
                for (unsigned snan = 0; snan < 3; snan++) {
                    s.float_rounding_mode = rounding;
                    s.floatx80_rounding_precision = precision;
                    s.flush_inputs_to_zero = options & 1;
                    s.flush_to_zero = (options >> 1) & 1;
                    s.default_nan_mode = (options >> 2) & 1;
                    s.rebias_overflow = (options >> 3) & 1;
                    s.rebias_underflow = (options >> 4) & 1;
                    s.float_exception_flags = options & 32 ? 0x7fff : 0;
                    s.floatx80_behaviour = options & 31;
                    s.float_snan_rule = snan;
                    for (unsigned exp = 0; exp < 256; exp++) {
                        for (unsigned f = 0; f < ARRAY_SIZE(fractions); f++) {
                            uint32_t bits = (exp << 23) | fractions[f];
                            check(bits, &s);
                            check(bits | 0x80000000u, &s);
                        }
                    }
                    /* Deterministic full-word input variation per status. */
                    for (unsigned i = 0; i < 256; i++) {
                        random ^= random << 13;
                        random ^= random >> 17;
                        random ^= random << 5;
                        check(random, &s);
                    }
                }
            }
        }
    }
    printf("{\"passed\":true,\"comparisons\":%" PRIu64
           ",\"status_configurations\":4608,\"exhaustive_normal_exponents\":[1,127,254]}\n",
           checked);
    return 0;
}
