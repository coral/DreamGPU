/* SPDX-License-Identifier: GPL-2.0-or-later
 * Persistent Win98/NT5 benchmark controller. Only its own fixed Half-Life process
 * and fresh console logs are managed. The COM1 command loop blocks in ReadFile.
 */
#define WIN32_LEAN_AND_MEAN
#include "../../guest/include/ownership.hpp"
#include <bit>
#include "pe-version.h"
extern "C" {
#include <windows.h>
#include "retail-settings.h"
#include "retail-media.h"
#include "serial-startup.h"

} /* extern C */
struct ProcessHandleDeleter {
    void operator()(void *handle) const noexcept {
        CloseHandle(handle);
    }
};
extern "C" {

#define TOKEN_MAX 64
#define LINE_MAX 160
#define OUTPUT_MAX 65536
#define RUN_TIMEOUT_MS 120000
#define STABLE_MS 300
static const char Game[] = "C:\\SIERRA\\Half-Life";
static const char Executable[] = "C:\\SIERRA\\Half-Life\\hl.exe";
static const char *Logs[2] = {"C:\\SIERRA\\Half-Life\\qconsole.log",
                              "C:\\SIERRA\\Half-Life\\valve\\qconsole.log"};
static HANDLE Serial;
static HANDLE StartupLog = INVALID_HANDLE_VALUE;
static BYTE Output[OUTPUT_MAX];
static BYTE Reading[OUTPUT_MAX];
static DWORD OutputBytes, ResultStart, ResultBytes;
static const char Hex[] = "0123456789abcdef";
static char Instance[98]; /* build SHA256 + '-' + PID/time/tick process identity */

void *memset(void *destination, int value, unsigned int bytes) {
    volatile BYTE *p = (volatile BYTE *)destination;
    unsigned int i;
    for (i = 0; i < bytes; ++i)
        p[i] = (BYTE)value;
    return destination;
}
#include "process-evidence.h"
#include "renderer-evidence.h"
static BOOL Equal(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}
static BOOL Token(const char *s) {
    DWORD n = 0;
    while (s[n]) {
        char c = s[n++];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-') ||
            n > TOKEN_MAX)
            return FALSE;
    }
    return n != 0;
}
static char *Append(char *out, const char *value) {
    while (*value)
        *out++ = *value++;
    *out = 0;
    return out;
}
static BOOL WriteAll(const void *data, DWORD bytes) {
    const BYTE *p = (const BYTE *)data;
    while (bytes) {
        DWORD written = 0, chunk = bytes > 1024 ? 1024 : bytes;
        if (!WriteFile(Serial, p, chunk, &written, NULL) || !written)
            return FALSE;
        p += written;
        bytes -= written;
    }
    return TRUE;
}
static BOOL Reply(const char *kind, const char *id, const char *code, BOOL payload) {
    char header[256], encoded[1024], *p = header;
    DWORD i = 0;
    p = Append(p, kind);
    p = Append(p, " ");
    p = Append(p, id);
    if (code) {
        p = Append(p, " ");
        p = Append(p, code);
    }
    if (payload && OutputBytes)
        p = Append(p, " ");
    if (!WriteAll(header, (DWORD)(p - header)))
        return FALSE;
    while (payload && i < OutputBytes) {
        DWORD n = 0;
        while (n < sizeof(encoded) && i < OutputBytes) {
            BYTE b = Output[i++];
            encoded[n++] = Hex[b >> 4];
            encoded[n++] = Hex[b & 15];
        }
        if (!WriteAll(encoded, n))
            return FALSE;
    }
    return WriteAll("\n", 1) && FlushFileBuffers(Serial);
}

static BOOL Space(BYTE c) {
    return c == ' ' || c == '\t';
}
static BOOL Number(const BYTE **position, const BYTE *end, BOOL fraction) {
    const BYTE *p = *position;
    BOOL digit = FALSE, nonzero = FALSE;
    while (p < end && *p >= '0' && *p <= '9') {
        digit = TRUE;
        nonzero |= *p != '0';
        ++p;
    }
    if (!digit)
        return FALSE;
    if (fraction && p < end && *p == '.') {
        ++p;
        digit = FALSE;
        while (p < end && *p >= '0' && *p <= '9') {
            digit = TRUE;
            nonzero |= *p != '0';
            ++p;
        }
        if (!digit)
            return FALSE;
    }
    *position = p;
    return nonzero;
}
static BOOL Word(const BYTE **position, const BYTE *end, const char *word) {
    const BYTE *p = *position;
    if (p == end || !Space(*p))
        return FALSE;
    while (p < end && Space(*p))
        ++p;
    while (*word) {
        if (p == end || *p++ != (BYTE)*word++)
            return FALSE;
    }
    *position = p;
    return TRUE;
}
static BOOL CompletedLine(const BYTE *p, const BYTE *end) {
    while (p < end && Space(*p))
        ++p;
    if (!Number(&p, end, FALSE) || !Word(&p, end, "frames"))
        return FALSE;
    if (p == end || !Space(*p))
        return FALSE;
    while (p < end && Space(*p))
        ++p;
    if (!Number(&p, end, TRUE) || !Word(&p, end, "seconds"))
        return FALSE;
    if (p == end || !Space(*p))
        return FALSE;
    while (p < end && Space(*p))
        ++p;
    if (!Number(&p, end, TRUE) || !Word(&p, end, "fps"))
        return FALSE;
    while (p < end && (Space(*p) || *p == '\r'))
        ++p;
    return p == end;
}
static BOOL Completed(const BYTE *data, DWORD bytes, BOOL truncated) {
    DWORD start = 0, i;
    ResultBytes = 0;
    /* A bounded tail may begin halfway through a line. Never match that one. */
    if (truncated) {
        while (start < bytes && data[start] != '\n')
            ++start;
        ++start;
    }
    for (i = start; i < bytes; ++i)
        if (data[i] == '\n') {
            if (CompletedLine(data + start, data + i)) {
                ResultStart = start;
                ResultBytes = i + 1 - start;
                return TRUE;
            }
            start = i + 1;
        }
    if (start < bytes && CompletedLine(data + start, data + bytes)) {
        ResultStart = start;
        ResultBytes = bytes - start;
        return TRUE;
    }
    return FALSE;
}

