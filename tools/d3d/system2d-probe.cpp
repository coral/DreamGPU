// SPDX-License-Identifier: GPL-2.0-or-later
// Public DirectDraw 7 loader, exact 2D coherence, and bounded completed-blit timing.
// No Direct3D interface is requested: an ordinary unknown 2D caller must work.
#define WIN32_LEAN_AND_MEAN
#define DIRECTDRAW_VERSION 0x0700
#include <windows.h>
#include <ddraw.h>

namespace {
class Log {
    HANDLE file_ = CreateFileA("C:\\DGDD2D.LOG", GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

  public:
    ~Log() {
        if (file_ != INVALID_HANDLE_VALUE)
            CloseHandle(file_);
    }
    bool valid() const {
        return file_ != INVALID_HANDLE_VALUE;
    }
    void line(const char *s) {
        DWORD bytes;
        if (valid()) {
            WriteFile(file_, s, lstrlenA(s), &bytes, nullptr);
            WriteFile(file_, "\r\n", 2, &bytes, nullptr);
            FlushFileBuffers(file_);
        }
    }
    void value(const char *name, DWORD value) {
        char line_[128];
        DWORD n = 0;
        while (*name && n < 115)
            line_[n++] = *name++;
        line_[n++] = '=';
        for (int shift = 28; shift >= 0; shift -= 4)
            line_[n++] = "0123456789abcdef"[(value >> shift) & 15];
        line_[n] = 0;
        line(line_);
    }
    bool result(const char *stage, HRESULT result) {
        if (FAILED(result)) {
            line(stage);
            value("HRESULT", (DWORD)result);
            return false;
        }
        return true;
    }
};
template <class T> class Com {
    T *pointer_ = nullptr;

  public:
    Com() = default;
    Com(const Com &) = delete;
    ~Com() {
        reset();
    }
    void reset() {
        if (pointer_) {
            pointer_->Release();
            pointer_ = nullptr;
        }
    }
    T **put() {
        reset();
        return &pointer_;
    }
    T *get() const {
        return pointer_;
    }
    T *operator->() const {
        return pointer_;
    }
};
struct Module {
    HMODULE value = nullptr;
    ~Module() {
        if (value)
            FreeLibrary(value);
    }
};
struct Window {
    HWND value = nullptr;
    ~Window() {
        if (value)
            DestroyWindow(value);
    }
};
struct Lock {
    IDirectDrawSurface7 *surface;
    DDSURFACEDESC2 desc{};
    HRESULT status;
    bool held;
    explicit Lock(IDirectDrawSurface7 *s) : surface(s) {
        desc.dwSize = sizeof(desc);
        status = s->Lock(nullptr, &desc, DDLOCK_WAIT, nullptr);
        held = SUCCEEDED(status);
    }
    ~Lock() {
        if (held)
            surface->Unlock(nullptr);
    }
    HRESULT release() {
        if (!held)
            return status;
        held = false;
        return surface->Unlock(nullptr);
    }
};
bool append(char *buffer, DWORD capacity, const char *name) {
    DWORD a = lstrlenA(buffer), b = lstrlenA(name);
    if (a >= capacity || b >= capacity - a)
        return false;
    CopyMemory(buffer + a, name, b + 1);
    return true;
}
bool system_module(HMODULE module, const char *name) {
    char expected[MAX_PATH], actual[MAX_PATH];
    DWORD n = GetSystemDirectoryA(expected, sizeof(expected));
    if (!n || n >= sizeof(expected) || !append(expected, sizeof(expected), "\\") ||
        !append(expected, sizeof(expected), name))
        return false;
    n = GetModuleFileNameA(module, actual, sizeof(actual));
    return n && n < sizeof(actual) && !lstrcmpiA(actual, expected);
}
bool clean_directory() {
    const char *names[] = {"ddraw.dll", "winedd.dll", "wined3d.dll", "dgpugl.dll",
                           "d3d8.dll",  "d3d9.dll",   "opengl32.dll"};
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, path, sizeof(path));
    if (!n || n >= sizeof(path))
        return false;
    while (n && path[n - 1] != '\\')
        --n;
    if (!n)
        return false;
    for (auto name : names) {
        path[n] = 0;
        if (!append(path, sizeof(path), name))
            return false;
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES)
            return false;
        DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
            return false;
    }
    // The fixed controller also sets C:\ as cwd. Validate it here so neither
    // ordinary DLL-search directory can contain a private provider.
    n = GetCurrentDirectoryA(sizeof(path), path);
    return n == 3 && path[1] == ':' && path[2] == '\\';
}
DWORD pattern(DWORD x, DWORD y) {
    const DWORD colors[] = {0x00ff0000, 0x0000ff00, 0x000000ff, 0x00ffffff};
    return colors[((x / 16) + (y / 16)) & 3];
}
DWORD encode(DWORD rgb, const DDPIXELFORMAT &format) {
    DWORD out = 0;
    if (rgb & 0xff0000)
        out |= format.dwRBitMask;
    if (rgb & 0x00ff00)
        out |= format.dwGBitMask;
    if (rgb & 0x0000ff)
        out |= format.dwBBitMask;
    return out;
}
bool layout(const DDSURFACEDESC2 &d, DWORD width, DWORD height) {
    DWORD bits = d.ddpfPixelFormat.dwRGBBitCount;
    return (d.ddpfPixelFormat.dwFlags & DDPF_RGB) && d.lpSurface && d.dwWidth >= width &&
           d.dwHeight >= height && (bits == 16 || bits == 32) && d.lPitch > 0 &&
           (DWORD)d.lPitch >= d.dwWidth * (bits / 8) && d.ddpfPixelFormat.dwRBitMask &&
           d.ddpfPixelFormat.dwGBitMask && d.ddpfPixelFormat.dwBBitMask;
}
bool pixels(Log &log, IDirectDrawSurface7 *surface, DWORD width, DWORD height, bool write,
            bool patterned) {
    Lock lock(surface);
    if (!log.result("Lock", lock.status))
        return false;
    if (!layout(lock.desc, width, height)) {
        log.line("FAIL lock layout");
        return false;
    }
    DWORD unit = lock.desc.ddpfPixelFormat.dwRGBBitCount / 8;
    DWORD mask = lock.desc.ddpfPixelFormat.dwRBitMask | lock.desc.ddpfPixelFormat.dwGBitMask |
                 lock.desc.ddpfPixelFormat.dwBBitMask;
    for (DWORD y = 0; y < height; ++y)
        for (DWORD x = 0; x < width; ++x) {
            BYTE *address = static_cast<BYTE *>(lock.desc.lpSurface) +
                            (ULONG_PTR)y * lock.desc.lPitch + x * unit;
            DWORD expected = encode(patterned ? pattern(x, y) : 0, lock.desc.ddpfPixelFormat),
                  actual = 0;
            if (write)
                CopyMemory(address, &expected, unit);
            else {
                CopyMemory(&actual, address, unit);
                if ((actual & mask) != expected) {
                    log.value("pixel_x", x);
                    log.value("pixel_y", y);
                    log.value("expected", expected);
                    log.value("actual", actual);
                    log.line("FAIL exact pixels");
                    return false;
                }
            }
        }
    return log.result("Unlock", lock.release());
}
bool create_surface(Log &log, IDirectDraw7 *dd, Com<IDirectDrawSurface7> &out, DWORD width,
                    DWORD height) {
    DDSURFACEDESC2 desc{};
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
    desc.dwWidth = width;
    desc.dwHeight = height;
    desc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    desc.ddpfPixelFormat.dwFlags = DDPF_RGB;
    desc.ddpfPixelFormat.dwRGBBitCount = 32;
    desc.ddpfPixelFormat.dwRBitMask = 0xff0000;
    desc.ddpfPixelFormat.dwGBitMask = 0xff00;
    desc.ddpfPixelFormat.dwBBitMask = 0xff;
    return log.result("CreateSurface offscreen RGB32",
                      dd->CreateSurface(&desc, out.put(), nullptr));
}
bool exercise(Log &log, IDirectDraw7 *dd, HWND window, bool timed) {
    Com<IDirectDrawSurface7> source, destination, primary;
    Com<IDirectDrawClipper> clipper;
    if (!log.result("SetCooperativeLevel NORMAL", dd->SetCooperativeLevel(window, DDSCL_NORMAL)))
        return false;
    if (!create_surface(log, dd, source, 128, 128) ||
        !create_surface(log, dd, destination, 128, 128))
        return false;
    if (!pixels(log, source.get(), 128, 128, true, true))
        return false;
    DDBLTFX fill{};
    fill.dwSize = sizeof(fill);
    fill.dwFillColor = 0;
    if (!log.result("Blt black", destination->Blt(nullptr, nullptr, nullptr,
                                                  DDBLT_COLORFILL | DDBLT_WAIT, &fill)) ||
        !pixels(log, destination.get(), 128, 128, false, false))
        return false;
    if (!log.result("Blt copy",
                    destination->Blt(nullptr, source.get(), nullptr, DDBLT_WAIT, nullptr)) ||
        !pixels(log, destination.get(), 128, 128, false, true))
        return false;
    // Color-key copy must retain existing pixels wherever the source is red.
    DDCOLORKEY key{0xff0000, 0xff0000};
    if (!log.result("SetColorKey", source->SetColorKey(DDCKEY_SRCBLT, &key)) ||
        !log.result("Blt clear", destination->Blt(nullptr, nullptr, nullptr,
                                                  DDBLT_COLORFILL | DDBLT_WAIT, &fill)) ||
        !log.result("Blt source key", destination->Blt(nullptr, source.get(), nullptr,
                                                       DDBLT_KEYSRC | DDBLT_WAIT, nullptr)))
        return false;
    {
        Lock lock(destination.get());
        if (!log.result("Lock keyed", lock.status) || !layout(lock.desc, 128, 128) ||
            lock.desc.ddpfPixelFormat.dwRGBBitCount != 32)
            return false;
        for (DWORD y = 0; y < 128; ++y)
            for (DWORD x = 0; x < 128; ++x) {
                DWORD value;
                CopyMemory(&value,
                           static_cast<BYTE *>(lock.desc.lpSurface) +
                               (ULONG_PTR)y * lock.desc.lPitch + x * 4,
                           4);
                DWORD expected = pattern(x, y);
                if (expected == 0xff0000)
                    expected = 0;
                if ((value & 0xffffff) != expected) {
                    log.line("FAIL source-key pixels");
                    return false;
                }
            }
        if (!log.result("Unlock keyed", lock.release()))
            return false;
    }
    if (!log.result("Blt restore",
                    destination->Blt(nullptr, source.get(), nullptr, DDBLT_WAIT, nullptr)))
        return false;
    if (timed) {
        LARGE_INTEGER frequency{}, begin{}, end{};
        if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
            return false;
        QueryPerformanceCounter(&begin);
        const DWORD deadline = GetTickCount();
        for (DWORD i = 0; i < 512; ++i) {
            if (GetTickCount() - deadline > 15000) {
                log.line("FAIL bounded blit deadline");
                return false;
            }
            if (!log.result("timed clear", destination->Blt(nullptr, nullptr, nullptr,
                                                            DDBLT_COLORFILL | DDBLT_WAIT, &fill)) ||
                !log.result("timed copy",
                            destination->Blt(nullptr, source.get(), nullptr, DDBLT_WAIT, nullptr)))
                return false;
            if ((i & 31) == 31) {
                MSG message;
                unsigned n = 0;
                while (n++ < 32 && PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&message);
                    DispatchMessageA(&message);
                }
            }
        }
        // Lock/readback completes the final operation; timing is not just the
        // duration of asynchronous command submission.
        if (!pixels(log, destination.get(), 128, 128, false, true))
            return false;
        QueryPerformanceCounter(&end);
        ULONGLONG ticks = (ULONGLONG)(end.QuadPart - begin.QuadPart);
        log.value("completed_blits", 1024);
        log.value("completed_pixels", 1024 * 128 * 128);
        log.value("elapsed_us", (DWORD)(ticks * 1000000 / (ULONGLONG)frequency.QuadPart));
    }
    DDSURFACEDESC2 front{};
    front.dwSize = sizeof(front);
    front.dwFlags = DDSD_CAPS;
    front.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    if (!log.result("Create primary", dd->CreateSurface(&front, primary.put(), nullptr)) ||
        !log.result("Create clipper", dd->CreateClipper(0, clipper.put(), nullptr)) ||
        !log.result("Clipper HWND", clipper->SetHWnd(0, window)) ||
        !log.result("SetClipper", primary->SetClipper(clipper.get())))
        return false;
    DDSURFACEDESC2 actual_front{};
    actual_front.dwSize = sizeof(actual_front);
    if (!log.result("Primary descriptor", primary->GetSurfaceDesc(&actual_front)))
        return false;
    log.value("primary_bpp", actual_front.ddpfPixelFormat.dwRGBBitCount);
    log.value("primary_caps", actual_front.ddsCaps.dwCaps);
    POINT origin{0, 0};
    if (!ClientToScreen(window, &origin))
        return false;
    RECT rectangle{origin.x, origin.y, origin.x + 128, origin.y + 128};
    if (!log.result("Blt window primary",
                    primary->Blt(&rectangle, destination.get(), nullptr, DDBLT_WAIT, nullptr)))
        return false;
    // Desktop-visible copy is checked through a DD primary->offscreen roundtrip;
    // no screenshot classifier or menu interaction is part of this oracle.
    if (!log.result("Blt primary readback",
                    destination->Blt(nullptr, primary.get(), &rectangle, DDBLT_WAIT, nullptr)) ||
        !pixels(log, destination.get(), 128, 128, false, true))
        return false;
    return true;
}
LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    return DefWindowProcA(window, message, wparam, lparam);
}
int run(HINSTANCE instance, bool native) {
    Log log;
    if (!log.valid())
        return 2;
    if (!clean_directory()) {
        log.line("FAIL private DLL neighbor or non-root cwd");
        return 3;
    }
    Module module;
    module.value = LoadLibraryA("ddraw.dll");
    if (!module.value || !system_module(module.value, "ddraw.dll")) {
        log.line("FAIL system ddraw loader");
        return 4;
    }
    using Factory = HRESULT(WINAPI *)(GUID *, void **, REFIID, IUnknown *);
    FARPROC address = GetProcAddress(module.value, "DirectDrawCreateEx");
    Factory factory = nullptr;
    static_assert(sizeof(address) == sizeof(factory));
    CopyMemory(&factory, &address, sizeof(factory));
    if (!factory) {
        log.line("FAIL DirectDrawCreateEx export");
        return 5;
    }
    // Public DirectDraw7 IID from the Microsoft SDK; no custom guest interface.
    const GUID iid = {0x15e65ec0, 0x3b9c, 0x11d2, {0xb9, 0x2f, 0x00, 0x60, 0x97, 0x97, 0xea, 0x5b}};
    WNDCLASSA wc{};
    wc.hInstance = instance;
    wc.lpfnWndProc = procedure;
    wc.lpszClassName = "DreamGPU.SystemDD2D";
    if (!RegisterClassA(&wc)) {
        log.line("FAIL window class");
        return 6;
    }
    Window window;
    window.value =
        CreateWindowExA(0, wc.lpszClassName, "DreamGPU public DirectDraw 2D", WS_OVERLAPPEDWINDOW,
                        24, 24, 320, 240, nullptr, nullptr, instance, nullptr);
    if (!window.value)
        return 7;
    ShowWindow(window.value, SW_SHOWNORMAL);
    UpdateWindow(window.value);
    for (unsigned pass = 0; pass < 2; ++pass) {
        Com<IDirectDraw7> dd;
        if (!log.result("DirectDrawCreateEx",
                        factory(nullptr, reinterpret_cast<void **>(dd.put()), iid, nullptr)))
            return 8;
        HMODULE provider = native ? module.value : GetModuleHandleA("winedd.dll");
        if (!provider || !system_module(provider, native ? "ddraw.dll" : "winedd.dll")) {
            log.line("FAIL native fallback or non-system Wine provider");
            return 9;
        }
        // A loaded Wine module alone is insufficient: the returned object's
        // actual QueryInterface implementation must belong to that provider.
        MEMORY_BASIC_INFORMATION memory{};
        void *method = (*reinterpret_cast<void ***>(dd.get()))[0];
        if (VirtualQuery(method, &memory, sizeof(memory)) != sizeof(memory) ||
            memory.AllocationBase != provider) {
            log.line("FAIL DirectDraw object does not belong to expected provider");
            return 9;
        }
        DDCAPS caps{};
        caps.dwSize = sizeof(caps);
        if (!log.result("GetCaps", dd->GetCaps(&caps, nullptr)))
            return 10;
        log.value("caps", caps.dwCaps);
        log.value("video_memory_total", caps.dwVidMemTotal);
        if (!exercise(log, dd.get(), window.value, pass == 0))
            return 11;
    }
    log.line(
        native
            ? "PASS automated sysddrawnative: native public DDRAW baseline, exact pixels and "
              "completed "
              "blits"
            : "PASS automated sysddraw: system Wine DirectDraw 2D, exact copy/key/primary pixels, "
              "two object lifecycles, 1024 completed blits");
    return 0;
}
} // namespace
extern "C" void WINAPI WinMainCRTStartup() {
    const char *arguments = GetCommandLineA();
    if (*arguments == '"') {
        ++arguments;
        while (*arguments && *arguments != '"')
            ++arguments;
        if (*arguments == '"')
            ++arguments;
    } else
        while (*arguments && *arguments != ' ' && *arguments != '\t')
            ++arguments;
    while (*arguments == ' ' || *arguments == '\t')
        ++arguments;
    if (*arguments && lstrcmpA(arguments, "--native"))
        ExitProcess(64);
    ExitProcess(run(GetModuleHandleA(nullptr), !lstrcmpA(arguments, "--native")));
}
