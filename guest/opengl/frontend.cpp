/* SPDX-License-Identifier: GPL-2.0-or-later
 * NT5 WGL frontend for the DreamGPU command ABI. This first slice exports the exact
 * scalar inventory below; it is not yet a complete OpenGL implementation/ICD.
 * Calls are batched per context. No user pointer enters the kernel ABI.
 */
/* This translation unit only marshals floating-point argument bits. Keeping
 * all of its helpers on the same integer-only target permits constant
 * specialization through the public wrappers without introducing x87 loads.
 * Actual floating-point arithmetic lives in the other translation units. */
#if defined(__i386__)
#if defined(__clang__)
#pragma clang attribute push(__attribute__((target("general-regs-only"))), apply_to = function)
#else
#pragma GCC push_options
#pragma GCC target("general-regs-only")
#endif
#endif
#define JGL_INLINE static inline __attribute__((always_inline))

#define WIN32_LEAN_AND_MEAN
#define _GDI32_
#include <windows.h>
#include <GL/gl.h>
#include "dg-escape.h"
#include "dg-window.h"
#include "gl.h"
#include "gl-funcs.h"
#include "internal.h"
#include "packing.h"
#include "transport.h"
#include "ownership.hpp"

extern "C" {

#define MAX_JGL_CONTEXTS 32
#define PACKET_WORDS (DG_ESCAPE_MAX_BYTES / 4)
typedef struct {
    DG_ESCAPE_REQUEST Request;
    ULONG Words[PACKET_WORDS];
} PACKET;
#include "names.h"
#include "state.h"
typedef struct {
    ULONG Id, Used, Records, Binding, Width, Height, Capabilities, ImageId;
    volatile LONG Owner;
    HDC DC;
    HWND Window;
    BOOL Created, Drawable, Failed, InBegin, Deleting, Uncertain, FrontDirty, EverCurrent,
        ShareEstablished;
    GLenum DrawBuffer;
    GLenum Error;
    JGL_UNPACK Unpack, Pack;
    JGL_ARRAY_STATE Arrays;
    JGL_NAMES *Names;
    JGL_STATE_CACHE State;
    void *LargeQuery;
    ULONG *Readback;
    PACKET Packet;
} JGL_CONTEXT;
static CRITICAL_SECTION Lock;
static DWORD Current = TLS_OUT_OF_INDEXES;
static JGL_CONTEXT *Contexts[MAX_JGL_CONTEXTS];
static ULONG Client, NextId;
static ULONG MaxWords = PACKET_WORDS, MaxRecords = DG_GL_MAX_RECORDS;
static ULONG MaxResultBytes = DG_ESCAPE_LEGACY_RESULT_BYTES;
static BOOL SecondarySupported;
static HDC Display;
static HANDLE Heap;
static HINSTANCE Module;

HINSTANCE JglModule(void) {
    return Module;
}
BOOL JglSupportsSecondary(void) {
    return SecondarySupported;
}
static BOOL SecondaryAvailable(void) {
    DG_ESCAPE_REQUEST request;
    DG_ESCAPE_REPLY reply;
    int result;
    ZeroMemory(&request, sizeof(request));
    ZeroMemory(&reply, sizeof(reply));
    request.Version = DG_ESCAPE_VERSION;
    request.Operation = DG_ESCAPE_QUERY;
    request.Client = Client;
    request.Function = FEnum_glSecondaryColor3f;
    result = JglTransportRequest(Display, sizeof(request), (LPCSTR)&request, sizeof(reply),
                                 (LPSTR)&reply);
    return result == 1 && reply.Version == DG_ESCAPE_VERSION && reply.Status == DG_ESCAPE_OK &&
           reply.FunctionWords == 3;
}
static JGL_NAMES *TextureNames(JGL_CONTEXT *c) {
    if (!c->Names) {
        c->Names = (JGL_NAMES *)HeapAlloc(Heap, HEAP_ZERO_MEMORY, sizeof(*c->Names));
        if (c->Names) {
            c->Names->References = 1;
            c->Names->Next = 1;
        }
    }
    return c->Names;
}
static void ReleaseNames(JGL_CONTEXT *c) {
    if (c->Names && !--c->Names->References)
        HeapFree(Heap, 0, c->Names);
    c->Names = NULL;
}
static BOOL PublishFront(JGL_CONTEXT *c);
static BOOL MatchingGeometry(JGL_CONTEXT *c);

JGL_INLINE JGL_CONTEXT *CurrentContext(void) {
    return (JGL_CONTEXT *)TlsGetValue(Current);
}
static void CommandError(JGL_CONTEXT *c, GLenum error);
static void Error(JGL_CONTEXT *c, GLenum error) {
    if (c && !c->Error)
        c->Error = error;
}
static BOOL Request(PACKET *packet, ULONG operation, ULONG words, DG_ESCAPE_REPLY *reply) {
    int result;
    ZeroMemory(&packet->Request, sizeof(packet->Request));
    packet->Request.Version = DG_ESCAPE_VERSION;
    packet->Request.Operation = operation;
    packet->Request.Client = operation == DG_ESCAPE_OPEN ? 0 : Client;
    packet->Request.Bytes = words * 4;
    ZeroMemory(reply, sizeof(*reply));
    result = JglTransportRequest(Display, sizeof(packet->Request) + words * 4, (LPCSTR)packet,
                                 sizeof(*reply), (LPSTR)reply);
    if (result != 1 || reply->Version != DG_ESCAPE_VERSION) {
        /* A missing reply cannot prove that the submitted operation failed
         * before execution. Keep its resource ownership until process exit. */
        reply->Version = 0;
        return FALSE;
    }
    return reply->Status == DG_ESCAPE_OK;
}
static BOOL CompletionKnown(const DG_ESCAPE_REPLY *reply) {
    if (reply->Version != DG_ESCAPE_VERSION)
        return FALSE;
    switch (reply->Status) {
        case DG_ESCAPE_OK:
        case DG_ESCAPE_INVALID:
        case DG_ESCAPE_UNSUPPORTED:
        case DG_ESCAPE_OWNER:
        case DG_ESCAPE_RESOURCES:
            return TRUE;
        case DG_ESCAPE_HOST:
            return reply->CompletedSequence != 0;
        default:
            return FALSE;
    }
}
static BOOL Flush(JGL_CONTEXT *c) {
    DG_ESCAPE_REPLY reply;
    BOOL result;
    if (c->Failed)
        return FALSE;
    if (!c->Used)
        return TRUE;
    result = Request(&c->Packet, DG_ESCAPE_SUBMIT, c->Used, &reply);
    c->Used = c->Records = 0;
    if (!result) {
        c->Failed = TRUE;
        if (!CompletionKnown(&reply))
            c->Uncertain = TRUE;
        Error(c, GL_INVALID_OPERATION);
    }
    return result;
}
JGL_INLINE ULONG *Record(JGL_CONTEXT *c, ULONG operation, ULONG context, ULONG drawable,
                         ULONG words) {
    ULONG *record;
    if (c->Failed || words > MaxWords - 8)
        return NULL;
    if (c->Used + 8 + words > MaxWords || c->Records == MaxRecords)
        if (!Flush(c))
            return NULL;
    record = c->Packet.Words + c->Used;
    record[0] = operation;
    record[1] = (8 + words) * 4;
    record[2] = 0;
    record[3] = context;
    record[4] = drawable;
    record[5] = record[6] = record[7] = 0;
    /* Every caller fills its payload before submission. Initialize only the
     * header here: clearing then overwriting every scalar byte is expensive
     * in an emulated guest. DATA calls explicitly initialize their padding. */
    c->Used += 8 + words;
    ++c->Records;
    return record + 8;
}
JGL_INLINE void ScalarForContext(JGL_CONTEXT *c, ULONG function, ULONG words,
                                 const void *arguments) {
    ULONG *record, i;
    if (!c)
        return;
    if (c->InBegin && function != FEnum_glBegin && function != FEnum_glEnd &&
        function != FEnum_glVertex2f && function != FEnum_glVertex3f &&
        function != FEnum_glVertex4f && function != FEnum_glColor3f &&
        function != FEnum_glColor4f && function != FEnum_glSecondaryColor3f &&
        function != FEnum_glNormal3f && function != FEnum_glTexCoord2f &&
        function != FEnum_glTexCoord4f && function != FEnum_glMaterialf &&
        function != FEnum_glEvalCoord1d && function != FEnum_glEvalCoord2d &&
        function != FEnum_glEvalPoint1 && function != FEnum_glEvalPoint2 &&
        function != FEnum_glIndexd && function != FEnum_glEdgeFlag &&
        function != FEnum_glCallList && function != DG_GL_RECORD_ERROR) {
        if (function == FEnum_glFlush || function == FEnum_glFinish)
            Error(c, GL_INVALID_OPERATION);
        else
            CommandError(c, GL_INVALID_OPERATION);
        return;
    }
    if (function == FEnum_glSecondaryColor3f && !SecondarySupported) {
        if (function == FEnum_glFlush || function == FEnum_glFinish)
            Error(c, GL_INVALID_OPERATION);
        else
            CommandError(c, GL_INVALID_OPERATION);
        return;
    }
    if (!SecondarySupported && (function == FEnum_glEnable || function == FEnum_glDisable) &&
        *(const ULONG *)arguments == 0x8458) {
        CommandError(c, GL_INVALID_ENUM);
        return;
    }
    if (c->Failed || (!c->Arrays.ListMode && StateUnchanged(&c->State, function, arguments)))
        return;
    record = Record(c, DG_GL_CALL, c->Id, c->Id, words + 1);
    if (record) {
        record[0] = function;
        for (i = 0; i < words; ++i)
            __builtin_memcpy(record + 1 + i, (const BYTE *)arguments + i * 4, 4);
        if (c->Arrays.ListMode != GL_COMPILE &&
            (c->Arrays.CaptureMode == 0 || c->Arrays.CaptureMode == GL_RENDER) &&
            (c->DrawBuffer == ~0u || c->DrawBuffer == GL_FRONT || c->DrawBuffer == GL_FRONT_LEFT ||
             c->DrawBuffer == GL_LEFT || c->DrawBuffer == GL_FRONT_AND_BACK) &&
            (function == FEnum_glBegin || function == FEnum_glEvalMesh1 ||
             function == FEnum_glEvalMesh2 ||
             (function == FEnum_glCopyPixels && ((const ULONG *)arguments)[4] == GL_COLOR) ||
             (function == FEnum_glClear && (*(const ULONG *)arguments & GL_COLOR_BUFFER_BIT))))
            c->FrontDirty = TRUE;
    }
}
JGL_INLINE void Scalar(ULONG function, ULONG words, const void *arguments) {
    ScalarForContext(CurrentContext(), function, words, arguments);
}
void JglScalarVector(ULONG function, ULONG words, const void *arguments) {
    Scalar(function, words, arguments);
}
/* Resource transitions are single-command transactions. Once one succeeds,
 * publish that step before attempting another; a later failure cannot make
 * cleanup replay an already completed destruction. Callers first flush or
 * explicitly abandon ordinary drawing, never replay its executed prefix. */
static BOOL Control(JGL_CONTEXT *c, ULONG operation, ULONG context, ULONG drawable,
                    const ULONG *arguments, ULONG words) {
    ULONG *record;
    if (c->Uncertain)
        return FALSE;
    c->Used = c->Records = 0;
    c->Failed = FALSE;
    record = Record(c, operation, context, drawable, words);
    if (!record)
        return FALSE;
    if (words)
        CopyMemory(record, arguments, words * 4);
    return Flush(c);
}
static BOOL FlushCurrent(JGL_CONTEXT *c) {
    ULONG *record;
    if (c->InBegin || c->Failed)
        return FALSE;
    record = Record(c, DG_GL_CALL, c->Id, c->Id, 1);
    if (!record)
        return FALSE;
    record[0] = FEnum_glFlush;
    return Flush(c) && (!c->FrontDirty || !MatchingGeometry(c) || PublishFront(c));
}
static void Unbind(JGL_CONTEXT *c) {
    TlsSetValue(Current, NULL);
    if (c)
        InterlockedExchange(&c->Owner, 0);
}
static void CommandError(JGL_CONTEXT *c, GLenum error) {
    if (c && c->Arrays.ListMode && !c->Failed) {
        ScalarForContext(c, DG_GL_RECORD_ERROR, 1, &error);
    } else {
        Error(c, error);
    }
}
void JglCommandError(GLenum error) {
    CommandError(CurrentContext(), error);
}
BOOL JglCommandReady(void) {
    JGL_CONTEXT *c = CurrentContext();
    if (!c)
        return FALSE;
    if (c->InBegin || c->Failed) {
        CommandError(c, GL_INVALID_OPERATION);
        return FALSE;
    }
    return TRUE;
}
BOOL JglReady(void) {
    JGL_CONTEXT *c = CurrentContext();
    if (!c)
        return FALSE;
    if (c->InBegin || c->Failed) {
        Error(c, GL_INVALID_OPERATION);
        return FALSE;
    }
    return TRUE;
}
JGL_ARRAY_STATE *JglArrays(void) {
    JGL_CONTEXT *c = CurrentContext();
    return c ? &c->Arrays : NULL;
}
void JglSetError(GLenum error) {
    Error(CurrentContext(), error);
}
JGL_UNPACK *JglUnpack(void) {
    JGL_CONTEXT *c = CurrentContext();
    return c ? &c->Unpack : NULL;
}
JGL_UNPACK *JglPack(void) {
    JGL_CONTEXT *c = CurrentContext();
    return c ? &c->Pack : NULL;
}
void JglDrawableSize(ULONG *width, ULONG *height) {
    JGL_CONTEXT *c = CurrentContext();
    *width = c ? c->Width : 0;
    *height = c ? c->Height : 0;
}
ULONG JglReadbackCapacity(void) {
    return MaxResultBytes;
}
ULONG *JglReadbackBuffer(void) {
    JGL_CONTEXT *c = CurrentContext();
    if (!c)
        return NULL;
    if (!c->Readback)
        c->Readback = (ULONG *)HeapAlloc(Heap, 0, MaxResultBytes);
    if (!c->Readback)
        Error(c, GL_OUT_OF_MEMORY);
    return c->Readback;
}
static void FreeContext(JGL_CONTEXT *c) {
    if (c->LargeQuery)
        HeapFree(Heap, 0, c->LargeQuery);
    if (c->Readback)
        HeapFree(Heap, 0, c->Readback);
    HeapFree(Heap, 0, c);
}

ULONG JglNextImageId(void) {
    JGL_CONTEXT *c = CurrentContext();
    if (!c || !JglReady())
        return 0;
    if (c->ImageId == 0xffffffffUL) {
        Error(c, GL_OUT_OF_MEMORY);
        return 0;
    }
    return ++c->ImageId;
}

ULONG JglMaxDataBytes(ULONG words) {
    return words <= MaxWords - 10 ? (MaxWords - 10 - words) * 4 : 0;
}
static BOOL CaptureData(ULONG function, const ULONG *arguments, ULONG words, const void *payload,
                        ULONG bytes, JGL_CAPTURE capture, void *opaque) {
    JGL_CONTEXT *c;
    ULONG *record;
    c = CurrentContext();
    if (!c)
        return FALSE;
    if (c->Failed ||
        (c->InBegin && function != FEnum_glMaterialfv && function != FEnum_glCallLists)) {
        if (function == FEnum_glDeleteTextures)
            Error(c, GL_INVALID_OPERATION);
        else
            CommandError(c, GL_INVALID_OPERATION);
        return FALSE;
    }
    if ((words && !arguments) || (bytes && !payload && !capture) || words > MaxWords - 10 ||
        bytes > JglMaxDataBytes(words)) {
        if (function == FEnum_glDeleteTextures)
            Error(c, GL_INVALID_VALUE);
        else
            CommandError(c, GL_INVALID_VALUE);
        return FALSE;
    }
    record = Record(c, DG_GL_DATA_CALL, c->Id, c->Id, 2 + words + (bytes + 3) / 4);
    if (!record)
        return FALSE;
    record[0] = function;
    record[1] = bytes;
    if (words)
        CopyMemory(record + 2, arguments, words * 4);
    if (bytes & 3)
        record[2 + words + bytes / 4] = 0;
    if (capture)
        capture(opaque, (BYTE *)(record + 2 + words));
    else if (bytes)
        CopyMemory(record + 2 + words, payload, bytes);
    if (c->Arrays.ListMode != GL_COMPILE &&
        (c->Arrays.CaptureMode == 0 || c->Arrays.CaptureMode == GL_RENDER) &&
        (function == FEnum_glDrawArrays || function == FEnum_glDrawElements ||
         (words == 8 &&
          (function == FEnum_glBitmap ||
           (function == FEnum_glDrawPixels && arguments[2] != GL_DEPTH_COMPONENT &&
            arguments[2] != GL_STENCIL_INDEX)) &&
          (arguments[6] & 2))) &&
        (c->DrawBuffer == ~0u || c->DrawBuffer == GL_FRONT || c->DrawBuffer == GL_FRONT_LEFT ||
         c->DrawBuffer == GL_LEFT || c->DrawBuffer == GL_FRONT_AND_BACK))
        c->FrontDirty = TRUE;
    return TRUE;
}

BOOL JglData(ULONG function, const ULONG *arguments, ULONG words, const void *payload,
             ULONG bytes) {
    return CaptureData(function, arguments, words, payload, bytes, NULL, NULL);
}
BOOL JglCaptureData(ULONG function, const ULONG *arguments, ULONG words, ULONG bytes,
                    JGL_CAPTURE capture, void *opaque) {
    if (!capture)
        return FALSE;
    return CaptureData(function, arguments, words, NULL, bytes, capture, opaque);
}

} /* extern C */
struct ContextDeleter {
    void operator()(JGL_CONTEXT *context) const noexcept {
        FreeContext(context);
    }
};
extern "C" {

BOOL JglQuery(ULONG function, const ULONG arguments[3], ULONG type, void *output, ULONG capacity,
              ULONG *bytes) {
    JGL_CONTEXT *c;
    ULONG *record, unit;
    int status;
    typedef struct {
        DG_ESCAPE_REPLY Reply;
        BYTE Data[];
    } QUERY_RESULT;
    union {
        DG_ESCAPE_REPLY Align;
        BYTE Data[sizeof(DG_ESCAPE_REPLY) + DG_ESCAPE_LEGACY_RESULT_BYTES];
    } small;
    QUERY_RESULT *result = (QUERY_RESULT *)small.Data;
    if (bytes)
        *bytes = 0;
    if (!JglReady())
        return FALSE;
    c = CurrentContext();
    if (!output || !capacity || capacity > MaxResultBytes || !Flush(c))
        return FALSE;
    if (capacity > DG_ESCAPE_LEGACY_RESULT_BYTES) {
        if (!c->LargeQuery)
            c->LargeQuery = HeapAlloc(Heap, 0, sizeof(DG_ESCAPE_REPLY) + MaxResultBytes);
        if (!c->LargeQuery) {
            Error(c, GL_OUT_OF_MEMORY);
            return FALSE;
        }
        result = (decltype(result))c->LargeQuery;
    }
    record = Record(c, DG_GL_QUERY, c->Id, c->Id, 4);
    if (!record)
        return FALSE;
    record[0] = function;
    CopyMemory(record + 1, arguments, 12);
    ZeroMemory(result, sizeof(result->Reply) + capacity);
    ZeroMemory(&c->Packet.Request, sizeof(c->Packet.Request));
    c->Packet.Request.Version = DG_ESCAPE_VERSION;
    c->Packet.Request.Operation = DG_ESCAPE_SUBMIT;
    c->Packet.Request.Client = Client;
    c->Packet.Request.Bytes = c->Used * 4;
    c->Packet.Request.ResultCapacity = capacity;
    status =
        JglTransportRequest(Display, sizeof(c->Packet.Request) + c->Used * 4, (LPCSTR)&c->Packet,
                            sizeof(result->Reply) + capacity, (LPSTR)result);
    c->Used = c->Records = 0;
    unit = type == DG_GL_RESULT_DOUBLE                                ? 8
           : (type == DG_GL_RESULT_INT || type == DG_GL_RESULT_FLOAT) ? 4
                                                                      : 1;
    if (status != 1 || result->Reply.Version != DG_ESCAPE_VERSION ||
        result->Reply.Status != DG_ESCAPE_OK || result->Reply.ResultType != type ||
        !result->Reply.ResultBytes || result->Reply.ResultBytes > capacity ||
        result->Reply.ResultBytes % unit ||
        (type == DG_GL_RESULT_STRING && result->Data[result->Reply.ResultBytes - 1])) {
        Error(c, result->Reply.DeviceError == DG_GL_ERROR_UNSUPPORTED ? GL_INVALID_ENUM
                                                                      : GL_INVALID_OPERATION);
        if (status != 1 || !CompletionKnown(&result->Reply)) {
            c->Failed = c->Uncertain = TRUE;
        } else if (result->Reply.Status == DG_ESCAPE_OK) {
            /* A nominally successful but malformed typed result must not
             * manufacture a fresh error on every later glGetError query. */
            c->Failed = TRUE;
        }
        return FALSE;
    }
    CopyMemory(output, result->Data, result->Reply.ResultBytes);
    if (bytes)
        *bytes = result->Reply.ResultBytes;
    return TRUE;
}

static JGL_CONTEXT *Lookup(HGLRC handle) {
    ULONG i, id = (ULONG)(ULONG_PTR)handle;
    for (i = 0; i < MAX_JGL_CONTEXTS; ++i)
        if (Contexts[i] && Contexts[i]->Id == id)
            return Contexts[i];
    return NULL;
}
static BOOL WindowGeometry(HDC dc, HWND *window, ULONG *width, ULONG *height) {
    RECT rect;
    DWORD owner;
    *window = WindowFromDC(dc);
    if (!*window || !GetWindowThreadProcessId(*window, &owner) || owner != GetCurrentProcessId() ||
        !GetClientRect(*window, &rect) || rect.right <= 0 || rect.bottom <= 0 ||
        rect.right > DG_GL_MAX_DIMENSION || rect.bottom > DG_GL_MAX_DIMENSION)
        return FALSE;
    *width = rect.right;
    *height = rect.bottom;
    return TRUE;
}
static BOOL MatchingGeometry(JGL_CONTEXT *c) {
    HWND window;
    ULONG width, height;
    return WindowGeometry(c->DC, &window, &width, &height) && window == c->Window &&
           width == c->Width && height == c->Height;
}
static BOOL Bind(JGL_CONTEXT *c) {
    DG_WINDOW_BIND request;
    DG_WINDOW_REPLY reply;
    ZeroMemory(&request, sizeof(request));
    ZeroMemory(&reply, sizeof(reply));
    request.Window = (ULONG)(ULONG_PTR)c->Window;
    request.Magic = DG_WINDOW_MAGIC;
    request.Version = DG_WINDOW_VERSION;
    request.Client = Client;
    request.Context = c->Id;
    request.Drawable = c->Id;
    if (JglTransportBind(c->DC, &request, &reply) <= 0 || reply.Version != DG_WINDOW_VERSION ||
        reply.Status != DG_ESCAPE_OK || !reply.Binding ||
        (reply.Capabilities & ~DG_WINDOW_CAP_FRONT_ONLY))
        return FALSE;
    if (c->Binding)
        JglTransportUnbind(c->Binding);
    c->Binding = reply.Binding;
    c->Capabilities = reply.Capabilities;
    return TRUE;
}
/* Only the display driver can distinguish a retired binding from a failed
 * native presentation. Recover once before any swap was submitted; never
 * replay GL commands or retry an outcome whose completion is uncertain. */
static BOOL PresentWindow(JGL_CONTEXT *c, ULONG flags) {
    DG_WINDOW_PRESENT present = {DG_WINDOW_MAGIC, DG_WINDOW_VERSION, c->Binding, flags};
    int status = JglTransportPresent(c->DC, &present);
    if (status == DG_WINDOW_PRESENT_REBIND) {
        if (!Bind(c)) {
            SetLastError(ERROR_BUSY);
            return FALSE;
        }
        present.Binding = c->Binding;
        status = JglTransportPresent(c->DC, &present);
    }
    if (status == 1)
        return TRUE;
    if (status == DG_WINDOW_PRESENT_REBIND || status == DG_WINDOW_PRESENT_NOT_READY) {
        SetLastError(ERROR_BUSY);
        return FALSE;
    }
    c->Failed = TRUE;
    Error(c, GL_INVALID_OPERATION);
    SetLastError(ERROR_GEN_FAILURE);
    return FALSE;
}
static void CloseIdleClient(PACKET *packet) {
    ULONG i;
    DG_ESCAPE_REPLY reply;
    for (i = 0; i < MAX_JGL_CONTEXTS; ++i)
        if (Contexts[i])
            return;
    if (Client && Request(packet, DG_ESCAPE_CLOSE, 0, &reply)) {
        Client = 0;
        ReleaseDC(NULL, Display);
        Display = NULL;
    }
}
HGLRC WINAPI wglCreateContext(HDC dc) {
    JGL_CONTEXT *c;
    ULONG i, share = 0;
    DG_ESCAPE_REPLY reply;
    HGLRC result = NULL;
    HWND window;
    ULONG width, height;
    dreamgpu::unique_owner<JGL_CONTEXT, ContextDeleter> pending;
    if (!WindowGeometry(dc, &window, &width, &height)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return NULL;
    }
    EnterCriticalSection(&Lock);
    for (i = 0; i < MAX_JGL_CONTEXTS && Contexts[i]; ++i) {
    }
    if (i == MAX_JGL_CONTEXTS || NextId == 0xffffffffUL)
        goto done;
    c = (JGL_CONTEXT *)HeapAlloc(Heap, HEAP_ZERO_MEMORY, sizeof(*c));
    if (!c)
        goto done;
    pending.reset(c);
    if (!Client) {
        Display = GetDC(NULL);
        if (!Display || !Request(&c->Packet, DG_ESCAPE_OPEN, 0, &reply)) {
            if (Display)
                ReleaseDC(NULL, Display);
            Display = NULL;
            goto done;
        }
        Client = reply.Client;
        MaxWords = reply.MaxBytes / 4 < PACKET_WORDS ? reply.MaxBytes / 4 : PACKET_WORDS;
        MaxRecords = reply.MaxRecords < DG_GL_MAX_RECORDS ? reply.MaxRecords : DG_GL_MAX_RECORDS;
        MaxResultBytes = reply.MaxResultBytes < DG_ESCAPE_MAX_RESULT_BYTES
                             ? reply.MaxResultBytes
                             : DG_ESCAPE_MAX_RESULT_BYTES;
        MaxResultBytes &= ~3UL;
        if (!Client || MaxWords < 32 || !MaxRecords ||
            MaxResultBytes < DG_ESCAPE_LEGACY_RESULT_BYTES) {
            if (Client)
                Request(&c->Packet, DG_ESCAPE_CLOSE, 0, &reply);
            Client = 0;
            ReleaseDC(NULL, Display);
            Display = NULL;
            goto done;
        }
        SecondarySupported = SecondaryAvailable();
    }
    c->Unpack.Alignment = c->Pack.Alignment = 4;
    for (i = 0; i < 5; ++i) {
        c->Arrays.Attribute[i].Size = i == 2 || i == 4 ? 3 : 4;
        c->Arrays.Attribute[i].Type = 0x1406; /* GL_FLOAT */
    }
    for (i = 0; i < MAX_JGL_CONTEXTS && Contexts[i]; ++i) {
    }
    c->Arrays.Attribute[5].Size = c->Arrays.Attribute[6].Size = 1;
    c->Arrays.Attribute[5].Type = GL_FLOAT;
    c->Arrays.Attribute[6].Type = GL_UNSIGNED_BYTE;
    c->DrawBuffer = GL_BACK;
    c->Id = ++NextId;
    c->DC = dc;
    c->Window = window;
    c->Width = width;
    c->Height = height;
    if (!Control(c, DG_GL_CREATE_CONTEXT, c->Id, 0, &share, 1)) {
        if (c->Uncertain) {
            /* No public handle exists, but creation may have executed. Keep
             * this bounded ownership record for kernel process cleanup. */
            c->Deleting = TRUE;
            Contexts[i] = pending.release();
        } else {
            CloseIdleClient(&c->Packet);
        }
        goto done;
    }
    c->Created = TRUE;
    Contexts[i] = pending.release();
    result = (HGLRC)(ULONG_PTR)c->Id;
done:
    LeaveCriticalSection(&Lock);
    if (!result)
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return result;
}
/* Names and binding targets are shared frontend state. Allocation and
 * IsTexture must not flush drawing or perform one kernel/host query per name. */
void APIENTRY glGenTextures(GLsizei count, GLuint *textures) {
    JGL_CONTEXT *c;
    JGL_NAMES *names;
    ULONG found = 0;
    if (!JglReady())
        return;
    c = CurrentContext();
    if (count < 0 || (count && !textures)) {
        Error(c, GL_INVALID_VALUE);
        return;
    }
    if (!count)
        return;
    EnterCriticalSection(&Lock);
    names = TextureNames(c);
    if (!names || (ULONG)count > DG_GL_MAX_TEXTURES - names->Count) {
        Error(c, GL_OUT_OF_MEMORY);
        goto done;
    }
    while (found < (ULONG)count) {
        GLuint candidate = names->Next++;
        JGL_NAME *entry;
        if (!candidate)
            continue;
        entry = FindName(names, candidate);
        if (entry->Name)
            continue;
        if (names->NativeLists) {
            ULONG query[3] = {candidate, 0, 0}, bytes = 0;
            GLboolean exists = 0;
            if (!JglQuery(FEnum_glIsTexture, query, DG_GL_RESULT_BOOL, &exists, 1, &bytes) ||
                bytes != 1)
                goto done;
            if (exists)
                continue;
        }
        entry->Name = candidate;
        entry->Target = 0;
        ++names->Count;
        textures[found++] = candidate;
    }
done:
    LeaveCriticalSection(&Lock);
}
void APIENTRY glBindTexture(GLenum target, GLuint texture) {
    JGL_CONTEXT *c;
    JGL_NAMES *names;
    JGL_NAME *entry = NULL;
    ULONG *record;
    if (!JglCommandReady())
        return;
    c = CurrentContext();
    if (target != GL_TEXTURE_1D && target != GL_TEXTURE_2D) {
        CommandError(c, GL_INVALID_ENUM);
        return;
    }
    if (!texture) {
        ULONG args[2] = {target, 0};
        ScalarForContext(c, FEnum_glBindTexture, 2, args);
        return;
    }
    EnterCriticalSection(&Lock);
    names = TextureNames(c);
    if (!names) {
        CommandError(c, GL_OUT_OF_MEMORY);
        goto done;
    }
    if (names->NativeLists) {
        ULONG args[2] = {target, texture};
        ScalarForContext(c, FEnum_glBindTexture, 2, args);
        goto done;
    }
    if (texture) {
        entry = FindName(names, texture);
        if (entry->Name && entry->Target && entry->Target != target) {
            CommandError(c, GL_INVALID_OPERATION);
            goto done;
        }
        if (!entry->Name && names->Count == DG_GL_MAX_TEXTURES) {
            CommandError(c, GL_OUT_OF_MEMORY);
            goto done;
        }
    }
    record = Record(c, DG_GL_CALL, c->Id, c->Id, 3);
    if (!record)
        goto done;
    record[0] = FEnum_glBindTexture;
    record[1] = target;
    record[2] = texture;
    if (entry) {
        if (!entry->Name)
            ++names->Count;
        entry->Name = texture;
        entry->Target = target;
    }
done:
    LeaveCriticalSection(&Lock);
}
GLboolean APIENTRY glIsTexture(GLuint texture) {
    JGL_CONTEXT *c;
    JGL_NAME *entry;
    GLboolean result = GL_FALSE;
    if (!JglReady() || !texture)
        return GL_FALSE;
    c = CurrentContext();
    EnterCriticalSection(&Lock);
    if (c->Names && c->Names->NativeLists) {
        ULONG query[3] = {texture, 0, 0}, bytes = 0;
        if (!JglQuery(FEnum_glIsTexture, query, DG_GL_RESULT_BOOL, &result, 1, &bytes) ||
            bytes != 1)
            result = GL_FALSE;
    } else if (c->Names) {
        entry = FindName(c->Names, texture);
        result = entry && entry->Name && entry->Target != 0;
    }
    LeaveCriticalSection(&Lock);
    return result;
}
void JglForgetTextures(ULONG count, const GLuint *textures) {
    JGL_CONTEXT *c = CurrentContext();
    ULONG i;
    if (!c || !c->Names)
        return;
    EnterCriticalSection(&Lock);
    for (i = 0; i < count; ++i) {
        JGL_NAME *entry;
        if (!textures[i])
            continue;
        entry = FindName(c->Names, textures[i]);
        if (entry && entry->Name) {
            entry->Name = 0;
            entry->Target = 1;
            --c->Names->Count;
        }
    }
    LeaveCriticalSection(&Lock);
}

/* The supported sharing contract is a newly created destination context.
 * Recreate its still-unused namespace with the existing native share operand;
 * never discard state from an already-used destination. */
BOOL WINAPI wglShareLists(HGLRC source, HGLRC destination) {
    JGL_CONTEXT *src, *dst;
    BOOL result = FALSE;
    ULONG share;
    EnterCriticalSection(&Lock);
    src = Lookup(source);
    dst = Lookup(destination);
    if (!src || !dst || src == dst || src->Failed || dst->Failed || src->Deleting ||
        dst->Deleting || src->Uncertain || dst->Uncertain || !src->Created || !dst->Created ||
        src->InBegin || dst->EverCurrent || dst->ShareEstablished || dst->Owner ||
        (src->Owner && src->Owner != (LONG)GetCurrentThreadId()))
        goto done;
    if (!TextureNames(src) || !Flush(src) ||
        !Control(dst, DG_GL_DESTROY_CONTEXT, dst->Id, 0, NULL, 0))
        goto done;
    dst->Created = FALSE;
    share = src->Id;
    if (!Control(dst, DG_GL_CREATE_CONTEXT, dst->Id, 0, &share, 1)) {
        dst->Failed = TRUE;
        goto done;
    }
    dst->Created = TRUE;
    dst->ShareEstablished = TRUE;
    dst->Names = src->Names;
    ++dst->Names->References;
    result = TRUE;
done:
    LeaveCriticalSection(&Lock);
    if (!result)
        SetLastError(ERROR_INVALID_HANDLE);
    return result;
}
BOOL WINAPI wglMakeCurrent(HDC dc, HGLRC handle) {
    JGL_CONTEXT *old = CurrentContext(), *c;
    LONG thread = GetCurrentThreadId();
    ULONG width, height, size[2];
    HWND window;
    BOOL result = FALSE;
    EnterCriticalSection(&Lock);
    if (old && old->InBegin && !old->Failed) {
        Error(old, GL_INVALID_OPERATION);
        goto done;
    }
    /* Releasing a failed context must still release its thread ownership.
     * Healthy releases also flush host GL, even with an empty guest packet. */
    if (old && !old->Failed && !FlushCurrent(old))
        goto done;
    Unbind(old);
    if (!handle) {
        result = TRUE;
        goto done;
    }
    c = Lookup(handle);
    if (!c || !c->Created || c->Failed || c->Deleting || c->InBegin ||
        (c->Owner && c->Owner != thread) || !WindowGeometry(dc, &window, &width, &height))
        goto done;
    /* A drawable belongs to this context in the initial frontend. Rebinding a
     * context to a resized/different window recreates its bounded backbuffer. */
    if (c->Drawable && (c->Window != window || c->Width != width || c->Height != height)) {
        if (!Flush(c) || !Control(c, DG_GL_DESTROY_DRAWABLE, 0, c->Id, NULL, 0))
            goto done;
        c->Drawable = FALSE;
        c->FrontDirty = FALSE;
    }
    c->DC = dc;
    c->Window = window;
    c->Width = width;
    c->Height = height;
    if (!c->Drawable) {
        size[0] = width;
        size[1] = height;
        if (!Control(c, DG_GL_CREATE_DRAWABLE, 0, c->Id, size, 2))
            goto done;
        c->Drawable = TRUE;
    }
    c->EverCurrent = TRUE;
    if (!Record(c, DG_GL_MAKE_CURRENT, c->Id, c->Id, 0) || !Flush(c) || !Bind(c))
        goto done;
    if (!TlsSetValue(Current, c))
        goto done;
    InterlockedExchange(&c->Owner, thread);
    result = TRUE;
done:
    /* WGL specifies that every failed MakeCurrent leaves this thread with
     * no current context, including invalid handles and failed flushes. */
    if (!result)
        Unbind(old);
    LeaveCriticalSection(&Lock);
    if (!result)
        SetLastError(ERROR_INVALID_HANDLE);
    return result;
}
BOOL WINAPI wglDeleteContext(HGLRC handle) {
    JGL_CONTEXT *c;
    ULONG i;
    BOOL result = FALSE;
    EnterCriticalSection(&Lock);
    c = Lookup(handle);
    if (!c || (c->Owner && c->Owner != (LONG)GetCurrentThreadId()))
        goto done;
    if (CurrentContext() == c) {
        if (!c->Failed && !c->InBegin)
            FlushCurrent(c);
        Unbind(c);
    }
    c->Deleting = TRUE;
    if (!c->Failed && !c->InBegin)
        Flush(c);
    /* Cleanup never replays a failed/unfinished batch. A fresh control packet
     * can destroy its host context even when drawing has been rejected. */
    c->Used = c->Records = 0;
    c->InBegin = FALSE;
    if (c->Uncertain)
        goto done;
    if (c->Binding) {
        JglTransportUnbind(c->Binding);
        c->Binding = 0;
    }
    if (c->Created) {
        if (!Control(c, DG_GL_DESTROY_CONTEXT, c->Id, 0, NULL, 0))
            goto done;
        c->Created = FALSE;
    }
    if (c->Drawable) {
        if (!Control(c, DG_GL_DESTROY_DRAWABLE, 0, c->Id, NULL, 0))
            goto done;
        c->Drawable = FALSE;
    }
    for (i = 0; i < MAX_JGL_CONTEXTS; ++i)
        if (Contexts[i] == c)
            Contexts[i] = NULL;
    CloseIdleClient(&c->Packet);
    ReleaseNames(c);
    FreeContext(c);
    result = TRUE;
done:
    LeaveCriticalSection(&Lock);
    if (!result)
        SetLastError(ERROR_INVALID_HANDLE);
    return result;
}
HGLRC WINAPI wglGetCurrentContext(void) {
    JGL_CONTEXT *c = CurrentContext();
    return c ? (HGLRC)(ULONG_PTR)c->Id : NULL;
}
HDC WINAPI wglGetCurrentDC(void) {
    JGL_CONTEXT *c = CurrentContext();
    return c ? c->DC : NULL;
}
BOOL WINAPI wglSwapBuffers(HDC dc) {
    JGL_CONTEXT *c = CurrentContext();
    HWND window;
    ULONG width, height;
    /* Report the boundary that rejected the swap; OpenGLide records the first
     * failure once, so initialization errors do not become a silent busy draw. */
    if (!c || c->DC != dc) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (c->InBegin) {
        SetLastError(ERROR_BUSY);
        return FALSE;
    }
    if (!WindowGeometry(c->DC, &window, &width, &height) || window != c->Window) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    if (width != c->Width || height != c->Height) {
        /* Windows can resize a current drawable without another MakeCurrent
         * call (UT's XP fullscreen transition does this). Reuse the existing
         * ordered drawable replacement: flush old commands, recreate bounded
         * attachments, then replace the binding for the same context.
         * Contents across a resize are undefined; never publish old geometry.
         * The unchanged-size path performs no extra transport operation. */
        if (!wglMakeCurrent(dc, (HGLRC)(ULONG_PTR)c->Id))
            return FALSE;
    }
    if (!Flush(c)) {
        SetLastError(ERROR_WRITE_FAULT);
        return FALSE;
    }
    if (!PresentWindow(c, 0))
        return FALSE;
    c->FrontDirty = FALSE;
    return TRUE;
}
BOOL JglListMode(ULONG mode) {
    JGL_CONTEXT *c = CurrentContext();
    if (!c)
        return FALSE;
    EnterCriticalSection(&Lock);
    JGL_NAMES *names = TextureNames(c);
    if (names)
        names->NativeLists = TRUE;
    LeaveCriticalSection(&Lock);
    if (!names) {
        Error(c, GL_OUT_OF_MEMORY);
        return FALSE;
    }
    c->Arrays.ListMode = mode;
    c->Arrays.ListAware = TRUE;
    if (!mode)
        ZeroMemory(&c->State, sizeof(c->State));
    return TRUE;
}
BOOL JglCompiling(void) {
    JGL_CONTEXT *c = CurrentContext();
    return c && c->Arrays.ListMode;
}
static void ListEffects(JGL_CONTEXT *c) {
    if (c->Arrays.ListMode == GL_COMPILE)
        return;
    ZeroMemory(&c->State, sizeof(c->State));
    c->DrawBuffer = ~0u;
    c->Arrays.ListAware = TRUE;
    if (!c->Arrays.CaptureMode || c->Arrays.CaptureMode == GL_RENDER)
        c->FrontDirty = TRUE;
}
void JglCallList(ULONG name) {
    JGL_CONTEXT *c = CurrentContext();
    if (!c || c->Failed)
        return;
    if (!name) {
        CommandError(c, GL_INVALID_VALUE);
        return;
    }
    if (!c->Arrays.ListAware && !JglListMode(c->Arrays.ListMode))
        return;
    ScalarForContext(c, FEnum_glCallList, 1, &name);
    ListEffects(c);
}
BOOL JglCallLists(ULONG count, const ULONG *offsets) {
    JGL_CONTEXT *c = CurrentContext();
    if (!c || c->Failed)
        return FALSE;
    if (!c->Arrays.ListAware && !JglListMode(c->Arrays.ListMode))
        return FALSE;
    BOOL result = JglData(FEnum_glCallLists, &count, 1, offsets, count * 4);
    if (result)
        ListEffects(c);
    return result;
}

static BOOL PublishFront(JGL_CONTEXT *c) {
    if (!c->FrontDirty)
        return TRUE;
    if (!(c->Capabilities & DG_WINDOW_CAP_FRONT_ONLY) || !MatchingGeometry(c))
        return FALSE;
    if (!PresentWindow(c, DG_WINDOW_PRESENT_FRONT_ONLY))
        return FALSE;
    c->FrontDirty = FALSE;
    return TRUE;
}
void APIENTRY glDrawBuffer(GLenum mode) {
    JGL_CONTEXT *c;
    if (!JglCommandReady())
        return;
    c = CurrentContext();
    if (!(c->Capabilities & DG_WINDOW_CAP_FRONT_ONLY)) {
        CommandError(c, GL_INVALID_OPERATION);
        return;
    }
    if (mode != GL_FRONT && mode != GL_FRONT_LEFT && mode != GL_BACK && mode != GL_BACK_LEFT &&
        mode != GL_LEFT && mode != GL_FRONT_AND_BACK && mode != GL_NONE) {
        CommandError(c, GL_INVALID_ENUM);
        return;
    }
    ScalarForContext(c, FEnum_glDrawBuffer, 1, &mode);
    if (c->Arrays.ListMode != GL_COMPILE)
        c->DrawBuffer = mode;
}
void APIENTRY glReadBuffer(GLenum mode) {
    JGL_CONTEXT *c;
    if (!JglCommandReady())
        return;
    c = CurrentContext();
    if (!(c->Capabilities & DG_WINDOW_CAP_FRONT_ONLY)) {
        CommandError(c, GL_INVALID_OPERATION);
        return;
    }
    if (mode != GL_FRONT && mode != GL_FRONT_LEFT && mode != GL_BACK && mode != GL_BACK_LEFT &&
        mode != GL_LEFT) {
        CommandError(c, GL_INVALID_ENUM);
        return;
    }
    ScalarForContext(c, FEnum_glReadBuffer, 1, &mode);
}
static PIXELFORMATDESCRIPTOR Format(void) {
    PIXELFORMATDESCRIPTOR p;
    ZeroMemory(&p, sizeof(p));
    p.nSize = sizeof(p);
    p.nVersion = 1;
    p.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    p.iPixelType = PFD_TYPE_RGBA;
    p.cColorBits = 32;
    p.cRedBits = p.cGreenBits = p.cBlueBits = p.cAlphaBits = 8;
    p.cRedShift = 16;
    p.cGreenShift = 8;
    p.cAlphaShift = 24;
    p.cDepthBits = 24;
    p.cStencilBits = 8;
    p.iLayerType = PFD_MAIN_PLANE;
    return p;
}
int WINAPI wglChoosePixelFormat(HDC dc, const PIXELFORMATDESCRIPTOR *requested) {
    HWND window;
    ULONG width, height;
    if (!requested || requested->nSize != sizeof(*requested) || requested->nVersion != 1 ||
        requested->iPixelType != PFD_TYPE_RGBA || requested->iLayerType != PFD_MAIN_PLANE ||
        (requested->dwFlags & (PFD_DRAW_TO_BITMAP | PFD_STEREO)) ||
        !WindowGeometry(dc, &window, &width, &height))
        return 0;
    /* Component sizes are preferences for the closest available format, not
     * minimum requirements. Legacy games commonly request 32 depth bits. Our
     * one format remains RGBA8/D24S8; DescribePixelFormat reports the actual
     * sizes so callers can decide whether that match is suitable. */
    return 1;
}
int WINAPI wglDescribePixelFormat(HDC dc, int format, UINT bytes,
                                  PIXELFORMATDESCRIPTOR *description) {
    PIXELFORMATDESCRIPTOR p = Format();
    (void)dc;
    /* NULL output enumerates the available format count, independent of index. */
    if (!description)
        return 1;
    if (format != 1)
        return 0;
    if (bytes) {
        UINT copied = bytes < sizeof(p) ? bytes : sizeof(p);
        p.nSize = (unsigned short)copied;
        CopyMemory(description, &p, copied);
    }
    return 1;
}
int WINAPI wglGetPixelFormat(HDC dc) {
    HWND window;
    ULONG width, height;
    if (!WindowGeometry(dc, &window, &width, &height))
        return 0;
    return GetPropA(window, "DreamGPU.WGL.PixelFormat.v1") == (HANDLE)(ULONG_PTR)1 ? 1 : 0;
}
BOOL WINAPI wglSetPixelFormat(HDC dc, int format, const PIXELFORMATDESCRIPTOR *description) {
    HWND window;
    ULONG width, height;
    /* Selection uses the index. The descriptor is only for metafile recording
     * in GDI and must not re-interpret the caller's original preferences. */
    (void)description;
    if (format != 1 || !WindowGeometry(dc, &window, &width, &height) ||
        GetPropA(window, "DreamGPU.WGL.PixelFormat.v1"))
        return FALSE;
    return SetPropA(window, "DreamGPU.WGL.PixelFormat.v1", (HANDLE)(ULONG_PTR)1);
}
void APIENTRY glPushAttrib(GLbitfield mask) {
    JGL_CONTEXT *c;
    if (!JglCommandReady())
        return;
    c = CurrentContext();
    if (mask & ~0x000fffffUL) {
        CommandError(c, 0x0501);
        return;
    }
    if (c->Arrays.ListAware) {
        ScalarForContext(c, FEnum_glPushAttrib, 1, &mask);
        return;
    }
    if (c->Arrays.ServerDepth == 16) {
        CommandError(c, 0x0503);
        return;
    }
    ScalarForContext(c, FEnum_glPushAttrib, 1, &mask);
    if (c->Failed)
        return;
    c->Arrays.ServerMasks[c->Arrays.ServerDepth] = mask;
    c->Arrays.ServerDrawBuffers[c->Arrays.ServerDepth++] = c->DrawBuffer;
}
void APIENTRY glPopAttrib(void) {
    JGL_CONTEXT *c;
    if (!JglCommandReady())
        return;
    c = CurrentContext();
    if (c->Arrays.ListAware) {
        ScalarForContext(c, FEnum_glPopAttrib, 0, NULL);
        if (c->Arrays.ListMode != GL_COMPILE) {
            ZeroMemory(&c->State, sizeof(c->State));
            c->DrawBuffer = ~0u;
        }
        return;
    }
    if (!c->Arrays.ServerDepth) {
        CommandError(c, 0x0504);
        return;
    }
    ScalarForContext(c, FEnum_glPopAttrib, 0, NULL);
    if (c->Failed)
        return;
    --c->Arrays.ServerDepth;
    /* Pop may restore any subset of these values. Invalidate conservatively
     * instead of shadowing another server attribute stack. */
    ZeroMemory(&c->State, sizeof(c->State));
    if (c->Arrays.ServerMasks[c->Arrays.ServerDepth] & GL_COLOR_BUFFER_BIT)
        c->DrawBuffer = c->Arrays.ServerDrawBuffers[c->Arrays.ServerDepth];
}
void APIENTRY glBegin(GLenum mode) {
    JGL_CONTEXT *c = CurrentContext();
    if (!c)
        return;
    if (c->InBegin) {
        CommandError(c, GL_INVALID_OPERATION);
        return;
    }
    if (mode > GL_POLYGON) {
        CommandError(c, GL_INVALID_ENUM);
        return;
    }
    c->InBegin = TRUE;
    ScalarForContext(c, FEnum_glBegin, 1, &mode);
}
void APIENTRY glEnd(void) {
    JGL_CONTEXT *c = CurrentContext();
    if (!c)
        return;
    if (!c->InBegin) {
        CommandError(c, GL_INVALID_OPERATION);
        return;
    }
    ScalarForContext(c, FEnum_glEnd, 0, NULL);
    c->InBegin = FALSE;
}
void APIENTRY glFlush(void) {
    JGL_CONTEXT *c = CurrentContext();
    if (c) {
        if (c->InBegin)
            Error(c, GL_INVALID_OPERATION);
        else
            FlushCurrent(c);
    }
}
void APIENTRY glFinish(void) {
    JGL_CONTEXT *c = CurrentContext();
    if (c) {
        if (c->InBegin)
            Error(c, GL_INVALID_OPERATION);
        else {
            ScalarForContext(c, FEnum_glFinish, 0, NULL);
            if (Flush(c))
                PublishFront(c);
        }
    }
}
GLenum APIENTRY glGetError(void) {
    JGL_CONTEXT *c = CurrentContext();
    GLenum error;
    ULONG args[3] = {0, 0, 0}, bytes;
    if (!c)
        return GL_NO_ERROR;
    if (c->InBegin)
        return GL_INVALID_OPERATION;
    error = c->Error;
    c->Error = GL_NO_ERROR;
    if (!error && !c->Failed &&
        !JglQuery(FEnum_glGetError, args, DG_GL_RESULT_INT, &error, sizeof(error), &bytes)) {
        error = c->Error;
        c->Error = GL_NO_ERROR;
    }
    return error;
}
#include "scalar.inc"

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    ULONG i;
    if (reason == DLL_PROCESS_ATTACH) {
        Module = module;
        JglTransportInitialize();
        Heap = GetProcessHeap();
        InitializeCriticalSection(&Lock);
        Current = TlsAlloc();
        return Current != TLS_OUT_OF_INDEXES;
    }
    if (reason == DLL_THREAD_DETACH) {
        /* Thread exit abandons only its binding. Never enter GDI under loader
         * lock; kernel process cleanup is the final ownership boundary. */
        JGL_CONTEXT *c = CurrentContext();
        if (c)
            InterlockedExchange(&c->Owner, 0);
    }
    if (reason == DLL_PROCESS_DETACH) {
        /* ExitProcess can terminate another thread while it owns the process
         * heap. The OS reclaims memory and the miniport retires client tokens;
         * touching the heap here could deadlock process termination. */
        if (reserved)
            return TRUE;
        for (i = 0; i < MAX_JGL_CONTEXTS; ++i)
            if (Contexts[i]) {
                ReleaseNames(Contexts[i]);
                FreeContext(Contexts[i]);
            }
        TlsFree(Current);
        DeleteCriticalSection(&Lock);
    }
    return TRUE;
}

#if defined(__i386__) && defined(__clang__)
#pragma clang attribute pop
#elif defined(__i386__)
#pragma GCC pop_options
#endif

} /* extern "C": preserve guest exports and shared C helpers. */
