# Native GL geometry diagnostics

The opt-in `dreamgpu_gl_geometry_*` QEMU trace events distinguish application
rendering bounds from the drawable and exported image bounds. They helped locate
the windowed DirectDraw clipping defect: Wine's `ORM_BACKBUFFER` path attempted
to render a desktop-sized intermediate surface into a smaller window backing.
Increasing the GL viewport did not increase that backing's storage.

The events are declared in
[`trace-events`](../vendor/qemu/hw/display/trace-events) and emitted by
[`dreamgpu-gl-platform.c`](../vendor/qemu/hw/display/dreamgpu-gl-platform.c):

- `dreamgpu_gl_geometry_export` records context identity, drawable and image
  dimensions, framebuffer identity, and buffer exchange selection.
- `dreamgpu_gl_geometry_state` records the actual GL viewport and scissor box at
  export. These queries run only when geometry export tracing is enabled.
- `dreamgpu_gl_geometry_texture` records level and dimensions of 2D texture
  storage uploads.
- `dreamgpu_gl_geometry_call` records selected viewport, scissor, texture,
  immediate vertex, and buffer-selection calls. Arguments are raw protocol
  words; floating-point arguments require bit reinterpretation.

All four events are disabled by default. Enable only the events needed for one
bounded diagnostic on an owned fixture. Record their prior states with QMP
`trace-event-get-state`, enable them with `trace-event-set-state`, and verify the
result before starting the workload. Restore the prior states in cleanup,
including after a failed workload. Verify the actual QEMU command line and its
`-D` output file; a consumer configuration field alone does not prove that trace
options reached the native process.

Per-call geometry tracing can generate tens of megabytes during a short game and
perturb execution substantially. Export-state tracing adds synchronous native GL
state queries. These traces are diagnostic evidence, not comparable performance
measurements. Prefer export/state events for size questions and narrowly scope
per-call tracing when it is necessary. The game controller's normal 2 MiB trace
collection limit remains unchanged; exceeding it fails that collection gate.
An oversized diagnostic log must be retained and summarized separately, without
converting the failed collection into an acceptance claim.

Validate a correction with geometry tracing disabled. The checked Wine backing
size policy and original framebuffer-to-texture copy are exercised by
[`test_wine_backbuffer_bounds.py`](../tests/guest/d3d/test_wine_backbuffer_bounds.py).
The public [`D3D7 probe`](../tools/d3d/probe7.cpp) checks distinct colors at the far
right and bottom of a window at a nonzero desktop origin, through both GPU
readback and displayed GDI pixels. A real-game acceptance also retains one
composited image showing the full client area; frame activity alone cannot prove
correct rendering bounds.
