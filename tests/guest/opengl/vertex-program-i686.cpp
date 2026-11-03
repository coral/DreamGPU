/* SPDX-License-Identifier: GPL-2.0-or-later
 * Actual i686 ARB_vertex_program float attribute wrappers with raw stdcall
 * argument words: exact bits, defaulted components and live x87 status. No
 * libc or transport.
 */
#include "attrib.cpp"

static volatile ULONG Captured[5], Function, Words, Calls;
__attribute__((noinline)) void JglScalarVector(ULONG function, ULONG words, const void *arguments) {
    const unsigned char *bytes = (const unsigned char *)arguments;
    Function = function;
    Words = words;
    Calls = Calls + 1;
    for (ULONG i = 0; i < words && i < 5; ++i)
        Captured[i] = bytes[i * 4] | ((ULONG)bytes[i * 4 + 1] << 8) |
                      ((ULONG)bytes[i * 4 + 2] << 16) | ((ULONG)bytes[i * 4 + 3] << 24);
}

/* Push raw words right to left and call a stdcall entry; it pops them. The C
 * caller never converts or loads a float argument itself. */
typedef void (*ENTRY)(void);
extern "C" void CallRaw(ENTRY function, const ULONG *words, ULONG count);
__asm__(".text\n"
        ".globl CallRaw\n"
        "CallRaw:\n"
        "pushl %esi\n"
        "movl 8(%esp), %eax\n"
        "movl 12(%esp), %esi\n"
        "movl 16(%esp), %ecx\n"
        "1:\n"
        "pushl -4(%esi,%ecx,4)\n"
        "decl %ecx\n"
        "jnz 1b\n"
        "call *%eax\n"
        "popl %esi\n"
        "ret\n");

static unsigned short Status(void) {
    unsigned short result;
    __asm__ volatile("fnstsw %0" : "=am"(result));
    return result;
}

static int Test(void) {
    static const ULONG patterns[][4] = {{0x80000000, 0x00000001, 0x007fffff, 0x7f7fffff},
                                        {0x7f800123, 0xff800321, 0x7fc055aa, 0x7f800000},
                                        {0xff800000, 0xffc01234, 0x80000001, 0x00800000}};
    static const struct {
        ENTRY function;
        ULONG components;
        BOOL vector;
    } cases[] = {{(ENTRY)glVertexAttrib1fARB, 1, FALSE}, {(ENTRY)glVertexAttrib2fARB, 2, FALSE},
                 {(ENTRY)glVertexAttrib3fARB, 3, FALSE}, {(ENTRY)glVertexAttrib4fARB, 4, FALSE},
                 {(ENTRY)glVertexAttrib1fvARB, 1, TRUE}, {(ENTRY)glVertexAttrib2fvARB, 2, TRUE},
                 {(ENTRY)glVertexAttrib3fvARB, 3, TRUE}, {(ENTRY)glVertexAttrib4fvARB, 4, TRUE}};
    for (ULONG n = 0; n < sizeof(cases) / sizeof(cases[0]); ++n)
        for (ULONG pattern = 0; pattern < sizeof(patterns) / sizeof(patterns[0]); ++pattern)
            for (ULONG seed = 0; seed < 2; ++seed) {
                const ULONG *p = patterns[pattern], index = n * 2 + seed;
                const ULONG count = cases[n].components, calls = Calls;
                ULONG words[5] = {index, p[0], p[1], p[2], p[3]}, i;
                __asm__ volatile("fninit" ::: "memory");
                if (seed)
                    __asm__ volatile("fldz; fldz; fdivp; fstp %%st(0)" ::: "memory");
                const unsigned short before = Status();
                if (seed && !(before & 1))
                    return 1;
                if (cases[n].vector)
                    words[1] = (ULONG)(ULONG_PTR)p;
                CallRaw(cases[n].function, words, cases[n].vector ? 2 : 1 + count);
                if (Calls != calls + 1 || Function != FEnum_glVertexAttrib4fARB || Words != 5)
                    return 2;
                const ULONG expected[5] = {index, p[0], count > 1 ? p[1] : 0, count > 2 ? p[2] : 0,
                                           count > 3 ? p[3] : 0x3f800000};
                for (i = 0; i < 5; ++i)
                    if (Captured[i] != expected[i])
                        return 3;
                if (Status() != before)
                    return 4;
                if (cases[n].vector) {
                    words[1] = 0;
                    CallRaw(cases[n].function, words, 2);
                    if (Calls != calls + 1 || Status() != before)
                        return 5;
                }
            }
    return 0;
}

extern "C" void _start(void) {
    int result = Test();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(result) : "memory");
    __builtin_unreachable();
}
