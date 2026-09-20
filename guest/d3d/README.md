# Guest Direct3D integration

The guest `dreamgpu.exe` installer owns the system DirectDraw/Direct3D6–9
providers and their WineD3D dependencies. Ordinary games use those system APIs
without neighboring DLLs or patched imports. Full installer/API activation has
passed on Windows 2000 and XP; remaining lifecycle and Win98 gates are recorded
in [the plan](../../docs/plan.md).

## Diagnostic probes

The bounded public probes live in `tools/d3d/`; this directory keeps the
runtime Wine adaptations. `tools/d3d/probe.cpp` builds as `DGD3D8.EXE` and `DGD3D9.EXE`. Each loads the source-built
Wine9x interface (`wined8.dll` or `wined9.dll`) from `C:\SIERRA\Half-Life`, creates
a HAL device with a 320×240 X8R8G8B8 lockable backbuffer, draws a fixed red triangle,
checks 512 exact RGB pixels through a real backbuffer read, presents and releases
its interfaces. Pretransformed vertices keep the workload focused on the
fixed-function draw path. Each process writes a fresh bounded stage/result log
at `C:\DGD3D8.LOG` or `C:\DGD3D9.LOG` and exits zero only after the pixel contract
passes. Window bounds are calculated from the requested 320×240 client area;
platform decoration metrics are never assumed. On failure the log includes the
first pixel coordinate, expected color, pitch and nonblack pixel count. Diagnostic
GL queries record current drawable, pending errors, read/draw buffers, viewport,
scissor and color mask around the draw/readback stages; their overhead is outside
any FPS measurement. Process completion needs an external timeout because a faulty driver can
block inside a graphics call.

Build on the MinGW host:

```sh
DREAMGPU_BUILD=guest cargo build --release
```

Each OS package contains the probes under `tools/`; its manifest records exact
source, recipe and binary hashes. The Win2000/Pentium3 executables have no C runtime dependency and
import only Kernel32/User32; the actual graphics interface is loaded explicitly.
The compiled package is a prerequisite for runtime acceptance, not evidence that
a Direct3D game works.

## Wine and DreamGPU binding

The system installer places `wined3d.dll`, `wined8.dll`, `wined9.dll` and
`dgpugl.dll` in the OS system directory. Private diagnostic fixtures may retain
their own copies. Current Wine packages directly import the uniquely named DreamGPU
frontend; games do not need a diagnostic process to preload an OpenGL alias.
Historical accepted probes/packages used an explicit `opengl32.dll` preload,
so retain their original package identities when interpreting recorded results.
The frontend must implement the core imports required by Wine.

The Cargo guest build applies the normal DreamGPU binding
when building pinned Wine9x: only Wine's GL translation objects map pixel-format
selection, description, query, assignment and buffer swapping to the `wgl*`
exports in `dgpugl.dll`. Its dynamic core-function lookup uses that same
module identity. The build checks PE imports so
stock GDI GL pixel-format calls cannot accidentally bypass DreamGPU's own context
state. Native DirectDraw switching objects retain their original behavior. The
Wine capability probe uses a hidden 32×32 popup so its client drawable is nonempty;
the original decorated 10×10 outer window had no usable client area on Win2000.

Do not raise advertised GL versions or extensions solely to get through Wine
initialization. Record the actual failed API/stage and implement its contract.
The first runtime gate is the exact pixel result, followed by a real game for
broader compatibility and performance acceptance.

## Direct3D 7

`tools/d3d/probe7.cpp` builds as `DGD3D7.EXE`. It explicitly loads `winedd.dll`, calls
DirectDrawCreateEx and queries Direct3D7. It creates a HAL device on a 320×240
32-bit RGB video-memory render target, checks the same 512 triangle/background
pixels through a real DirectDraw surface lock, and presents with a window-clipped
primary-surface blit. It writes `C:\DGD3D7.LOG`; the runner accepts only the fixed
`PASS automated d3d7:` result prefix. The builder audits Kernel32/User32 for D3D8/9 and Kernel32/User32/GDI32
for D3D6/7, and records each exact binary identity. The current D3D7 oracle
also checks512 presented pixels through GetPixel (1,024 total).

