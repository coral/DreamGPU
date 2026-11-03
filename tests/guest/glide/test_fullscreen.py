#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Full screen by default for glide2x.dll and glide3x.dll (shared OpenGLide core).

Compiles the actual patched sources on the host:
- GLutil.cpp GetOptions/FindConfig against real files: a missing OpenGLid.ini
  is written with InitFullScreen=1; a file from an earlier build (or another
  OpenGLide) is regenerated once, because its InitFullScreen=0 was only the old
  default; a current file's InitFullScreen=0 keeps a window; an unwritable
  directory or undeletable stale file keeps the defaults without crashing.
- platform/windows/window.cpp under a recording Win32/WGL stub: the display
  switches to the Glide resolution (CDS_FULLSCREEN, desktop depth), the window
  becomes a popup at 0,0, and the mode, style and position are restored on
  close and on every failed open. The windowed override changes no mode.
The Glide 3 front end reaches the same core open/close/shutdown paths."""
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import tempfile

from source import patched_source

ROOT = Path(__file__).resolve().parents[3]
glutil = patched_source('GLutil.cpp')
window = patched_source('platform/windows/window.cpp')
donor_header = (ROOT / 'vendor/qemu-xtra/openglide/GlOgl.h').read_text()
config = donor_header[donor_header.index('struct ConfigStruct'):]
config = config[:config.index('};') + 2]

OPTIONS = r'''
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#define OGL_MIN_FRAME_BUFFER 2
#define OGL_MAX_FRAME_BUFFER 16
#define OGL_MIN_TEXTURE_BUFFER 2
#define OGL_MAX_TEXTURE_BUFFER 32
#define INIFILE "OpenGLid.ini"
''' + config + r'''
ConfigStruct UserConfig;
const char *OpenGLideVersion = "0.09rc9";
static unsigned calls;
static void GlideMsg(const char *, ...) { ++calls; }
''' + glutil[glutil.index('static char * FindConfig'):glutil.index('void annotate_last')] + r'''
int main() {
    GetOptions();
    std::printf("fullscreen=%d texmem=%d calls=%u\n", UserConfig.InitFullScreen,
                UserConfig.TextureMemorySize, calls);
}
'''

STUB_WINDOWS = r'''
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
typedef int BOOL;
typedef unsigned long DWORD;
typedef unsigned short WORD;
typedef unsigned int UINT;
typedef long LONG;
typedef void *LPVOID;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef intptr_t LRESULT;
struct HWND__;
typedef HWND__ *HWND;
struct HDC__;
typedef HDC__ *HDC;
struct HGLRC__;
typedef HGLRC__ *HGLRC;
struct HINSTANCE__;
typedef HINSTANCE__ *HINSTANCE;
struct HMENU__;
typedef HMENU__ *HMENU;
#define WINAPI
#define TRUE 1
#define FALSE 0
typedef LRESULT (*WNDPROC)(HWND, UINT, WPARAM, LPARAM);
struct RECT { LONG left, top, right, bottom; };
struct WNDCLASS {
    UINT style; WNDPROC lpfnWndProc; int cbClsExtra, cbWndExtra; HINSTANCE hInstance;
    void *hIcon, *hCursor, *hbrBackground; const char *lpszMenuName, *lpszClassName;
};
struct PIXELFORMATDESCRIPTOR {
    WORD nSize, nVersion; DWORD dwFlags; unsigned char iPixelType, cColorBits, cAlphaBits,
        cDepthBits, cStencilBits;
};
struct DEVMODE { WORD dmSize; DWORD dmFields, dmBitsPerPel, dmPelsWidth, dmPelsHeight; };
#define ZeroMemory(p, n) std::memset((p), 0, (n))
constexpr int GWL_STYLE = -16, GWL_EXSTYLE = -20;
constexpr LONG WS_POPUP = LONG(0x80000000u), WS_VISIBLE = 0x10000000, WS_CLIPCHILDREN = 0x02000000,
               WS_CLIPSIBLINGS = 0x04000000, WS_CAPTION = 0x00C00000, WS_OVERLAPPEDWINDOW = 0x00CF0000;
constexpr DWORD WS_EX_TOPMOST = 8, WS_EX_NOACTIVATE = 0x08000000;
constexpr UINT SWP_NOZORDER = 4, SWP_NOACTIVATE = 0x10, SWP_FRAMECHANGED = 0x20;
constexpr DWORD CDS_FULLSCREEN = 4, DM_BITSPERPEL = 0x40000, DM_PELSWIDTH = 0x80000,
                DM_PELSHEIGHT = 0x100000;
constexpr LONG DISP_CHANGE_SUCCESSFUL = 0, DISP_CHANGE_FAILED = -1;
constexpr int BITSPIXEL = 12, SM_CXSCREEN = 0, SM_CYSCREEN = 1;
constexpr DWORD PFD_DOUBLEBUFFER = 1, PFD_DRAW_TO_WINDOW = 4, PFD_SUPPORT_OPENGL = 0x20;
constexpr unsigned char PFD_TYPE_RGBA = 0;
constexpr UINT CS_OWNDC = 0x20, MB_OK = 0, WM_ACTIVATE = 6, WM_ACTIVATEAPP = 0x1c,
               WM_MOUSEACTIVATE = 0x21, WM_NCLBUTTONDOWN = 0xa1;
constexpr int CW_USEDEFAULT = int(0x80000000u), SW_SHOW = 5, MA_NOACTIVATEANDEAT = 4;
constexpr unsigned GL_FRAMEBUFFER_SRGB = 0x8db9;

namespace fake {
struct Window { LONG style; RECT rect; bool alive; };
struct Mode { DWORD w, h, bpp; };
inline std::vector<std::string> log;
inline std::map<HWND, Window> windows;
inline std::vector<Mode> modes;
inline Mode desktop, registry;
inline bool fail_create, fail_change;
inline void note(const std::string &s) { log.push_back(s); }
inline std::string num(long v) { return std::to_string(v); }
}
inline HWND GetActiveWindow() { return nullptr; }
inline HINSTANCE GetModuleHandle(const char *) { return nullptr; }
inline BOOL RegisterClass(const WNDCLASS *) { return TRUE; }
inline HWND CreateWindowEx(DWORD, const char *, const char *, LONG, int, int, int, int, HWND, HMENU,
                           HINSTANCE, void *) { return nullptr; }
inline int ShowCursor(BOOL) { return 0; }
inline BOOL ShowWindow(HWND, int) { return TRUE; }
inline LRESULT DefWindowProc(HWND, UINT, WPARAM, LPARAM) { return 0; }
inline int MessageBox(HWND, const char *, const char *, UINT) { return 0; }
inline DWORD GetLastError() { return 0; }
inline BOOL IsWindow(HWND h) { return fake::windows.count(h) && fake::windows[h].alive; }
inline LONG GetWindowLong(HWND h, int index) {
    return index == GWL_STYLE && IsWindow(h) ? fake::windows[h].style : 0;
}
inline LONG SetWindowLong(HWND h, int index, LONG value) {
    fake::note("SetWindowLong style=" + fake::num((unsigned long)(DWORD)value));
    if (!IsWindow(h) || index != GWL_STYLE)
        return 0;
    const LONG old = fake::windows[h].style;
    fake::windows[h].style = value;
    return old;
}
inline BOOL GetWindowRect(HWND h, RECT *r) {
    if (!IsWindow(h)) return FALSE;
    *r = fake::windows[h].rect;
    return TRUE;
}
inline BOOL GetClientRect(HWND h, RECT *r) {
    if (!IsWindow(h)) return FALSE;
    const RECT w = fake::windows[h].rect;
    *r = {0, 0, w.right - w.left - 16, w.bottom - w.top - 39};
    return TRUE;
}
inline BOOL AdjustWindowRectEx(RECT *r, LONG, BOOL, DWORD) {
    r->left -= 8; r->top -= 31; r->right += 8; r->bottom += 8;
    return TRUE;
}
inline HMENU GetMenu(HWND) { return nullptr; }
inline BOOL MoveWindow(HWND h, int x, int y, int w, int hgt, BOOL) {
    fake::note("MoveWindow " + fake::num(x) + "," + fake::num(y) + " " + fake::num(w) + "x" + fake::num(hgt));
    if (IsWindow(h)) fake::windows[h].rect = {x, y, x + w, y + hgt};
    return TRUE;
}
inline BOOL SetWindowPos(HWND h, HWND, int x, int y, int w, int hgt, UINT flags) {
    fake::note("SetWindowPos " + fake::num(x) + "," + fake::num(y) + " " + fake::num(w) + "x" +
               fake::num(hgt) + " flags=" + fake::num(flags));
    if (!IsWindow(h)) return FALSE;
    fake::windows[h].rect = {x, y, x + w, y + hgt};
    return TRUE;
}
inline HWND GetDesktopWindow() { return nullptr; }
inline HDC GetDC(HWND h) {
    // A stale window handle has no DC; NULL is the screen.
    return !h ? reinterpret_cast<HDC>(1) : IsWindow(h) ? reinterpret_cast<HDC>(2) : nullptr;
}
inline int ReleaseDC(HWND, HDC) { return 1; }
inline int GetDeviceCaps(HDC dc, int index) {
    return dc && index == BITSPIXEL ? int(fake::desktop.bpp) : 0;
}
inline int GetSystemMetrics(int index) {
    return index == SM_CXSCREEN ? int(fake::desktop.w) : index == SM_CYSCREEN ? int(fake::desktop.h) : 0;
}
inline BOOL EnumDisplaySettings(const char *, DWORD i, DEVMODE *m) {
    if (i >= fake::modes.size()) return FALSE;
    m->dmPelsWidth = fake::modes[i].w;
    m->dmPelsHeight = fake::modes[i].h;
    m->dmBitsPerPel = fake::modes[i].bpp;
    return TRUE;
}
inline LONG ChangeDisplaySettings(DEVMODE *m, DWORD flags) {
    if (!m) {
        fake::note("ChangeDisplaySettings restore");
        fake::desktop = fake::registry;
        return DISP_CHANGE_SUCCESSFUL;
    }
    fake::note("ChangeDisplaySettings " + fake::num(m->dmPelsWidth) + "x" + fake::num(m->dmPelsHeight) +
               "x" + fake::num(m->dmBitsPerPel) + " fields=" + fake::num(m->dmFields) + " flags=" + fake::num(flags));
    if (fake::fail_change) return DISP_CHANGE_FAILED;
    fake::desktop = {m->dmPelsWidth, m->dmPelsHeight, m->dmBitsPerPel};
    return DISP_CHANGE_SUCCESSFUL;
}
inline int GetPixelFormat(HDC) { return 0; }
inline int ChoosePixelFormat(HDC, const PIXELFORMATDESCRIPTOR *) { return 1; }
inline BOOL SetPixelFormat(HDC, int, const PIXELFORMATDESCRIPTOR *) { return TRUE; }
inline int DescribePixelFormat(HDC, int, UINT, PIXELFORMATDESCRIPTOR *p) {
    p->cColorBits = 32; p->cDepthBits = 24;
    return 1;
}
inline HGLRC wglCreateContext(HDC) {
    fake::note("wglCreateContext");
    return fake::fail_create ? nullptr : reinterpret_cast<HGLRC>(3);
}
inline BOOL wglMakeCurrent(HDC, HGLRC) { return TRUE; }
inline BOOL wglDeleteContext(HGLRC) { fake::note("wglDeleteContext"); return TRUE; }
inline void *wglGetProcAddress(const char *) { return nullptr; }
inline BOOL SwapBuffers(HDC) { return TRUE; }
inline BOOL SetDeviceGammaRamp(HDC, void *) { return TRUE; }
inline BOOL GetDeviceGammaRamp(HDC, void *) { return TRUE; }
inline void glEnable(unsigned) {}
inline void glDisable(unsigned) {}
'''

STUB_GLOGL = r'''
#pragma once
#include <cstdint>
typedef unsigned int FxU32;
typedef unsigned short FxU16;
typedef uintptr_t FxU;
struct ConfigStruct { bool InitFullScreen, QEmu, FramebufferSRGB, PrecisionFix; float Resolution; };
struct OpenGLStruct { int WindowWidth, WindowHeight, WindowOffset; };
extern ConfigStruct UserConfig;
extern OpenGLStruct OpenGL;
void GlideMsg(const char *, ...);
void Error(const char *, ...);
'''

STUB_PLATFORM = r'''
#pragma once
bool InitialiseOpenGLWindow(FxU wnd, int x, int y, int width, int height);
void FinaliseOpenGLWindow(void);
void SetGamma(float value);
void RestoreGamma();
void SetGammaTable(void *);
void GetGammaTable(void *);
bool SetScreenMode(int &xsize, int &ysize);
void ResetScreenMode();
void SetSwapInterval(const int);
void SwapBuffers();
'''

WINDOW_TEST = r'''
#include <cassert>
#include <cstdio>
#include "GlOgl.h"
#include "windows.h"
#include "platform/window.h"
ConfigStruct UserConfig;
OpenGLStruct OpenGL;
void GlideMsg(const char *, ...) {}
void Error(const char *, ...) {}
using fake::log;
static const LONG app_style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
static HWND app() {
    static uintptr_t next = 100;
    HWND h = reinterpret_cast<HWND>(next++);
    fake::windows[h] = {app_style, {40, 40, 696, 559}, true};
    return h;
}
static void reset(bool fullscreen, unsigned w = 1024, unsigned h = 768) {
    log.clear();
    UserConfig = {};
    UserConfig.InitFullScreen = fullscreen;
    fake::desktop = fake::registry = {w, h, 16};
    fake::modes = {{640, 480, 32}, {640, 480, 16}, {800, 600, 16}, {1024, 768, 16}};
    fake::fail_create = fake::fail_change = false;
}
static bool has(const std::string &s) {
    for (auto &l : log) if (l == s) return true;
    return false;
}
static size_t at(const std::string &s) {
    for (size_t i = 0; i < log.size(); ++i) if (log[i] == s) return i;
    return log.size();
}
static const std::string popup = "SetWindowLong style=" + std::to_string((unsigned long)(DWORD)(WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS));
static const std::string original = "SetWindowLong style=" + std::to_string((unsigned long)(DWORD)app_style);
static const std::string mode640 = "ChangeDisplaySettings 640x480x16 fields=" + std::to_string(DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL) + " flags=4";
static const std::string at_origin = "SetWindowPos 0,0 640x480 flags=" + std::to_string(SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
static const std::string put_back = "SetWindowPos 40,40 656x519 flags=" + std::to_string(SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
int main() {
    // Default: the display takes the Glide mode at the desktop depth (never
    // the 32-bit 640x480 listed first) and the window is a popup at 0,0.
    reset(true);
    HWND h = app();
    assert(InitialiseOpenGLWindow(FxU(h), 0, 0, 640, 480));
    assert(has(mode640) && has(popup) && has(at_origin));
    assert(at(mode640) < at(at_origin) && at(at_origin) < at("wglCreateContext"));
    assert(fake::desktop.w == 640 && fake::desktop.h == 480 && fake::windows[h].style != app_style);
    log.clear();
    FinaliseOpenGLWindow();
    assert(at("wglDeleteContext") < at("ChangeDisplaySettings restore"));
    assert(at("ChangeDisplaySettings restore") < at(original) && has(put_back));
    assert(fake::desktop.w == 1024 && fake::windows[h].style == app_style);
    assert(fake::windows[h].rect.left == 40 && fake::windows[h].rect.bottom == 559);
    log.clear();
    FinaliseOpenGLWindow(); // a second close changes nothing
    assert(!has("ChangeDisplaySettings restore") && !has(original));

    // Reopen after the previous window was destroyed: the desktop depth comes
    // from the screen DC, not the stale handle, so the mode still switches.
    fake::windows[h].alive = false;
    reset(true);
    HWND again = app();
    assert(InitialiseOpenGLWindow(FxU(again), 0, 0, 640, 480) && has(mode640));
    fake::windows[again].alive = false; // destroyed before close
    log.clear();
    FinaliseOpenGLWindow();
    assert(has("ChangeDisplaySettings restore") && log.back() != original);
    assert(fake::desktop.w == 1024);

    // A failed open after the mode change restores mode, style and position.
    reset(true);
    h = app();
    fake::fail_create = true;
    assert(!InitialiseOpenGLWindow(FxU(h), 0, 0, 640, 480));
    assert(has(mode640) && has("ChangeDisplaySettings restore") && has(original) && has(put_back));
    assert(fake::desktop.w == 1024 && fake::windows[h].style == app_style);

    // Already at the Glide mode: no change and nothing to restore.
    reset(true, 640, 480);
    h = app();
    assert(InitialiseOpenGLWindow(FxU(h), 0, 0, 640, 480));
    assert(!has(mode640) && has(popup) && has(at_origin));
    log.clear();
    FinaliseOpenGLWindow();
    assert(!has("ChangeDisplaySettings restore") && has(original));

    // No such mode at the desktop depth, or a refused change: a popup at 0,0
    // on the unchanged desktop, and no restore of a mode never set.
    for (bool refused : {false, true}) {
        reset(true);
        if (refused) fake::fail_change = true;
        else fake::modes = {{640, 480, 32}};
        h = app();
        assert(InitialiseOpenGLWindow(FxU(h), 0, 0, 640, 480) && has(popup));
        assert(fake::desktop.w == 1024);
        log.clear();
        FinaliseOpenGLWindow();
        assert(!has("ChangeDisplaySettings restore") && has(original));
    }

    // InitFullScreen=0 keeps a window: no mode change, no style change.
    reset(false);
    h = app();
    assert(InitialiseOpenGLWindow(FxU(h), 0, 0, 640, 480));
    for (auto &l : log) assert(l.rfind("ChangeDisplaySettings", 0) != 0 && l.rfind("SetWindowLong", 0) != 0);
    assert(has("MoveWindow 40,40 656x519"));
    log.clear();
    FinaliseOpenGLWindow();
    assert(!has("ChangeDisplaySettings restore") && !has(original) && fake::desktop.w == 1024);
    std::puts("PASS Glide full screen window: mode at desktop depth, popup at 0,0, restored on close "
              "and failed open; windowed override changes no mode");
}
'''


def run_options(compiler, binary, directory):
    result = subprocess.run([str(binary)], cwd=directory, check=True, capture_output=True, text=True,
                            timeout=60)
    return dict(item.split('=') for item in result.stdout.split())


compiler = os.environ.get('CXX') or shutil.which('clang++')
if not compiler:
    raise SystemExit('clang++ (or CXX) is required for the actual-source sanitizer gate')
flags = ['-std=c++20', '-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']
with tempfile.TemporaryDirectory(prefix='dg-glide-fullscreen-') as temporary:
    work = Path(temporary)
    (work / 'options.cpp').write_text(OPTIONS)
    subprocess.run([compiler, *flags, '-w', str(work / 'options.cpp'), '-o', str(work / 'options')],
                   check=True)

    # No file: written with the full-screen default and DreamGPU's version.
    fresh = work / 'fresh'
    fresh.mkdir()
    values = run_options(compiler, work / 'options', fresh)
    assert values['fullscreen'] == '1', values
    written = (fresh / 'OpenGLid.ini').read_text()
    assert 'Version=0.09rc9-dg2\n' in written and '\nInitFullScreen=1\n' in written
    keys = [line.split('=', 1)[0] for line in written.splitlines() if '=' in line]
    assert keys.count('InitFullScreen') == 1, keys

    # Written by an earlier build: InitFullScreen=0 was only its default.
    stale = work / 'stale'
    stale.mkdir()
    (stale / 'OpenGLid.ini').write_text('Version=0.09rc9\n\n[Options]\nInitFullScreen=0\n'
                                        'TextureMemorySize=32\n')
    values = run_options(compiler, work / 'options', stale)
    assert values['fullscreen'] == '1' and values['texmem'] == '16', values
    assert (stale / 'OpenGLid.ini').read_text() == written

    # The current version's InitFullScreen=0 is the windowed override (also
    # with CRLF line endings), and the file is left exactly as it was.
    for ending in ('\n', '\r\n'):
        windowed = work / ('windowed' + str(len(ending)))
        windowed.mkdir()
        text = written.replace('\nInitFullScreen=1\n', '\nInitFullScreen=0\n')
        text = text.replace('\nTextureMemorySize=16\n', '\nTextureMemorySize=24\n')
        (windowed / 'OpenGLid.ini').write_bytes(text.replace('\n', ending).encode())
        before = (windowed / 'OpenGLid.ini').read_bytes()
        values = run_options(compiler, work / 'options', windowed)
        assert values['fullscreen'] == '0' and values['texmem'] == '24', values
        assert (windowed / 'OpenGLid.ini').read_bytes() == before

    # Read-only directory: no file can be written, or a stale one removed.
    if os.geteuid() != 0:
        for name, content in (('readonly', None), ('undeletable', 'Version=0.09rc9\nInitFullScreen=0\n')):
            directory = work / name
            directory.mkdir()
            if content:
                (directory / 'OpenGLid.ini').write_text(content)
            directory.chmod(stat.S_IRUSR | stat.S_IXUSR)
            try:
                values = run_options(compiler, work / 'options', directory)
            finally:
                directory.chmod(stat.S_IRWXU)
            assert values['fullscreen'] == '1', (name, values)
            assert int(values['calls']) < 4, (name, values)  # no regeneration recursion

    stub = work / 'stub'
    (stub / 'platform').mkdir(parents=True)
    (stub / 'GL').mkdir()
    (stub / 'windows.h').write_text(STUB_WINDOWS)
    (stub / 'GlOgl.h').write_text(STUB_GLOGL)
    (stub / 'platform/window.h').write_text(STUB_PLATFORM)
    (stub / 'io.h').write_text('#pragma once\n')
    (stub / 'GL/glext.h').write_text('#pragma once\n')
    (stub / 'GL/wgl.h').write_text('#pragma once\n')
    (work / 'window.cpp').write_text(window)
    (work / 'window-test.cpp').write_text(WINDOW_TEST)
    subprocess.run([compiler, *flags, '-w', '-DWIN32', '-I', str(stub), str(work / 'window.cpp'),
                    str(work / 'window-test.cpp'), '-o', str(work / 'window-test')], check=True)
    subprocess.run([str(work / 'window-test')], check=True)

# Glide 3 opens, closes and shuts down through the same core paths, and the
# provider unload closes an open context (restoring the display).
glide3 = (ROOT / 'guest/glide/glide3.cpp').read_text()
assert re.search(r'grSstWinOpen\(hWnd, screen_resolution, refresh_rate, color_format, '
                 r'origin_location,\s+nColBuffers, nAuxBuffers\)', glide3)
close = glide3[glide3.index('static FxBool grSstWinClose_body'):]
assert 'grSstWinClose();' in close[:close.index('\n}\n')]
shutdown = glide3[glide3.index('static void grGlideShutdown_body'):]
assert 'grGlideShutdown();' in shutdown[:shutdown.index('\n}\n')]
library = (ROOT / 'vendor/qemu-xtra/openglide/platform/windows/library.cpp').read_text()
detach = library[library.index('case DLL_PROCESS_DETACH:'):]
assert 'grGlideShutdown( );' in detach[:detach.index('break;')]
sst = patched_source('grguSstGlide.cpp')
shutdown_core = sst[sst.index('grGlideShutdown( void )'):]
assert 'grSstWinClose( );' in shutdown_core[:shutdown_core.index('\n}')]
close_core = sst[sst.index('grSstWinClose( void )'):]
assert 'FinaliseOpenGLWindow( );' in close_core[:close_core.index('\n}\n')]
print('PASS Glide full screen configuration: InitFullScreen=1 default, versioned regeneration of '
      'earlier files, windowed override kept, unwritable directories safe; Glide 3 uses the same paths')
