/* SPDX-License-Identifier: GPL-2.0-or-later
 * Actual i686 fog coordinate vector wrappers with raw stdcall argument words:
 * exact bits and live x87 status. No libc or transport.
 */
#include "fog.cpp"

static volatile ULONG Captured, Function, Words, Calls;

__attribute__((noinline)) void JglScalarVector(ULONG function, ULONG words, const void *arguments) {
    const unsigned char *bytes = (const unsigned char *)arguments;
    Function = function;
    Words = words;
    Calls = Calls + 1;
    Captured =
        bytes[0] | ((ULONG)bytes[1] << 8) | ((ULONG)bytes[2] << 16) | ((ULONG)bytes[3] << 24);
}

/* Push one raw word and call a stdcall entry; it pops it. */
typedef void (*ENTRY)(void);
extern "C" void CallRaw(ENTRY function, ULONG word);
__asm__(".text\n"
        ".globl CallRaw\n"
        "CallRaw:\n"
        "movl 4(%esp), %eax\n"
        "pushl 8(%esp)\n"
        "call *%eax\n"
        "ret\n");

static unsigned short Status(void) {
    unsigned short result;
    __asm__ volatile("fnstsw %0" : "=am"(result));
    return result;
}

static int Test(void) {
    /* Signaling NaNs, denormals, negative zero and infinities. */
    static const ULONG patterns[] = {0x7f800123, 0xff800321, 0x00000001, 0x80000000,
                                     0x7f800000, 0x007fffff, 0x7fc055aa};
    static const ENTRY entries[] = {(ENTRY)glFogCoordfv, (ENTRY)glFogCoordfvEXT};
    for (ENTRY entry : entries)
        for (ULONG pattern = 0; pattern < sizeof(patterns) / sizeof(patterns[0]); ++pattern)
            for (ULONG seed = 0; seed < 2; ++seed) {
                const ULONG calls = Calls;
                __asm__ volatile("fninit" ::: "memory");
                if (seed)
                    __asm__ volatile("fldz; fldz; fdivp; fstp %%st(0)" ::: "memory");
                const unsigned short before = Status();
                if (seed && !(before & 1))
                    return 1;
                CallRaw(entry, (ULONG)(ULONG_PTR)&patterns[pattern]);
                if (Calls != calls + 1 || Function != FEnum_glFogCoordf || Words != 1)
                    return 2;
                if (Captured != patterns[pattern])
                    return 3;
                if (Status() != before)
                    return 4;
                CallRaw(entry, 0);
                if (Calls != calls + 1 || Status() != before)
                    return 5;
            }
    return 0;
}

extern "C" void _start(void) {
    int result = Test();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(result) : "memory");
    __builtin_unreachable();
}
