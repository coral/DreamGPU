/* SPDX-License-Identifier: GPL-2.0-or-later
 * Separate public-system-provider capability test; not an installer smoke gate.
 */
static void IndexedImmediateProbe(void) {
    IDirect3DDevice2 *older = NULL;
    if (!HR(IDirect3DDevice3_QueryInterface(device, IID_IDirect3DDevice2, (void **)&older),
            "FAIL indexed Device2 query"))
        return;
    Log("STAGE indexed immediate Device2 and Device3 public interfaces");
    for (UINT version = 2; version <= 3 && !failed; ++version) {
        D3DTLVERTEX vertices[3] = {};
        const float x[] = {160, 80, 240}, y[] = {180, 60, 60};
        for (UINT i = 0; i < 3; ++i) {
            vertices[i].sx = x[i];
            vertices[i].sy = y[i];
            vertices[i].sz = .5f;
            vertices[i].rhw = 1;
            vertices[i].color = 0xff00ff00;
        }
        D3DRECT rect = {0, 0, 320, 240};
        if (!HR(IDirect3DViewport3_Clear2(view, 1, &rect, D3DCLEAR_TARGET, 0xff102030, 1, 0),
                "FAIL indexed target clear") ||
            !HR(IDirect3DDevice3_BeginScene(device), "FAIL indexed BeginScene"))
            break;
        HRESULT result = version == 2 ? IDirect3DDevice2_Index(older, 0)
                                      : IDirect3DDevice3_Index(device, 0);
        Check(result == D3DERR_NOTINBEGIN, "FAIL indexed Index outside Begin", result);
        result = version == 2
            ? IDirect3DDevice2_BeginIndexed(older, D3DPT_TRIANGLELIST, D3DVT_TLVERTEX, vertices, 3, 0)
            : IDirect3DDevice3_BeginIndexed(device, D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, vertices, 3, 0);
        BOOL begun = SUCCEEDED(result);
        if (begun) {
            HRESULT invalid = version == 2 ? IDirect3DDevice2_Index(older, 3)
                                           : IDirect3DDevice3_Index(device, 3);
            Check(invalid == DDERR_INVALIDPARAMS, "FAIL indexed out-of-range Index", invalid);
            invalid = version == 2
                ? IDirect3DDevice2_Begin(older, D3DPT_TRIANGLELIST, D3DVT_TLVERTEX, 0)
                : IDirect3DDevice3_Begin(device, D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, 0);
            Check(invalid == D3DERR_INBEGIN, "FAIL indexed nested Begin", invalid);
            // BeginIndexed owns its vertices; subsequent caller writes must not
            // corrupt the draw queued by valid indices after the rejected calls.
            for (UINT i = 0; i < 3; ++i)
                vertices[i].color = 0xffff0000;
        }
        const WORD order[] = {1, 2, 0};
        for (UINT i = 0; i < 3 && SUCCEEDED(result); ++i)
            result = version == 2 ? IDirect3DDevice2_Index(older, order[i])
                                  : IDirect3DDevice3_Index(device, order[i]);
        if (begun) {
            HRESULT ended = version == 2 ? IDirect3DDevice2_End(older, 0)
                                        : IDirect3DDevice3_End(device, 0);
            if (SUCCEEDED(result))
                result = ended;
        }
        if (begun) {
            HRESULT ended = version == 2 ? IDirect3DDevice2_End(older, 0)
                                        : IDirect3DDevice3_End(device, 0);
            Check(ended == D3DERR_NOTINBEGIN, "FAIL indexed repeated End", ended);
        }
        HRESULT scene = IDirect3DDevice3_EndScene(device);
        if (!HR(result, "FAIL indexed BeginIndexed/Index/End") ||
            !HR(scene, "FAIL indexed EndScene") || failed)
            break;
        DDSURFACEDESC2 mapped = {};
        mapped.dwSize = sizeof(mapped);
        if (!HR(IDirectDrawSurface4_Lock(target, NULL, &mapped, DDLOCK_READONLY | DDLOCK_WAIT, NULL),
                "FAIL indexed result Lock"))
            break;
        if (Check(mapped.lpSurface && mapped.lPitch >= 1280, "FAIL indexed layout", mapped.lPitch)) {
            for (UINT region = 0; region < 2 && !failed; ++region)
                for (UINT yy = 0; yy < 16 && !failed; ++yy)
                    for (UINT xx = 0; xx < 16; ++xx) {
                        UINT px = xx + (region ? 152 : 0), py = yy + (region ? 92 : 0);
                        DWORD actual = *(DWORD *)((BYTE *)mapped.lpSurface + py * mapped.lPitch + px * 4) & 0xffffff;
                        if (!Check(actual == (region ? 0x00ff00u : 0x102030u),
                                   "FAIL indexed exact target pixel", actual))
                            break;
                    }
        }
        HR(IDirectDrawSurface4_Unlock(target, NULL), "FAIL indexed result Unlock");
    }
    IDirect3DDevice2_Release(older);
    if (!failed)
        Log("PASS indexed immediate Device2/Device3: owned vertices, rejected calls, 1024 exact drawn and untouched pixels");
}