/* Win98-compatible 64-bit file operations. A low word of 0xffffffff is
 * valid; inspect the last-error sentinel before treating it as failure. */
static BOOL FileSize(HANDLE file, LARGE_INTEGER *size) {
    DWORD high = 0, low;
    SetLastError(ERROR_SUCCESS);
    low = GetFileSize(file, &high);
    if (low == INVALID_FILE_SIZE && GetLastError() != ERROR_SUCCESS)
        return FALSE;
    size->LowPart = low;
    size->HighPart = (LONG)high;
    return TRUE;
}
static BOOL SeekFile(HANDLE file, LARGE_INTEGER position) {
    LONG high = position.HighPart;
    DWORD low;
    SetLastError(ERROR_SUCCESS);
    low = SetFilePointer(file, (LONG)position.LowPart, &high, FILE_BEGIN);
    if (low == INVALID_SET_FILE_POINTER && GetLastError() != ERROR_SUCCESS)
        return FALSE;
    if (low != position.LowPart || high != position.HighPart) {
        SetLastError(ERROR_SEEK);
        return FALSE;
    }
    return TRUE;
}
typedef struct {
    DWORD Size, High, Changed, ReadSize, ReadHigh;
    FILETIME Time, ReadTime;
    BOOL Seen, Read;
} LOG_STATE;
static BOOL SameTime(FILETIME a, FILETIME b) {
    return a.dwLowDateTime == b.dwLowDateTime && a.dwHighDateTime == b.dwHighDateTime;
}
/* Returns1 only for a stable, fresh, complete result. Large logs yield only
 * their final64KiB. A quiet loading log never counts as a completed benchmark. */
static BOOL PollLog(const char *path, LOG_STATE *state, BOOL exited) {
    WIN32_FILE_ATTRIBUTE_DATA attr;
    HANDLE file;
    DWORD now = GetTickCount(), read = 0, i;
    LARGE_INTEGER size, start, after;
    FILETIME written;
    BOOL complete = FALSE;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &attr) ||
        (attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return FALSE;
    if (!state->Seen || state->Size != attr.nFileSizeLow || state->High != attr.nFileSizeHigh ||
        !SameTime(state->Time, attr.ftLastWriteTime)) {
        state->Seen = TRUE;
        state->Size = attr.nFileSizeLow;
        state->High = attr.nFileSizeHigh;
        state->Time = attr.ftLastWriteTime;
        state->Changed = now;
    }
    if ((!state->Size && !state->High) || (!exited && (DWORD)(now - state->Changed) < STABLE_MS) ||
        (state->Read && state->ReadSize == state->Size && state->ReadHigh == state->High &&
         SameTime(state->ReadTime, state->Time)))
        return FALSE;
    file = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    if (!FileSize(file, &size) || size.QuadPart <= 0 || !FlushFileBuffers(file))
        goto done;
    start.QuadPart = size.QuadPart > OUTPUT_MAX ? size.QuadPart - OUTPUT_MAX : 0;
    if (!SeekFile(file, start) ||
        !ReadFile(file, Reading, (DWORD)(size.QuadPart - start.QuadPart), &read, NULL) ||
        read != (DWORD)(size.QuadPart - start.QuadPart) || !FileSize(file, &after) ||
        after.QuadPart != size.QuadPart || !GetFileTime(file, NULL, NULL, &written) ||
        !SameTime(written, state->Time))
        goto done;
    for (i = 0; i < read; ++i)
        Output[i] = Reading[i];
    OutputBytes = read;
    state->Read = TRUE;
    state->ReadSize = state->Size;
    state->ReadHigh = state->High;
    state->ReadTime = state->Time;
    complete = Completed(Output, OutputBytes, start.QuadPart != 0);
    if (complete) {
        for (i = 0; i < ResultBytes; ++i)
            Output[i] = Output[ResultStart + i];
        OutputBytes = ResultBytes;
    }
done:
    CloseHandle(file);
    return complete;
}
static BOOL CALLBACK CloseOwnedWindow(HWND window, LPARAM process) {
    DWORD owner;
    GetWindowThreadProcessId(window, &owner);
    if (owner == (DWORD)process)
        PostMessageA(window, WM_CLOSE, 0, 0);
    return TRUE;
}
static BOOL StopOwned(PROCESS_INFORMATION *process) {
    if (WaitForSingleObject(process->hProcess, 0) == WAIT_OBJECT_0)
        return TRUE;
    EnumWindows(CloseOwnedWindow, (LPARAM)process->dwProcessId);
    if (WaitForSingleObject(process->hProcess, 5000) == WAIT_OBJECT_0)
        return TRUE;
    if (!TerminateProcess(process->hProcess, 125) &&
        WaitForSingleObject(process->hProcess, 0) != WAIT_OBJECT_0)
        return FALSE;
    return WaitForSingleObject(process->hProcess, 1000) == WAIT_OBJECT_0;
}
static void CaptureFailureProcess(PROCESS_INFORMATION *process) {
    char text[1024];
    DWORD bytes = 0, i;
    ProcessEvidence(process->dwProcessId, process->hProcess, text, sizeof(text), &bytes);
    if (OutputBytes > OUTPUT_MAX - bytes)
        OutputBytes = OUTPUT_MAX - bytes;
    for (i = 0; i < bytes; i++)
        Output[OutputBytes++] = (BYTE)text[i];
}

