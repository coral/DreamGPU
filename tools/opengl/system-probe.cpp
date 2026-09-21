/* SPDX-License-Identifier: GPL-2.0-or-later
 * Basic system-loader/pixel oracle. Run from a directory without private API
 * providers. Uses Microsoft's opengl32 and GDI pixel-format/swap entry points.
 * A pass proves this bounded operation, not complete OpenGL conformance.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

#include "../common/provider-clean.h"

namespace {
#ifdef DG_GL_BORDER_PROBE
constexpr const char *probe_log = "C:\\DGBORDER.LOG";
#else
constexpr const char *probe_log = "C:\\DGSYSGL.LOG";
#endif
class Log {
    HANDLE file_ = CreateFileA(probe_log, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

  public:
    Log() = default;
    Log(const Log &) = delete;
    ~Log() {
        if (file_ != INVALID_HANDLE_VALUE)
            CloseHandle(file_);
    }
    bool valid() const {
        return file_ != INVALID_HANDLE_VALUE;
    }
    void line(const char *text) {
        DWORD written;
        if (valid()) {
            WriteFile(file_, text, lstrlenA(text), &written, nullptr);
            WriteFile(file_, "\r\n", 2, &written, nullptr);
            FlushFileBuffers(file_);
        }
    }
    void value(const char *name, DWORD number) {
        char text[96];
        unsigned used = 0;
        while (*name && used < 84)
            text[used++] = *name++;
        text[used++] = ' ';
        for (int shift = 28; shift >= 0; shift -= 4)
            text[used++] = "0123456789abcdef"[(number >> shift) & 15];
        text[used] = 0;
        line(text);
    }
    void diagnostic(const char *path) {
        DWORD saved = GetLastError();
        line(path);
        HANDLE input = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (input == INVALID_HANDLE_VALUE) {
            value("DIAGNOSTIC unavailable", GetLastError());
        } else {
            char bytes[256];
            DWORD remaining = 4096;
            while (remaining) {
                DWORD count = remaining < sizeof(bytes) ? remaining : sizeof(bytes);
                DWORD read = 0, written = 0;
                if (!ReadFile(input, bytes, count, &read, nullptr) || !read)
                    break;
                WriteFile(file_, bytes, read, &written, nullptr);
                remaining -= read;
            }
            CloseHandle(input);
            line("END_DIAGNOSTIC");
        }
        SetLastError(saved);
    }
};

bool append(char *path, DWORD capacity, const char *name) {
    DWORD a = lstrlenA(path), b = lstrlenA(name);
    if (a >= capacity || b >= capacity - a)
        return false;
    for (DWORD i = 0; i <= b; ++i)
        path[a + i] = name[i];
    return true;
}

bool clean_application_directory() {
    return provider_clean::launch();
}

bool system_module(HMODULE module, const char *name, Log &log) {
    char actual[MAX_PATH], expected[MAX_PATH];
    DWORD a = GetModuleFileNameA(module, actual, sizeof(actual));
    DWORD b = GetSystemDirectoryA(expected, sizeof(expected));
    if (!a || a >= sizeof(actual) || !b || b >= sizeof(expected) ||
        !append(expected, sizeof(expected), "\\") || !append(expected, sizeof(expected), name))
        return false;
    log.line(actual);
    return lstrcmpiA(actual, expected) == 0;
}

template <typename T> T entry(HMODULE module, const char *name) {
    FARPROC raw = GetProcAddress(module, name);
    T result;
    static_assert(sizeof(result) == sizeof(raw));
    auto *to = reinterpret_cast<unsigned char *>(&result);
    auto *from = reinterpret_cast<const unsigned char *>(&raw);
    for (unsigned i = 0; i < sizeof(result); ++i)
        to[i] = from[i];
    return result;
}

class Module {
  public:
    HMODULE value = LoadLibraryA("opengl32.dll");
    Module() = default;
    Module(const Module &) = delete;
    ~Module() {
        if (value)
            FreeLibrary(value);
    }
};

class Window {
    HINSTANCE instance_ = GetModuleHandleA(nullptr);
    ATOM registered_ = 0;

  public:
    HWND value = nullptr;
    Window() {
        WNDCLASSA cls = {};
        cls.style = CS_OWNDC;
        cls.lpfnWndProc = DefWindowProcA;
        cls.hInstance = instance_;
        cls.lpszClassName = "DreamGPUSystemGLProbe";
        registered_ = RegisterClassA(&cls);
        if (registered_)
            value = CreateWindowExA(0, cls.lpszClassName, "DreamGPU system OpenGL probe",
                                    WS_POPUP | WS_VISIBLE, 37, 53, 64, 64, nullptr, nullptr,
                                    instance_, nullptr);
    }
    Window(const Window &) = delete;
    ~Window() {
        if (value)
            DestroyWindow(value);
        if (registered_)
            UnregisterClassA("DreamGPUSystemGLProbe", instance_);
    }
};

class DC {
    HWND window_;

  public:
    HDC value;
    explicit DC(HWND window) : window_(window), value(GetDC(window)) {}
    DC(const DC &) = delete;
    ~DC() {
        if (value)
            ReleaseDC(window_, value);
    }
};

using MakeCurrent = BOOL(WINAPI *)(HDC, HGLRC);
using DeleteContext = BOOL(WINAPI *)(HGLRC);
class Context {
    MakeCurrent bind_;
    DeleteContext destroy_;

  public:
    HGLRC value;
    Context(HGLRC context, MakeCurrent bind, DeleteContext destroy)
        : bind_(bind), destroy_(destroy), value(context) {}
    Context(const Context &) = delete;
    ~Context() {
        if (value) {
            bind_(nullptr, nullptr);
            destroy_(value);
        }
    }
};

#ifdef DG_GL_BORDER_PROBE
#include "border-probe.inc"
#endif

bool run(Log &log) {
    if (!clean_application_directory()) {
        log.line("FAIL private provider, mismatched cwd, or unreadable helper directory");
        return false;
    }
    Module module;
    if (!module.value || !system_module(module.value, "opengl32.dll", log)) {
        log.line("FAIL standard system OpenGL loader");
        return false;
    }
    auto create = entry<HGLRC(WINAPI *)(HDC)>(module.value, "wglCreateContext");
    auto bind = entry<MakeCurrent>(module.value, "wglMakeCurrent");
    auto destroy = entry<DeleteContext>(module.value, "wglDeleteContext");
    auto string = entry<const GLubyte *(WINAPI *)(GLenum)>(module.value, "glGetString");
    auto color =
        entry<void(WINAPI *)(GLclampf, GLclampf, GLclampf, GLclampf)>(module.value, "glClearColor");
    auto clear = entry<void(WINAPI *)(GLbitfield)>(module.value, "glClear");
    auto read = entry<void(WINAPI *)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *)>(
        module.value, "glReadPixels");
    auto error = entry<GLenum(WINAPI *)()>(module.value, "glGetError");
    if (!create || !bind || !destroy || !string || !color || !clear || !read || !error) {
        log.line("FAIL standard exports");
        return false;
    }
    Window window;
    if (!window.value) {
        log.line("FAIL window");
        return false;
    }
    DC dc(window.value);
    PIXELFORMATDESCRIPTOR requested = {};
    requested.nSize = sizeof(requested);
    requested.nVersion = 1;
    requested.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    requested.iPixelType = PFD_TYPE_RGBA;
    requested.cColorBits = 24;
    requested.cDepthBits = 24;
    requested.iLayerType = PFD_MAIN_PLANE;
    // Bounded loader discovery telemetry only. Rendering still uses the
    // standard system APIs below, never a private driver or transport path.
    DWORD query = 0x1101;
    log.value("QUERYESCSUPPORT OPENGL_GETINFO",
              ExtEscape(dc.value, QUERYESCSUPPORT, sizeof(query),
                        reinterpret_cast<const char *>(&query), 0, nullptr));
    if (GetVersion() & 0x80000000UL) {
        // Win16 OPENGL_GETINFO is two DWORDs plus ANSI[262], exactly 270 bytes.
        BYTE info[270] = {};
        DWORD version = 0, driver = 0;
        query = 0;
        log.value("OPENGL_GETINFO result",
                  ExtEscape(dc.value, 0x1101, sizeof(query), reinterpret_cast<const char *>(&query),
                            sizeof(info), reinterpret_cast<char *>(info)));
        CopyMemory(&version, info, 4);
        CopyMemory(&driver, info + 4, 4);
        log.value("OPENGL_GETINFO version", version);
        log.value("OPENGL_GETINFO driver", driver);
    } else {
        struct {
            DWORD version, driver;
            WCHAR name[256];
        } info{};
        query = 0;
        log.value("OPENGL_GETINFO result",
                  ExtEscape(dc.value, 0x1101, sizeof(query), reinterpret_cast<const char *>(&query),
                            sizeof(info), reinterpret_cast<char *>(&info)));
        log.value("OPENGL_GETINFO version", info.version);
        log.value("OPENGL_GETINFO driver", info.driver);
    }
    int format = dc.value ? ChoosePixelFormat(dc.value, &requested) : 0;
    PIXELFORMATDESCRIPTOR actual = {};
    int described = format ? DescribePixelFormat(dc.value, format, sizeof(actual), &actual) : 0;
    log.value("ChoosePixelFormat", format);
    log.value("DescribePixelFormat", described);
    log.value("PIXELFORMATDESCRIPTOR flags", actual.dwFlags);
    if (!format || !described || (actual.dwFlags & PFD_GENERIC_FORMAT) ||
        !SetPixelFormat(dc.value, format, &actual)) {
        log.line("FAIL accelerated GDI pixel format");
        return false;
    }
    SetLastError(ERROR_SUCCESS);
    HGLRC created = create(dc.value);
    DWORD create_error = GetLastError();
    Context context(created, bind, destroy);
    SetLastError(ERROR_SUCCESS);
    BOOL bound = context.value && bind(dc.value, context.value);
    DWORD bind_error = GetLastError();
    if (!bound) {
        log.value("wglCreateContext error", create_error);
        log.value("wglMakeCurrent error", bind_error);
        log.value("ICD loaded", GetModuleHandleA("dgpuicd.dll") ? 1 : 0);
        log.line("FAIL system context");
        return false;
    }
    const GLubyte *vendor = string(GL_VENDOR);
    if (!vendor || lstrcmpA(reinterpret_cast<const char *>(vendor), "DreamGPU")) {
        log.line("FAIL accelerated DreamGPU vendor");
        return false;
    }
    HMODULE icd = GetModuleHandleA("dgpuicd.dll");
    if (!icd || !system_module(icd, "dgpuicd.dll", log)) {
        log.line("FAIL registered system ICD");
        return false;
    }
    unsigned char pixels[64 * 64 * 4];
    for (unsigned pass = 0; pass < 2; ++pass) {
        color(pass ? 0.0f : 1.0f, pass ? 1.0f : 0.0f, 0.0f, 1.0f);
        clear(GL_COLOR_BUFFER_BIT);
        read(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        if (error() != GL_NO_ERROR) {
            log.line("FAIL system GL operation");
            return false;
        }
        for (unsigned i = 0; i < sizeof(pixels); i += 4)
            if (pixels[i] != (pass ? 0 : 255) || pixels[i + 1] != (pass ? 255 : 0) ||
                pixels[i + 2]) {
                log.line("FAIL exact system GL pixels");
                return false;
            }
        if (!SwapBuffers(dc.value)) {
            log.line("FAIL GDI SwapBuffers");
            return false;
        }
    }
#ifdef DG_GL_BORDER_PROBE
    if (!border_probe(module.value, log))
        return false;
    log.line("PASS automated bordergl: dimensions=1,2 sampling_cases=10 storage_roundtrips=4 provider=dgpuicd.dll");
#else
    log.line("PASS automated sysgl: system_gl_basic_pixels=8192 swaps=2 provider=dgpuicd.dll");
#endif
    return true;
}

DWORD body() {
    Log log;
    if (!log.valid())
        return 2;
    log.line("BEGIN system_gl_basic_pixel_probe");
    if (run(log))
        return 0;
    // Only fixed diagnostic receipts; no serial-supplied paths or new probe run.
    log.diagnostic("C:\\DGICDIN.LOG");
    log.diagnostic("C:\\DGICD.LOG");
    return 1;
}
} // namespace

extern "C" void WINAPI WinMainCRTStartup() {
    ExitProcess(body());
}
