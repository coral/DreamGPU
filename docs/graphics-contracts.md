# Graphics contracts and milestone verification

The current target is reliable behavior for shipped OpenGL 1.1/WGL, DirectDraw,
legacy Direct3D through the D3D8/9 providers, and Glide2x on Windows 98/2000/XP,
with macOS and Linux hosts. Earlier game and installer passes establish those
workloads only. Entrypoint dispatch coverage does not establish API conformance.

`tests/graphics/contracts.json` is the executable inventory. Each row identifies
an operation, format, resource, usage and pool, its implementation status, and
its tests. Broad rows remain to be expanded; `catalog_complete` is deliberately
false. Optional formats require actual per-usage capability checks. Unsupported,
unverified and missing mandatory behavior have different meanings.

## Current implementation milestone

This batch addresses related resource and API failures before another installer
and disposable-VM acceptance cycle:

- Surface Lock/GetDC propagates failed downloads and allocation failures without
  publishing stale memory. Failed ReleaseDC writeback retains the DC and CPU
  authority so the caller can retry.
- Buffer mapping checks bounds, invalid current contexts and failed native maps
  before publishing dirty ranges or discard state.
- Normalized backbuffer depth/stencil has CPU download and writeback paths,
  pitch/orientation conversion, staged downloads and aspect preservation during
  partial clears. Failed GL state pushes unwind only successfully saved frames.
  Transfers use explicit GL1.1 state groups and preflight every required native
  entrypoint. The private Wine provider exports the same client/pixel/raster
  implementations as the ICD, including CPU depth upload operations.
  FBO/PBO CPU depth transfers and floating-point depth are still unsupported.
- Legacy BeginIndexed/Index/End copies caller vertices and expands indices into
  the existing draw path, with bounds, allocation, nesting and error handling.
- GL texture work preserves all seven GL1.1 scalar types, component/index formats,
  bitmap indices, sized internal formats, empty definitions and proxy feasibility.
  Split subimages are checked before writing; native errors preserve the context.
  Supplied 1D/2D image borders participate in filtering through the Mesa provider
  and a private CGL sampler, behind a separate negotiated capability.
- Win98 installation can migrate the exact legacy `qemumini.drv`/`qemumini.vxd`
  binding, preserving verified originals and private backups for rollback.
  Installed INF short/long aliases and bounded nonterminated registry strings
  are handled without treating legacy files as DreamGPU-owned files.
- Capability probes capture installed GL/WGL and D3D8/9 claims independently of
  rendering validation. DGCAP6 exercises indexed draws and depth Lock/edit/clear
  contents, beyond the existing triangle smoke tests. The separate `bordergl`
  probe runs through system OpenGL/WGL and checks supplied-border pixels, caller
  memory ownership, negative-offset updates and preserved texture contents.
- Juke screenshot events identify their CPU or prepared-image source and
  publication freshness. A screenshot request alone does not synchronize new
  guest rendering; CPU publication generations are included when available.

Patch provenance stays checked in `support/guest/d3d/patches`; vendor trees are
not edited. New texture-border behavior is negotiated between frontend and host.
Use matching native and guest artifacts when testing the milestone.

`DG_CAP_GL_TEXTURE_IMAGES` gates the extended existing image-record semantics;
`DG_CAP_GL_TEXTURE_BORDERS` additionally promises supplied border texels in
filtering. Linux's patched Mesa provider implements 1D/2D storage and sampling.
On macOS, a private fragment sampler compensates for CGL retaining supplied
border bytes while ignoring them during native sampling. It uses an RGBA16 atlas
with complete mip images, projected coordinates, wrap modes, texture environments,
COMBINE and fog; native vertex processing and depth/stencil/blend tests remain
active. Temporary texture-unit and program state is restored after immediate,
array and evaluator draws. The atlas is invalidated by shared texture versions,
retains referenced storage through name deletion and is charged against the
resource budget. This does not advertise a public shader or multitexture API.
Exact native tests check 1D edges and 2D corners, signed border updates, copies,
readback and subsequent draws. Differential tests compare all six minification
filters and base-format/environment/fog combinations with ordinary textures.
Linux provider software runtime tests separately exercise sampling and resource
lifecycles; acceptance on Linux hardware and all installed guest APIs remains
part of the broader verification matrix.

## Required local verification

Run these once after the milestone sources settle. Use fresh output directories;
reports include exact source and log hashes and reject source changes during a
run. Native graphics verification is an explicit required job: ordinary
`cargo test` skips these GPU tests.

```sh
python3 scripts/quality/graphics.py inventory
python3 scripts/quality/graphics.py source --output target/graphics-m1-source
python3 scripts/quality/graphics.py native --output target/graphics-m1-native
```

The native job rebuilds QEMU from this checkout, runs host unit tests and every
ignored diskless native GPU test with one test thread. A successful command that
runs zero tests or leaves native tests ignored fails verification. Do not use
`DREAMGPU_BUILD=sdk` to claim a rebuilt native renderer.

Run `source-i686` inside the pinned Linux guest toolchain container, using the
same mounted checkout. It verifies the real 32-bit frontend ABI and packing:

```sh
python3 scripts/quality/graphics.py source-i686 --output target/graphics-m1-i686
```

Build the combined installer once using [the guest build instructions](build.md).
Keep the audited installer manifest and hashes with the verification artifacts.
Only repeat packaging and VM tests for a failed case that requires a fix or a
later milestone; source tests do not require guest installation.

## Installed API acceptance

Use disposable clones. Do not install the milestone into the user's VM.
The fixed serial runner accepts `capd3d6`, `capd3d8`, `capd3d9` and `capgl`;
[capability.md](../tools/d3d/capability.md) documents staging and collected JSON.
Capability JSON is bounded and retained verbatim. A capture records claims;
it never becomes a rendering pass merely because the process exited normally.

For each of the six OS/host combinations, retain the installed provider identities,
capability capture, exact public API results and relevant screenshots. Required
chains include render/read/edit/upload/render, partial updates with untouched
texels, palette changes without recreation, target A/B/A preservation, context
sharing/deletion, GPU/GDI ordering, allocation/transfer failure and retry, and
fullscreen/minimize/reset/exit. Integer data needs exact checks; floating-point
and rasterization comparisons need stated tolerances.

Golf acceptance requires a visible animated golfer and a completed shot.
Terrain/HUD rendering, a triangle, or a successful installer alone is insufficient.
Glide, execute buffers, D3D7/8/9 fixed-function behavior, resource pressure and
lifecycle cases still need the expanded installed suite. Live GPU snapshot
restoration remains unsupported.

```sh
python3 scripts/quality/graphics.py acceptance
```

This currently fails closed and lists known gaps, including missing installed
API evidence. Evidence ingestion for full conformance is not yet implemented.
Neither local sanitizer success nor capability capture can clear this gate.
