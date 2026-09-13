# Guest tools

These programs run inside Windows to install a matched package, expose a bounded
control channel, or exercise graphics behavior. They are separate from the
[runtime drivers and API implementations](../guest/README.md).

| Directory | Programs |
| --- | --- |
| `benchmark/` | Fixed serial command runner, package installer and startup helpers |
| `nt/` | NT installer, driver/channel diagnostics and controlled shutdown |
| `win9x/` | Win98 installation and OS prerequisite diagnostics |
| `opengl/` | Public API, window, mode, retained-frame and lifecycle probes |
| `glide/` | Public Glide pixel and presentation probe |
| `d3d/` | Direct3D6/7/8/9 probes and fixed game preparation |
| `unreal/` | Fixed UT99 media preparation, setup, run and log collection |

Cargo builds these programs and keeps them in each OS package's `tools/`
directory, separate from the runtime drivers:

```sh
DREAMGPU_BUILD=all cargo build --release
```

Use the [pinned guest toolchain](../support/guest/toolchain/README.md), or set
`DREAMGPU_GUEST_HOST` to an SSH builder. CMake targets under
`support/guest/cmake/` also support focused rebuilds in Cargo's generated build
directory. There are no separate Python compiler recipes.

Host-side serial/QMP control, sampling and fixture adapters remain under
`scripts/`; see [automation ownership](../docs/automation.md).

Use fixed commands, fresh bounded logs and process-owned cleanup for acceptance.
A passing source build is distinct from an installed-package check or real GPU
pixel oracle. Screenshots can record a result, but menu clicking and timed image
inspection are not the benchmark control loop. See the
[UT99 automation contract](unreal/README.md).

Some Win98 tools diagnose OS prerequisites such as storage and loader patches. They are fixture utilities: DreamGPU does not implement audio.
The consuming application owns audio, and the shared QEMU fork retains Juke's
existing audio backend. Audio-only diagnostic utilities live with that consumer. Original OS, vendor driver and game payloads stay outside
this source tree.

Moving a source file into `tools/` does not change its license. Retain its
per-file SPDX identifiers and upstream notices; the root [LICENSE](../LICENSE)
and [component inventory](../docs/licensing.md) describe the applicable terms.
