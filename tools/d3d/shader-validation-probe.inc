/* SPDX-License-Identifier: GPL-2.0-or-later
 * d3d8.dll's ValidatePixelShader and ValidateVertexShader, called the way
 * D3DX8's assembler calls them. D3DX8 is linked statically into games (Unreal
 * Tournament 2003, 3DMark2001 SE); after assembling each shader it looks both
 * up in the loaded d3d8.dll with GetProcAddress, passes NULL caps and
 * declaration, return_error and a NULL-initialized string when the caller wants
 * errors, fails the assembly with D3DXERR_INVALIDDATA on a failure and frees
 * the string with HeapFree(GetProcessHeap(), 0, ...). Included by probe.cpp
 * for D3D8 only and run on the module the D3D object comes from: the system
 * d3d8.dll (DreamGPU's switcher, which forwards both to Wine since 0055;
 * before, without msd3d8.dll, they reached its E_FAIL stubs) for DGSYS8,
 * wined8.dll for DGD3D8. Wine 0054 implements the runtime's contract (version
 * token, against the caps when given; a process-heap string, empty unless
 * return_error is set); the earlier stubs failed with caps or a declaration and
 * never wrote the string. Each case checks the HRESULT, whether a string was
 * written, that it is empty unless a failure's message was requested, and
 * that HeapFree accepts it, as upstream Wine's d3d8 tests check on Windows;
 * message texts are not compared (test_shader_validation.py checks Wine's).
 */
