// SPDX-License-Identifier: GPL-2.0-or-later
// Reuse the strict Win32/transport seam, not its WGL implementations: those are
// the actual maintained frontend.cpp included by this existing test fixture.
#define main SeparateFrontendSuite
#include "frontend-test.cpp"
#undef main
using GLvoid = void;
using GLbyte = signed char;
using PROC = void (*)();
using COLORREF = DWORD;
using LPLAYERPLANEDESCRIPTOR = void *;
#define ERROR_CALL_NOT_IMPLEMENTED 120
#define ERROR_REVISION_MISMATCH 1306
#define ERROR_INVALID_PIXEL_FORMAT 2000
#define ERROR_INVALID_PARAMETER 87
#define WGL_SWAP_MAIN_PLANE 1
static int SystemFormats[3]{};
extern "C" int GetPixelFormat(HDC dc) {
    auto window = (ULONG_PTR)dc;
    return window < 3 ? SystemFormats[window] : 0;
}
extern "C" PROC wglGetProcAddress(LPCSTR) {
    return nullptr; // Extension lookup is outside this context/ownership gate.
}
static DWORD GetLastError() {
    return LastError;
}
static void IcdTrace(const char *, DWORD, DWORD) {
    // Diagnostic file I/O is separately tested; it cannot alter these decisions.
}
#include "actual-icd-context.inc"
static unsigned Callbacks;
static void APIENTRY Set(const ClientTable *table) {
    assert(table == &Table && table->Count == 336);
    auto context = CurrentContext();
    assert(context && context->Owner == (LONG)GetCurrentThreadId() && context->Created &&
           context->Drawable && context->Binding);
    ++Callbacks;
}
static void clean() {
    Reset();
    SystemFormats[0] = SystemFormats[1] = SystemFormats[2] = 0;
    Callbacks = 0;
}
static HGLRC create(HDC dc) {
    SystemFormats[(ULONG_PTR)dc] = 1;
    HGLRC context = DrvCreateContext(dc);
    assert(context && wglGetPixelFormat(dc) == 1);
    return context;
}
static void formats() {
    clean();
    BYTE bytes[sizeof(PIXELFORMATDESCRIPTOR) + 8];
    memset(bytes, 0xa5, sizeof(bytes));
    assert(DrvDescribePixelFormat((HDC)1, 0, 0, nullptr) == 1);
    assert(!DrvDescribePixelFormat((HDC)1, 2, sizeof(bytes), (PIXELFORMATDESCRIPTOR *)bytes));
    for (BYTE byte : bytes)
        assert(byte == 0xa5);
    assert(DrvDescribePixelFormat((HDC)1, 1, 2, (PIXELFORMATDESCRIPTOR *)bytes) == 1);
    for (unsigned i = 2; i < sizeof(bytes); ++i)
        assert(bytes[i] == 0xa5);
    assert(DrvDescribePixelFormat((HDC)1, 1, sizeof(bytes), (PIXELFORMATDESCRIPTOR *)bytes) == 1);
    for (unsigned i = sizeof(PIXELFORMATDESCRIPTOR); i < sizeof(bytes); ++i)
        assert(bytes[i] == 0xa5);
    PIXELFORMATDESCRIPTOR format;
    memcpy(&format, bytes, sizeof(format));
    assert(format.cColorBits == 32 && format.cAlphaBits == 8 && format.cDepthBits == 24 &&
           format.cStencilBits == 8 && !format.cAccumBits && !format.cAuxBuffers &&
           !(format.dwFlags & (PFD_STEREO | PFD_DRAW_TO_BITMAP)));
    assert(!DrvCreateContext((HDC)1) && LastError == ERROR_INVALID_PIXEL_FORMAT && !Calls &&
           !Allocations);
    auto context = create((HDC)1);
    assert(DrvSetPixelFormat((HDC)1, 1) && DrvSetPixelFormat((HDC)1, 1));
    assert(!wglSetPixelFormat((HDC)1, 1,
                              nullptr)); // GDI public one-time rule differs from driver attachment.
    assert(!DrvSetPixelFormat((HDC)1, 2) && LastError == ERROR_INVALID_PIXEL_FORMAT);
    assert(!DrvCreateLayerContext((HDC)1, 1) && LastError == ERROR_INVALID_PARAMETER);
    assert(DrvDeleteContext(context) && !Allocations);
}
static void threads_and_swap() {
    clean();
    auto context = create((HDC)1);
    assert(DrvSetContext((HDC)1, context, Set) == &Table && Callbacks == 1);
    assert(wglGetCurrentContext() == context && wglGetCurrentDC() == (HDC)1);
    Thread = 1;
    assert(!DrvSetContext((HDC)1, context, Set) && Callbacks == 1);
    assert(!DrvDeleteContext(context) && !DrvReleaseContext(context));
    assert(!wglGetCurrentContext() && Lookup(context)->Owner == 1);
    Thread = 0;
    glClear(GL_COLOR_BUFFER_BIT);
    auto flushes = NativeFlushes;
    assert(DrvReleaseContext(context) && NativeFlushes == flushes + 1 && !Lookup(context)->Owner);
    Thread = 1;
    assert(DrvSetContext((HDC)1, context, Set) == &Table && Callbacks == 2);
    auto presents = Presents;
    assert(!DrvSwapBuffers((HDC)2) && LastError == ERROR_INVALID_HANDLE && Presents == presents);
    glBegin(GL_TRIANGLES);
    assert(!DrvSwapBuffers((HDC)1) && LastError == ERROR_BUSY && Presents == presents);
    glEnd();
    assert(!DrvSwapLayerBuffers((HDC)1, 3) && LastError == ERROR_INVALID_PARAMETER);
    assert(DrvSwapLayerBuffers((HDC)1, WGL_SWAP_MAIN_PLANE) && Presents == presents + 1);
    assert(!DrvReleaseContext((HGLRC)999) && wglGetCurrentContext() == context);
    assert(DrvReleaseContext(context));
    Thread = 0;
    assert(DrvSetContext((HDC)1, context, nullptr) == &Table);
    assert(DrvDeleteContext(context) && !wglGetCurrentContext() && !Allocations && Closes == 1);
}
static void sharing() {
    clean();
    auto source = create((HDC)1), destination = create((HDC)2);
    assert(DrvSetContext((HDC)1, source, Set));
    glBegin(GL_TRIANGLES);
    auto calls = Calls;
    assert(!DrvShareLists(source, destination) && Calls == calls);
    glEnd();
    assert(DrvShareLists(source, destination));
    auto names = Lookup(source)->Names;
    assert(names && Lookup(destination)->Names == names && names->References == 2 &&
           SharedWith[Lookup(destination)->Id] == Lookup(source)->Id);
    assert(DrvSetContext((HDC)2, destination, Set));
    calls = Calls;
    assert(!DrvShareLists(source, destination) && Calls == calls);
    assert(DrvDeleteContext(source) && names->References == 1);
    assert(DrvSwapBuffers((HDC)2));
    assert(DrvDeleteContext(destination) && !Allocations);

    clean();
    source = create((HDC)1);
    destination = create((HDC)2);
    assert(DrvSetContext((HDC)1, source, Set));
    FailOperation = DG_GL_CREATE_CONTEXT;
    FailStatus = DG_ESCAPE_HOST;
    FailError = DG_GL_ERROR_LIMIT;
    assert(!DrvShareLists(source, destination));
    assert(!Lookup(destination)->Created && Lookup(destination)->Failed &&
           Lookup(source)->Created && wglGetCurrentContext() == source);
    assert(DrvDeleteContext(destination) && DrvDeleteContext(source) && !Allocations);

    clean();
    source = create((HDC)1);
    destination = create((HDC)2);
    assert(DrvSetContext((HDC)1, source, Set));
    LostOperation = DG_GL_DESTROY_CONTEXT;
    ExecuteLost = TRUE;
    assert(!DrvShareLists(source, destination) && Lookup(destination)->Uncertain);
    calls = Calls;
    assert(!DrvDeleteContext(destination) && Calls == calls);
    clean(); // An uncertain owned object remains until process teardown; no replay.
}
static void failures_and_resize() {
    clean();
    auto context = create((HDC)1);
    FailTls = TRUE;
    assert(!DrvSetContext((HDC)1, context, Set) && !Callbacks && !wglGetCurrentContext() &&
           !Lookup(context)->Owner);
    FailTls = FALSE;
    assert(DrvDeleteContext(context) && !Allocations);
    clean();
    context = create((HDC)1);
    assert(DrvSetContext((HDC)1, context, Set));
    auto id = Lookup(context)->Id;
    Width = 640;
    Height = 480;
    assert(DrvSwapBuffers((HDC)1) && Lookup(context)->Id == id && Lookup(context)->Width == Width &&
           Lookup(context)->Height == Height && Callbacks == 1);
    auto presents = Presents;
    Width = 800;
    FailOperation = DG_GL_CREATE_DRAWABLE;
    FailStatus = DG_ESCAPE_HOST;
    FailError = DG_GL_ERROR_LIMIT;
    assert(!DrvSwapBuffers((HDC)1) && Presents == presents && !wglGetCurrentContext() &&
           !Lookup(context)->Owner && !Lookup(context)->Drawable);
    assert(DrvDeleteContext(context) && !Allocations);
}
int main() {
    formats();
    threads_and_swap();
    sharing();
    failures_and_resize();
    assert(DllMain(nullptr, DLL_PROCESS_DETACH, nullptr));
    puts(
        "PASS actual ICD+frontend: format adoption/bounds, callback publication, thread ownership, "
        "shared namespace lifetime/failures, swap and resize transaction");
}
