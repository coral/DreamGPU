/* SPDX-License-Identifier: GPL-2.0-or-later
 * Public installed D3D8/9 capability claims, separate from rendering acceptance.
 * Compile twice. No device/window creation, CRT, or application-local provider.
 */
#define WIN32_LEAN_AND_MEAN
#define CINTERFACE
#define COBJMACROS
#include <windows.h>
#include "entry.h"
#include "system-loader.h"
#include "capability-json.h"
#if DG_D3D_VERSION == 8
#include <d3d8.h>
using D3D = IDirect3D8;
using CaptureDeviceCaps = D3DCAPS8;
using CaptureAdapterIdentifier = D3DADAPTER_IDENTIFIER8;
#define D3D_CALL(name, ...) IDirect3D8_##name(__VA_ARGS__)
#define CAP_API "d3d8.dll"
#define CAP_PROVIDER "wined8.dll"
#define CAP_FACTORY "Direct3DCreate8"
#define CAP_JSON "C:\\DGCP8.JSON"
#define CAP_LOG "C:\\DGCP8.LOG"
#elif DG_D3D_VERSION == 9
#include <d3d9.h>
using D3D = IDirect3D9;
using CaptureDeviceCaps = D3DCAPS9;
using CaptureAdapterIdentifier = D3DADAPTER_IDENTIFIER9;
#define D3D_CALL(name, ...) IDirect3D9_##name(__VA_ARGS__)
#define CAP_API "d3d9.dll"
#define CAP_PROVIDER "wined9.dll"
#define CAP_FACTORY "Direct3DCreate9"
#define CAP_JSON "C:\\DGCP9.JSON"
#define CAP_LOG "C:\\DGCP9.LOG"
#else
#error Unsupported capture API version
#endif
#include "capability-capture.inc"

static HANDLE log_file = INVALID_HANDLE_VALUE;
static void Log(const char *text) {
    if (log_file == INVALID_HANDLE_VALUE)
        return;
    DWORD written;
    WriteFile(log_file, text, lstrlenA(text), &written, NULL);
    WriteFile(log_file, "\r\n", 2, &written, NULL);
    FlushFileBuffers(log_file);
}
static bool WriteJson(void *context, const char *bytes, unsigned count) {
    DWORD written = 0;
    return WriteFile(context, bytes, count, &written, NULL) && written == count;
}
static void ModulePath(CapabilityJson &json, const char *name) {
    char path[MAX_PATH];
    const HMODULE module = GetModuleHandleA(name);
    const DWORD length = module ? GetModuleFileNameA(module, path, sizeof(path)) : 0;
    if (length && length < sizeof(path))
        json.string(path, length);
    else
        json.text("null");
}

extern "C" void WINAPI WinMainCRTStartup(void) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    log_file = CreateFileA(CAP_LOG, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    HANDLE output = CreateFileA(CAP_JSON, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (output == INVALID_HANDLE_VALUE) {
        Log("FAIL capability JSON output open");
        if (log_file != INVALID_HANDLE_VALUE)
            CloseHandle(log_file);
        ExitProcess(2);
    }
    CapabilityJson json(WriteJson, output);
    json.text("{\"schema\":1,\"kind\":\"dreamgpu.d3d.capabilities\",\"api_version\":");
    json.number(DG_D3D_VERSION);
    json.text(",\"sdk_version\":"); json.number(D3D_SDK_VERSION);
    json.text(",\"os_version_raw\":"); json.hex(GetVersion());
    json.text(",\"device_type\":\"HAL\",\"query\":\"CheckDeviceFormat\","
              "\"evidence\":\"reported_capabilities_only\",\"pool_validation\":false,"
              "\"execution_validation\":false,\"adapter_format_scope\":\"current_display_mode\","
              "\"identity_string_encoding\":\"ANSI bytes mapped to U+0000..U+00FF\",");
    bool complete = false;
    const char *error = nullptr;
    Log("STAGE installed system provider capability capture");
    HMODULE runtime = system_loader::load(CAP_API, Log);
    D3D *d3d = nullptr;
    if (!runtime) {
        error = "system_provider_load_or_identity";
    } else {
        using Create = D3D *(WINAPI *)(UINT);
        const auto create = Entry<Create>(runtime, CAP_FACTORY);
        if (!create)
            error = "factory_export";
        else if (!(d3d = create(D3D_SDK_VERSION)))
            error = "factory_creation";
        else if (!system_loader::object(d3d->lpVtbl, CAP_PROVIDER))
            error = "provider_object_or_dependency_identity";
    }
    json.text("\"modules\":{\"api\":"); ModulePath(json, CAP_API);
    json.text(",\"provider\":"); ModulePath(json, CAP_PROVIDER);
    json.text(",\"wined3d\":"); ModulePath(json, "wined3d.dll");
    json.text(",\"opengl\":"); ModulePath(json, "dgpugl.dll");
    json.text("},");
    if (!error) {
        complete = CaptureAdapters(json, d3d);
    } else {
        json.text("\"adapter_count\":null,\"adapters\":[]");
        Log(error);
    }
    json.text(",\"error\":");
    if (error)
        json.string(error);
    else
        json.text("null");
    json.text(",\"complete\":");
    json.text(complete ? "true" : "false");
    json.text("}\n");
    const bool saved = json.flush() && FlushFileBuffers(output);
    CloseHandle(output);
    if (d3d)
        D3D_CALL(Release, d3d);
    if (runtime)
        FreeLibrary(runtime);
    const bool success = complete && saved;
    Log(success ? "PASS capability claims captured; rendering and pools remain unverified"
                : "FAIL incomplete capability capture; inspect JSON and output errors");
    if (log_file != INVALID_HANDLE_VALUE)
        CloseHandle(log_file);
    ExitProcess(success ? 0 : 1);
}