/* Failure-only modal text evidence. Limit both time and bytes, and inspect only
 * this owned process's window classes/titles and standard dialog static labels. Never read edit
 * controls or send input to a modal dialog. */
typedef struct {
    DWORD Process, Started, Calls, Bytes;
    BYTE Text[4096];
} MODAL_SCAN;
static BOOL ModalBudget(MODAL_SCAN *scan) {
    return scan->Calls < 32 && scan->Bytes < 4096 && (DWORD)(GetTickCount() - scan->Started) < 500;
}
static void ModalAppend(MODAL_SCAN *scan, const char *text) {
    while (*text && scan->Bytes < sizeof(scan->Text)) {
        BYTE value = (BYTE)*text++;
        scan->Text[scan->Bytes++] =
            (value == '\r' || value == '\n' || value == '\t' || (value >= 32 && value < 127))
                ? value
                : '?';
    }
}
static void ModalText(HWND window, MODAL_SCAN *scan, const char *prefix) {
    char text[512];
    DWORD_PTR result = 0;
    DWORD owner;
    if (!ModalBudget(scan))
        return;
    GetWindowThreadProcessId(window, &owner);
    if (owner != scan->Process)
        return;
    text[0] = 0;
    ++scan->Calls;
    if (!SendMessageTimeoutA(window, WM_GETTEXT, sizeof(text), (LPARAM)text,
                             SMTO_ABORTIFHUNG | SMTO_BLOCK, 50, &result))
        return;
    text[sizeof(text) - 1] = 0;
    if (text[0]) {
        ModalAppend(scan, prefix);
        ModalAppend(scan, text);
        ModalAppend(scan, "\r\n");
    }
}
static BOOL CALLBACK ModalChild(HWND window, LPARAM parameter) {
    MODAL_SCAN *scan = (MODAL_SCAN *)parameter;
    char cls[32];
    if (!ModalBudget(scan))
        return FALSE;
    if (GetClassNameA(window, cls, sizeof(cls)) && Equal(cls, "Static"))
        ModalText(window, scan, "OWNED_MODAL static: ");
    return ModalBudget(scan);
}
static BOOL CALLBACK ModalWindow(HWND window, LPARAM parameter) {
    MODAL_SCAN *scan = (MODAL_SCAN *)parameter;
    DWORD owner;
    char cls[32];
    if (!ModalBudget(scan))
        return FALSE;
    GetWindowThreadProcessId(window, &owner);
    if (owner != scan->Process)
        return TRUE;
    if (!GetClassNameA(window, cls, sizeof(cls)))
        return TRUE;
    ModalAppend(scan, "OWNED_WINDOW class: ");
    ModalAppend(scan, cls);
    ModalAppend(scan, IsWindowVisible(window) ? " visible=1\r\n" : " visible=0\r\n");
    ModalText(window, scan, "OWNED_WINDOW title: ");
    if (Equal(cls, "#32770"))
        EnumChildWindows(window, ModalChild, parameter);
    return ModalBudget(scan);
}
static void CaptureOwnedModal(DWORD process) {
    MODAL_SCAN scan = {};
    DWORD i;
    scan.Process = process;
    scan.Started = GetTickCount();
    EnumWindows(ModalWindow, (LPARAM)&scan);
    if (!scan.Bytes || scan.Bytes > sizeof(scan.Text))
        return;
    /* Preserve the existing result unchanged when no modal was found. */
    if (OutputBytes > OUTPUT_MAX - scan.Bytes - 2)
        OutputBytes = OUTPUT_MAX - scan.Bytes - 2;
    if (OutputBytes && OutputBytes <= OUTPUT_MAX - 2 && Output[OutputBytes - 1] != '\n') {
        Output[OutputBytes++] = '\r';
        Output[OutputBytes++] = '\n';
    }
    for (i = 0; i < scan.Bytes && OutputBytes < OUTPUT_MAX; i++)
        Output[OutputBytes++] = scan.Text[i];
}

static const char *ProcessLaunchFailure(const char *reason) {
    DWORD error = GetLastError(), i;
    char *p = (char *)Output;
    p = Append(p, "CREATE_PROCESS_ERROR 0x");
    for (i = 0; i < 8; i++)
        *p++ = Hex[(error >> (28 - i * 4)) & 15];
    p = Append(p, "\r\n");
    OutputBytes = (DWORD)(p - (char *)Output);
    return reason;
}

