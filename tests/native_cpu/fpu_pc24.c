/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "fpu/softfloat.h"
#include "target/i386/tcg/fpu-pc24.h"
#include <time.h>
#include <fenv.h>

static floatx80 reference(floatx80 a, floatx80 b, float_status *s, unsigned op) {
    switch (op) {
        case 0:
            return floatx80_add(a, b, s);
        case 1:
            return floatx80_sub(a, b, s);
        case 2:
            return floatx80_mul(a, b, s);
        default:
            return floatx80_div(a, b, s);
    }
}
static floatx80 from_single(uint32_t b) {
    return (floatx80){.high = (uint16_t)(((b >> 23) & 255) + 16383 - 127) | ((b >> 16) & 0x8000),
                      .low = ((uint64_t)((b & 0x7fffff) | 0x800000)) << 40};
}
__attribute__((noinline)) static floatx80 candidate(floatx80 a, floatx80 b, float_status *s,
                                                    unsigned op) {
    return fpu_pc24_binary(a, b, s, op);
}
static uint64_t random_state = UINT64_C(0x4182b392c87893);
static uint32_t random32(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 7;
    random_state ^= random_state << 17;
    return random_state;
}
static float_status status(void) {
    float_status s = {0};
    set_floatx80_rounding_precision(floatx80_precision_s, &s);
    set_float_rounding_mode(float_round_nearest_even, &s);
    return s;
}
static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static void check(floatx80 a, floatx80 b, unsigned op, float_status s, uint64_t *hits,
                  uint64_t *tests) {
    float_status ref = s, got = s;
    floatx80 x = reference(a, b, &ref, op), y;
    float_status admission = s;
    if (fpu_pc24_try(a, b, &admission, op, &y))
        ++*hits;
    y = candidate(a, b, &got, op);
    ++*tests;
    if (x.high != y.high || x.low != y.low ||
        ref.float_exception_flags != got.float_exception_flags) {
        fprintf(
            stderr,
            "mismatch op%u a=%04x:%016llx b=%04x:%016llx ref=%04x:%016llx/%x got=%04x:%016llx/%x\n",
            op, a.high, (unsigned long long)a.low, b.high, (unsigned long long)b.low, x.high,
            (unsigned long long)x.low, ref.float_exception_flags, y.high, (unsigned long long)y.low,
            got.float_exception_flags);
        abort();
    }
}
int main(void) {
    assert(fegetround() == FE_TONEAREST);
    uint64_t tests = 0, hits = 0;
    const uint32_t edge[] = {0x00800000, 0x00800001, 0x3f000000, 0x3f000001, 0x3f7fffff,
                             0x3f800000, 0x3f800001, 0x3fc00000, 0x40000000, 0x4b000000,
                             0x4b000001, 0x4b7fffff, 0x7f7fffff};
    for (unsigned op = 0; op < 4; ++op) {
        for (unsigned i = 0; i < G_N_ELEMENTS(edge); ++i)
            for (unsigned j = 0; j < G_N_ELEMENTS(edge); ++j)
                for (unsigned signs = 0; signs < 4; ++signs)
                    check(from_single(edge[i] | ((signs & 1) << 31)),
                          from_single(edge[j] | ((signs >> 1) << 31)), op, status(), &hits, &tests);
        for (unsigned i = 0; i < 250000; ++i) {
            uint32_t a = (random32() & 0x807fffff) | ((1 + random32() % 254) << 23),
                     b = (random32() & 0x807fffff) | ((1 + random32() % 254) << 23);
            float_status s = status();
            float_raise(i & 0x3f, &s);
            check(from_single(a), from_single(b), op, s, &hits, &tests);
        }
        /* All precision/rounding guards fall back, including FCW restore. */
        for (unsigned i = 0; i < 12000; ++i) {
            float_status s = status();
            set_float_rounding_mode(i % 4, &s);
            set_floatx80_rounding_precision(i % 3, &s);
            floatx80 a = from_single(0x3f800000 + (random32() & 0x7fffff)),
                     b = from_single(0x40000000 + (random32() & 0x7fffff));
            if (i & 1)
                a.low |= random32(); /* full precision loaded operand */
            if (i & 2)
                a.high += 256; /* extended exponent cannot be narrowed */
            check(a, b, op, s, &hits, &tests);
        }
    }
    printf("differential tests=%llu fast_hits=%llu mismatches=0 (result bits and complete "
           "exception flags)\n",
           (unsigned long long)tests, (unsigned long long)hits);
    enum { N = 65536, REPEATS = 16 };
    static floatx80 as[N], bs[N];
    for (unsigned i = 0; i < N; ++i) {
        as[i] = from_single((random32() & 0x807fffff) | ((115 + random32() % 25) << 23));
        bs[i] = from_single((random32() & 0x807fffff) | ((115 + random32() % 25) << 23));
    }
    volatile uint64_t sink = 0;
    for (unsigned op = 0; op < 4; ++op) {
        for (unsigned trial = 0; trial < 3; ++trial) {
            double times[2];
            uint64_t sum[2] = {0, 0};
            for (unsigned which = 0; which < 2; ++which) {
                double start = now();
                for (unsigned j = 0; j < REPEATS; ++j)
                    for (unsigned i = 0; i < N; ++i) {
                        float_status s = status();
                        floatx80 v = which ? candidate(as[i], bs[i], &s, op)
                                           : reference(as[i], bs[i], &s, op);
                        sum[which] += v.low + v.high + s.float_exception_flags;
                    }
                times[which] = now() - start;
                sink ^= sum[which];
            }
            assert(sum[0] == sum[1]);
            printf("op=%s trial=%u count=%u soft_ns=%.2f candidate_ns=%.2f speedup=%.2f\n",
                   (const char *[]){"add", "sub", "mul", "div"}[op], trial, N * REPEATS,
                   times[0] * 1e9 / (N * REPEATS), times[1] * 1e9 / (N * REPEATS),
                   times[0] / times[1]);
        }
    }
    printf("sink=%llu\n", (unsigned long long)sink);
    return 0;
}
