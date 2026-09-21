/* SPDX-License-Identifier: GPL-2.0-or-later
 * Capture public installed GL/WGL capability claims; no feature-completeness claim.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include "../d3d/entry.h"
#include "../d3d/system-loader.h"
#include "../d3d/capability-json.h"

static bool WriteJson(void *context, const char *bytes, unsigned count) {
    DWORD written = 0;
    return WriteFile(context, bytes, count, &written, NULL) && written == count;
}
static void ModulePath(CapabilityJson &json, HMODULE module) {
    char path[MAX_PATH];
    DWORD length = module ? GetModuleFileNameA(module, path, sizeof(path)) : 0;
    if (length && length < sizeof(path))
        json.string(path, length);
    else
        json.text("null");
}
static bool Capture(HMODULE module, HWND window, CapabilityJson &json) {
    using Create = HGLRC(WINAPI *)(HDC);
    using Bind = BOOL(WINAPI *)(HDC, HGLRC);
    using Delete = BOOL(WINAPI *)(HGLRC);
    using String = const GLubyte *(APIENTRY *)(GLenum);
    using Integer = void(APIENTRY *)(GLenum, GLint *);
    using Error = GLenum(APIENTRY *)(void);
    Create create = Entry<Create>(module, "wglCreateContext");
    Bind bind = Entry<Bind>(module, "wglMakeCurrent");
    Delete destroy = Entry<Delete>(module, "wglDeleteContext");
    String string = Entry<String>(module, "glGetString");
    Integer integer = Entry<Integer>(module, "glGetIntegerv");
    Error error = Entry<Error>(module, "glGetError");
    if (!create || !bind || !destroy || !string || !integer || !error) {
        json.text(",\"error\":\"missing_public_entrypoint\"");
        return false;
    }
    HDC dc = GetDC(window);
    if (!dc) {
        json.text(",\"error\":\"window_dc\"");
        return false;
    }
    PIXELFORMATDESCRIPTOR requested = {};
    requested.nSize = sizeof(requested);
    requested.nVersion = 1;
    requested.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    requested.iPixelType = PFD_TYPE_RGBA;
    requested.cColorBits = requested.cDepthBits = 24;
    int chosen = ChoosePixelFormat(dc, &requested);
    int count = DescribePixelFormat(dc, 1, 0, NULL);
    json.text(",\"os_version\":");
    json.hex(GetVersion());
    json.text(",\"module\":");
    ModulePath(json, module);
    json.text(",\"chosen_pixel_format\":");
    json.number(chosen);
    json.text(",\"pixel_formats\":[");
    bool valid = chosen > 0 && count > 0 && count <= 256;
    for (int i = 1; i <= count && i <= 256; ++i) {
        PIXELFORMATDESCRIPTOR pfd = {};
        if (i > 1)
            json.text(",");
        int result = DescribePixelFormat(dc, i, sizeof(pfd), &pfd);
        valid = valid && result != 0;
        // Every descriptor byte is retained; the named fields make common
        // distinctions easy to inspect without treating color depth as a cap.
        json.text("{\"index\":"); json.number(i);
        json.text(",\"describe_result\":"); json.number(result);
        json.text(",\"flags\":"); json.hex(pfd.dwFlags);
        json.text(",\"color_bits\":"); json.number(pfd.cColorBits);
        json.text(",\"depth_bits\":"); json.number(pfd.cDepthBits);
        json.text(",\"stencil_bits\":"); json.number(pfd.cStencilBits);
        json.text(",\"descriptor_bytes\":[");
        const BYTE *bytes = reinterpret_cast<const BYTE *>(&pfd);
        for (unsigned j = 0; j < sizeof(pfd); ++j) {
            if (j) json.text(",");
            json.number(bytes[j]);
        }
        json.text("]}");
    }
    json.text("],\"pixel_format_count\":"); json.number(count);
    json.text(",\"pixel_formats_complete\":"); json.text(valid ? "true" : "false");
    PIXELFORMATDESCRIPTOR selected = {};
    HGLRC context = NULL;
    bool bound = valid && DescribePixelFormat(dc, chosen, sizeof(selected), &selected) &&
                 SetPixelFormat(dc, chosen, &selected) && (context = create(dc)) && bind(dc, context);
    json.text(",\"context_created\":"); json.text(bound ? "true" : "false");
    if (bound) {
        HMODULE icd = GetModuleHandleA("dgpuicd.dll");
        HMODULE frontend = GetModuleHandleA("dgpugl.dll");
        json.text(",\"icd_module\":"); ModulePath(json, icd);
        json.text(",\"frontend_module\":"); ModulePath(json, frontend);
        // dgpuicd links the frontend translation units directly. dgpugl is a
        // separate provider for Wine/Glide and need not load in a system-WGL
        // application; if it is present, still reject an app-local substitute.
        json.text(",\"icd_embeds_frontend\":true");
        const bool installed = system_loader::module(icd, "dgpuicd.dll") &&
                               (!frontend || system_loader::module(frontend, "dgpugl.dll"));
        json.text(",\"provider_identity_verified\":"); json.text(installed ? "true" : "false");
        valid = valid && installed;
        const struct { GLenum key; const char *name; } strings[] = {
            {GL_VENDOR, "vendor"}, {GL_RENDERER, "renderer"},
            {GL_VERSION, "version"}, {GL_EXTENSIONS, "extensions"},
        };
        GLenum string_errors[4] = {};
        bool string_complete[4] = {};
        unsigned string_index = 0;
        for (const auto &item : strings) {
            json.text(","); json.string(item.name); json.text(":");
            const GLubyte *value = string(item.key);
            bool terminated = false;
            if (value) {
                unsigned length = 0;
                while (length < 16384 && value[length])
                    ++length;
                terminated = length < 16384;
                json.string(reinterpret_cast<const char *>(value), length);
            } else {
                json.text("null");
            }
            const GLenum result = error();
            string_errors[string_index] = result;
            string_complete[string_index++] = terminated;
            valid = valid && terminated && result == GL_NO_ERROR;
        }
        json.text(",\"string_queries\":[");
        for (unsigned i = 0; i < string_index; ++i) {
            if (i)
                json.text(",");
            json.text("{\"name\":"); json.string(strings[i].name);
            json.text(",\"error\":"); json.hex(string_errors[i]);
            json.text(",\"complete\":"); json.text(string_complete[i] ? "true" : "false");
            json.text("}");
        }
        json.text("]");
        const struct { GLenum key; const char *name; unsigned count; } limits[] = {
            {GL_MAX_TEXTURE_SIZE, "max_texture_size", 1},
            {GL_MAX_VIEWPORT_DIMS, "max_viewport_dims", 2},
            {GL_MAX_LIGHTS, "max_lights", 1}, {GL_MAX_CLIP_PLANES, "max_clip_planes", 1},
            {GL_MAX_MODELVIEW_STACK_DEPTH, "max_modelview_stack_depth", 1},
            {GL_MAX_PROJECTION_STACK_DEPTH, "max_projection_stack_depth", 1},
            {GL_MAX_TEXTURE_STACK_DEPTH, "max_texture_stack_depth", 1},
            {GL_MAX_ATTRIB_STACK_DEPTH, "max_attrib_stack_depth", 1},
            {GL_MAX_CLIENT_ATTRIB_STACK_DEPTH, "max_client_attrib_stack_depth", 1},
            {GL_MAX_LIST_NESTING, "max_list_nesting", 1},
            {GL_MAX_PIXEL_MAP_TABLE, "max_pixel_map_table", 1},
            {GL_AUX_BUFFERS, "aux_buffers", 1}, {GL_SUBPIXEL_BITS, "subpixel_bits", 1},
        };
        json.text(",\"limits\":[");
        bool first = true;
        for (const auto &item : limits) {
            GLint values[2] = {};
            integer(item.key, values);
            GLenum result = error();
            valid = valid && result == GL_NO_ERROR;
            if (!first)
                json.text(",");
            first = false;
            json.text("{\"name\":"); json.string(item.name);
            json.text(",\"enum\":"); json.hex(item.key);
            json.text(",\"error\":"); json.hex(result);
            json.text(",\"values\":[");
            for (unsigned i = 0; i < item.count; ++i) {
                if (i) json.text(",");
                if (values[i] < 0) json.text("-");
                json.number(values[i] < 0 ? 0u - static_cast<unsigned>(values[i]) : values[i]);
            }
            json.text("]}");
        }
        json.text("]");
        if (!bind(NULL, NULL))
            valid = false;
    }
    if (context && !destroy(context))
        valid = false;
    if (!ReleaseDC(window, dc))
        valid = false;
    return valid && bound && json.good();
}
extern "C" void WINAPI WinMainCRTStartup(void) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    HMODULE module = system_loader::load("opengl32.dll");
    WNDCLASSA cls = {};
    cls.style = CS_OWNDC;
    cls.lpfnWndProc = DefWindowProcA;
    cls.hInstance = GetModuleHandleA(NULL);
    cls.lpszClassName = "DreamGPUCapabilityGL";
    ATOM registered = RegisterClassA(&cls);
    HWND window = registered ? CreateWindowExA(0, cls.lpszClassName, "DreamGPU capabilities",
        WS_POPUP, 0, 0, 32, 32, NULL, NULL, cls.hInstance, NULL) : NULL;
    HANDLE output = CreateFileA("C:\\DGCPGL.JSON", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    bool passed = false;
    if (output != INVALID_HANDLE_VALUE) {
        CapabilityJson json(WriteJson, output);
        json.text("{\"schema\":1,\"kind\":\"dreamgpu.opengl.capabilities\","
                  "\"api\":\"opengl-wgl\",\"evidence\":\"reported_capabilities_only\","
                  "\"execution_validation\":false");
        if (!module)
            json.text(",\"error\":\"system_provider_load_or_identity\"");
        else if (!window)
            json.text(",\"error\":\"window_creation\"");
        else
            passed = Capture(module, window, json);
        json.text(",\"capture_complete\":"); json.text(passed ? "true" : "false");
        json.text(",\"complete\":"); json.text(passed ? "true" : "false");
        json.text(",\"semantic_complete\":false}\n");
        const bool saved = json.flush() && FlushFileBuffers(output);
        passed = saved && passed;
        CloseHandle(output);
    }
    if (window) DestroyWindow(window);
    if (registered) UnregisterClassA(cls.lpszClassName, cls.hInstance);
    if (module) FreeLibrary(module);
    HANDLE log = CreateFileA("C:\\DGCAPGL.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (log != INVALID_HANDLE_VALUE) {
        const char *message = passed ? "PASS GL/WGL capability capture; rendering unverified\r\n"
                                     : "FAIL GL/WGL capability capture\r\n";
        DWORD written;
        WriteFile(log, message, lstrlenA(message), &written, NULL);
        CloseHandle(log);
    }
    ExitProcess(passed ? 0 : 1);
}
