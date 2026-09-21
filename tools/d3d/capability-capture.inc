// SPDX-License-Identifier: GPL-2.0-or-later
// Shared by the installed-provider executable and the host contract harness.
// CheckDeviceFormat reports a usage claim: it does not test pools, locking,
// depth/color pairing, creation, rendering results, or resource lifetimes.
struct CapabilityFormat {
    const char *name;
    D3DFORMAT value;
};
#define CAP_FORMAT(name) {#name, D3DFMT_##name}
static const CapabilityFormat capability_formats[] = {
    CAP_FORMAT(R8G8B8), CAP_FORMAT(A8R8G8B8), CAP_FORMAT(X8R8G8B8),
    CAP_FORMAT(R5G6B5), CAP_FORMAT(X1R5G5B5), CAP_FORMAT(A1R5G5B5),
    CAP_FORMAT(A4R4G4B4), CAP_FORMAT(R3G3B2), CAP_FORMAT(A8),
    CAP_FORMAT(A8R3G3B2), CAP_FORMAT(X4R4G4B4), CAP_FORMAT(A2B10G10R10),
    CAP_FORMAT(G16R16), CAP_FORMAT(A8P8), CAP_FORMAT(P8), CAP_FORMAT(L8),
    CAP_FORMAT(A8L8), CAP_FORMAT(A4L4), CAP_FORMAT(V8U8), CAP_FORMAT(L6V5U5),
    CAP_FORMAT(X8L8V8U8), CAP_FORMAT(Q8W8V8U8), CAP_FORMAT(V16U16),
    CAP_FORMAT(A2W10V10U10), CAP_FORMAT(UYVY), CAP_FORMAT(YUY2),
    CAP_FORMAT(DXT1), CAP_FORMAT(DXT2), CAP_FORMAT(DXT3), CAP_FORMAT(DXT4), CAP_FORMAT(DXT5),
    CAP_FORMAT(D16_LOCKABLE), CAP_FORMAT(D32), CAP_FORMAT(D15S1), CAP_FORMAT(D24S8),
    CAP_FORMAT(D16), CAP_FORMAT(D24X8), CAP_FORMAT(D24X4S4),
#if DG_D3D_VERSION == 8
    CAP_FORMAT(W11V11U10),
#else
    CAP_FORMAT(A8B8G8R8), CAP_FORMAT(X8B8G8R8), CAP_FORMAT(A2R10G10B10),
    CAP_FORMAT(A16B16G16R16), CAP_FORMAT(L16), CAP_FORMAT(D32F_LOCKABLE),
    CAP_FORMAT(D24FS8), CAP_FORMAT(Q16W16V16U16), CAP_FORMAT(R16F),
    CAP_FORMAT(G16R16F), CAP_FORMAT(A16B16G16R16F), CAP_FORMAT(R32F),
    CAP_FORMAT(G32R32F), CAP_FORMAT(A32B32G32R32F), CAP_FORMAT(CxV8U8),
    CAP_FORMAT(MULTI2_ARGB8), CAP_FORMAT(G8R8_G8B8), CAP_FORMAT(R8G8_B8G8),
#endif
};
#undef CAP_FORMAT

struct CapabilityUsage {
    const char *resource_name;
    D3DRESOURCETYPE resource;
    const char *usage_name;
    DWORD usage;
};
static const CapabilityUsage capability_usages[] = {
    {"surface", D3DRTYPE_SURFACE, "plain_surface", 0},
    {"surface", D3DRTYPE_SURFACE, "render_target", D3DUSAGE_RENDERTARGET},
    {"surface", D3DRTYPE_SURFACE, "depth_stencil", D3DUSAGE_DEPTHSTENCIL},
    {"texture_2d", D3DRTYPE_TEXTURE, "sampling", 0},
    {"texture_2d", D3DRTYPE_TEXTURE, "render_target", D3DUSAGE_RENDERTARGET},
    {"texture_2d", D3DRTYPE_TEXTURE, "depth_stencil", D3DUSAGE_DEPTHSTENCIL},
    {"texture_2d", D3DRTYPE_TEXTURE, "dynamic_sampling", D3DUSAGE_DYNAMIC},
    {"texture_cube", D3DRTYPE_CUBETEXTURE, "sampling", 0},
    {"texture_cube", D3DRTYPE_CUBETEXTURE, "render_target", D3DUSAGE_RENDERTARGET},
    {"texture_cube", D3DRTYPE_CUBETEXTURE, "depth_stencil", D3DUSAGE_DEPTHSTENCIL},
    {"texture_cube", D3DRTYPE_CUBETEXTURE, "dynamic_sampling", D3DUSAGE_DYNAMIC},
    {"texture_volume", D3DRTYPE_VOLUMETEXTURE, "sampling", 0},
    {"texture_volume", D3DRTYPE_VOLUMETEXTURE, "dynamic_sampling", D3DUSAGE_DYNAMIC},
};

