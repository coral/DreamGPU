/* SPDX-License-Identifier: GPL-2.0-or-later
 * Execute the complete production frontend packet path as optimized i686.
 * Only platform services are stubbed; Scalar/Record/Flush are the real source.
 */
#include "frontend.cpp"

static JGL_CONTEXT Context;
static PACKET Captured[4];
static ULONG CapturedWords[4], Submissions, TransportError, TlsCalls;

void *memcpy(void *destination, const void *source, size_t bytes) {
    volatile BYTE *out = (volatile BYTE *)destination;
    const BYTE *in = (const BYTE *)source;
    size_t i;
    for (i = 0; i < bytes; ++i)
        out[i] = in[i];
    return destination;
}
void *memset(void *destination, int value, size_t bytes) {
    volatile BYTE *out = (volatile BYTE *)destination;
    size_t i;
    for (i = 0; i < bytes; ++i)
        out[i] = (BYTE)value;
    return destination;
}
void *TlsGetValue(DWORD key) {
    ++TlsCalls;
    if (key != 0)
        TransportError = 1;
    return &Context;
}
int ExtEscape(HDC dc, int escape, int input_bytes, LPCSTR input, int output_bytes, LPSTR output) {
    const DG_ESCAPE_REQUEST *request = (const DG_ESCAPE_REQUEST *)input;
    DG_ESCAPE_REPLY *reply = (DG_ESCAPE_REPLY *)output;
    if (dc != (HDC)100 || escape != DG_ESCAPE || Submissions >= 4 ||
        input_bytes < (int)sizeof(*request) ||
        (ULONG)input_bytes != sizeof(*request) + request->Bytes || output_bytes != sizeof(*reply) ||
        request->Version != DG_ESCAPE_VERSION || request->Operation != DG_ESCAPE_SUBMIT ||
        request->Client != 51 || request->Bytes > DG_ESCAPE_MAX_BYTES || (request->Bytes & 3) ||
        request->Function || request->ResultCapacity || request->Reserved || request->Reserved2) {
        TransportError = 2;
        return 0;
    }
    memcpy(&Captured[Submissions], input, input_bytes);
    CapturedWords[Submissions++] = request->Bytes / 4;
    memset(reply, 0, sizeof(*reply));
    reply->Version = DG_ESCAPE_VERSION;
    reply->Status = DG_ESCAPE_OK;
    reply->CompletedSequence = Submissions;
    return 1;
}

/* No C float expression is evaluated before entering a public wrapper.
 * These raw stack words include signaling NaNs and denormal operands. */
extern "C" void CallColorRaw(const ULONG *);
extern "C" void CallVertexRaw(const ULONG *);
extern "C" void CallOrthoRaw(const ULONG *);
extern "C" void CallOffsetRaw(const ULONG *);
extern "C" void CallSecondaryRaw(const ULONG *);
__asm__(".text\n"
        ".globl CallColorRaw\nCallColorRaw:\n"
        "movl 4(%esp), %eax\n"
        "pushl 12(%eax)\npushl 8(%eax)\npushl 4(%eax)\npushl 0(%eax)\n"
        "call glColor4f\nret\n"
        ".globl CallVertexRaw\nCallVertexRaw:\n"
        "movl 4(%esp), %eax\n"
        "pushl 8(%eax)\npushl 4(%eax)\npushl 0(%eax)\n"
        "call glVertex3f\nret\n"
        ".globl CallSecondaryRaw\nCallSecondaryRaw:\n"
        "movl 4(%esp), %eax\n"
        "pushl 8(%eax)\npushl 4(%eax)\npushl 0(%eax)\n"
        "call glSecondaryColor3fEXT\nret\n"
        ".globl CallOffsetRaw\nCallOffsetRaw:\n"
        "movl 4(%esp), %eax\n"
        "pushl 4(%eax)\npushl 0(%eax)\n"
        "call glPolygonOffset\nret\n"
        ".globl CallOrthoRaw\nCallOrthoRaw:\n"
        "movl 4(%esp), %eax\n"
        "pushl 44(%eax)\npushl 40(%eax)\npushl 36(%eax)\npushl 32(%eax)\n"
        "pushl 28(%eax)\npushl 24(%eax)\npushl 20(%eax)\npushl 16(%eax)\n"
        "pushl 12(%eax)\npushl 8(%eax)\npushl 4(%eax)\npushl 0(%eax)\n"
        "call glOrtho\nret\n");

