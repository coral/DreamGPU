/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <initializer_list>
#include "frontend.cpp"

static void *Tls[4];
static HANDLE PixelFormats[3];
static ULONG Thread, Allocations, Calls, QueryCalls, NativeFlushes, Closes;
static ULONG DestroyContexts, DestroyDrawables, NativeBegins, NativeEnds, Sequence;
static ULONG FailOperation, FailStatus, FailError, LostOperation, MalformedQuery;
static BOOL ExecuteLost, FailPresent, FailTls, FailHeap, FailBind;
static ULONG RetiredPresents, UnreadyPresents, Binds;
static ULONG BindCapabilities, Presents, PresentFlags;
static ULONG SharedWith[128];
static BOOL HostContexts[128], HostDrawables[128];
static ULONG Width = 320, Height = 240;
static ULONG HostSecondaryWords;
static ULONG HostMaxRecords, HostMaxBytes, LargestSubmission;

void *TlsGetValue(DWORD key) {
    assert(key == 0);
    return Tls[Thread];
}
BOOL TlsSetValue(DWORD key, void *value) {
    assert(key == 0);
    if (value && FailTls)
        return FALSE;
    Tls[Thread] = value;
    return TRUE;
}
DWORD TlsAlloc(void) {
    return 0;
}
BOOL TlsFree(DWORD key) {
    assert(key == 0);
    return TRUE;
}
void InitializeCriticalSection(CRITICAL_SECTION *lock) {
    lock->Locked = 0;
}
void DeleteCriticalSection(CRITICAL_SECTION *lock) {
    assert(!lock->Locked);
}
void EnterCriticalSection(CRITICAL_SECTION *lock) {
    assert(!lock->Locked);
    lock->Locked = 1;
}
void LeaveCriticalSection(CRITICAL_SECTION *lock) {
    assert(lock->Locked);
    lock->Locked = 0;
}
LONG InterlockedExchange(volatile LONG *p, LONG value) {
    LONG old = *p;
    *p = value;
    return old;
}
HANDLE GetProcessHeap(void) {
    return (HANDLE)1;
}
void *HeapAlloc(HANDLE heap, DWORD flags, size_t bytes) {
    void *p;
    assert(heap == (HANDLE)1 && (flags == HEAP_ZERO_MEMORY || !flags));
    if (FailHeap)
        return NULL;
    p = calloc(1, bytes);
    assert(p);
    ++Allocations;
    return p;
}
BOOL HeapFree(HANDLE heap, DWORD flags, void *p) {
    assert(heap == (HANDLE)1 && !flags && Allocations);
    free(p);
    --Allocations;
    return TRUE;
}
DWORD GetCurrentThreadId(void) {
    return Thread + 1;
}
DWORD GetCurrentProcessId(void) {
    return 1;
}
static DWORD LastError;
void SetLastError(DWORD error) {
    LastError = error;
}
HANDLE GetPropA(HWND window, LPCSTR key) {
    (void)key;
    return PixelFormats[(ULONG_PTR)window];
}
BOOL SetPropA(HWND window, LPCSTR key, HANDLE value) {
    (void)key;
    PixelFormats[(ULONG_PTR)window] = value;
    return TRUE;
}
HWND WindowFromDC(HDC dc) {
    return dc == (HDC)1 || dc == (HDC)2 ? (HWND)dc : NULL;
}
DWORD GetWindowThreadProcessId(HWND window, DWORD *process) {
    assert(window);
    *process = 1;
    return 1;
}
BOOL GetClientRect(HWND window, RECT *rect) {
    assert(window);
    *rect = (RECT){0, 0, (LONG)Width, (LONG)Height};
    return TRUE;
}
HDC GetDC(HWND window) {
    assert(!window);
    return (HDC)100;
}
int ReleaseDC(HWND window, HDC dc) {
    assert(!window && dc == (HDC)100);
    return 1;
}
BOOL IsWindow(HWND window) {
    return window != NULL;
}
int DrawEscape(HDC dc, int escape, int bytes, LPCSTR input) {
    assert(dc && escape == DG_DRAW_ESCAPE && bytes == sizeof(DG_WINDOW_PRESENT) && input);
    ++Presents;
    PresentFlags = ((const DG_WINDOW_PRESENT *)input)->Flags;
    if (RetiredPresents) {
        --RetiredPresents;
        return DG_WINDOW_PRESENT_REBIND;
    }
    if (UnreadyPresents) {
        --UnreadyPresents;
        return DG_WINDOW_PRESENT_NOT_READY;
    }
    return !FailPresent;
}

static ULONG HostMaxResultBytes = DG_ESCAPE_MAX_RESULT_BYTES;

