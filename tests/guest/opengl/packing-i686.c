/* SPDX-License-Identifier: GPL-2.0-or-later
 * No libc or graphics transport: exercise the actual generated public scalar
 * wrappers using raw i686 stack arguments and inspect their packed records.
 */
typedef unsigned int ULONG, GLenum, GLbitfield, GLuint;
typedef int GLint, GLsizei;
typedef unsigned char BYTE, GLboolean;
typedef unsigned short GLushort;
typedef float GLfloat, GLclampf;
typedef double GLdouble, GLclampd;
#define NULL ((void *)0)
#define APIENTRY __attribute__((stdcall))
#include "gl-funcs.h"
#include "packing.h"

static volatile ULONG Captured[16], Function, Count;

static __attribute__((noinline)) void Scalar(ULONG function, ULONG count, const void *arguments) {
    const ULONG *words = arguments;
    ULONG i;
    Function = function;
    Count = count;
    for (i = 0; i < count; ++i)
        Captured[i] = words[i];
}

#include "scalar.inc"

/* Pass raw integer words, so the C caller cannot itself quiet an sNaN or load
 * a subnormal through x87 before the wrapper being tested receives it.
 * The public stdcall wrappers remove their own argument stack bytes. */
extern void CallColorBits(const ULONG *words);
extern void CallOrthoBits(const ULONG *words);
__asm__(".text\n"
        ".globl CallColorBits\n"
        "CallColorBits:\n"
        "movl 4(%esp), %eax\n"
        "pushl 12(%eax)\n"
        "pushl 8(%eax)\n"
        "pushl 4(%eax)\n"
        "pushl 0(%eax)\n"
        "call glColor4f\n"
        "ret\n"
        ".globl CallOrthoBits\n"
        "CallOrthoBits:\n"
        "movl 4(%esp), %eax\n"
        "pushl 44(%eax)\n"
        "pushl 40(%eax)\n"
        "pushl 36(%eax)\n"
        "pushl 32(%eax)\n"
        "pushl 28(%eax)\n"
        "pushl 24(%eax)\n"
        "pushl 20(%eax)\n"
        "pushl 16(%eax)\n"
        "pushl 12(%eax)\n"
        "pushl 8(%eax)\n"
        "pushl 4(%eax)\n"
        "pushl 0(%eax)\n"
        "call glOrtho\n"
        "ret\n");

static unsigned short Status(void) {
    unsigned short status;
    __asm__ volatile("fnstsw %0" : "=am"(status));
    return status;
}

static int Equal(ULONG function, const ULONG *expected, ULONG count) {
    ULONG i;
    if (Function != function || Count != count)
        return 0;
    for (i = 0; i < count; ++i)
        if (Captured[i] != expected[i])
            return 0;
    return 1;
}

static int Test(void) {
    static const ULONG finite[] = {0x80000000, 1, 0x007fffff, 0x7f7fffff};
    static const ULONG nonfinite[] = {0x7f800123, 0xff800321, 0x7fc055aa, 0x7f800000};
    static const ULONG doubles[] = {
        0,          0x80000000, /* negative zero */
        1,          0,          /* smallest positive subnormal */
        0xffffffff, 0x000fffff, /* largest positive subnormal */
        0xffffffff, 0x7fefffff, /* largest finite value */
        0x123,      0x7ff00000, /* signaling NaN */
        0x456,      0xfff80000, /* quiet NaN with sign and payload */
    };
    __asm__ volatile("fninit" ::: "memory");
    CallColorBits(finite);
    if (!Equal(FEnum_glColor4f, finite, 4))
        return 1;
    if (Status() & 0x3f)
        return 2;

    __asm__ volatile("fninit" ::: "memory");
    CallColorBits(nonfinite);
    if (!Equal(FEnum_glColor4f, nonfinite, 4))
        return 3;
    if (Status() & 0x3f)
        return 4;

    __asm__ volatile("fninit" ::: "memory");
    CallOrthoBits(doubles);
    if (!Equal(FEnum_glOrtho, doubles, 12))
        return 5;
    if (Status() & 0x3f)
        return 6;
    return 0;
}

#ifdef __cplusplus
extern "C"
#endif
    void _start(void) {
    int result = Test();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(result) : "memory");
    __builtin_unreachable();
}