The DreamGPU Wine binding uses a 64 MiB texture-accounting budget. Wine's conservative
capability fallback otherwise selected a RIVA 128 with only 4 MiB: after reserving
3 MiB for a 1024×768×32 desktop, a primary-sized DirectDraw surface failed with
`DDERR_OUTOFVIDEOMEMORY` before reaching the GPU. The explicit bounded translator
budget is separate from the framebuffer BAR and host physical VRAM; it does not
change the frontend's advertised GL version or extensions. The native texture
storage guard remains 256 MiB. Direct3D7 passed on both hosts after this correction; Direct3D8/9 had already
passed their smaller windowed contracts. See the exact scopes and hashes in
the acceptance manifest (`benchmarks/retro-gpu/d3d-acceptance.json`, historical Juke evidence).

## Original Unreal game fixture

`prepare-unreal.py --output <new-private-directory>` verifies the original
UT99 GOTY D3DDrv.dll SHA256 before replacing its single UTF16 `ddraw.dll`
loader name with the equal-length `dgddr.dll` alias. The output is a separate
private copy; original media and Windows' DirectDraw remain untouched.
`dgddr.dll` is the source-built `winedd.dll`, alongside `wined3d.dll` and
`dgpugl.dll`. The generated `DGD3D.INI` selects the original D3D renderer
at640×480×32 windowed with no sound/server actors and no triple buffering.

After the fixed original UT setup, `utdsetup` copies these specific inputs;
`utd3d` runs CityIntro through the shared bounded UT process/log loop. The
helper verifies the exact three GPU provider paths and uses separate result
and engine logs. The initial Linux game reached D3D initialization and rejected
a valid D24S8 depth-buffer surface. Failure-only Wine descriptor logging showed
that capability enumeration accepted the real WGL24/8 framebuffer while resource
creation had no core nontexturable D24S8 format entry. Adding that storage entry
lets CityIntro initialize. Two later source-backed fixes corrected native texture-copy
source bounds and Wine selection of an unadvertised rectangle texture resource.
The live owned WLog observer now establishes engine readiness without waiting for
the original buffered file logger. Linux completes the ten-second game run with
multiple GPU frames, subsequent host submissions, and zero native GL rejections.
Cadence remains low: the saved CPU profile points to guest MMIO and QEMU dispatch.
The next measurement records foreground ownership and bounded query/MMIO counts.
This bounded delivery gate is not a rendered pixel oracle or a game FPS result.
See the recorded progress (`benchmarks/retro-gpu/ut-d3d-progress.json`, historical Juke evidence).
No game FPS is inferred from an API probe or a successful asset installation.

Wine surface failures write only bounded numeric descriptors and source return
lines to `C:\DGDDRAW.LOG` (16KiB limit). Successful allocations/draws do no log
I/O. The fixed D3D game helper deletes the previous file and includes this log in
its result evidence. Frozen diagnostic packages retain the patched Wine source
for exact line attribution.

Historical Wine copy-diagnostic packages used the former optional builder mode. The exact diagnostic patches remain covered by `tests/guest/d3d/test_wine_patches.py`; the production Cargo recipe applies the normal base variant. The historical diagnostic package records at most 64 surface-transition tuples and counts during the fixed game's named measurement interval. It writes `C:\DGWCOPY.LOG` every 128 transfer/present boundaries and at the observed end event; the fixed D3D helper collects that bounded log automatically. A boundary count is not a frame count; front-buffer GL flushes can publish without Wine swapchain Present. An overflow count makes missing tuple detail explicit. Normal Wine builds omit these counters.

The package manifest maps numeric source IDs to pinned Wine filenames. Location-call lines are captured before instrumentation; `root_source/root_line` identify the outer location request, while `source/line` identify nested transitions. Kinds are: 1 location transition, 2 framebuffer download, 3 texture download, 5 CPU blit destination, 6 GetDC, 7 map, and 8 CPU blit source. Roles are 0 nonswapchain, 1 front buffer, 2 first back buffer, and 3 other swapchain surface. CPU reads requested by Lock/GetDC must retain coherence; seeing readbacks alone does not prove that enabling FBOs would remove them. The existing non-FBO paths already support framebuffer-to-texture GPU copies and back-to-front presentation.


The accepted Wine front-buffer path treats actual swapchain buffers as render
targets for GPU blitter selection, even when DirectDraw gives the front buffer
`usage=0`. A read-only map adds a CPU copy without making a still-valid GPU copy
obsolete. Writable maps and GetDC invalidate GPU locations as before; genuine
partial CPU uploads now propagate preparation failures instead of submitting into
undefined texture storage. `test_wine_blit_usage.py` checks the real selection
predicate and pinned Wine front-coordinate translation. The extended D3D7 probe
checks 512 target Lock pixels and 512 post-blit GDI pixels.