static ULONG Execute(const ULONG *r) {
    ULONG op = r[0], context = r[3], drawable = r[4];
    assert(context < 128 && drawable < 128);
    switch (op) {
        case DG_GL_CREATE_CONTEXT:
            assert(!HostContexts[context]);
            assert(!r[8] || HostContexts[r[8]]);
            SharedWith[context] = r[8];
            HostContexts[context] = TRUE;
            return 0;
        case DG_GL_CREATE_DRAWABLE:
            assert(!HostDrawables[drawable]);
            if ((uint64_t)r[8] * r[9] * 20 > DG_GL_MAX_IMAGE_BYTES)
                return DG_GL_ERROR_LIMIT;
            HostDrawables[drawable] = TRUE;
            return 0;
        case DG_GL_DESTROY_CONTEXT:
            ++DestroyContexts;
            if (!HostContexts[context])
                return DG_GL_ERROR_CONTEXT;
            HostContexts[context] = FALSE;
            return 0;
        case DG_GL_DESTROY_DRAWABLE:
            ++DestroyDrawables;
            if (!HostDrawables[drawable])
                return DG_GL_ERROR_DRAWABLE;
            HostDrawables[drawable] = FALSE;
            return 0;
        case DG_GL_MAKE_CURRENT:
            assert(HostContexts[context] && HostDrawables[drawable]);
            return 0;
        case DG_GL_CALL:
            assert(HostContexts[context] && HostDrawables[drawable]);
            if (r[8] == FEnum_glFlush)
                ++NativeFlushes;
            if (r[8] == FEnum_glBegin)
                ++NativeBegins;
            if (r[8] == FEnum_glEnd)
                ++NativeEnds;
            return 0;
        case DG_GL_QUERY:
            assert(HostContexts[context] && HostDrawables[drawable]);
            return 0;
        case DG_GL_DATA_CALL: {
            ULONG payload_end = r[1] - ((4 - (r[9] & 3)) & 3), i;
            assert(HostContexts[context] && HostDrawables[drawable]);
            for (i = payload_end; i < r[1]; ++i)
                assert(!((const BYTE *)r)[i]);
            return 0;
        }
        default:
            assert(!"unexpected native operation");
            return 0;
    }
}
int ExtEscape(HDC dc, int escape, int input_bytes, LPCSTR input, int output_bytes, LPSTR output) {
    DG_ESCAPE_REPLY *reply = (DG_ESCAPE_REPLY *)output;
    const DG_ESCAPE_REQUEST *request = (const DG_ESCAPE_REQUEST *)input;
    const ULONG *words = (const ULONG *)(request + 1);
    ULONG at = 0, records = 0;
    if (escape == WNDOBJ_SETUP) {
        const DG_WINDOW_BIND *bind = (const DG_WINDOW_BIND *)input;
        DG_WINDOW_REPLY *bound = (DG_WINDOW_REPLY *)output;
        assert(dc && input_bytes == sizeof(*bind) && output_bytes == sizeof(*bound));
        ++Binds;
        if (FailBind)
            return 0;
        *bound =
            (DG_WINDOW_REPLY){DG_WINDOW_VERSION, DG_ESCAPE_OK, bind->Drawable, BindCapabilities};
        return 1;
    }
    assert(dc == (HDC)100 && escape == DG_ESCAPE);
    assert(input_bytes >= 0 && (size_t)input_bytes == sizeof(*request) + request->Bytes &&
           request->Bytes <= DG_ESCAPE_MAX_BYTES);
    assert(output_bytes >= 0 && (size_t)output_bytes == sizeof(*reply) + request->ResultCapacity);
    assert((request->Operation == DG_ESCAPE_QUERY || !request->Function) && !request->Reserved &&
           !request->Reserved2);
    ZeroMemory(output, output_bytes);
    ++Calls;
    reply->Version = DG_ESCAPE_VERSION;
    reply->Status = DG_ESCAPE_OK;
    reply->MaxBytes = HostMaxBytes;
    reply->MaxRecords = HostMaxRecords;
    reply->MaxResultBytes = HostMaxResultBytes;
    reply->Client = 1;
    if (request->Operation == DG_ESCAPE_OPEN) {
        assert(!request->Client);
        return 1;
    }
    assert(request->Client == 1);
    if (request->Operation == DG_ESCAPE_QUERY) {
        assert(request->Function == FEnum_glSecondaryColor3f);
        reply->FunctionWords = HostSecondaryWords;
        return 1;
    }
    if (request->Operation == DG_ESCAPE_CLOSE) {
        ++Closes;
        for (at = 0; at < 128; ++at)
            assert(!HostContexts[at] && !HostDrawables[at]);
        return 1;
    }
    assert(request->Operation == DG_ESCAPE_SUBMIT);
    assert(request->Bytes <= HostMaxBytes);
    reply->CompletedSequence = ++Sequence;
    while (at * 4 < request->Bytes) {
        const ULONG *r = words + at;
        ULONG error;
        ++records;
        assert(records <= HostMaxRecords && r[1] >= 32 && !(r[1] & 3));
        assert(r[1] <= request->Bytes - at * 4);
        assert(!r[2] && !r[5] && !r[6] && !r[7]);
        if (r[0] == FailOperation) {
            FailOperation = 0;
            reply->Status = FailStatus;
            reply->DeviceError = FailError;
            if (FailStatus == DG_ESCAPE_TIMEOUT)
                reply->CompletedSequence = 0;
            return 1;
        }
        if (r[0] == LostOperation) {
            LostOperation = 0;
            if (ExecuteLost)
                Execute(r);
            ZeroMemory(reply, sizeof(*reply));
            return 0;
        }
        error = Execute(r);
        if (error) {
            reply->Status = DG_ESCAPE_HOST;
            reply->DeviceError = error;
            return 1;
        }
        if (r[0] == DG_GL_QUERY) {
            assert(records == 1 && request->Bytes == 48);
            ++QueryCalls;
            if (r[8] == FEnum_glReadPixels) {
                assert(request->ResultCapacity <= HostMaxResultBytes);
                reply->ResultBytes = request->ResultCapacity;
                reply->ResultType = DG_GL_RESULT_INT;
                memset(reply + 1, 0x5a, request->ResultCapacity);
            } else if (r[8] == FEnum_glIsTexture) {
                assert(request->ResultCapacity == 1);
                reply->ResultBytes = 1;
                reply->ResultType = DG_GL_RESULT_BOOL;
                *((BYTE *)(reply + 1)) = r[9] == 3; /* explicit bound name */
            } else {
                assert(r[8] == FEnum_glGetError && request->ResultCapacity == 4);
                reply->ResultBytes = MalformedQuery ? 2 : 4;
                reply->ResultType = DG_GL_RESULT_INT;
            }
        }
        at += r[1] / 4;
    }
    if (records > LargestSubmission)
        LargestSubmission = records;
    return 1;
}

