# Installed D3D capability capture

Build `dg_capability_d3d8` / `dg_capability_d3d9` to produce `DGCAP8.EXE` /
`DGCAP9.EXE`. Stage the requested helper at the fixed root path `C:\DGCAP8.EXE` /
`C:\DGCAP9.EXE` in a disposable installed fixture. The existing runner accepts
`PROBE <request-id> capd3d8` / `capd3d9`; the host CLI accepts the same probe names.
The executables need no arguments and write `C:\DGCP8.JSON` / `C:\DGCP9.JSON`.
Diagnostic logs use `C:\DGCP8.LOG` / `C:\DGCP9.LOG`. Host collection preserves the
JSON as `capability.json` beside `engine-output.txt` and its SHA256 in the report.
The ordinary system loader and actual provider/dependency module checks are the
same as the existing installed pixel probes. No device or visible window is
created by these capture tools.

Schema 1 (`kind: dreamgpu.d3d.capabilities`) contains:

- API and SDK version, raw guest OS version, loaded module paths, HAL device type,
  and explicit `reported_capabilities_only` evidence. `execution_validation` and
  `pool_validation` are false.
- `formats`: index dictionary of format names and exact enum/FourCC values.
- `usages`: index dictionary of resource names/enums and usage names/bitmasks.
- `format_query_columns`: `format_index`, `usage_index`, `hresult`.
- `adapters`: every reported adapter, identity/driver version/GUID bytes, named
  caps and the complete raw caps DWORD array, raw/major/minor shader versions,
  current display mode, and `format_queries` rows `[format-index, usage-index,
  "0xHRESULT"]`. Each row uses that adapter's current display-mode format and HAL.
  HRESULTs preserve all 32 bits, including successful nonzero return values.
- `complete`: all adapters' metadata was obtained; unsupported format results do
  not make capture incomplete. Failed identity/caps/mode queries retain their
  HRESULT and use null for unavailable data. A failed mode query leaves the
  adapter's matrix null, because its adapter-format argument is unavailable.

The matrix covers common color/indexed/alpha/luminance, depth/stencil, DXT1–5,
bump and YUV formats; D3D9 adds higher-precision and floating-point formats. It
queries plain surfaces, sampling, render targets, depth-stencil and dynamic
textures separately for valid resource/usage pairings. It does not query modern
9Ex-only formats. ANSI identity bytes map to U+0000–U+00FF for lossless byte
reconstruction; raw caps are little-endian SDK layout words, preserving float
bit patterns. Dictionaries avoid repeated strings so two-adapter captures fit
the runner's 64-KiB result bound. Larger results fail collection explicitly;
JSON is never silently replaced with a diagnostic tail.

`CheckDeviceFormat` reports a usage claim, not successful allocation, pool
behavior, locking, depth/color compatibility, rendering results, or lifetime
correctness. `complete: true` means capture completeness only. See Microsoft's
[CheckDeviceFormat contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3d9-checkdeviceformat)
and [resource/usage combinations](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dusage).