static void CaptureShader(CapabilityJson &json, DWORD version) {
    json.text("{\"raw\":");
    json.hex(version);
    json.text(",\"major\":");
    json.number((version >> 8) & 255);
    json.text(",\"minor\":");
    json.number(version & 255);
    json.text("}");
}

static void CaptureCaps(CapabilityJson &json, const CaptureDeviceCaps &caps) {
    json.text("{\"byte_size\":");
    json.number(sizeof(caps));
    // Include every ABI word, preserving floating point bit patterns and fields
    // added by D3D9 as well as the named fields useful for baseline comparisons.
    json.text(",\"raw_dwords\":[");
    static_assert(sizeof(caps) % sizeof(DWORD) == 0);
    for (unsigned offset = 0; offset < sizeof(caps); offset += sizeof(DWORD)) {
        DWORD word;
        CopyMemory(&word, reinterpret_cast<const BYTE *>(&caps) + offset, sizeof(word));
        if (offset)
            json.text(",");
        json.hex(word);
    }
    json.text("],\"reported\":{");
#define CAP_FIELD(name) json.text("\"" #name "\":"); json.hex(caps.name)
#define CAP_NEXT(name) json.text(","); CAP_FIELD(name)
    CAP_FIELD(Caps); CAP_NEXT(Caps2); CAP_NEXT(Caps3); CAP_NEXT(DevCaps);
    CAP_NEXT(PresentationIntervals); CAP_NEXT(CursorCaps); CAP_NEXT(PrimitiveMiscCaps);
    CAP_NEXT(RasterCaps); CAP_NEXT(ZCmpCaps); CAP_NEXT(SrcBlendCaps); CAP_NEXT(DestBlendCaps);
    CAP_NEXT(AlphaCmpCaps); CAP_NEXT(ShadeCaps); CAP_NEXT(TextureCaps); CAP_NEXT(TextureFilterCaps);
    CAP_NEXT(CubeTextureFilterCaps); CAP_NEXT(VolumeTextureFilterCaps); CAP_NEXT(TextureAddressCaps);
    CAP_NEXT(VolumeTextureAddressCaps); CAP_NEXT(LineCaps); CAP_NEXT(StencilCaps); CAP_NEXT(FVFCaps);
    CAP_NEXT(TextureOpCaps); CAP_NEXT(VertexProcessingCaps);
#undef CAP_FIELD
#undef CAP_NEXT
#define CAP_LIMIT(name) json.text(",\"" #name "\":"); json.number(caps.name)
    CAP_LIMIT(MaxTextureWidth); CAP_LIMIT(MaxTextureHeight); CAP_LIMIT(MaxVolumeExtent);
    CAP_LIMIT(MaxTextureRepeat); CAP_LIMIT(MaxTextureAspectRatio); CAP_LIMIT(MaxAnisotropy);
    CAP_LIMIT(MaxTextureBlendStages); CAP_LIMIT(MaxSimultaneousTextures); CAP_LIMIT(MaxActiveLights);
    CAP_LIMIT(MaxUserClipPlanes); CAP_LIMIT(MaxVertexBlendMatrices); CAP_LIMIT(MaxVertexBlendMatrixIndex);
    CAP_LIMIT(MaxPrimitiveCount); CAP_LIMIT(MaxVertexIndex); CAP_LIMIT(MaxStreams); CAP_LIMIT(MaxStreamStride);
    CAP_LIMIT(MaxVertexShaderConst);
#undef CAP_LIMIT
    json.text("},\"vertex_shader\":");
    CaptureShader(json, caps.VertexShaderVersion);
    json.text(",\"pixel_shader\":");
    CaptureShader(json, caps.PixelShaderVersion);
    json.text("}");
}

static void CaptureIdentity(CapabilityJson &json, const CaptureAdapterIdentifier &identity) {
    json.text("{\"driver\":");
    json.string(identity.Driver, sizeof(identity.Driver));
    json.text(",\"description\":");
    json.string(identity.Description, sizeof(identity.Description));
#if DG_D3D_VERSION == 9
    json.text(",\"device_name\":");
    json.string(identity.DeviceName, sizeof(identity.DeviceName));
#endif
    json.text(",\"driver_version_high\":");
    json.hex(identity.DriverVersion.HighPart);
    json.text(",\"driver_version_low\":");
    json.hex(identity.DriverVersion.LowPart);
#define IDENTITY_FIELD(name) json.text(",\"" #name "\":"); json.hex(identity.name)
    IDENTITY_FIELD(VendorId); IDENTITY_FIELD(DeviceId); IDENTITY_FIELD(SubSysId);
    IDENTITY_FIELD(Revision); IDENTITY_FIELD(WHQLLevel);
#undef IDENTITY_FIELD
    json.text(",\"device_identifier_bytes\":[");
    const BYTE *guid = reinterpret_cast<const BYTE *>(&identity.DeviceIdentifier);
    for (unsigned i = 0; i < sizeof(identity.DeviceIdentifier); ++i) {
        if (i)
            json.text(",");
        json.number(guid[i]);
    }
    json.text("]}");
}