static BOOL FixedAcknowledged(const char *kind, const char *id);
#include "launch-phase.h"

static const char *Run(const char *id, const char *demo) {
    char command[384], path[192], *p;
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    LOG_STATE logs[2];
    DWORD i, started, attributes;
    const char *error;
    BOOL complete = FALSE, resumed = FALSE, console_observed = FALSE;
    OutputBytes = 0;
    p = Append(path, Game);
    p = Append(p, "\\valve\\");
    p = Append(p, demo);
    Append(p, ".dem");
    attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
        return "missing-demo";
    for (i = 0; i < 2; ++i) {
        if (!DeleteFileA(Logs[i]) && GetLastError() != ERROR_FILE_NOT_FOUND &&
            GetLastError() != ERROR_PATH_NOT_FOUND)
            return "stale-log-delete";
        if (GetFileAttributesA(Logs[i]) != INVALID_FILE_ATTRIBUTES)
            return "stale-log-remains";
    }
    if (!DgRetailMediaAvailable())
        return "retail-media-missing";
    {
        const char *setting_error = ConfigureRetailGl();
        if (setting_error)
            return setting_error;
    }
    p = Append(command, "\"");
    p = Append(p, Executable);
    p = Append(p, "\" -windowed -toconsole -nosound -dev -gl -condebug +timedemo ");
    Append(p, demo);
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    memset(&process, 0, sizeof(process));
    memset(logs, 0, sizeof(logs));
    if (!CreateProcessA(Executable, command, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, Game,
                        &startup, &process))
        return ProcessLaunchFailure("launch-failed");
    dreamgpu::unique_owner<void, ProcessHandleDeleter> process_owner(process.hProcess);
    error = LaunchObserved(id, &process, &resumed);
    CloseHandle(process.hThread);
    if (!error) {
        error = "timeout";
        started = GetTickCount();
        for (;;) {
            BOOL exited = WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0;
            for (i = 0; i < 2; ++i)
                if (PollLog(Logs[i], &logs[i], exited)) {
                    complete = TRUE;
                    break;
                }
            if (OutputBytes && !console_observed) {
                console_observed = TRUE;
                if (!Reply("CONSOLE_OBSERVED", id, NULL, FALSE)) {
                    error = "serial-write";
                    break;
                }
            }
            if (complete) {
                error = ResultObserved(id);
                if (!error) {
                    /* OBSERVED is sent only after host sampling has stopped.
                     * Inspect only our still-owned child and its fresh log. */
                    BOOL prefix =
                        DgRenderText((char *)Output, sizeof(Output), &OutputBytes, "\r\n");
                    BOOL modules = DgRendererModules(process.dwProcessId, process.hProcess, Game,
                                                     (char *)Output, sizeof(Output), &OutputBytes);
                    BOOL observed = FALSE;
                    BOOL console = DgRendererLog(Logs[i], (char *)Reading, 32768, (char *)Output,
                                                 sizeof(Output), &OutputBytes, &observed);
                    if (!prefix || !modules || (observed && !console) ||
                        !DgRenderText((char *)Output, sizeof(Output), &OutputBytes,
                                      console ? "RENDERER_PROOF system-icd-and-engine-strings\r\n"
                                              : "ENGINE_GL_STRINGS unavailable\r\n"
                                                "RENDERER_PROOF system-icd-modules\r\n"))
                        error = "renderer-proof-failed";
                }
                break;
            }
            if (exited) {
                error = "process-exited";
                break;
            }
            if ((DWORD)(GetTickCount() - started) >= RUN_TIMEOUT_MS)
                break;
            Sleep(100);
        }
    }
    if (!resumed) {
        /* A suspended process cannot handle WM_CLOSE. Never release the main
         * thread on failed/missing host arming; terminate only our owned child. */
        if (!TerminateProcess(process.hProcess, 125) ||
            WaitForSingleObject(process.hProcess, 1000) != WAIT_OBJECT_0)
            error = "cleanup-failed";
    } else {
        if (error) {
            CaptureFailureProcess(&process);
            CaptureOwnedModal(process.dwProcessId);
        }
        if (!StopOwned(&process))
            error = "cleanup-failed";
    }
    return error;
}

/* Fixed probe whitelist: the serial peer cannot provide an executable path,
 * command line, file to read, or shell command. */
