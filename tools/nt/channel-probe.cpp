/* SPDX-License-Identifier: GPL-2.0-or-later
 * Actual NT5 user-channel acceptance. No guest pointers cross the driver ABI.
 * Typed queries, bad-result isolation, immutable texture data and seven export
 * credit cycles are checked independently of a public OpenGL frontend. */
#define WIN32_LEAN_AND_MEAN
extern "C" {
#include <windows.h>
#include "dg-escape.h"
#include "gl.h"
#include "gl-funcs.h"
static HDC Display;
static HANDLE LogFile;
static ULONG Client, Used;
static struct {
    DG_ESCAPE_REQUEST Header;
    ULONG Words[DG_ESCAPE_MAX_BYTES / 4];
} Packet;
static struct {
    DG_ESCAPE_REPLY Header;
    ULONG Words[DG_ESCAPE_MAX_RESULT_BYTES / 4];
} Reply;
static void Log(const char *text) {
    DWORD written;
    WriteFile(LogFile, text, lstrlenA(text), &written, NULL);
    WriteFile(LogFile, "\r\n", 2, &written, NULL);
    FlushFileBuffers(LogFile);
}
static BOOL Check(BOOL result, const char *message) {
    if (result) {
        Log(message);
        return TRUE;
    }
    Log(message);
    MessageBoxA(NULL, message, "FAIL DreamGPU NT channel", MB_OK | MB_ICONERROR);
    ExitProcess(2);
    return FALSE;
}
static BOOL Request(ULONG operation, ULONG expected, ULONG capacity) {
    int result;
    ULONG i;
    CHAR text[192];
    Packet.Header.Version = DG_ESCAPE_VERSION;
    Packet.Header.Operation = operation;
    Packet.Header.Client = Client;
    Packet.Header.Bytes = operation == DG_ESCAPE_SUBMIT ? Used * 4 : 0;
    Packet.Header.Reserved = Packet.Header.Reserved2 = 0;
    Packet.Header.ResultCapacity = capacity;
    memset(&Reply, 0xa5, sizeof(Reply));
    result = ExtEscape(Display, DG_ESCAPE, sizeof(Packet.Header) + Packet.Header.Bytes,
                       (LPCSTR)&Packet, sizeof(Reply.Header) + capacity, (LPSTR)&Reply);
    if (result != 1 || Reply.Header.Version != DG_ESCAPE_VERSION ||
        Reply.Header.Status != expected ||
        Reply.Header.MaxResultBytes != DG_ESCAPE_MAX_RESULT_BYTES) {
        wsprintfA(text, "FAIL request op=%lu expected=%lu result=%d status=%lu native=%lu seq=%lu",
                  operation, expected, result, Reply.Header.Status, Reply.Header.DeviceError,
                  Reply.Header.CompletedSequence);
        Check(FALSE, text);
    }
    if (expected != DG_ESCAPE_OK) {
        Check(!Reply.Header.ResultBytes && !Reply.Header.ResultType,
              "failed request has no result identity");
    }
    for (i = Reply.Header.ResultBytes; i < capacity; ++i)
        if (((BYTE *)Reply.Words)[i])
            Check(FALSE, "FAIL unused query output is not zero");
    return TRUE;
}
static ULONG *Record(ULONG op, ULONG n) {
    ULONG *r = Packet.Words + Used;
    if (Used + 8 + n > DG_ESCAPE_MAX_BYTES / 4)
        Check(FALSE, "FAIL diagnostic batch bound");
    memset(r, 0, (8 + n) * 4);
    r[0] = op;
    r[1] = (8 + n) * 4;
    r[3] = r[4] = 1;
    Used += 8 + n;
    return r + 8;
}
static void Call(ULONG function, ULONG n, ULONG a, ULONG b, ULONG c, ULONG d) {
    ULONG values[4], i, *r = Record(DG_GL_CALL, 1 + n);
    values[0] = a;
    values[1] = b;
    values[2] = c;
    values[3] = d;
    r[0] = function;
    for (i = 0; i < n; ++i)
        r[1 + i] = values[i];
}
static void Query(ULONG function, ULONG a, ULONG b, ULONG c, ULONG capacity, ULONG expected) {
    ULONG *r;
    Used = 0;
    Packet.Header.Function = 0;
    r = Record(DG_GL_QUERY, 4);
    r[0] = function;
    r[1] = a;
    r[2] = b;
    r[3] = c;
    Request(DG_ESCAPE_SUBMIT, expected, capacity);
}
static void Data(ULONG function, const ULONG *args, ULONG count, const ULONG *pixels, ULONG bytes) {
    ULONG *r = Record(DG_GL_DATA_CALL, 2 + count + (bytes + 3) / 4);
    r[0] = function;
    r[1] = bytes;
    memcpy(r + 2, args, count * 4);
    if (bytes)
        memcpy(r + 2 + count, pixels, bytes);
}
static void Tests(void) {
    ULONG *r, i, frame, pixels[16], args[8];
    static const ULONG texcoords[4][2] = {
        {0, 0}, {0x3f800000, 0}, {0x3f800000, 0x3f800000}, {0, 0x3f800000}};
    static const ULONG vertices[4][2] = {{0xbf800000, 0xbf800000},
                                         {0x3f800000, 0xbf800000},
                                         {0x3f800000, 0x3f800000},
                                         {0xbf800000, 0x3f800000}};
    Request(DG_ESCAPE_OPEN, DG_ESCAPE_OK, 0);
    Client = Reply.Header.Client;
    Check(Client != 0 && Reply.Header.MaxBytes == DG_ESCAPE_MAX_BYTES,
          "PASS v2 open bounded channel");
    Packet.Header.Function = FEnum_glTexImage2D;
    Request(DG_ESCAPE_QUERY, DG_ESCAPE_OK, 0);
    Check(Reply.Header.FunctionWords == (DG_GL_FUNCTION_INLINE_DATA | 8),
          "PASS advertised texture data signature");
    Packet.Header.Function = FEnum_glGetIntegerv;
    Request(DG_ESCAPE_QUERY, DG_ESCAPE_OK, 0);
    Check(Reply.Header.FunctionWords == (DG_GL_FUNCTION_QUERY | 1),
          "PASS advertised typed query signature");
    Packet.Header.Function = 0;
    Used = 0;
    Record(DG_GL_CREATE_CONTEXT, 1);
    r = Record(DG_GL_CREATE_DRAWABLE, 2);
    r[0] = 32;
    r[1] = 16;
    Record(DG_GL_MAKE_CURRENT, 0);
    Request(DG_ESCAPE_SUBMIT, DG_ESCAPE_OK, 0);
    Query(FEnum_glGetIntegerv, 0x0ba2, 0, 0, 512, DG_ESCAPE_OK);
    Check(Reply.Header.ResultType == DG_GL_RESULT_INT && Reply.Header.ResultBytes == 16 &&
              !Reply.Words[0] && !Reply.Words[1] && Reply.Words[2] == 32 && Reply.Words[3] == 16,
          "PASS exact integer viewport query");
    Query(FEnum_glGetFloatv, 0x0b00, 0, 0, 512, DG_ESCAPE_OK);
    Check(Reply.Header.ResultType == DG_GL_RESULT_FLOAT && Reply.Header.ResultBytes == 16 &&
              Reply.Words[0] == 0x3f800000 && Reply.Words[3] == 0x3f800000,
          "PASS exact float color query");
    Query(FEnum_glGetDoublev, 0x0ba6, 0, 0, 512, DG_ESCAPE_OK);
    Check(Reply.Header.ResultType == DG_GL_RESULT_DOUBLE && Reply.Header.ResultBytes == 128,
          "PASS bounded double matrix result");
    for (i = 0; i < 16; ++i)
        Check(!Reply.Words[i * 2] && Reply.Words[i * 2 + 1] == (i % 5 ? 0 : 0x3ff00000),
              "PASS identity matrix element");
    Query(FEnum_glGetBooleanv, 0x0b71, 0, 0, 512, DG_ESCAPE_OK);
    Check(Reply.Header.ResultType == DG_GL_RESULT_BOOL && Reply.Header.ResultBytes == 1 &&
              !((BYTE *)Reply.Words)[0],
          "PASS exact Boolean depth-test query");
    Query(FEnum_glGetString, 0x1f00, 0, 0, 512, DG_ESCAPE_OK);
    Check(Reply.Header.ResultType == DG_GL_RESULT_STRING && Reply.Header.ResultBytes > 1 &&
              !((BYTE *)Reply.Words)[Reply.Header.ResultBytes - 1],
          "PASS bounded NUL string result");
    Query(FEnum_glGetIntegerv, 0xffffffff, 0, 0, 512, DG_ESCAPE_HOST);
    Query(FEnum_glGetDoublev, 0x0ba6, 0, 0, 4, DG_ESCAPE_HOST);
    Query(FEnum_glGetIntegerv, 0x0ba2, 0, 0, 512, DG_ESCAPE_OK);
    Check(Reply.Header.ResultBytes == 16 && Reply.Words[2] == 32,
          "PASS query errors return no stale bytes and recover");
    Used = 0;
    Call(FEnum_glBindTexture, 2, 0x0de1, 7, 0, 0);
    Call(FEnum_glTexParameteri, 3, 0x0de1, 0x2801, 0x2600, 0);
    Call(FEnum_glTexParameteri, 3, 0x0de1, 0x2800, 0x2600, 0);
    args[0] = 0x0de1;
    args[1] = 0;
    args[2] = 0x1908;
    args[3] = args[4] = 4;
    args[5] = 0;
    args[6] = 0x1908;
    args[7] = 0x1401;
    for (i = 0; i < 16; ++i)
        pixels[i] = 0xff0000ff; /* tightly packed RGBA red */
    Data(FEnum_glTexImage2D, args, 8, pixels, sizeof(pixels));
    Request(DG_ESCAPE_SUBMIT, DG_ESCAPE_OK, 0);
    Query(FEnum_glGetTexLevelParameteriv, 0x0de1, 0, 0x1000, 512, DG_ESCAPE_OK);
    Check(Reply.Header.ResultBytes == 4 && Reply.Words[0] == 4,
          "PASS immutable texture upload and width query");
    Used = 0;
    args[2] = args[3] = 1;
    args[4] = args[5] = 2;
    for (i = 0; i < 4; ++i)
        pixels[i] = 0xff00ff00;
    Data(FEnum_glTexSubImage2D, args, 8, pixels, 16);
    Request(DG_ESCAPE_SUBMIT, DG_ESCAPE_OK, 0);
    Used = 0;
    Data(FEnum_glTexSubImage2D, args, 8, pixels, 16);
    Packet.Words[9] = 0xffffffff; /* invalid immutable payload before host submission */
    Request(DG_ESCAPE_SUBMIT, DG_ESCAPE_INVALID, 0);
    Check(!Reply.Header.CompletedSequence, "PASS malformed data rejected before native sequence");
    for (frame = 0; frame < 7; ++frame) {
        Used = 0;
        Call(FEnum_glEnable, 1, 0x0de1, 0, 0, 0);
        Call(FEnum_glBegin, 1, 7, 0, 0, 0);
        for (i = 0; i < 4; ++i) {
            Call(FEnum_glTexCoord2f, 2, texcoords[i][0], texcoords[i][1], 0, 0);
            Call(FEnum_glVertex3f, 3, vertices[i][0], vertices[i][1], 0, 0);
        }
        Call(FEnum_glEnd, 0, 0, 0, 0, 0);
        Record(DG_GL_PRESENT, 0);
        Request(DG_ESCAPE_SUBMIT, DG_ESCAPE_OK, 0);
        Sleep(20);
    }
    Log("PASS seven textured export cycles without credit starvation");
    Packet.Header.Function = 0;
    Request(DG_ESCAPE_CLOSE, DG_ESCAPE_OK, 0);
    Client = 0;
    Log("PASS explicit client cleanup");
}
void __stdcall WinMainCRTStartup(void) {
    LogFile = CreateFileA("C:\\DGCHAN.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, NULL);
    if (LogFile == INVALID_HANDLE_VALUE)
        ExitProcess(3);
    Display = CreateDCA("DISPLAY", NULL, NULL, NULL);
    Check(Display != NULL, "PASS display opened");
    Tests();
    DeleteDC(Display);
    Log("DONE PASS all typed queries, bounded data, failure isolation and seven presents");
    CloseHandle(LogFile);
    MessageBoxA(NULL,
                "PASS typed queries, immutable textures, failed-query isolation, seven GPU "
                "presents and cleanup",
                "DreamGPU NT channel v2", MB_OK);
    ExitProcess(0);
}

} /* extern C */
