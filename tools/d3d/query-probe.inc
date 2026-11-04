/* SPDX-License-Identifier: GPL-2.0-or-later
 * D3D9 occlusion and event queries through the public API (D3D8 has no
 * queries; the D3D8 build contains none of this). Included by probe.cpp after
 * rtt-probe.inc, whose depth format choice it reuses. With
 * ARB_occlusion_query and NV_fence (DG_CAP_GL_OCCLUSION_QUERY) WineD3D
 * creates D3DQUERYTYPE_OCCLUSION queries (D3DERR_NOTAVAILABLE before) and
 * backs D3DQUERYTYPE_EVENT with fences (before, GetData of an issued event
 * failed with D3DERR_INVALIDCALL). Without them (an older host) occlusion
 * queries are D3DERR_NOTAVAILABLE and the case is recorded and skipped.
 * Pretransformed XYZRHW quads with half-pixel edges, so every pixel centre is
 * clearly inside or outside whatever WineD3D's 63/128 pixel offset, over a
 * 320x240 D24S8 (or D16) depth surface with LESSEQUAL depth testing:
 * - a near quad (z 0.2) over 80x80 pixels, outside any query;
 * - visible: 40x50 pixels at z 0.5 elsewhere, exactly 2000 samples;
 * - partial: 80x80 pixels at z 0.8 half behind the near quad, exactly 3200;
 * - hidden: 60x60 pixels at z 0.8 wholly behind it, exactly 0;
 * - reused: the visible query issued again (Wine reuses its GL query) around a
 *   full-window quad at z 0.9, which passes only where nothing nearer was
 *   drawn: 76800 - 6400 - 2000 - 3200 = 65200;
 * - an event issued after all of it, polled with D3DGETDATA_FLUSH until it
 *   signals. This Wine answers a pending event S_OK with FALSE (Windows
 *   answers S_FALSE): both count as pending.
 * Every result is polled with D3DGETDATA_FLUSH for at most five seconds.
 */