static unsigned short Status(void) {
    unsigned short status;
    __asm__ volatile("fnstsw %0" : "=am"(status));
    return status;
}
static void Reset(ULONG records, ULONG words) {
    memset(&Context, 0, sizeof(Context));
    memset(&Context.Packet, 0xa5, sizeof(Context.Packet));
    memset(Captured, 0x5a, sizeof(Captured));
    memset(CapturedWords, 0, sizeof(CapturedWords));
    Context.Id = 17;
    Context.DrawBuffer = GL_BACK;
    Current = 0;
    Client = 51;
    Display = (HDC)100;
    MaxRecords = records;
    MaxWords = words;
    Submissions = TransportError = TlsCalls = 0;
}
static int RecordEquals(const ULONG *record, ULONG function, const ULONG *arguments, ULONG words) {
    ULONG i;
    if (record[0] != DG_GL_CALL || record[1] != (9 + words) * 4 || record[2] || record[3] != 17 ||
        record[4] != 17 || record[5] || record[6] || record[7] || record[8] != function)
        return 0;
    for (i = 0; i < words; ++i)
        if (record[9 + i] != arguments[i])
            return 0;
    return 1;
}

static int Test(void) {
    static const ULONG colors[][4] = {{0x80000000, 1, 0x007fffff, 0x7f7fffff},
                                      {0x7f800123, 0xff800321, 0x7fc055aa, 0x7f800000}};
    static const ULONG doubles[12] = {0,          0x80000000, 1,          0,
                                      0xffffffff, 0x000fffff, 0xffffffff, 0x7fefffff,
                                      0x123,      0x7ff00000, 0x456,      0xfff80000};
    ULONG input[12], pattern, seed, i, triangle = 4;
    unsigned short before;
    for (pattern = 0; pattern < 2; ++pattern)
        for (seed = 0; seed < 2; ++seed) {
            Reset(DG_GL_MAX_RECORDS, PACKET_WORDS);
            __asm__ volatile("fninit" ::: "memory");
            if (seed)
                __asm__ volatile("fldz; fldz; fdivp; fstp %%st(0)" ::: "memory");
            before = Status();
            if (seed && !(before & 1))
                return 1;
            memcpy(input, colors[pattern], sizeof(colors[pattern]));
            CallColorRaw(input);
            memset(input, 0, sizeof(input));
            if (Status() != before)
                return 2;
            memcpy(input, doubles, sizeof(doubles));
            CallOrthoRaw(input);
            memset(input, 0, sizeof(input));
            if (Status() != before)
                return 3;
            if (Context.Used != 34 || Context.Records != 2 || TlsCalls != 2 ||
                !RecordEquals(Context.Packet.Words, FEnum_glColor4f, colors[pattern], 4) ||
                !RecordEquals(Context.Packet.Words + 13, FEnum_glOrtho, doubles, 12) ||
                Context.Packet.Words[34] != 0xa5a5a5a5)
                return 4;
            if (!Flush(&Context) || TransportError || Submissions != 1 || CapturedWords[0] != 34 ||
                Context.Used || Context.Records || Status() != before)
                return 5;
            memset(&Context.Packet, 0, sizeof(Context.Packet));
            if (!RecordEquals(Captured[0].Words, FEnum_glColor4f, colors[pattern], 4) ||
                !RecordEquals(Captured[0].Words + 13, FEnum_glOrtho, doubles, 12))
                return 6;

            /* The shared vector entry must also traverse real TLS/Scalar/Record,
             * snapshot caller data immediately, and preserve special-value bits. */
            Reset(DG_GL_MAX_RECORDS, PACKET_WORDS);
            memcpy(input, colors[pattern], 12);
            JglScalarVector(FEnum_glNormal3f, 3, input);
            memset(input, 0, sizeof(input));
            if (!Flush(&Context) || TransportError || TlsCalls != 1 || CapturedWords[0] != 12 ||
                !RecordEquals(Captured[0].Words, FEnum_glNormal3f, colors[pattern], 3) ||
                Status() != before)
                return 7;

            Reset(DG_GL_MAX_RECORDS, PACKET_WORDS);
            SecondarySupported = TRUE;
            CallSecondaryRaw(colors[pattern]);
            if (Status() != before || Context.Records != 1 ||
                !RecordEquals(Context.Packet.Words, FEnum_glSecondaryColor3f, colors[pattern], 3))
                return 12;
            /* Cached float state compares raw bits without consuming signaling
             * NaNs/denormals or changing the caller's live x87 exception flags. */
            Reset(DG_GL_MAX_RECORDS, PACKET_WORDS);
            CallOffsetRaw(colors[pattern]);
            CallOffsetRaw(colors[pattern]);
            if (Status() != before || Context.Records != (pattern ? 2 : 1) ||
                !RecordEquals(Context.Packet.Words, FEnum_glPolygonOffset, colors[pattern], 2))
                return 11;
        }

    /* Force the record budget to split inside Begin/End. Neither command may
     * repeat, and each public entry needs exactly one TLS lookup. */
    Reset(2, PACKET_WORDS);
    __asm__ volatile("fninit" ::: "memory");
    before = Status();
    glBegin(triangle);
    CallVertexRaw(colors[1]);
    glEnd();
    if (TlsCalls != 3 || Submissions != 1 || CapturedWords[0] != 22 ||
        !RecordEquals(Captured[0].Words, FEnum_glBegin, &triangle, 1) ||
        !RecordEquals(Captured[0].Words + 10, FEnum_glVertex3f, colors[1], 3) || Context.InBegin ||
        Context.Error || !Flush(&Context) || TransportError || Submissions != 2 ||
        CapturedWords[1] != 9 || !RecordEquals(Captured[1].Words, FEnum_glEnd, NULL, 0) ||
        Status() != before)
        return 8;

    /* Independently force the byte limit; capture both distinct immutable
     * batches through the real request/reply path. */
    Reset(DG_GL_MAX_RECORDS, 24);
    for (i = 0; i < 2; ++i)
        CallColorRaw(colors[i]);
    if (!Flush(&Context) || TransportError || Submissions != 2 || TlsCalls != 2 ||
        CapturedWords[0] != 13 || CapturedWords[1] != 13 || Status() != before)
        return 9;
    for (i = 0; i < 2; ++i)
        if (!RecordEquals(Captured[i].Words, FEnum_glColor4f, colors[i], 4))
            return 10;
    return 0;
}

extern "C" void _start(void) {
    int result = Test();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(result) : "memory");
    __builtin_unreachable();
}

void JglTransportInitialize(void) {}
int JglTransportRequest(HDC display, int input_bytes, LPCSTR input, int output_bytes,
                        LPSTR output) {
    return ExtEscape(display, DG_ESCAPE, input_bytes, input, output_bytes, output);
}

int JglTransportBind(HDC dc, const DG_WINDOW_BIND *request, DG_WINDOW_REPLY *reply) {
    return ExtEscape(dc, WNDOBJ_SETUP, sizeof(*request), (LPCSTR)request, sizeof(*reply),
                     (LPSTR)reply);
}
int JglTransportPresent(HDC dc, const DG_WINDOW_PRESENT *present) {
    return DrawEscape(dc, DG_DRAW_ESCAPE, sizeof(*present), (LPCSTR)present);
}
void JglTransportUnbind(ULONG binding) {
    (void)binding;
}
