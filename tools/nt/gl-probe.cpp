/* SPDX-License-Identifier: GPL-2.0-or-later
 * Real NT5 user -> GDI -> miniport -> QEMU GL diagnostic. No OpenGL DLL claim.
 */
#define WIN32_LEAN_AND_MEAN
extern "C" {
#include <windows.h>
#include <winioctl.h>
#include "dg-kernel.h"
#include "dg-escape.h"
#include "gl.h"
#include "gl-funcs.h"

static HANDLE LogFile;
static HDC Display;
static struct {
    DG_ESCAPE_REQUEST Request;
    ULONG Words[1024];
} Packet;
static DG_ESCAPE_REPLY Reply;
static ULONG Client, Used;
static BOOL UserGuardExercised;
static void Log(const char *line);

static BOOL DenyUserKernelChannel(void) {
    HANDLE device;
    ULONG version = DG_KERNEL_VERSION;
    DWORD returned = 0, error;
    DG_KERNEL_INTERFACE channel;
    BOOL success;
    CHAR line[192];
    /* VideoPort denies read/write display handles to user mode on NT5.  This
     * private control code is FILE_ANY_ACCESS, so try a metadata-only handle.
     * VideoPort may still reject the open before the IRP guard is reached. */
    device = CreateFileA("\\\\.\\DISPLAY1", 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         OPEN_EXISTING, 0, NULL);
    if (device == INVALID_HANDLE_VALUE) {
        wsprintfA(line,
                  "INCONCLUSIVE: user kernel-channel IRP guard unavailable; display open error=%lu",
                  GetLastError());
        Log(line);
        return TRUE; /* Independent GL coverage can proceed; this is no guard pass. */
    }
    ZeroMemory(&channel, sizeof(channel));
    success = DeviceIoControl(device, IOCTL_VIDEO_DG_KERNEL, &version, sizeof(version), &channel,
                              sizeof(channel), &returned, NULL);
    error = GetLastError();
    CloseHandle(device);
    wsprintfA(line, "User kernel-channel request: success=%d error=%lu bytes=%lu", success, error,
              returned);
    Log(line);
    UserGuardExercised = TRUE;
    return !success && error == ERROR_ACCESS_DENIED && !returned && !channel.Submit &&
           !channel.Context;
}

static void Log(const char *line) {
    DWORD written;
    WriteFile(LogFile, line, lstrlenA(line), &written, NULL);
    WriteFile(LogFile, "\r\n", 2, &written, NULL);
    FlushFileBuffers(LogFile);
}

static BOOL Request(ULONG operation, ULONG client, ULONG function, ULONG expected) {
    CHAR line[256];
    int result;
    Packet.Request.Version = DG_ESCAPE_VERSION;
    Packet.Request.Operation = operation;
    Packet.Request.Client = client;
    Packet.Request.Bytes = operation == DG_ESCAPE_SUBMIT ? Used * 4 : 0;
    Packet.Request.Function = function;
    Packet.Request.Reserved = 0;
    ZeroMemory(&Reply, sizeof(Reply));
    result = ExtEscape(Display, DG_ESCAPE, sizeof(Packet.Request) + Packet.Request.Bytes,
                       (LPCSTR)&Packet, sizeof(Reply), (LPSTR)&Reply);
    wsprintfA(line,
              "op=%lu result=%d status=%lu token=%lu generation=%lu function_words=%lu "
              "host_error=%lu sequence=%lu",
              operation, result, Reply.Status, Reply.Client, Reply.Generation, Reply.FunctionWords,
              Reply.DeviceError, Reply.CompletedSequence);
    Log(line);
    return result == 1 && Reply.Version == DG_ESCAPE_VERSION && Reply.Status == expected;
}

static ULONG *Record(ULONG operation, ULONG context, ULONG drawable, ULONG arguments) {
    ULONG *record = &Packet.Words[Used];
    ZeroMemory(record, (8 + arguments) * 4);
    record[0] = operation;
    record[1] = (8 + arguments) * 4;
    /* Deliberately forged identity/generation are overwritten by the driver. */
    record[2] = 0xdeadbeef;
    record[3] = context;
    record[4] = drawable;
    record[7] = 0xdeadbeef;
    Used += 8 + arguments;
    return record + 8;
}

static void ColorFrame(void) {
    ULONG *args;
    args = Record(DG_GL_CALL, 1, 1, 5);
    args[0] = FEnum_glClearColor;
    args[1] = 0x3f800000;
    args[2] = 0;
    args[3] = 0;
    args[4] = 0x3f800000;
    args = Record(DG_GL_CALL, 1, 1, 2);
    args[0] = FEnum_glClear;
    args[1] = 0x4000;
    args = Record(DG_GL_CALL, 1, 1, 2);
    args[0] = FEnum_glEnable;
    args[1] = 0x0c11;
    args = Record(DG_GL_CALL, 1, 1, 5);
    args[0] = FEnum_glScissor;
    args[1] = 0;
    args[2] = 0;
    args[3] = 320;
    args[4] = 120;
    args = Record(DG_GL_CALL, 1, 1, 5);
    args[0] = FEnum_glClearColor;
    args[1] = 0;
    args[2] = 0;
    args[3] = 0x3f800000;
    args[4] = 0x3f800000;
    args = Record(DG_GL_CALL, 1, 1, 2);
    args[0] = FEnum_glClear;
    args[1] = 0x4000;
    args = Record(DG_GL_CALL, 1, 1, 2);
    args[0] = FEnum_glDisable;
    args[1] = 0x0c11;
    Record(DG_GL_PRESENT, 1, 1, 0);
}

void WINAPI WinMainCRTStartup(void) {
    ULONG *args, escape = DG_ESCAPE, frame;
    BOOL leak = FALSE, passed = FALSE;
    LPCSTR command = GetCommandLineA();
    while (*command) {
        if (*command++ == '/') {
            leak = TRUE;
            break;
        }
    }
    LogFile = CreateFileA(leak ? "C:\\DGLEAK.LOG" : "C:\\DGGL.LOG", GENERIC_WRITE, FILE_SHARE_READ,
                          NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (LogFile == INVALID_HANDLE_VALUE)
        ExitProcess(1);
    Display = GetDC(NULL);
    if (!Display ||
        ExtEscape(Display, QUERYESCSUPPORT, sizeof(escape), (LPCSTR)&escape, 0, NULL) != 1) {
        Log("FAIL: active display does not expose the DreamGPU GL diagnostic escape");
        goto done;
    }
    if (!DenyUserKernelChannel())
        goto done;
    if (!Request(DG_ESCAPE_OPEN, 0, 0, DG_ESCAPE_OK))
        goto done;
    Client = Reply.Client;
    if (!Client || Reply.MaxBytes < sizeof(Packet.Words))
        goto done;
    if (!Request(DG_ESCAPE_QUERY, Client + 1024, FEnum_glClearColor, DG_ESCAPE_OWNER))
        goto done;
    if (!Request(DG_ESCAPE_QUERY, Client, FEnum_glClearColor, DG_ESCAPE_OK) ||
        Reply.FunctionWords != 4)
        goto done;
    if (!Request(DG_ESCAPE_QUERY, Client, FEnum_glClear, DG_ESCAPE_OK) || Reply.FunctionWords != 1)
        goto done;
    if (!Request(DG_ESCAPE_QUERY, Client, 0xffffffff, DG_ESCAPE_OK) ||
        Reply.FunctionWords != 0xffffffff)
        goto done;
    Used = 0;
    Record(DG_GL_PRESENT, 1, 1, 0);
    Packet.Words[5] = DG_GL_PRESENT_EXCLUSIVE;
    if (!Request(DG_ESCAPE_SUBMIT, Client, 0, DG_ESCAPE_INVALID))
        goto done;
    Used = 0;
    args = Record(DG_GL_CREATE_CONTEXT, 1, 0, 1);
    args[0] = 0;
    args = Record(DG_GL_CREATE_DRAWABLE, 0, 1, 2);
    args[0] = 320;
    args[1] = 240;
    Record(DG_GL_MAKE_CURRENT, 1, 1, 0);
    ColorFrame();
    Log("Expected drawable: 320x240, opaque red top half and blue bottom half (GL scissor y=0 is "
        "bottom)");
    if (!Request(DG_ESCAPE_SUBMIT, Client, 0, DG_ESCAPE_OK))
        goto done;
    for (frame = 1; frame < 7; ++frame) {
        /* Let the ordinary DreamGPU event loop return export credits. This is a
         * lifecycle diagnostic, not a FPS benchmark or a busy retry loop. */
        Sleep(20);
        Used = 0;
        ColorFrame();
        if (!Request(DG_ESCAPE_SUBMIT, Client, 0, DG_ESCAPE_OK))
            goto done;
    }
    if (leak) {
        Log("PASS: intentionally exiting with live context/drawable; kernel exit callback must "
            "close client");
        passed = TRUE;
        Client = 0;
        goto done;
    }
    if (!Request(DG_ESCAPE_CLOSE, Client, 0, DG_ESCAPE_OK))
        goto done;
    if (!Request(DG_ESCAPE_QUERY, Client, FEnum_glClearColor, DG_ESCAPE_OWNER))
        goto done;
    Client = 0;
    Log("PASS GL: process ownership, function query, privileged flag rejection, seven GL frames, "
        "explicit close");
    Log(UserGuardExercised ? "PASS GUARD: actual user IRP denied with empty output"
                           : "INCONCLUSIVE GUARD: display device could not be opened; no user IRP "
                             "reached miniport");
    passed = TRUE;
done:
    if (Client)
        Request(DG_ESCAPE_CLOSE, Client, 0, DG_ESCAPE_OK);
    if (!passed)
        Log("FAIL");
    if (Display)
        ReleaseDC(NULL, Display);
    CloseHandle(LogFile);
    ExitProcess(passed ? 0 : 1);
}

} /* extern C */
