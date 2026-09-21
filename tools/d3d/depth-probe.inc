/* SPDX-License-Identifier: GPL-2.0-or-later
 * Public DirectDraw depth lifecycle, separate from installer smoke coverage.
 * Clear -> Lock -> verify -> CPU edit -> partial clear -> Lock -> verify.
 * The second clear must upload CPU depth before preserving its untouched half.
 */
static BOOL LegacyDepthProbe(IDirectDrawSurface *depth, IDirect3DViewport *viewport) {
    D3DRECT full = {0, 0, 32, 32}, partial = {0, 0, 16, 32};
    DDSURFACEDESC locked = {};
    if (!HR(IDirect3DViewport_Clear(viewport, 1, &full, D3DCLEAR_ZBUFFER),
            "FAIL depth initial clear"))
        return FALSE;
    for (UINT pass = 0; pass < 2; ++pass) {
        locked = {};
        locked.dwSize = sizeof(locked);
        if (!HR(IDirectDrawSurface_Lock(depth, NULL, &locked,
                DDLOCK_WAIT | (pass ? DDLOCK_READONLY : 0), NULL), "FAIL depth Lock"))
            return FALSE;
        if (Check(locked.lpSurface && locked.lPitch >= 64 &&
                  locked.ddpfPixelFormat.dwZBufferBitDepth == 16,
                  "FAIL depth D16 layout", locked.lPitch)) {
            for (UINT y = 0; y < 32 && !failed; ++y) {
                WORD *row = reinterpret_cast<WORD *>(static_cast<BYTE *>(locked.lpSurface) +
                                                    y * locked.lPitch);
                for (UINT x = 0; x < 32; ++x) {
                    const WORD expected = pass && x >= 16 ? 0 : 0xffff;
                    if (!Check(row[x] == expected, "FAIL depth exact D16 pixel", row[x])) {
                        Number("depth pass", pass);
                        Number("depth x", x);
                        Number("depth y", y);
                        break;
                    }
                    if (!pass) row[x] = 0;
                }
            }
        }
        if (!HR(IDirectDrawSurface_Unlock(depth, NULL), "FAIL depth Unlock") || failed)
            return FALSE;
        if (!pass && !HR(IDirect3DViewport_Clear(viewport, 1, &partial, D3DCLEAR_ZBUFFER),
                          "FAIL depth partial clear after CPU edit"))
            return FALSE;
    }
    // Restore the conventional far clear for subsequent rendering probes.
    if (!HR(IDirect3DViewport_Clear(viewport, 1, &full, D3DCLEAR_ZBUFFER),
            "FAIL depth final clear"))
        return FALSE;
    Log("PASS depth lifecycle: 2048 exact D16 pixels, CPU writeback and partial-clear preservation");
    return TRUE;
}