static void Reset(void) {
    ULONG i;
    /* Test process exit reclaims deliberately retained uncertain ownership. */
    for (i = 0; i < MAX_JGL_CONTEXTS; ++i)
        if (Contexts[i]) {
            ReleaseNames(Contexts[i]);
            FreeContext(Contexts[i]);
            Contexts[i] = NULL;
        }
    assert(!Allocations);
    ZeroMemory(PixelFormats, sizeof(PixelFormats));
    ZeroMemory(Tls, sizeof(Tls));
    ZeroMemory(HostContexts, sizeof(HostContexts));
    ZeroMemory(HostDrawables, sizeof(HostDrawables));
    Thread = Client = NextId = Calls = QueryCalls = NativeFlushes = Closes = 0;
    DestroyContexts = DestroyDrawables = NativeBegins = NativeEnds = Sequence = 0;
    FailOperation = FailStatus = FailError = LostOperation = MalformedQuery = 0;
    ExecuteLost = FailPresent = FailTls = FailBind = FALSE;
    RetiredPresents = UnreadyPresents = Binds = 0;
    Width = 320;
    Height = 240;
    MaxWords = PACKET_WORDS;
    MaxRecords = DG_GL_MAX_RECORDS;
    HostMaxResultBytes = DG_ESCAPE_MAX_RESULT_BYTES;
    HostMaxRecords = DG_GL_MAX_RECORDS;
    HostMaxBytes = DG_ESCAPE_MAX_BYTES;
    LargestSubmission = HostSecondaryWords = 0;
    assert(DllMain(NULL, DLL_PROCESS_ATTACH, NULL));
}
static HGLRC Create(void) {
    HGLRC context = wglCreateContext((HDC)1);
    assert(context);
    assert(wglMakeCurrent((HDC)1, context));
    return context;
}
static void ErrorDrains(void) {
    ULONG i, calls = Calls;
    assert(glGetError() == GL_INVALID_OPERATION);
    for (i = 0; i < 1000; ++i)
        assert(glGetError() == GL_NO_ERROR);
    assert(Calls == calls);
}
static ULONG Captures;
static void CapturePacket(void *opaque, BYTE *destination) {
    JGL_CONTEXT *c = CurrentContext();
    assert(destination >= (BYTE *)c->Packet.Words &&
           destination + 3 <= (BYTE *)(c->Packet.Words + MaxWords));
    memcpy(destination, opaque, 3);
    ++Captures;
}
int main(void) {
    HGLRC context, other;
    JGL_CONTEXT *c;
    ULONG calls, flushes, destroyed, i;

    Reset();
    FailHeap = TRUE;
    assert(!wglCreateContext((HDC)1) && !Allocations && !Client && !Lock.Locked);
    FailHeap = FALSE;
    Reset();
    context = Create();
    c = Lookup(context);
    assert(!JglSupportsSecondary());
    glSecondaryColor3fEXT(1, 0, 0);
    assert(c->Error == GL_INVALID_OPERATION && !c->Records);
    c->Error = 0;
    glEnable(0x8458);
    assert(c->Error == GL_INVALID_ENUM && !c->Records);
    c->Error = 0;
    Reset();
    HostSecondaryWords = 3;
    context = Create();
    c = Lookup(context);
    assert(JglSupportsSecondary());
    assert(c->Arrays.Attribute[4].Size == 3);
    glBegin(GL_TRIANGLES);
    glSecondaryColor3fEXT(1, 0, 0);
    glEnd();
    assert(c->Records == 3 && !c->Error);
    /* Drop repeated valid state before marshalling, while preserving errors,
     * attribute restores and independent contexts. No host query is needed. */
    Reset();
    context = Create();
    c = Lookup(context);
    calls = QueryCalls;
    for (i = 0; i < 1000; ++i) {
        glEnable(0x0be2);
        glBlendFunc(0x0302, 0x0303);
        glPolygonOffset(1.0f, 2.0f);
    }
    assert(c->Records == 3 && QueryCalls == calls);
    glDisable(0x0be2);
    glDisable(0x0be2);
    glEnable(0x0be2);
    assert(c->Records == 5);
    /* Unknown enums are never cached, even if the preceding call is equal. */
    glEnable(0xdead);
    glEnable(0xdead);
    assert(c->Records == 7);
    glBlendFunc(0x0308, 0x0308);
    glBlendFunc(0x0308, 0x0308);
    assert(c->Records == 9);
    /* A repeated setter is still illegal inside Begin/End. */
    glBegin(GL_TRIANGLES);
    glEnable(0x0be2);
    assert(c->Error == GL_INVALID_OPERATION);
    assert(c->Records == 10);
    c->Error = GL_NO_ERROR;
    glEnd();
    glPushAttrib(0x000fffff);
    glDisable(0x0be2);
    glPopAttrib();
    i = c->Records;
    glDisable(0x0be2);
    assert(c->Records == i + 1); /* Must override restored enable. */
    glBlendFunc(0x0302, 0x0303);
    glPolygonOffset(1.0f, 2.0f);
    assert(c->Records == i + 3);
    other = wglCreateContext((HDC)2);
    assert(other);
    assert(wglMakeCurrent((HDC)2, other));
    glEnable(0x0be2);
    assert(Lookup(other)->Records == 1);
    assert(wglMakeCurrent((HDC)1, context));
    i = c->Records;
    glDisable(0x0be2);
    assert(c->Records == i);

    /* Bulk replies are lazy context-owned heap storage, never scalar stack
     * expansion; old drivers still constrain every query to512bytes. */
    for (ULONG maximum = 512; maximum <= 65536; maximum *= 128) {
        ULONG args[3] = {0, 0, 128 | (128 << 16)}, bytes;
        BYTE *output = (BYTE *)malloc(65540);
        ULONG allocations;
        Reset();
        HostMaxResultBytes = maximum;
        context = Create();
        assert(JglReadbackCapacity() == maximum);
        assert(JglReadbackBuffer());
        memset(output, 0xcc, 65540);
        calls = Calls;
        if (maximum == 512) {
            assert(!JglQuery(FEnum_glReadPixels, args, DG_GL_RESULT_INT, output, 65536, &bytes));
            assert(Calls == calls);
        } else {
            assert(JglQuery(FEnum_glReadPixels, args, DG_GL_RESULT_INT, output, 65536, &bytes));
            assert(bytes == 65536);
            for (i = 0; i < 65536; ++i)
                assert(output[i] == 0x5a);
            for (; i < 65540; ++i)
                assert(output[i] == 0xcc);
            allocations = Allocations;
            assert(JglQuery(FEnum_glReadPixels, args, DG_GL_RESULT_INT, output, 65536, &bytes));
            assert(Allocations == allocations);
        }
        assert(wglMakeCurrent(NULL, NULL));
        assert(wglDeleteContext(context));
        assert(!Allocations);
        free(output);
    }
    /* A newer DLL must honor an older driver's 256-record reply. The new
     * 1024-record budget also splits inside Begin without changing GL order. */
    for (ULONG budget = 256; budget <= 1024; budget *= 4) {
        Reset();
        HostMaxRecords = budget;
        context = Create();
        assert(MaxRecords == budget);
        glBegin(4 /* GL_TRIANGLES */);
        for (i = 0; i < budget + 6; ++i)
            glVertex3f(1.0f, 2.0f, 3.0f);
        glEnd();
        glFlush();
        assert(LargestSubmission == budget && NativeBegins == 1 && NativeEnds == 1);
        assert(wglMakeCurrent(NULL, NULL));
        assert(wglDeleteContext(context));
        assert(!Allocations);
    }
    /* The byte budget is independent of the record budget. */
    Reset();
    HostMaxBytes = 4096;
    context = Create();
    assert(MaxWords == 1024);
    glBegin(4 /* GL_TRIANGLES */);
    for (i = 0; i < 1026; ++i)
        glVertex3f(1.0f, 2.0f, 3.0f);
    glEnd();
    glFlush();
    assert(LargestSubmission < MaxRecords && NativeBegins == 1 && NativeEnds == 1);
    assert(wglMakeCurrent(NULL, NULL));
    assert(wglDeleteContext(context));
    assert(!Allocations);

    {
        PIXELFORMATDESCRIPTOR requested, described;
        Reset();
        requested = Format();
        requested.cDepthBits = 32;
        assert(wglChoosePixelFormat((HDC)1, &requested) == 1);
        assert(wglDescribePixelFormat((HDC)1, 0, 0, NULL) == 1);
        assert(!wglDescribePixelFormat((HDC)1, 0, sizeof(described), &described));
        ZeroMemory(&described, sizeof(described));
        assert(wglDescribePixelFormat((HDC)1, 1, 2, &described) == 1 && described.nSize == 2);
        assert(wglDescribePixelFormat((HDC)1, 1, sizeof(described), &described) == 1);
        assert(described.cDepthBits == 24 && described.cStencilBits == 8);
        assert(wglSetPixelFormat((HDC)1, 1, &requested));
        assert(wglGetPixelFormat((HDC)1) == 1 && !wglGetPixelFormat((HDC)2));
        assert(!wglSetPixelFormat((HDC)1, 1, NULL));
        assert(!wglSetPixelFormat((HDC)1, 2, &requested));
        requested.cColorBits = 64;
        requested.cAlphaBits = 16;
        requested.cAccumBits = 64;
        requested.cStencilBits = 16;
        assert(wglChoosePixelFormat((HDC)1, &requested) == 1);
        requested.dwFlags |= PFD_DRAW_TO_BITMAP;
        assert(!wglChoosePixelFormat((HDC)1, &requested));
        requested = Format();
        requested.dwFlags |= PFD_STEREO;
        assert(!wglChoosePixelFormat((HDC)1, &requested));
        requested = Format();
        requested.nSize = 0;
        assert(!wglChoosePixelFormat((HDC)1, &requested));
        requested = Format();
        requested.iPixelType = 1;
        assert(!wglChoosePixelFormat((HDC)1, &requested));
        requested = Format();
        assert(!wglChoosePixelFormat(NULL, &requested));
        assert(!wglChoosePixelFormat((HDC)1, NULL));
        assert(!Calls && !Allocations);
    }

    Reset();
    context = Create();
    c = Lookup(context);
    {
        ULONG args[8] = {0}, expected[4];
        BYTE pixel[3] = {0x81, 0x22, 0x7f};
        const GLfloat color[4] = {-0.0f, 0.25f, -0.5f, 1.0f};
        /* Reused packets contain arbitrary prior bytes. Headers, scalar bit
         * patterns and the final DATA word must still be fully initialized. */
        memset(c->Packet.Words, 0xa5, sizeof(c->Packet.Words));
        memcpy(expected, color, sizeof(expected));
        glColor4f(color[0], color[1], color[2], color[3]);
        assert(c->Packet.Words[8] == FEnum_glColor4f);
        assert(!memcmp(c->Packet.Words + 9, expected, sizeof(expected)));
        assert(JglData(FEnum_glTexImage2D, args, 8, pixel, sizeof(pixel)));
        ULONG prior = c->Used;
        Captures = 0;
        assert(JglCaptureData(FEnum_glTexImage2D, args, 8, 3, CapturePacket, pixel));
        assert(Captures == 1 && !memcmp(c->Packet.Words + prior + 18, pixel, 3));
        assert(((BYTE *)(c->Packet.Words + prior + 18))[3] == 0);
        assert(!JglCaptureData(FEnum_glTexImage2D, args, 8, JglMaxDataBytes(8) + 1, CapturePacket,
                               pixel) &&
               Captures == 1);
        assert(Flush(c));
        assert(wglDeleteContext(context));
    }

    Reset();
    BindCapabilities = DG_WINDOW_CAP_FRONT_ONLY;
    context = Create();
    // A same-size display deactivate/reactivate retires only the GDI binding.
    // Retrying that binding must not recreate the drawable or resubmit GL.
    glClear(GL_COLOR_BUFFER_BIT);
    calls = Calls;
    ULONG bindings = Binds, presentations = Presents;
    RetiredPresents = 1;
    assert(wglSwapBuffers((HDC)1));
    assert(Binds == bindings + 1 && Presents == presentations + 2 && Calls == calls + 1);
    assert(!DestroyDrawables && !Lookup(context)->Failed && wglGetCurrentContext() == context);
    // A second loss is bounded: return failure, preserve context, retry on
    // the application's following call rather than spinning or replaying GL.
    RetiredPresents = 2;
    bindings = Binds;
    presentations = Presents;
    calls = Calls;
    assert(!wglSwapBuffers((HDC)1) && LastError == ERROR_BUSY);
    assert(Binds == bindings + 1 && Presents == presentations + 2 && Calls == calls);
    assert(!Lookup(context)->Failed && !Lookup(context)->Uncertain);
    assert(wglSwapBuffers((HDC)1));
    // Temporarily invalid GDI clip snapshots do not retire the GL context.
    UnreadyPresents = 1;
    bindings = Binds;
    assert(!wglSwapBuffers((HDC)1) && Binds == bindings && !Lookup(context)->Failed);
    assert(wglSwapBuffers((HDC)1));
    // A mode transition may refuse rebinding until the window is ready.
    RetiredPresents = 1;
    FailBind = TRUE;
    assert(!wglSwapBuffers((HDC)1) && !Lookup(context)->Failed);
    FailBind = FALSE;
    RetiredPresents = 1;
    assert(wglSwapBuffers((HDC)1));
    // Front-only publication uses the same recovery without a buffer swap.
    glDrawBuffer(GL_FRONT);
    glClear(GL_COLOR_BUFFER_BIT);
    RetiredPresents = 1;
    bindings = Binds;
    assert(PublishFront(Lookup(context)) && Binds == bindings + 1);
    assert(PresentFlags == DG_WINDOW_PRESENT_FRONT_ONLY && !Lookup(context)->FrontDirty);
    glClear(GL_COLOR_BUFFER_BIT);
    UnreadyPresents = 1;
    assert(!PublishFront(Lookup(context)) && Lookup(context)->FrontDirty &&
           !Lookup(context)->Failed);
    assert(PublishFront(Lookup(context)) && !Lookup(context)->FrontDirty);
    assert(wglDeleteContext(context));
    BindCapabilities = 0;

    Reset();
    context = Create();
    FailPresent = TRUE;
    assert(!wglSwapBuffers((HDC)1));
    assert(LastError == ERROR_GEN_FAILURE);
    ErrorDrains();
    assert(wglMakeCurrent(NULL, NULL) && !wglGetCurrentContext());
    assert(wglDeleteContext(context) && !Allocations && Closes == 1);

    Reset();
    context = Create();
    MalformedQuery = TRUE;
    assert(glGetError() == GL_INVALID_OPERATION);
    calls = Calls;
    for (i = 0; i < 1000; ++i)
        assert(glGetError() == GL_NO_ERROR);
    assert(Calls == calls);
    assert(wglDeleteContext(context));

    Reset();
    context = Create();
    c = Lookup(context);
    glClear(GL_COLOR_BUFFER_BIT);
    flushes = NativeFlushes;
    assert(!wglMakeCurrent((HDC)1, (HGLRC)999));
    assert(!wglGetCurrentContext() && !wglGetCurrentDC() && !c->Owner &&
           NativeFlushes == flushes + 1);
    assert(wglMakeCurrent((HDC)1, context));
    flushes = NativeFlushes;
    assert(wglMakeCurrent((HDC)1, context) && NativeFlushes == flushes + 1);
    flushes = NativeFlushes;
    assert(wglMakeCurrent(NULL, NULL) && NativeFlushes == flushes + 1);
    Thread = 1;
    assert(wglMakeCurrent((HDC)1, context));
    Thread = 0;
    other = Create();
    assert(!wglMakeCurrent((HDC)1, context) && !wglGetCurrentContext());
    assert(!Lookup(other)->Owner && c->Owner == 2);
    assert(!wglDeleteContext(context));
    assert(wglDeleteContext(other));
    Thread = 1;
    assert(wglDeleteContext(context));
    Thread = 0;

    Reset();
    Width = Height = 4096;
    context = wglCreateContext((HDC)1);
    assert(context);
    FailOperation = DG_GL_CREATE_DRAWABLE;
    FailStatus = DG_ESCAPE_HOST;
    FailError = DG_GL_ERROR_LIMIT;
    assert(!wglMakeCurrent((HDC)1, context));
    c = Lookup(context);
    assert(c->Created && !c->Drawable && c->Failed && !c->Uncertain);
    assert(wglDeleteContext(context) && DestroyContexts == 1 && !DestroyDrawables && !Allocations);

    Reset();
    context = Create();
    c = Lookup(context);
    FailOperation = DG_GL_DESTROY_DRAWABLE;
    FailStatus = DG_ESCAPE_RESOURCES;
    assert(!wglDeleteContext(context));
    assert(!c->Created && c->Drawable && c->Deleting && !wglGetCurrentContext());
    destroyed = DestroyContexts;
    assert(!wglMakeCurrent((HDC)1, context));
    assert(wglDeleteContext(context) && DestroyContexts == destroyed && DestroyDrawables == 1);

    Reset();
    context = Create();
    c = Lookup(context);
    LostOperation = DG_GL_DESTROY_CONTEXT;
    ExecuteLost = TRUE;
    assert(!wglDeleteContext(context));
    assert(c->Created && c->Uncertain && c->Deleting);
    assert(!HostContexts[c->Id] && HostDrawables[c->Id]);
    calls = Calls;
    assert(!wglDeleteContext(context) && Calls == calls && Allocations == 1 && !Closes);

    Reset();
    LostOperation = DG_GL_CREATE_CONTEXT;
    ExecuteLost = TRUE;
    assert(!wglCreateContext((HDC)1));
    assert(Allocations == 1 && Contexts[0]->Uncertain && HostContexts[1] && !Closes);

    Reset();
    FailOperation = DG_GL_CREATE_CONTEXT;
    FailStatus = DG_ESCAPE_RESOURCES;
    assert(!wglCreateContext((HDC)1) && !Allocations && Closes == 1 && !Client);
    context = Create();
    assert(wglDeleteContext(context));

    Reset();
    context = Create();
    c = Lookup(context);
    glBegin(GL_QUADS);
    FailOperation = DG_GL_CALL;
    FailStatus = DG_ESCAPE_TIMEOUT;
    for (i = 0; i < MaxRecords + 4; ++i)
        glVertex2f(0, 0);
    assert(c->Failed && c->Uncertain && c->InBegin);
    assert(wglMakeCurrent(NULL, NULL) && !c->Owner);
    calls = Calls;
    assert(!wglDeleteContext(context) && Calls == calls);

    Reset();
    context = Create();
    glBegin(GL_QUADS);
    glClear(GL_COLOR_BUFFER_BIT);
    for (i = 0; i < 1000; ++i)
        glVertex2f(0, 0);
    glEnd();
    assert(glGetError() == GL_INVALID_OPERATION);
    glFlush();
    assert(NativeBegins == 1 && NativeEnds == 1 && glGetError() == GL_NO_ERROR);
    assert(wglDeleteContext(context));

    Reset();
    assert(!wglSwapBuffers((HDC)1) && LastError == ERROR_INVALID_HANDLE);
    context = Create();
    assert(!wglSwapBuffers((HDC)2) && LastError == ERROR_INVALID_HANDLE);
    glBegin(GL_QUADS);
    assert(!wglSwapBuffers((HDC)1) && LastError == ERROR_BUSY);
    glEnd();
    assert(wglDeleteContext(context));

    Reset();
    BindCapabilities = DG_WINDOW_CAP_FRONT_ONLY;
    context = Create();
    calls = Presents;
    glDrawBuffer(GL_FRONT);
    glClear(GL_COLOR_BUFFER_BIT);
    glFlush();
    assert(Presents == calls + 1 && PresentFlags == DG_WINDOW_PRESENT_FRONT_ONLY);
    glFlush();
    assert(Presents == calls + 1); /* No idle re-publication. */
    glDrawBuffer(GL_BACK);
    glClear(GL_COLOR_BUFFER_BIT);
    glFlush();
    assert(Presents == calls + 1);
    assert(wglSwapBuffers((HDC)1) && Presents == calls + 2 && PresentFlags == 0);
    glDrawBuffer(GL_FRONT);
    glBegin(GL_QUADS);
    glVertex2f(0, 0);
    glEnd();
    assert(wglMakeCurrent(NULL, NULL) && Presents == calls + 3 &&
           PresentFlags == DG_WINDOW_PRESENT_FRONT_ONLY);
    assert(wglDeleteContext(context));
    BindCapabilities = 0;
    Reset();
    BindCapabilities = DG_WINDOW_CAP_FRONT_ONLY;
    context = Create();
    calls = Presents;
    glDrawBuffer(GL_FRONT);
    glClear(GL_COLOR_BUFFER_BIT);
    assert(wglDeleteContext(context) && Presents == calls + 1 &&
           PresentFlags == DG_WINDOW_PRESENT_FRONT_ONLY);
    Reset();
    context = Create();
    glDrawBuffer(GL_FRONT);
    glClear(GL_COLOR_BUFFER_BIT);
    calls = Presents;
    Width = 640;
    assert(wglSwapBuffers((HDC)1) && !Lookup(context)->Failed && Presents == calls + 1);
    assert(wglGetCurrentContext() == context && Lookup(context)->Width == 640);
    assert(DestroyDrawables == 1 && !DestroyContexts && HostDrawables[Lookup(context)->Id]);
    {
        ULONG destroyed = DestroyDrawables;
        assert(wglSwapBuffers((HDC)1) && DestroyDrawables == destroyed);
    }
    glClear(GL_COLOR_BUFFER_BIT);
    glFlush();
    assert(Presents == calls + 3);
    assert(wglDeleteContext(context));
    BindCapabilities = 0;
    /* Failed resize must never present the old attachment, retain thread
     * ownership, or leak the already-destroyed drawable during cleanup. */
    Reset();
    context = Create();
    calls = Presents;
    Width = 640;
    FailOperation = DG_GL_CREATE_DRAWABLE;
    FailStatus = DG_ESCAPE_HOST;
    FailError = DG_GL_ERROR_LIMIT;
    assert(!wglSwapBuffers((HDC)1) && Presents == calls && !wglGetCurrentContext());
    assert(!Lookup(context)->Owner && !Lookup(context)->Drawable && Lookup(context)->Failed);
    assert(wglDeleteContext(context) && !Allocations && DestroyDrawables == 1);
    for (ULONG operation : {DG_GL_DESTROY_DRAWABLE, DG_GL_CREATE_DRAWABLE}) {
        Reset();
        context = Create();
        c = Lookup(context);
        calls = Presents;
        Width = 640;
        LostOperation = operation;
        ExecuteLost = TRUE;
        assert(!wglSwapBuffers((HDC)1) && Presents == calls && !wglGetCurrentContext());
        assert(c->Uncertain && !c->Owner);
        assert(HostDrawables[c->Id] == (operation == DG_GL_CREATE_DRAWABLE));
        calls = Calls;
        assert(!wglDeleteContext(context) && Calls == calls && Allocations == 1);
    }
    Reset();
    context = Create();
    glDrawBuffer(GL_FRONT);
    assert(glGetError() == GL_INVALID_OPERATION && !Lookup(context)->Failed);
    assert(wglDeleteContext(context));

    Reset();
    context = Create();
    {
        GLuint names[4] = {0}, next = 0;
        HGLRC dst;
        ULONG queries = QueryCalls;
        glBindTexture(GL_TEXTURE_2D, 3);
        assert(glIsTexture(3));
        glGenTextures(4, names);
        assert(names[0] == 1 && names[1] == 2 && names[2] == 4 && names[3] == 5);
        assert(Lookup(context)->Names->Count == 5 && !glIsTexture(names[0]) &&
               QueryCalls == queries);
        dst = wglCreateContext((HDC)2);
        assert(wglShareLists(context, dst));
        assert(wglMakeCurrent((HDC)2, dst));
        glGenTextures(1, &next);
        assert(next == 6);
        assert(Lookup(context)->Names == Lookup(dst)->Names);
        JglForgetTextures(4, names);
        assert(Lookup(dst)->Names->Count == 2 && glIsTexture(3));
        assert(wglDeleteContext(context));
        glGenTextures(1, &next);
        assert(next == 7);
        assert(wglDeleteContext(dst) && !Allocations);
    }

    /* Collision/tombstone chains, wrapping generated IDs and bounded fullness. */
    Reset();
    context = Create();
    {
        GLuint names[DG_GL_MAX_TEXTURES], collision = 1 + JGL_NAME_SLOTS, next[2];
        ULONG queries = QueryCalls;
        glBindTexture(GL_TEXTURE_2D, 1);
        glBindTexture(GL_TEXTURE_2D, collision);
        assert(glIsTexture(1) && glIsTexture(collision) && !glIsTexture(0));
        names[0] = 1;
        JglForgetTextures(1, names);
        assert(!glIsTexture(1) && glIsTexture(collision));
        glBindTexture(GL_TEXTURE_1D, collision);
        assert(glGetError() == GL_INVALID_OPERATION);
        names[0] = collision;
        JglForgetTextures(1, names);
        glBindTexture(GL_TEXTURE_1D, collision);
        assert(glIsTexture(collision));
        JglForgetTextures(1, names);
        Lookup(context)->Names->Next = 0xffffffffu;
        glGenTextures(2, next);
        assert(next[0] == 0xffffffffu && next[1] == 1 && !glIsTexture(next[0]));
        JglForgetTextures(2, next);
        glGenTextures(DG_GL_MAX_TEXTURES, names);
        assert(Lookup(context)->Names->Count == DG_GL_MAX_TEXTURES);
        glBindTexture(GL_TEXTURE_2D, names[0]);
        assert(glIsTexture(names[0]));
        next[0] = 123;
        glGenTextures(1, next);
        assert(glGetError() == GL_OUT_OF_MEMORY && next[0] == 123);
        glBindTexture(GL_TEXTURE_2D, 0xfffffffeu);
        assert(glGetError() == GL_OUT_OF_MEMORY && !glIsTexture(0xfffffffeu));
        assert(QueryCalls == queries); /* None of this name traffic submits a query. */
        JglForgetTextures(DG_GL_MAX_TEXTURES, names);
        assert(!Lookup(context)->Names->Count);
        glGenTextures(1, next);
        assert(!glIsTexture(next[0]));
        assert(wglDeleteContext(context) && !Allocations);
    }

    Reset();
    BindCapabilities = DG_WINDOW_CAP_FRONT_ONLY;
    context = Create();
    c = Lookup(context);
    glDrawBuffer(GL_FRONT);
    glPushAttrib(GL_COLOR_BUFFER_BIT);
    glDrawBuffer(GL_BACK);
    glPopAttrib();
    assert(c->DrawBuffer == GL_FRONT && !c->Arrays.ServerDepth);
    for (i = 0; i < 16; ++i)
        glPushAttrib(1);
    assert(c->Arrays.ServerDepth == 16);
    glPushAttrib(1);
    assert(glGetError() == 0x503);
    for (i = 0; i < 16; ++i)
        glPopAttrib();
    glPopAttrib();
    assert(glGetError() == 0x504);
    assert(wglDeleteContext(context));
    BindCapabilities = 0;

    /* Sharing must preserve the source, reject a used/current destination,
     * and retain uncertain destruction for process cleanup. */
    Reset();
    context = Create();
    {
        HGLRC dst = wglCreateContext((HDC)2);
        ULONG id = Lookup(dst)->Id;
        assert(dst && wglShareLists(context, dst) && SharedWith[id] == Lookup(context)->Id);
        assert(wglGetCurrentContext() == context);
        assert(wglMakeCurrent((HDC)2, dst));
        calls = Calls;
        assert(!wglShareLists(context, dst) && Calls == calls);
        assert(wglDeleteContext(context) && wglDeleteContext(dst));
    }
    Reset();
    context = Create();
    {
        HGLRC dst = wglCreateContext((HDC)2);
        calls = Calls;
        Thread = 1;
        assert(!wglShareLists(context, dst) && Calls == calls);
        Thread = 0;
        LostOperation = DG_GL_DESTROY_CONTEXT;
        ExecuteLost = TRUE;
        assert(!wglShareLists(context, dst) && Lookup(dst)->Uncertain);
        calls = Calls;
        assert(!wglDeleteContext(dst) && Calls == calls);
    }

    Reset();
    context = Create();
    c = Lookup(context);
    assert(JglListMode(GL_COMPILE));
    const ULONG errorStart = c->Used;
    glBegin(GL_POLYGON + 1);
    assert(!c->InBegin && !c->Error && c->Records == 1);
    assert(c->Packet.Words[errorStart + 8] == DG_GL_RECORD_ERROR &&
           c->Packet.Words[errorStart + 9] == GL_INVALID_ENUM);
    glBegin(GL_TRIANGLES);
    glDrawBuffer(0xdead); /* Illegal inside Begin is deferred, without changing it. */
    assert(c->InBegin && !c->Error && c->Records == 3);
    glEnd();
    JglSetError(GL_INVALID_VALUE); /* Immediate query/client-state error stays local. */
    assert(c->Error == GL_INVALID_VALUE && c->Records == 4);
    c->Error = 0;
    assert(JglListMode(0));
    assert(wglDeleteContext(context));

    Reset();
    context = Create();
    c = Lookup(context);
    DllMain(NULL, DLL_THREAD_DETACH, NULL);
    assert(!c->Owner);
    Tls[0] = NULL;
    Thread = 1;
    assert(wglMakeCurrent((HDC)1, context));
    assert(DllMain(NULL, DLL_PROCESS_DETACH, (void *)1) && Allocations == 1 && !Closes);
    Thread = 0;
    Reset();
    assert(DllMain(NULL, DLL_PROCESS_DETACH, NULL));
    puts("PASS actual frontend: error drain, native flush, ownership, rejected allocation, "
         "independent teardown, uncertain completion, Begin batches, loader cleanup");
    return 0;
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