typedef struct {
    const char *name, *path, *log, *arguments;
    DWORD timeout;
} PROBE_SPEC;
static const PROBE_SPEC Probes[] = {
    {"sysinstall", "C:\\DGINST.EXE", "C:\\DGINST.LOG", "", 450000},
    {"sysresume", "C:\\DGCONT.EXE", "C:\\DGCONT.LOG", "", 450000},
    {"sysrecover", "C:\\DGRECOV.EXE", "C:\\DGRECOV.LOG", "", 450000},
    {"sysui", "C:\\DGSETUI.EXE", "C:\\DGSETUI.LOG", "", 450000},
    {"hlevidence", "C:\\DGHLEVID.EXE", "C:\\DGHLEVID.LOG", "", 10000},
    {"sysrepair", "C:\\DGREPAIR.EXE", "C:\\DGREPAIR.LOG", "", 450000},
    {"sysupgrade", "C:\\DGUPGR.EXE", "C:\\DGUPGR.LOG", "", 450000},
    {"sysrollback", "C:\\DGUNDO.EXE", "C:\\DGUNDO.LOG", "", 450000},
    {"sysremove", "C:\\DGREMOVE.EXE", "C:\\DGREMOVE.LOG", "", 450000},
    {"drvbind", "C:\\DGDRVB.EXE", "C:\\DGDRVB.LOG", "", 120000},
    {"drvcheck", "C:\\DGDRVC.EXE", "C:\\DGDRVC.LOG", "", 120000},
    {"drvrestore", "C:\\DGDRVR.EXE", "C:\\DGDRVR.LOG", "", 120000},
    {"sysd3d6", "C:\\DGSYS6.EXE", "C:\\DGSYS6.LOG", "", 30000},
    {"sysd3d7", "C:\\DGSYS7.EXE", "C:\\DGSYS7.LOG", "", 30000},
    {"sysd3d8", "C:\\DGSYS8.EXE", "C:\\DGSYS8.LOG", "", 30000},
    {"sysd3d9", "C:\\DGSYS9.EXE", "C:\\DGSYS9.LOG", "", 30000},
    {"sysgl", "C:\\DGSYSGL.EXE", "C:\\DGSYSGL.LOG", "", 30000},
    {"sysglide", "C:\\DGSYSGR.EXE", "C:\\DGSYSGR.LOG", "", 30000},
    {"setupcheck", "C:\\DGSETTST.EXE", "C:\\DGSETTST.LOG", "", 120000},
    {"ntrename", "C:\\DGRP.EXE", "C:\\DGRP.LOG", "", 30000},
    {"ntrestore", "C:\\DGRS.EXE", "C:\\DGRS.LOG", "", 30000},
    {"ntruntime", "C:\\DGRT.EXE", "C:\\DGRT.LOG", "", 30000},
    {"ntloader", "C:\\DGLOAD.EXE", "C:\\DGLOAD.LOG", "", 90000},
    {"sysddraw", "C:\\DGDD2D.EXE", "C:\\DGDD2D.LOG", "", 60000},
    {"sysddrawnative", "C:\\DGDD2D.EXE", "C:\\DGDD2D.LOG", "--native", 60000},
    {"hldebug", "C:\\SIERRA\\Half-Life\\DGHLDBG.EXE", "C:\\DGHLDBG.LOG", "", 40000},
    {"win9xinstall", "E:\\DG9INST.EXE", "C:\\DG9INST.LOG", "", 120000},
    {"win9xdiag", "C:\\SIERRA\\Half-Life\\DG9AUDIT.EXE", "C:\\DG9AUDIT.LOG", "", 30000},
    {"ntdiag", "C:\\SIERRA\\Half-Life\\DGAUDIT.EXE", "C:\\DGDIAG.LOG", "", 30000},
    {"dual", "C:\\SIERRA\\Half-Life\\DGDUAL.EXE", "C:\\DGDUAL.LOG", "", 70000},
    {"lifecycle", "C:\\SIERRA\\Half-Life\\DGLOOP.EXE", "C:\\DGLOOP.LOG", "", 30000},
    {"arrays", "C:\\SIERRA\\Half-Life\\DGWGL.EXE", "C:\\DGWGL.LOG", " -arrays", 30000},
    {"modes", "C:\\SIERRA\\Half-Life\\DGMODE.EXE", "C:\\DGMODE.LOG", "", 30000},
    {"win98", "C:\\SIERRA\\Half-Life\\DGWGL9.EXE", "C:\\DGWGL.LOG", "", 30000},
    {"loader", "C:\\SIERRA\\Half-Life\\DGLOAD.EXE", "C:\\DGLOAD.LOG", "", 30000},
    {"setup98", "C:\\SIERRA\\Half-Life\\DGSET9.EXE", "C:\\DGSET9.LOG", "", 30000},
    {"update98", "D:\\DGDRV.EXE", "C:\\DGDRV.LOG", "", 30000},
    {"windows", "C:\\SIERRA\\Half-Life\\DGWIN.EXE", "C:\\DGWIN.LOG", "", 30000},
    {"d3d6", "C:\\SIERRA\\Half-Life\\DGD3D6.EXE", "C:\\DGD3D6.LOG", "", 30000},
    {"d3d7", "C:\\SIERRA\\Half-Life\\DGD3D7.EXE", "C:\\DGD3D7.LOG", "", 30000},
    {"d3d8", "C:\\SIERRA\\Half-Life\\DGD3D8.EXE", "C:\\DGD3D8.LOG", "", 30000},
    {"d3d9", "C:\\SIERRA\\Half-Life\\DGD3D9.EXE", "C:\\DGD3D9.LOG", "", 30000},
    {"glide", "C:\\SIERRA\\Half-Life\\DGGLIDE.EXE", "C:\\DGGLIDE.LOG", "", 30000},
    {"utsetup", "C:\\SIERRA\\Half-Life\\DGUTSET.EXE", "C:\\DGUTSET.LOG", "", 500000},
    {"utlogs", "C:\\SIERRA\\Half-Life\\DGUTLOG.EXE", "C:\\DGUTLOG.LOG", "", 10000},
    {"ntupdate", "E:\\DGDRV.EXE", "C:\\DGDRV.LOG", "", 120000},
    {"utdsetup", "C:\\SIERRA\\Half-Life\\DGUTDS.EXE", "C:\\DGUTDS.LOG", "", 10000},
    {"utd3d", "C:\\SIERRA\\Half-Life\\DGUTD3.EXE", "C:\\DGUTD3.LOG", "", 95000},
    {"utglide", "C:\\SIERRA\\Half-Life\\DGUT.EXE", "C:\\DGUT.LOG", "", 95000}};