#if DG_D3D_VERSION == 9
namespace query_probe {

struct Vertex {
    float x, y, z, rhw;
    DWORD color;
};
static constexpr DWORD Fvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE;
static constexpr DWORD PollMilliseconds = 5000;

/* A strip quad over pixel centres left..right-1 and top..bottom-1. */
static BOOL Quad(Device *dev, int left, int top, int right, int bottom, float z, DWORD color) {
    const float l = left - .5f, t = top - .5f, r = right - .5f, b = bottom - .5f;
    const Vertex v[4] = {
        {l, t, z, 1, color}, {r, t, z, 1, color}, {l, b, z, 1, color}, {r, b, z, 1, color}};
    return HR(DEVICE(DrawPrimitiveUP, dev, D3DPT_TRIANGLESTRIP, 2, v, sizeof(Vertex)),
              "FAIL query probe draw");
}
/* One query around one quad. */
static BOOL Counted(Device *dev, IDirect3DQuery9 *query, int left, int top, int right, int bottom,
                    float z, DWORD color) {
    return HR(IDirect3DQuery9_Issue(query, D3DISSUE_BEGIN), "FAIL occlusion query begin") &&
           Quad(dev, left, top, right, bottom, z, color) &&
           HR(IDirect3DQuery9_Issue(query, D3DISSUE_END), "FAIL occlusion query end");
}
/* The occlusion result, polled until S_OK; exactly `expected` samples. */
static BOOL Samples(IDirect3DQuery9 *query, DWORD expected, const char *name) {
    const DWORD start = GetTickCount();
    DWORD samples = 0xdeadbeef, polls = 0;
    HRESULT hr;
    while ((hr = IDirect3DQuery9_GetData(query, &samples, sizeof(samples), D3DGETDATA_FLUSH)) ==
           S_FALSE) {
        ++polls;
        if (GetTickCount() - start > PollMilliseconds) {
            Log(name);
            return Check(FALSE, "FAIL occlusion query never completed", polls);
        }
        Sleep(0);
    }
    if (!HR(hr, "FAIL occlusion query GetData")) {
        Log(name);
        return FALSE;
    }
    Number(name, samples);
    Number("pending polls", polls);
    return Check(samples == expected, "FAIL exact occlusion samples", expected);
}
/* The event's GetData until it signals. */
static BOOL Signalled(IDirect3DQuery9 *event) {
    const DWORD start = GetTickCount();
    DWORD polls = 0;
    for (;;) {
        BOOL signalled = FALSE;
        const HRESULT hr =
            IDirect3DQuery9_GetData(event, &signalled, sizeof(signalled), D3DGETDATA_FLUSH);
        if (hr != S_OK && hr != S_FALSE)
            return HR(hr, "FAIL event query GetData");
        if (hr == S_OK && signalled == TRUE)
            break;
        if (!Check(hr == S_FALSE || signalled == FALSE, "FAIL event query value", signalled))
            return FALSE;
        ++polls;
        if (GetTickCount() - start > PollMilliseconds)
            return Check(FALSE, "FAIL event query never signalled", polls);
        Sleep(0);
    }
    Number("event query pending polls", polls);
    return TRUE;
}
static BOOL State(Device *dev, Surface *backbuffer, Surface *depth) {
    D3DVIEWPORT9 viewport = {0, 0, 320, 240, 0, 1};
    return HR(DEVICE(SetRenderTarget, dev, 0, backbuffer), "FAIL query probe render target") &&
           HR(DEVICE(SetDepthStencilSurface, dev, depth), "FAIL query probe depth surface") &&
           HR(DEVICE(SetViewport, dev, &viewport), "FAIL query probe viewport") &&
           HR(DEVICE(SetVertexShader, dev, NULL), "FAIL query probe vertex shader") &&
           HR(DEVICE(SetPixelShader, dev, NULL), "FAIL query probe pixel shader") &&
           HR(DEVICE(SetFVF, dev, Fvf), "FAIL query probe FVF") &&
           HR(DEVICE(SetTexture, dev, 0, NULL), "FAIL query probe texture") &&
           HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1),
              "FAIL query probe colour operation") &&
           HR(DEVICE(SetTextureStageState, dev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE),
              "FAIL query probe colour argument") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_ZENABLE, D3DZB_TRUE), "FAIL query probe depth") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_ZWRITEENABLE, TRUE),
              "FAIL query probe depth writes") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_ZFUNC, D3DCMP_LESSEQUAL),
              "FAIL query probe depth function") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_STENCILENABLE, FALSE),
              "FAIL query probe stencil") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_ALPHABLENDENABLE, FALSE),
              "FAIL query probe blending") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_ALPHATESTENABLE, FALSE),
              "FAIL query probe alpha test") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_SCISSORTESTENABLE, FALSE),
              "FAIL query probe scissor") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_COLORWRITEENABLE, 0xf),
              "FAIL query probe colour writes") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_FOGENABLE, FALSE), "FAIL query probe fog") &&
           HR(DEVICE(SetRenderState, dev, D3DRS_CULLMODE, D3DCULL_NONE), "FAIL query probe cull");
}

