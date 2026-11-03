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

- Fixed-function multitexture (`DG_CAP_GL_MULTITEXTURE`) exposes eight units
  through ARB_multitexture with combine, crossbar and dot3 environments.
  `ActiveTexture` moves binding ownership on the host; every unit's bindings,
  enables, environments, coordinate generation and texture matrices follow the
  active unit, and TEXTURE_BIT push/pop restores all units. Client arrays for
  units 1–7 travel in `DrawArraysEXT`, a sixteen-slot compact layout under a
  distinct function number so older kernels' seven-slot admission is unchanged.
  Wine's fast vertex path packs up to two units; more fall back to its strided
  path. The C mirror of the Rust context state is size-checked at QEMU startup.
- Fixed-function state (`DG_CAP_GL_FIXED_STATE`) adds constant blend colour,
  blend equations (subtract/reverse/min/max, separate RGB/alpha), separate blend
  factors, two-sided stencil with wrapping operations, point parameters and
  sprites, anisotropic filtering and LOD bias. With both capabilities Wine
  reports 8 stages and 8 simultaneous textures, TextureOpCaps `0x0280bbff`,
  StencilCaps `0x1ff` (TWOSIDED, INCR/DECR), MaxAnisotropy 16, BLENDOP,
  SEPARATEALPHABLEND and BLENDFACTOR. Frontend uploads accept every GL1.2
  packed pixel type, which Wine uses for its per-unit dummy textures.
- Wine patch 0027 keeps texture-less fixed-function stages active (see the
  patch README).
- Texture control (`DG_CAP_GL_TEXTURE_CONTROL`) adds ATI_texture_env_combine3
  (MODULATE_ADD/SIGNED_ADD/SUBTRACT), ATI_texture_mirror_once and
  SGIS_generate_mipmap. Values ride existing records; native levels generated
  below a defined image are charged with that image. Wine then claims
  ADDSMOOTH, BLENDTEXTUREALPHAPM, MODULATEALPHA_ADDCOLOR,
  MODULATECOLOR_ADDALPHA and MULTIPLYADD, MIRRORONCE addressing and
  automatic mipmap generation. The macOS border sampler evaluates the
  combine3 modes itself.
- Compressed textures (`DG_CAP_GL_COMPRESSED_TEXTURES`) add ARB_texture_compression
  and EXT_texture_compression_s3tc for 2D images: `CompressedTexImage2D` with an
  exact payload or none (zero-defined storage), block-row `CompressedTexSubImage2D`
  bands, `GetCompressedTexImage` through the shared texture read cache, the
  host's fixed four-format list, and S3TC/generic compressed internal formats for
  `TexImage2D`. Levels are charged at their compressed size. Every split 2D
  upload now starts its tiles on 4-pixel boundaries of the rectangle (gathering
  padded rows instead of sending single rows), and the host's zero fill uses
  4-row bands, so uncompressed sources into compressed storage stay block
  aligned. Rules that depend on the level (edge-only partial blocks, bounds,
  format) are GL errors, never record faults.
- Fog coordinates (`DG_CAP_GL_FOG_COORD`) add EXT_fog_coord: `FogCoordf` inside
  Begin/End and lists, `FOG_COORDINATE_SOURCE`, and a per-vertex FLOAT/DOUBLE
  coordinate in DrawArraysEXT slot 14 (slot 15 stays reserved), restoring the
  current coordinate after array draws. Without it Wine emulated D3D's
  specular-alpha fog of pretransformed vertices by blending vertex colours toward
  the fog colour before texturing, so fogged textured geometry darkened instead
  of fading to the fog colour. Wine's fast vertex path now packs those fog
  coordinates; other fog-coordinate states still use its strided path.
- Name-sized scalars (`TexParameter`, `TexEnv`, `Hint`) are pre-validated by the
  frontend against the same tables as their vector forms. An unknown,
  vector-only or unnegotiated name is a local `GL_INVALID_ENUM`; previously the
  host rejected the record as malformed, which failed the whole batch and left
  the context permanently failed.

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
The Win2000 regression also starts at **Quick Start**, when the launcher hands
off to the course process: the initial course must retain its requested display
mode without an Alt+Tab workaround. Open and close menus, click and drag the shot
control, and switch away and back; UI and golfer pixels must survive without
hovering to repaint them. A correctly sized launcher menu does not cover this
handoff. Equal-sized fullscreen mode requests still need the native ownership
call, and windowed swapchains must not restore a fullscreen display mode.

Linux result (2026-11-03, installer built from patches 0001–0026, QEMU
`e8e4a38a`): Quick Start keeps 800×600 scanout and renders golfer, terrain and HUD,
and opening menus/options/preferences no longer erases them. With the preferences
modal open, guest Win+D then Alt+Tab leaves black terrain apart from GDI-repainted
rectangles and blank YES/NO boxes until the modal closes. Closing it restores the
course, but the toolbar's right half stays blank. A bounded call trace shows Golf
presents with a full-frame `BltFast` from an 800×600 system-memory canvas and
issues no DirectDraw call at all after reactivation until the modal closes. The
same sequence is **pixel-identical** with Microsoft's DirectDraw on DreamGPU and
on a stock QEMU Cirrus adapter with Microsoft's driver and DirectDraw. Those
artifacts are therefore the game's own Windows 2000 behavior, not a DreamGPU
defect; do not "fix" them with blanket invalidation. A completed shot is still
unverified. Evidence: `target/golf-coherence-linux/verification.json`.

Linux multitexture result (2026-11-03, patches 0001–0027, frontend with
GL1.2 packed uploads, multitexture/fixed-state host): a fresh Win2000 clone
installs to a terminal receipt, and `sysgl`, `bordergl`, `sysglide`, `sysddraw`,
`sysd3d6`–`sysd3d9` and all four capability captures pass, including the D3D8/9
multi-stage probe. The Half-Life `jrgperf` timedemo ran at 193.6 and 205.0 FPS
(177.4 before multitexture). UT99 through D3D renders CityIntro with lightmaps
in a window. macOS CGL, Win98 and XP remain unverified for these capabilities.
With texture control the installed D3D9 caps add CANAUTOGENMIPMAP (Caps2
`0x60020000`), TextureOpCaps `0x0386ffff` and MIRRORONCE addressing
(`0x3f`); the stage probe renders all thirteen claimed cases exactly.

With compressed textures, fresh Win2000 and Windows XP clones (the user's XP
image, never modified) install to terminal receipts and pass every probe; the
D3D8/9 probes render DXT1/DXT3/DXT5 blocks exactly on both. Half-Life runs at
192 FPS on Win2000.

With fog coordinates, a textured pretransformed quad fogs from red to blue
exactly on Win2000 and XP through D3D8/9; the same probe reads red fading to
black (the emulation) on the previous build.

`capd3d6` additionally checks fullscreen primary/GDI coherence at 640×480×16.
It compares exact RGB565 pixels through primary Lock, GetDC, source blits,
batched GDI writes followed by ReleaseDC, and partial/keyed primary blits over
changing GDI backgrounds. This tests destination preservation at the installed
API boundary; the native compositor's clipped-copy tests alone cannot establish
that Wine supplies the correct untouched pixels when publishing a whole window.
Glide, execute buffers, D3D7/8/9 fixed-function behavior, resource pressure and
lifecycle cases still need the expanded installed suite. Live GPU snapshot
restoration remains unsupported.

```sh
python3 scripts/quality/graphics.py acceptance
```

This currently fails closed and lists known gaps, including missing installed
API evidence. Evidence ingestion for full conformance is not yet implemented.
Neither local sanitizer success nor capability capture can clear this gate.