static const PROBE_SPEC *FindProbe(const char *name) {
    unsigned i;
    for (i = 0; i < sizeof(Probes) / sizeof(Probes[0]); ++i)
        if (Equal(name, Probes[i].name))
            return &Probes[i];
    return NULL;
}
static void StartupStatus(const char *stage, DWORD error);
/* Windows 2000+ foreground permission must pass along the exact owned launch
 * chain before the helper runs. Win98 has no such API. Permission denial is
 * evidence, never a license to attach arbitrary input queues or send keys. */
static void GrantProbeForeground(DWORD process) {
    typedef BOOL(WINAPI * ALLOW_FOREGROUND)(DWORD);
    HMODULE user = GetModuleHandleA("USER32.DLL");
    ALLOW_FOREGROUND allow =
        user ? std::bit_cast<ALLOW_FOREGROUND>(GetProcAddress(user, "AllowSetForegroundWindow"))
             : nullptr;
    if (!allow) {
        StartupStatus("PROBE_FOREGROUND_UNAVAILABLE", ERROR_CALL_NOT_IMPLEMENTED);
        return;
    }
    SetLastError(ERROR_SUCCESS);
    if (allow(process))
        StartupStatus("PROBE_FOREGROUND_GRANTED_PID", process);
    else
        StartupStatus("PROBE_FOREGROUND_DENIED", GetLastError());
}

/* Fixed acknowledgement holds only the owned game alive for one host capture.
 * Restore blocking COM behavior before returning to the ordinary command loop. */
static BOOL FixedAcknowledged(const char *kind, const char *id) {
    COMMTIMEOUTS saved, timed;
    DWORD began = GetTickCount(), used = 0, read;
    char line[LINE_MAX], expected[LINE_MAX];
    BOOL ok = FALSE;
    char *end = Append(expected, kind);
    end = Append(end, " ");
    Append(end, id);
    if (!GetCommTimeouts(Serial, &saved))
        return FALSE;
    timed = saved;
    timed.ReadIntervalTimeout = MAXDWORD;
    timed.ReadTotalTimeoutMultiplier = 0;
    timed.ReadTotalTimeoutConstant = 50;
    if (!SetCommTimeouts(Serial, &timed))
        return FALSE;
    while (GetTickCount() - began < 5000) {
        char byte;
        if (!ReadFile(Serial, &byte, 1, &read, NULL))
            break;
        if (!read)
            continue;
        if (byte == '\n') {
            if (used && line[used - 1] == '\r')
                used--;
            line[used] = 0;
            ok = Equal(line, expected);
            break;
        }
        if (!byte || used == sizeof(line) - 1)
            break;
        line[used++] = byte;
    }
    if (!SetCommTimeouts(Serial, &saved))
        ok = FALSE;
    return ok;
}

static BOOL CaptureAcknowledged(const char *id) {
    return FixedAcknowledged("CAPTURED", id);
}

static BOOL ProgramCompatible(const char *path) {
    BYTE header[4096];
    DWORD count = 0, version = GetVersion();
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    BOOL read = ReadFile(file, header, sizeof(header), &count, NULL);
    CloseHandle(file);
    return read && DgCompatiblePe(header, count, version & 255, (version >> 8) & 255);
}

