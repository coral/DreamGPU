/* SPDX-License-Identifier: GPL-2.0-or-later
 * Exercise actual public vector aliases, including stdcall and special-value
 * inputs. No libc, graphics transport, or floating-point caller conversion.
 */
#include "compatibility.cpp"

static volatile ULONG Captured[4], Function, Words, Calls;
static const void *volatile Pointer;

__attribute__((noinline)) void JglScalarVector(ULONG function, ULONG words, const void *arguments) {
    const unsigned char *bytes = (const unsigned char *)arguments;
    ULONG i;
    Function = function;
    Words = words;
    Pointer = arguments;
    Calls = Calls + 1;
    for (i = 0; i < words; ++i)
        Captured[i] = bytes[i * 4] | ((ULONG)bytes[i * 4 + 1] << 8) |
                      ((ULONG)bytes[i * 4 + 2] << 16) | ((ULONG)bytes[i * 4 + 3] << 24);
}

typedef void(APIENTRY *VECTOR_ALIAS)(const GLfloat *);
static VECTOR_ALIAS volatile Aliases[] = {glVertex3fv, glColor3fv, glColor4fv, glTexCoord2fv,
                                          glNormal3fv};
static const ULONG Functions[] = {FEnum_glVertex3f, FEnum_glColor3f, FEnum_glColor4f,
                                  FEnum_glTexCoord2f, FEnum_glNormal3f};
static const ULONG Counts[] = {3, 3, 4, 2, 3};

static unsigned short Status(void) {
    unsigned short result;
    __asm__ volatile("fnstsw %0" : "=am"(result));
    return result;
}

static int Test(void) {
    static const ULONG patterns[][4] = {{0x80000000, 0x00000001, 0x007fffff, 0x7f7fffff},
                                        {0x7f800123, 0xff800321, 0x7fc055aa, 0x7f800000},
                                        {0xff800000, 0xffc01234, 0x80000001, 0x00800000}};
    ULONG alias, pattern, seed, i, calls;
    unsigned short before;
    for (alias = 0; alias < sizeof(Aliases) / sizeof(Aliases[0]); ++alias) {
        for (pattern = 0; pattern < sizeof(patterns) / sizeof(patterns[0]); ++pattern) {
            for (seed = 0; seed < 2; ++seed) {
                __asm__ volatile("fninit" ::: "memory");
                if (seed)
                    __asm__ volatile("fldz; fldz; fdivp; fstp %%st(0)" ::: "memory");
                before = Status();
                if (seed && !(before & 1))
                    return 1;
                calls = Calls;
                Aliases[alias]((const GLfloat *)patterns[pattern]);
                if (Calls != calls + 1 || Function != Functions[alias] || Words != Counts[alias])
                    return 2;
                /* The original vector must reach the shared packer directly;
                 * a scalar wrapper or intermediate stack array is a regression. */
                if (Pointer != patterns[pattern])
                    return 3;
                for (i = 0; i < Counts[alias]; ++i)
                    if (Captured[i] != patterns[pattern][i])
                        return 4;
                if (Status() != before)
                    return 5;
                calls = Calls;
                Aliases[alias](NULL);
                if (Calls != calls || Status() != before)
                    return 6;
            }
        }
    }
    return 0;
}

extern "C" void _start(void) {
    int result = Test();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(result) : "memory");
    __builtin_unreachable();
}

void JglForgetTextures(ULONG count, const GLuint *textures) {
    (void)count;
    (void)textures;
}