static void CaptureQuerySchema(CapabilityJson &json) {
    json.text("\"format_query_columns\":[\"format_index\",\"usage_index\",\"hresult\"],\"formats\":[");
    bool first = true;
    for (const auto &format : capability_formats) {
        if (!first) json.text(",");
        first = false;
        json.text("{\"name\":"); json.string(format.name);
        json.text(",\"value\":"); json.hex(format.value);
        json.text("}");
    }
    json.text("],\"usages\":[");
    first = true;
    for (const auto &usage : capability_usages) {
        if (!first) json.text(",");
        first = false;
        json.text("{\"resource\":"); json.string(usage.resource_name);
        json.text(",\"resource_value\":"); json.number(usage.resource);
        json.text(",\"usage\":"); json.string(usage.usage_name);
        json.text(",\"usage_value\":"); json.hex(usage.usage);
        json.text("}");
    }
    json.text("],");
}

static void CaptureMatrix(CapabilityJson &json, D3D *d3d, UINT adapter, D3DFORMAT adapter_format) {
    json.text("[");
    // Index dictionaries keep complete captures within the runner's bounded
    // serial payload without discarding unsupported combinations or raw errors.
    for (unsigned f = 0; f < sizeof(capability_formats) / sizeof(capability_formats[0]); ++f) {
        for (unsigned u = 0; u < sizeof(capability_usages) / sizeof(capability_usages[0]); ++u) {
            if (!json.good())
                return;
            const auto &format = capability_formats[f];
            const auto &usage = capability_usages[u];
            const HRESULT result = D3D_CALL(CheckDeviceFormat, d3d, adapter, D3DDEVTYPE_HAL,
                    adapter_format, usage.usage, usage.resource, format.value);
            if (f || u)
                json.text(",");
            json.text("\n[");
            json.number(f); json.text(","); json.number(u); json.text(",");
            json.hex(result);
            json.text("]");
        }
    }
    json.text("]");
}

// Returns metadata capture completeness, not whether formats are supported.
// Unsupported formats are normal successful observations with raw HRESULTs.
static bool CaptureAdapters(CapabilityJson &json, D3D *d3d) {
    CaptureQuerySchema(json);
    const UINT count = D3D_CALL(GetAdapterCount, d3d);
    bool complete = count != 0;
    json.text("\"adapter_count\":");
    json.number(count);
    json.text(",\"adapters\":[");
    for (UINT adapter = 0; adapter < count && json.good(); ++adapter) {
        CaptureAdapterIdentifier identity = {};
        CaptureDeviceCaps caps = {};
        D3DDISPLAYMODE mode = {};
        const HRESULT identity_result = D3D_CALL(GetAdapterIdentifier, d3d, adapter, 0, &identity);
        const HRESULT caps_result = D3D_CALL(GetDeviceCaps, d3d, adapter, D3DDEVTYPE_HAL, &caps);
        const HRESULT mode_result = D3D_CALL(GetAdapterDisplayMode, d3d, adapter, &mode);
        complete = complete && SUCCEEDED(identity_result) && SUCCEEDED(caps_result) && SUCCEEDED(mode_result);
        if (adapter)
            json.text(",");
        json.text("\n{\"ordinal\":");
        json.number(adapter);
        json.text(",\"identity_hresult\":");
        json.hex(identity_result);
        json.text(",\"identity\":");
        if (SUCCEEDED(identity_result))
            CaptureIdentity(json, identity);
        else
            json.text("null");
        json.text(",\"caps_hresult\":");
        json.hex(caps_result);
        json.text(",\"caps\":");
        if (SUCCEEDED(caps_result))
            CaptureCaps(json, caps);
        else
            json.text("null");
        json.text(",\"display_mode_hresult\":");
        json.hex(mode_result);
        json.text(",\"display_mode\":");
        if (SUCCEEDED(mode_result)) {
            json.text("{\"width\":"); json.number(mode.Width);
            json.text(",\"height\":"); json.number(mode.Height);
            json.text(",\"refresh_hz\":"); json.number(mode.RefreshRate);
            json.text(",\"format_value\":"); json.hex(mode.Format);
            json.text("}");
        } else {
            json.text("null");
        }
        json.text(",\"format_queries\":");
        if (SUCCEEDED(mode_result))
            CaptureMatrix(json, d3d, adapter, mode.Format);
        else
            json.text("null");
        json.text("}");
    }
    json.text("]");
    return complete && json.good();
}