static const char *Probe(const char *id, const PROBE_SPEC *spec) {
    const char *path = spec->path, *log = spec->log;
    OutputBytes = 0;
    if (!ProgramCompatible(path))
        return "executable-platform";
    char command[256], *p = Append(command, "\"");
    p = Append(p, path);
    p = Append(p, "\"");
    Append(p, spec->arguments);
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    LOG_STATE state;
    DWORD status, exit_code = 1, began;
    const char *error = NULL;
    HANDLE phases[3], capture_done = NULL, minimized = NULL;
    DWORD phase_count = 1, phase = 0;
    BOOL foreground = Equal(spec->name, "utd3d") || Equal(spec->name, "utglide");
    BOOL timed = Equal(spec->name, "utd3d") || Equal(spec->name, "utglide") ||
                 Equal(spec->name, "lifecycle") || Equal(spec->name, "dual");
    OutputBytes = 0;
    if (!DeleteFileA(log) && GetLastError() != ERROR_FILE_NOT_FOUND &&
        GetLastError() != ERROR_PATH_NOT_FOUND)
        return "stale-log-delete";
    if (GetFileAttributesA(log) != INVALID_FILE_ATTRIBUTES)
        return "stale-log-remains";
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    memset(&process, 0, sizeof(process));
    memset(&state, 0, sizeof(state));
    if (timed) {
        if (Equal(spec->name, "dual"))
            minimized = CreateEventA(NULL, TRUE, FALSE, "DreamGPUHostMinimized");
        phases[1] = CreateEventA(NULL, TRUE, FALSE, "DreamGPUGameMeasuring");
        phases[2] = CreateEventA(NULL, TRUE, FALSE, "DreamGPUGameMeasured");
        capture_done = CreateEventA(NULL, TRUE, FALSE, "DreamGPUGameCaptureDone");
        if (!phases[1] || !phases[2] || !capture_done ||
            (Equal(spec->name, "dual") && !minimized)) {
            if (minimized)
                CloseHandle(minimized);
            if (capture_done)
                CloseHandle(capture_done);
            if (phases[1])
                CloseHandle(phases[1]);
            if (phases[2])
                CloseHandle(phases[2]);
            return "phase-event-create";
        }
        ResetEvent(phases[1]);
        ResetEvent(phases[2]);
        ResetEvent(capture_done);
        if (minimized)
            ResetEvent(minimized);
        phase_count = 2;
    }
    const char *working_directory =
        (Equal(spec->name, "sysinstall") || Equal(spec->name, "sysresume") ||
         Equal(spec->name, "sysupgrade") || Equal(spec->name, "sysrepair") ||
         Equal(spec->name, "sysrecover") || Equal(spec->name, "sysui") ||
         Equal(spec->name, "hlevidence") || Equal(spec->name, "sysrollback") ||
         Equal(spec->name, "sysremove") || Equal(spec->name, "drvbind") ||
         Equal(spec->name, "drvcheck") || Equal(spec->name, "drvrestore") ||
         Equal(spec->name, "sysgl") || Equal(spec->name, "sysglide") ||
         Equal(spec->name, "setupcheck") || Equal(spec->name, "ntloader") ||
         Equal(spec->name, "ntruntime") || Equal(spec->name, "ntrename") ||
         Equal(spec->name, "ntrestore") || Equal(spec->name, "sysddraw") ||
         Equal(spec->name, "sysddrawnative") || Equal(spec->name, "sysd3d6") ||
         Equal(spec->name, "sysd3d7") || Equal(spec->name, "sysd3d8") ||
         Equal(spec->name, "sysd3d9"))
            ? "C:\\"
            : Game;
    if (!CreateProcessA(path, command, NULL, NULL, FALSE, foreground ? CREATE_SUSPENDED : 0, NULL,
                        working_directory, &startup, &process)) {
        const char *launch_error = ProcessLaunchFailure("launch-failed");
        if (timed) {
            CloseHandle(phases[1]);
            CloseHandle(phases[2]);
            CloseHandle(capture_done);
            if (minimized)
                CloseHandle(minimized);
        }
        return launch_error;
    }
    phases[0] = process.hProcess;
    if (foreground) {
        GrantProbeForeground(process.dwProcessId);
        if (ResumeThread(process.hThread) == (DWORD)-1) {
            StartupStatus("PROBE_RESUME_FAILED", GetLastError());
            error = "probe-resume-failed";
            TerminateProcess(process.hProcess, 125);
        }
    }
    CloseHandle(process.hThread);
    if (!error && !Reply("STARTED", id, NULL, FALSE))
        error = "serial-write";
    if (!error) {
        began = GetTickCount();
        for (;;) {
            DWORD elapsed = GetTickCount() - began;
            HANDLE waiting[2];
            waiting[0] = phases[0];
            waiting[1] = timed && phase < 2 ? phases[phase + 1] : NULL;
            status = WaitForMultipleObjects(phase_count, waiting, FALSE,
                                            elapsed < spec->timeout ? spec->timeout - elapsed : 0);
            if (status == WAIT_OBJECT_0 + 1) {
                if (!Reply(phase ? "MEASURED" : "MEASURING", id, NULL, FALSE)) {
                    error = "serial-write";
                    break;
                }
                if (!phase && minimized) {
                    if (FixedAcknowledged("MINIMIZED", id))
                        SetEvent(minimized);
                    else
                        error = "minimize-ack";
                }
                if (phase) {
                    if (CaptureAcknowledged(id))
                        SetEvent(capture_done);
                    else
                        error = "capture-ack";
                    /* Even a missing ACK must let the helper finish its bounded
                     * wait and clean up its own child before we collect logs. */
                }
                if (++phase == 2)
                    phase_count = 1;
                continue;
            }
            if (status != WAIT_OBJECT_0)
                error = "probe-timeout";
            else if (!GetExitCodeProcess(process.hProcess, &exit_code) || exit_code)
                error = "probe-failed";
            break;
        }
    }
    if (timed) {
        CloseHandle(phases[1]);
        CloseHandle(phases[2]);
        CloseHandle(capture_done);
        if (minimized)
            CloseHandle(minimized);
    }
    if (error) {
        CaptureFailureProcess(&process);
        CaptureOwnedModal(process.dwProcessId);
    }
    if (!StopOwned(&process))
        error = "cleanup-failed";
    CloseHandle(process.hProcess);
    /* PollLog preserves a bounded diagnostic tail even without a timedemo
     * line. Probe completion is the owned process's exit status, never a
     * quiet log or a guessed delay. */
    PollLog(log, &state, TRUE);
    if (!error && !OutputBytes)
        error = "missing-probe-log";
    return error;
}

#include "install-command.h"
static void StartupStatus(const char *stage, DWORD error) {
    char line[128], *p = Append(line, stage);
    DWORD written;
    unsigned i;
    p = Append(p, " error=0x");
    for (i = 0; i < 8; ++i)
        *p++ = Hex[(error >> (28 - i * 4)) & 15];
    *p++ = '\r';
    *p++ = '\n';
    if (StartupLog != INVALID_HANDLE_VALUE) {
        WriteFile(StartupLog, line, (DWORD)(p - line), &written, NULL);
        FlushFileBuffers(StartupLog);
    }
}