Linux's final Wine package (`wine9x-front-blit-v12`) completed post-precache
CityIntro with 3,895 accepted native presents in 10.010782 seconds and no measured
ReadPixels/GetTexImage queries. This is a native presentation counter, not engine
FPS or physical display refresh. The original frame-copy path downloaded a
1024x768 render target and re-uploaded the swapchain front each frame. Four fixed
GDI workloads and idle also passed after the classification change. Exact
artifacts and limits are recorded in `benchmarks/retro-gpu/ut-d3d-progress.json`,
`d3d-acceptance.json` and `desktop-after3d.json`; existing baselines are preserved.

## Direct3D 6

`tools/d3d/probe6.cpp` uses the actual legacy `DirectDrawCreate` factory, then queries
`IDirectDraw4` and `IDirect3D3`. Separate legacy checks verify Direct3D1 device
enumeration and an RGB device with system-memory color/depth surfaces. Two
clears and an intervening writable CPU map check 2,048 RGB565 pixels, including
preservation outside a partial clear.

The legacy device also enumerates an indexed 8-bit texture format, loads a
system-memory Texture1 with an attached palette, obtains its handle, and draws
through a Direct3D1 execute buffer. It checks 512 rendered pixels across palette
updates, including color-key transparency, and verifies CPU palette indices
remain unchanged after GPU upload.

The HAL Device3/Viewport3 check sets `D3DVIEWPORT2`, clears on the main thread,
draws a fixed triangle on a worker, then checks 512 target pixels through Lock
plus 512 window-front pixels through GetPixel on the main thread. The worker
remains alive during readback to verify shared graphics resources and command
publication; bounded waits pump window messages and fail the whole probe process
if a worker cannot finish safely. The system-loader variant is `DGSYS6.EXE`.
It deletes/releases its viewport, device, surfaces and window explicitly.
`DirectDrawCreateEx` accepts the newer interface contract and is not the D3D6
factory path.

```sh
DREAMGPU_BUILD=guest cargo build --release
```

The built PE4 helper is `DGD3D6.EXE`. It writes `C:\DGDRV.LOG` and does
not install a driver or alter Windows configuration. Earlier readonly-CD
acceptance staged this diagnostic under the fixed `DGDRV.EXE` probe alias;
that historical staging name is not the current artifact name. Keep the
original executable/ISO identities with that evidence. The standard package's
`DGDRV.EXE` is the driver installer.
The first Mac NT execution passes1,024 exact pixels with zero native GL
rejections. Linux acceptance is separate; see `d3d-acceptance.json`.

See [guest tools](../../tools/README.md) for probe builders and
[execution evidence](../../docs/progress.md) for current acceptance. Historical
benchmark filenames above remain references into the original Juke tree.

## Public system providers

The system route installs the reviewed switchers as Windows' public `ddraw.dll`,
`d3d8.dll` and `d3d9.dll`, with Wine's providers and `dgpugl.dll` in the same
system directory. An ordinary unknown application uses Wine without a per-game
registry enrollment or DLL copy. Preserved native aliases support the switcher's
explicit compatibility routes; native and Wine COM factories are never exchanged
after an object has been returned. The earlier app-local instructions above
describe the retained diagnostic and historical game fixtures.

`DGDD2D.EXE` exercises an ordinary DirectDraw-only client through the public loader,
verifies the actual COM provider, exact fill/copy/color-key/primary pixels and two
object lifecycles, and times completed offscreen blits. Its `--native` variant
checks the original public provider with the same workload. A native primary-blit
failure remains a failure even when the independent offscreen timing succeeded.

The checked Wine patches retain the original pitched/overlapping row paths while
coalescing proven contiguous, nonoverlapping CPU copies. CPU surface maps skip GL
context binding only when the exact color SYSMEM, USER_MEMORY or DIB location is
already current. Stale copies, depth/stencil and GPU/PBO locations retain their
original synchronization path. These changes preserve GPU rendering and avoid
unnecessary Windows 98 window/atom thunks during ordinary CPU surface access.

Provider installation requires recorded original DLL identities, a cold boot and
an exact rollback receipt. Passing a bounded API probe does not establish broad
game compatibility or a universal 2D performance guarantee.
