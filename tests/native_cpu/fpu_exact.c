/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "fpu/softfloat.h"
#include "target/i386/tcg/fpu-pc24.h"

static uint64_t seed = UINT64_C(0xb732c906fc118d25), checks;
static uint64_t random64(void)
{
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    return seed;
}

static void pair(floatx80 a, floatx80 b, float_status status)
{
    for (unsigned quiet = 0; quiet < 2; ++quiet) {
        float_status ref = status, got = status;
        FloatRelation expected = quiet ? floatx80_compare_quiet(a, b, &ref)
                                       : floatx80_compare(a, b, &ref);
        assert(expected == fpu_exact_compare(a, b, &got, quiet));
        assert(get_float_exception_flags(&ref) == get_float_exception_flags(&got));
        ++checks;
    }
    for (FpuPc24Op op = FPU_PC24_ADD; op <= FPU_PC24_DIV; ++op) {
        float_status ref = status, got = status;
        floatx80 expected;
        switch (op) {
        case FPU_PC24_ADD: expected = floatx80_add(a, b, &ref); break;
        case FPU_PC24_SUB: expected = floatx80_sub(a, b, &ref); break;
        case FPU_PC24_MUL: expected = floatx80_mul(a, b, &ref); break;
        case FPU_PC24_DIV: expected = floatx80_div(a, b, &ref); break;
        default: abort();
        }
        floatx80 actual = fpu_pc24_binary(a, b, &got, op);
        if (expected.high != actual.high || expected.low != actual.low ||
            get_float_exception_flags(&ref) != get_float_exception_flags(&got)) {
            fprintf(stderr, "op=%d pc=%d mode=%d a=%04x:%016" PRIx64
                    " b=%04x:%016" PRIx64 " expected=%04x:%016" PRIx64
                    "/%x actual=%04x:%016" PRIx64 "/%x\n", op,
                    get_floatx80_rounding_precision(&status),
                    get_float_rounding_mode(&status), a.high, a.low, b.high, b.low,
                    expected.high, expected.low, get_float_exception_flags(&ref),
                    actual.high, actual.low, get_float_exception_flags(&got));
            abort();
        }
        ++checks;
    }
}

int main(void)
{
    const floatx80 edges[] = {
        { .high = 0, .low = 0 }, { .high = 0x8000, .low = 0 },
        { .high = 0, .low = 1 }, { .high = 0, .low = UINT64_C(0x8000000000000000) },
        { .high = 1, .low = UINT64_C(0x8000000000000000) },
        { .high = 1, .low = UINT64_C(0x8000000000000001) },
        { .high = 0x3f81, .low = UINT64_C(0x8000000000000000) },
        { .high = 0x3fff, .low = UINT64_C(0x8000000000000000) },
        { .high = 0xbfff, .low = UINT64_C(0x8000000000000000) },
        { .high = 0x3fff, .low = UINT64_C(0x8000000000000001) },
        { .high = 0x3fff, .low = UINT64_C(0x8000008000000000) },
        { .high = 0x3fff, .low = UINT64_C(0x8000010000000000) },
        { .high = 0x3fff, .low = 0 },
        { .high = 0x3fff, .low = UINT64_C(0x7fffffffffffffff) },
        { .high = 0x407e, .low = UINT64_C(0xffffff0000000000) },
        { .high = 0x407f, .low = UINT64_C(0x8000000000000000) },
        { .high = 0x7ffe, .low = UINT64_C(0xffffffffffffffff) },
        { .high = 0x7fff, .low = UINT64_C(0x8000000000000000) },
        { .high = 0x7fff, .low = UINT64_C(0x8000000000000001) },
        { .high = 0x7fff, .low = UINT64_C(0xc000000000000001) },
        { .high = 0x7fff, .low = 0 },
    };
    for (unsigned mode = 0; mode < 4; ++mode) {
        for (unsigned precision = 0; precision < 3; ++precision) {
            float_status status = {0};
            set_float_default_nan_pattern(0b11000000, &status);
            set_float_2nan_prop_rule(float_2nan_prop_x87, &status);
            set_float_ftz_detection(float_ftz_after_rounding, &status);
            set_float_rounding_mode(mode, &status);
            set_floatx80_rounding_precision(precision, &status);
            for (unsigned i = 0; i < G_N_ELEMENTS(edges); ++i) {
                for (unsigned j = 0; j < G_N_ELEMENTS(edges); ++j) {
                    for (unsigned signs = 0; signs < 4; ++signs) {
                        floatx80 a = edges[i], b = edges[j];
                        a.high ^= (signs & 1) << 15;
                        b.high ^= (signs & 2) << 14;
                        set_float_exception_flags((i + j) & 0x3f, &status);
                        pair(a, b, status);
                    }
                }
            }
            for (unsigned i = 0; i < 5000; ++i) {
                floatx80 a = { .high = random64(), .low = random64() };
                floatx80 b = { .high = random64(), .low = random64() };
                set_float_exception_flags(i & 0x3f, &status);
                pair(a, b, status);
                pair(a, a, status);
                b = a; b.high ^= 0x8000;
                pair(a, b, status);
                pair(a, edges[i % G_N_ELEMENTS(edges)], status);
                float_status convert = status;
                a = float32_to_floatx80((uint32_t)random64(), &convert);
                pair(a, edges[i % G_N_ELEMENTS(edges)], status);
                pair(edges[i % G_N_ELEMENTS(edges)], a, status);
            }
        }
    }
    printf("PASS exact x87 group: %llu result/full-flag comparisons, all x87 rounding "
           "modes and precisions; signed zeros/identities/cancellation and special fallback\n",
           (unsigned long long)checks);
    return 0;
}