static void Run(D3D *d3d, Device *dev, Surface *backbuffer) {
    IDirect3DQuery9 *visible = NULL, *partial = NULL, *hidden = NULL, *event = NULL;
    Surface *depth = NULL;
    Log("STAGE D3D9 occlusion and event queries");
    /* Support is asked with a NULL query first, as applications do. */
    const HRESULT supported = DEVICE(CreateQuery, dev, D3DQUERYTYPE_OCCLUSION, NULL);
    Number("CreateQuery(OCCLUSION, NULL)", (DWORD)supported);
    Number("CreateQuery(EVENT, NULL)", (DWORD)DEVICE(CreateQuery, dev, D3DQUERYTYPE_EVENT, NULL));
    Number("CreateQuery(TIMESTAMP, NULL)",
           (DWORD)DEVICE(CreateQuery, dev, D3DQUERYTYPE_TIMESTAMP, NULL));
    /* A host without occlusion queries (an older QEMU) makes WineD3D report
     * them unavailable, as a driver without the feature does: record and skip,
     * so activation does not depend on the host's capability set. */
    if (supported == D3DERR_NOTAVAILABLE) {
        Log("SKIP D3D9 occlusion and event queries: D3DQUERYTYPE_OCCLUSION not available");
        return;
    }
    const D3DFORMAT format = rtt_probe::DepthFormat(d3d);
    if (!HR(supported, "FAIL occlusion queries unavailable") ||
        !Check(format != D3DFMT_UNKNOWN, "FAIL no depth-stencil format for queries", 0) ||
        !HR(DEVICE(CreateDepthStencilSurface, dev, 320, 240, format, D3DMULTISAMPLE_NONE, 0, FALSE,
                   &depth, NULL),
            "FAIL query probe depth surface creation") ||
        !HR(DEVICE(CreateQuery, dev, D3DQUERYTYPE_OCCLUSION, &visible),
            "FAIL visible occlusion query") ||
        !HR(DEVICE(CreateQuery, dev, D3DQUERYTYPE_OCCLUSION, &partial),
            "FAIL partial occlusion query") ||
        !HR(DEVICE(CreateQuery, dev, D3DQUERYTYPE_OCCLUSION, &hidden),
            "FAIL hidden occlusion query") ||
        !HR(DEVICE(CreateQuery, dev, D3DQUERYTYPE_EVENT, &event), "FAIL event query")) {
        goto done;
    }
    Number("occlusion query data size", IDirect3DQuery9_GetDataSize(visible));
    Number("event query data size", IDirect3DQuery9_GetDataSize(event));
    if (!Check(IDirect3DQuery9_GetDataSize(visible) == sizeof(DWORD),
               "FAIL occlusion query data size", IDirect3DQuery9_GetDataSize(visible)) ||
        !State(dev, backbuffer, depth) ||
        !HR(DEVICE(Clear, dev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff000000, 1, 0),
            "FAIL query probe clear") ||
        !HR(DEVICE(BeginScene, dev), "FAIL query probe begin scene"))
        goto done;
    {
        const BOOL drawn = Quad(dev, 101, 41, 181, 121, .2f, 0xff20a040) &&
                           Counted(dev, visible, 21, 151, 61, 201, .5f, 0xff4060c0) &&
                           Counted(dev, partial, 141, 41, 221, 121, .8f, 0xffc04020) &&
                           Counted(dev, hidden, 111, 51, 171, 111, .8f, 0xffc0c020);
        if (!HR(DEVICE(EndScene, dev), "FAIL query probe end scene") || !drawn)
            goto done;
    }
    if (!HR(IDirect3DQuery9_Issue(event, D3DISSUE_END), "FAIL event query issue") ||
        !Samples(visible, 2000, "visible occlusion samples") ||
        !Samples(partial, 3200, "partially occluded samples") ||
        !Samples(hidden, 0, "hidden samples") || !Signalled(event))
        goto done;
    /* The visible query again, around a full-window quad behind everything
     * drawn, in a new scene; then the event again. */
    if (!HR(DEVICE(BeginScene, dev), "FAIL query probe second scene"))
        goto done;
    {
        const BOOL drawn = Counted(dev, visible, 0, 0, 320, 240, .9f, 0xff101010);
        if (!HR(DEVICE(EndScene, dev), "FAIL query probe second end scene") || !drawn)
            goto done;
    }
    if (!HR(IDirect3DQuery9_Issue(event, D3DISSUE_END), "FAIL event query reissue") ||
        !Samples(visible, 76800 - 6400 - 2000 - 3200, "reused query samples") ||
        !Samples(hidden, 0, "hidden samples, polled again") || !Signalled(event))
        goto done;
    Log("PASS D3D9 occlusion queries: visible 2000, partially occluded 3200, hidden 0 and "
        "reused 65200 exact samples; event query signalled twice");
done:
    if (visible)
        IDirect3DQuery9_Release(visible);
    if (partial)
        IDirect3DQuery9_Release(partial);
    if (hidden)
        IDirect3DQuery9_Release(hidden);
    if (event)
        IDirect3DQuery9_Release(event);
    DEVICE(SetDepthStencilSurface, dev, NULL);
    DEVICE(SetRenderState, dev, D3DRS_ZENABLE, D3DZB_FALSE);
    if (depth)
        SURFACE(Release, depth);
}

} // namespace query_probe
#endif
