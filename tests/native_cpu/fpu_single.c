/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "fpu/softfloat.h"
#include "target/i386/tcg/fpu-single.h"

static uint64_t random_state = UINT64_C(0x7b302e0f4e932ab1);
static uint64_t random64(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 7;
    random_state ^= random_state << 17;
    return random_state;
}

static void check_load(float32 value, float_status status)
{
    float_status ref = status, got = status;
    floatx80 expected = float32_to_floatx80(value, &ref);
    floatx80 actual = fpu_load_single(value, &got);
    assert(expected.high == actual.high && expected.low == actual.low);
    assert(get_float_exception_flags(&ref) == get_float_exception_flags(&got));
}

static void check_store(floatx80 value, float_status status)
{
    float_status ref = status, got = status;
    float32 expected = floatx80_to_float32(value, &ref);
    float32 actual = fpu_store_single(value, &got);
    assert(expected == actual);
    assert(get_float_exception_flags(&ref) == get_float_exception_flags(&got));
}

int main(void)
{
    const uint32_t edges[] = {0, 1, 0x007fffff, 0x00800000, 0x00800001,
                              0x3f7fffff, 0x3f800000, 0x3f800001, 0x7f7fffff,
                              0x7f800000, 0x7f800001, 0x7fc00000, 0x7fffffff};
    const floatx80 extended[] = {
        { .high = 0, .low = 0 }, { .high = 0x8000, .low = 0 },
        { .high = 0, .low = 1 }, { .high = 0, .low = UINT64_C(0x8000000000000000) },
        { .high = 0x3fff, .low = 0 }, /* unsupported unnormal */
        { .high = 0x3fff, .low = UINT64_C(0x8000000000000001) }, /* inexact */
        { .high = 0x3f69, .low = UINT64_C(0x8000000000000000) }, /* underflow */
        { .high = 0x3f81, .low = UINT64_C(0x8000000000000000) }, /* FLT_MIN */
        { .high = 0x407f, .low = UINT64_C(0x8000000000000000) }, /* overflow */
        { .high = 0x7fff, .low = UINT64_C(0x8000000000000000) },
        { .high = 0x7fff, .low = UINT64_C(0x8000000000000001) },
        { .high = 0x7fff, .low = UINT64_C(0xc000000000000001) },
    };
    uint64_t cases = 0;
    for (unsigned mode = 0; mode < 4; ++mode) {
        for (unsigned precision = 0; precision < 3; ++precision) {
            float_status status = {0};
            set_float_default_nan_pattern(0b11000000, &status);
            set_float_2nan_prop_rule(float_2nan_prop_x87, &status);
            set_float_ftz_detection(float_ftz_after_rounding, &status);
            set_float_rounding_mode(mode, &status);
            set_floatx80_rounding_precision(precision, &status);
            for (unsigned sign = 0; sign < 2; ++sign) {
                for (unsigned i = 0; i < G_N_ELEMENTS(edges); ++i) {
                    float32 value = edges[i] | (sign << 31);
                    check_load(value, status);
                    float_status convert = status;
                    check_store(float32_to_floatx80(value, &convert), status);
                    cases += 2;
                }
                for (unsigned i = 0; i < G_N_ELEMENTS(extended); ++i) {
                    floatx80 value = extended[i];
                    value.high |= sign << 15;
                    check_store(value, status);
                    ++cases;
                }
            }
            for (unsigned i = 0; i < 10000; ++i) {
                set_float_exception_flags(i & 0x3f, &status);
                float32 value = random64();
                check_load(value, status);
                float_status convert = status;
                check_store(float32_to_floatx80(value, &convert), status);
                floatx80 raw = { .high = random64(), .low = random64() };
                check_store(raw, status);
                cases += 3;
            }
        }
    }
    printf("PASS exact single load/store: %llu result/flag comparisons, all x86 rounding "
           "modes and precisions; zeros/denormals/NaNs/overflow/inexact fallback\n",
           (unsigned long long)cases);
    return 0;
}
