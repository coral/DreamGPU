/* SPDX-License-Identifier: GPL-2.0-or-later
 * Actual i686 ARB_fragment_program float parameter wrappers with raw stdcall
 * argument words: exact bits and live x87 status. No libc or transport.
 */
#include "program.cpp"

static volatile ULONG Captured[6], Function, Words, Calls;
BOOL JglReady(void) {
    return TRUE;
}
BOOL JglFragmentProgramAvailable(void) {
    return TRUE;
}
void JglSetError(GLenum) {
    Calls = Calls + 100;
}
__attribute__((noinline)) void JglScalarVector(ULONG function, ULONG words, const void *arguments) {
    const unsigned char *bytes = (const unsigned char *)arguments;
    Function = function;
    Words = words;
    Calls = Calls + 1;
    for (ULONG i = 0; i < words && i < 6; ++i)
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
        ULONG record;
        BOOL vector;
    } cases[] = {{(ENTRY)glProgramEnvParameter4fARB, FEnum_glProgramEnvParameter4fARB, FALSE},
                 {(ENTRY)glProgramEnvParameter4fvARB, FEnum_glProgramEnvParameter4fARB, TRUE},
                 {(ENTRY)glProgramLocalParameter4fARB, FEnum_glProgramLocalParameter4fARB, FALSE},
                 {(ENTRY)glProgramLocalParameter4fvARB, FEnum_glProgramLocalParameter4fARB, TRUE}};
    for (ULONG n = 0; n < sizeof(cases) / sizeof(cases[0]); ++n)
        for (ULONG pattern = 0; pattern < sizeof(patterns) / sizeof(patterns[0]); ++pattern)
            for (ULONG seed = 0; seed < 2; ++seed) {
                const ULONG *p = patterns[pattern], index = 3 + n * 1000;
                ULONG words[6] = {0x8804, index, p[0], p[1], p[2], p[3]}, calls = Calls, i;
                __asm__ volatile("fninit" ::: "memory");
                if (seed)
                    __asm__ volatile("fldz; fldz; fdivp; fstp %%st(0)" ::: "memory");
                const unsigned short before = Status();
                if (seed && !(before & 1))
                    return 1;
                if (cases[n].vector)
                    words[2] = (ULONG)(ULONG_PTR)p;
                CallRaw(cases[n].function, words, cases[n].vector ? 3 : 6);
                if (Calls != calls + 1 || Function != cases[n].record || Words != 6)
                    return 2;
                const ULONG expected[6] = {0x8804, index, p[0], p[1], p[2], p[3]};
                for (i = 0; i < 6; ++i)
                    if (Captured[i] != expected[i])
                        return 3;
                if (Status() != before)
                    return 4;
                if (cases[n].vector) {
                    words[2] = 0;
                    CallRaw(cases[n].function, words, 3);
                    if (Calls != calls + 1 || Status() != before)
                        return 5;
                }
                /* Another target is a local error and submits nothing. */
                words[0] = 0x8620;
                words[2] = (ULONG)(ULONG_PTR)p;
                CallRaw(cases[n].function, words, cases[n].vector ? 3 : 6);
                if (Calls != calls + 101 || Status() != before)
                    return 6;
                Calls = calls + 1;
            }
    return 0;
}

extern "C" void _start(void) {
    int result = Test();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(result) : "memory");
    __builtin_unreachable();
}
