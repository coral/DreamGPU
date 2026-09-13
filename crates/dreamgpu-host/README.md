# DreamGPU native engine

This allocation-free `no_std` static library owns bounded 2D execution planning,
desktop/cursor validation, the process-scoped GL context/drawable registry, GL
function vocabulary and validation, scalar and vertex-array GL execution, and
texture namespaces, references, storage accounting, uploads and fence ordering.

QEMU invokes it through `include/dreamgpu-host.h`. Meson builds the archive with a
separate Cargo target directory, so an enclosing Cargo build cannot recursively
lock its own target directory. Ordinary builds use checked-in bindings and need no
bindgen installation. `bindings/generate.py` regenerates GL types from platform GL
headers and the pinned protocol vocabulary. Field names have a `dg_` prefix to
avoid Linux epoxy macro expansion changing the generated Rust interface.

The wire ABI remains unchanged. The internal C ABI uses explicit-width values,
opaque native handles and synchronous callbacks. It is an implementation boundary,
not an independently versioned public ABI. QEMU and this archive must be built
together.

The main ownership rules are:

- QEMU snapshots guest commands before validation. Rust borrows only those
  immutable host copies. It never creates a reference to guest-mapped VRAM;
  guest CPUs can access that RAM outside QEMU's global lock. Bounded integer
  transfer descriptors call QEMU's existing RAM/dirty-tracking primitive.
- The render worker owns registry mutation. Resource destruction drains pending
  publication before reuse. Rust does not retain registry references across C
  callbacks, and does not copy fields written by the completion worker.
- Texture names use a fixed 8192-entry table with a global 4096-object limit.
  Backward-shift deletion keeps load/unload churn from accumulating tombstones.
  Namespace, binding, default-object and attribute-stack references are distinct;
  deleting a name does not release storage retained by another binding.
- Texture storage is bounded by the existing 256 MiB contract. A failed
  initialization clear still accounts allocated storage and marks its level
  undefined until a complete upload initializes it.
- Fence dependencies skip the producing context and an already ordered reader.
  A new write invalidates reader ordering. Context serials are never reused.
- Platform GL consumes copied client arrays synchronously. Current attributes and
  client pointer state are restored before returning the immutable payload.
- Panics abort; no unwinding crosses QEMU. The aborting DWARF personality resolves
  references retained by precompiled `core`/compiler builtins and never swallows
  an unexpected unwind.

Native C still supplies OS context/export APIs, image publication and retirement,
memory allocation, and the remaining attachment, query/readback, attribute-stack
and texture-clear/copy routines. These boundaries are explicit rather than an
assertion that every renderer routine has already been converted.

Run `cargo test -p dreamgpu-host` for source-level protocol, ABI, lifetime,
failure and execution-policy tests. The DreamGPU renderer crate's diskless QEMU
suite exercises the linked native library with actual GPU pixel oracles; these
are the integration gates for each coherent conversion batch.