#if DG_D3D_VERSION == 8
namespace shader_validation_probe {

typedef HRESULT(WINAPI *ValidatePixel)(const DWORD *, const D3DCAPS8 *, BOOL, char **);
typedef HRESULT(WINAPI *ValidateVertex)(const DWORD *, const DWORD *, const D3DCAPS8 *, BOOL,
                                        char **);

/* What a call must leave in the string: none, an empty one, or any (a failure
 * with return_error; Wine explains it, the runtime's wording is not checked). */
enum Text { Untouched, Empty, Message };
static char *const Sentinel = (char *)(ULONG_PTR)0xcafeface;

static BOOL Expect(const char *name, HRESULT hr, HRESULT expected, BOOL wanted, char *errors,
                   Text text) {
    BOOL right = hr == expected;
    if (!wanted)
        right &= errors == Sentinel;
    else if (text == Untouched)
        right &= errors == Sentinel;
    else if (errors == Sentinel || !errors)
        right = FALSE;
    else {
        right &= text != Empty || !errors[0];
        if (!HeapFree(GetProcessHeap(), 0, errors)) {
            Log("FAIL shader validation string is not a process heap block");
            right = FALSE;
        }
    }
    if (!right) {
        Log("FAIL shader validation");
        Log(name);
        Number("shader validation hr", (DWORD)hr);
        Number("shader validation expected hr", (DWORD)expected);
        Number("shader validation string", (DWORD)(ULONG_PTR)errors);
        failed = TRUE;
    }
    return right;
}

static void Run(HMODULE runtime) {
    static const DWORD ps14[] = {D3DPS_VERSION(1, 4), 0x0009fffe, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                 0x0000ffff};
    static const DWORD vs11[] = {D3DVS_VERSION(1, 1), 0x0009fffe, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                 0x0000ffff};
    static const DWORD declaration[] = {D3DVSD_STREAM(0),
                                        D3DVSD_REG(D3DVSDE_POSITION, D3DVSDT_FLOAT3), D3DVSD_END()};
    Log("STAGE d3d8.dll shader validation (D3DX8's ValidatePixelShader/ValidateVertexShader)");
    const ValidatePixel pixel = Entry<ValidatePixel>(runtime, "ValidatePixelShader");
    const ValidateVertex vertex = Entry<ValidateVertex>(runtime, "ValidateVertexShader");
    if (!Check(pixel && vertex, "FAIL d3d8.dll shader validation exports", GetLastError()))
        return;
    DWORD ps[2] = {0, 0x0000ffff}, vs[2] = {0, 0x0000ffff};
    D3DCAPS8 caps = {};
    BOOL ok = TRUE;
    /* Each case with and without a string, with and without return_error. */
    const auto pixel_case = [&](const char *name, const DWORD *code, const D3DCAPS8 *with,
                                HRESULT expected, BOOL null_code) {
        for (int want = 0; want < 2; ++want)
            for (int message = 0; message < 2; ++message) {
                char *errors = Sentinel;
                const HRESULT hr = pixel(code, with, message, want ? &errors : NULL);
                const Text text = null_code                     ? Untouched
                                  : message && FAILED(expected) ? Message
                                                                : Empty;
                ok &= Expect(name, hr, expected, want, errors, text);
            }
    };
    const auto vertex_case = [&](const char *name, const DWORD *code, const DWORD *decl,
                                 const D3DCAPS8 *with, HRESULT expected) {
        for (int want = 0; want < 2; ++want)
            for (int message = 0; message < 2; ++message) {
                char *errors = Sentinel;
                const HRESULT hr = vertex(code, decl, with, message, want ? &errors : NULL);
                ok &= Expect(name, hr, expected, want, errors,
                             message && FAILED(expected) ? Message : Empty);
            }
    };
    /* D3DX8's exact calls first: an assembled ps.1.4 and vs.1.1. */
    pixel_case("D3DX8 ps.1.4, caps NULL", ps14, NULL, S_OK, FALSE);
    vertex_case("D3DX8 vs.1.1, declaration and caps NULL", vs11, NULL, NULL, S_OK);
    pixel_case("NULL pixel shader", NULL, NULL, E_FAIL, TRUE);
    vertex_case("NULL vertex shader", NULL, NULL, NULL, E_FAIL);
    vertex_case("vs.1.1 with a declaration", vs11, declaration, NULL, S_OK);
    for (DWORD minor = 0; minor <= 5; ++minor) {
        ps[0] = D3DPS_VERSION(1, minor);
        const HRESULT supported = minor <= 4 ? S_OK : E_FAIL;
        pixel_case("ps.1.x, caps NULL", ps, NULL, supported, FALSE);
        caps.PixelShaderVersion = D3DPS_VERSION(1, 4);
        pixel_case("ps.1.x, caps ps.1.4", ps, &caps, supported, FALSE);
        caps.PixelShaderVersion = ps[0];
        pixel_case("ps.1.x, caps equal", ps, &caps, supported, FALSE);
        caps.PixelShaderVersion = ps[0] - 1;
        pixel_case("ps.1.x, caps below the shader", ps, &caps, E_FAIL, FALSE);
        if (!ok) {
            Number("shader validation pixel shader version", ps[0]);
            return;
        }
    }
    ps[0] = D3DPS_VERSION(2, 0);
    pixel_case("ps.2.0", ps, NULL, E_FAIL, FALSE);
    ps[0] = D3DVS_VERSION(1, 1);
    pixel_case("a vertex shader token as a pixel shader", ps, NULL, E_FAIL, FALSE);
    for (DWORD minor = 0; minor <= 2; ++minor) {
        vs[0] = D3DVS_VERSION(1, minor);
        const HRESULT supported = minor <= 1 ? S_OK : E_FAIL;
        vertex_case("vs.1.x, caps NULL", vs, NULL, NULL, supported);
        caps.VertexShaderVersion = D3DVS_VERSION(1, 1);
        vertex_case("vs.1.x, caps vs.1.1", vs, NULL, &caps, supported);
        caps.VertexShaderVersion = vs[0] - 1;
        vertex_case("vs.1.x, caps below the shader", vs, NULL, &caps, E_FAIL);
        if (!ok) {
            Number("shader validation vertex shader version", vs[0]);
            return;
        }
    }
    vs[0] = D3DVS_VERSION(2, 0);
    vertex_case("vs.2.0", vs, NULL, NULL, E_FAIL);
    vs[0] = D3DPS_VERSION(1, 1);
    vertex_case("a pixel shader token as a vertex shader", vs, NULL, NULL, E_FAIL);
    if (ok)
        Log("PASS d3d8.dll shader validation: D3DX8's calls succeed; ps.1.0-1.4 and vs.1.0-1.1 "
            "against caps NULL, equal, below and above, declarations, process-heap strings "
            "freed by HeapFree, empty unless requested on failure");
}

} // namespace shader_validation_probe
#endif