static BOOL OpenSerial(void) {
    DCB config;
    COMMTIMEOUTS timeouts;
    Serial = OpenStartupSerial();
    if (Serial == INVALID_HANDLE_VALUE) {
        StartupStatus("CreateFileCOM1", GetLastError());
        return FALSE;
    }
    StartupStatus("CreateFileCOM1", 0);
    memset(&config, 0, sizeof(config));
    config.DCBlength = sizeof(config);
    if (!GetCommState(Serial, &config)) {
        StartupStatus("GetCommState", GetLastError());
        return FALSE;
    }
    StartupStatus("GetCommState", 0);
    config.BaudRate = CBR_115200;
    config.ByteSize = 8;
    config.Parity = NOPARITY;
    config.StopBits = ONESTOPBIT;
    config.fBinary = TRUE;
    config.fParity = FALSE;
    config.fOutxCtsFlow = FALSE;
    config.fOutxDsrFlow = FALSE;
    config.fDtrControl = DTR_CONTROL_ENABLE;
    config.fDsrSensitivity = FALSE;
    config.fTXContinueOnXoff = TRUE;
    config.fOutX = FALSE;
    config.fInX = FALSE;
    config.fErrorChar = FALSE;
    config.fNull = FALSE;
    config.fRtsControl = RTS_CONTROL_ENABLE;
    config.fAbortOnError = FALSE;
    memset(&timeouts, 0, sizeof(timeouts));
    timeouts.WriteTotalTimeoutConstant = 5000;
    if (!SetCommState(Serial, &config)) {
        StartupStatus("SetCommState", GetLastError());
        return FALSE;
    }
    StartupStatus("SetCommState", 0);
    if (!SetCommTimeouts(Serial, &timeouts)) {
        StartupStatus("SetCommTimeouts", GetLastError());
        return FALSE;
    }
    StartupStatus("SetCommTimeouts", 0);
    StartupStatus("SERIAL_READY", 0);
    return TRUE;
}
void WINAPI WinMainCRTStartup(void) {
    char line[LINE_MAX];
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    DWORD length = 0;
    FILETIME started;
    DWORD identity[4];
    unsigned i, j;
    char *instance;
    BOOL malformed = FALSE;
    GetSystemTimeAsFileTime(&started);
    identity[0] = GetCurrentProcessId();
    identity[1] = started.dwHighDateTime;
    identity[2] = started.dwLowDateTime;
    identity[3] = GetTickCount();
    instance = Append(Instance, DG_RUNNER_ID);
    *instance++ = '-';
    for (i = 0; i < 4; ++i)
        for (j = 0; j < 8; ++j)
            *instance++ = Hex[(identity[i] >> (28 - j * 4)) & 15];
    *instance = 0;
    StartupLog = CreateFileA("C:\\DGPUBEN.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    StartupStatus("START", 0);
    if (!OpenSerial()) {
        if (StartupLog != INVALID_HANDLE_VALUE)
            CloseHandle(StartupLog);
        ExitProcess(1);
    }
    if (StartupLog != INVALID_HANDLE_VALUE)
        CloseHandle(StartupLog);
    StartupLog = INVALID_HANDLE_VALUE;
    for (;;) {
        char byte;
        DWORD read;
        if (!ReadFile(Serial, &byte, 1, &read, NULL) || read != 1)
            break;
        if (byte != '\n') {
            if (!byte || length == sizeof(line) - 1)
                malformed = TRUE;
            else
                line[length++] = byte;
            continue;
        }
        if (length && line[length - 1] == '\r')
            --length;
        line[length] = 0;
        if (malformed) {
            if (!Reply("ERROR", "-", "bad-line", FALSE))
                break;
        } else {
            char *id = line, *demo = NULL, *p;
            while (*id && *id != ' ')
                ++id;
            if (*id)
                *id++ = 0;
            p = id;
            while (*p && *p != ' ')
                ++p;
            if (*p) {
                *p++ = 0;
                demo = p;
            }
            if (!Token(id)) {
                if (!Reply("ERROR", "-", "bad-id", FALSE))
                    break;
            } else if (Equal(line, "IDENTIFY") && !demo) {
                if (!Reply("IDENTITY", id, DG_RUNNER_ID, FALSE))
                    break;
            } else if (Equal(line, "INSPECT") && !demo) {
                if (!Reply("INSTANCE", id, Instance, FALSE))
                    break;
            } else if (Equal(line, "PING") && !demo) {
                if (!Reply("READY", id, NULL, FALSE))
                    break;
            } else if (Equal(line, "INSTALL") && !demo) {
                const char *error = InstallPackage(id);
                if (error && !Reply("ERROR", id, error, FALSE))
                    break;
            } else if (Equal(line, "PROBE") && demo && FindProbe(demo)) {
                const char *error = Probe(id, FindProbe(demo));
                if (!Reply(error ? "ERROR" : "RESULT", id, error, TRUE))
                    break;
            } else if (Equal(line, "RUN") && demo && Token(demo)) {
                const char *error = Run(id, demo);
                if (!Reply(error ? "ERROR" : "RESULT", id, error, TRUE))
                    break;
            } else if (!Reply("ERROR", id, "bad-command", FALSE))
                break;
        }
        length = 0;
        malformed = FALSE;
    }
    CloseHandle(Serial);
    ExitProcess(0);
}

} /* extern C */
