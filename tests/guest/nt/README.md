Run the actual GL limit, signature-cache and immutable-batch validators with
address/undefined-behavior sanitizers:

```sh
python3 tests/guest/nt/test_gl.py
```

`CC` can select GCC or Clang. These host tests do not require a Windows SDK.
The driver itself is built with `DREAMGPU_BUILD=guest cargo build --release` on the
pinned MinGW host (or through `DREAMGPU_GUEST_HOST`).

`gl-limits.c` exercises an older 256-record host, the 1024/1025 boundary,
independent byte bounds, hostile capability values, generation wrap and a
new OPEN against a changed host with the same saved generation. Ten thousand
ordinary cache checks cause no additional limit-register reads. The immutable
packet allocation stays 64 KiB. QUERY remains a sole-record operation.

`gl-registers.c` exercises the production MMIO cache used by the miniport.
Ten thousand ordinary submissions reuse capability, command-DMA/generation
and query-DMA settings without repeating those register accesses. Native reset,
generation wrap and every new OPEN invalidate the cache; a same-generation
OPEN refreshes capabilities after restoring a snapshot on another host.
The test also covers changed physical addresses/capacities, unsupported hosts,
and error-to-success completion without leaking the previous error code.
Busy/completed-sequence checks and the blocking completion wait remain in the
transport; these cache tests do not replace native reset/migration validation.

Native QEMU tests separately verify that an oversized batch cannot execute its
clear prefix and that a primitive remains ordered across a 1024-record boundary:

```sh
cargo test -p dreamgpu --features presentation --test native_qemu qemu_large_scalar_batches_preserve_order_and_reject_overflow -- --ignored --test-threads=1
```

For explicit diagnostics, QEMU's `dreamgpu_gl_submit`, `dreamgpu_gl_work`
and `dreamgpu_gl_complete` trace events report batch size, preparation,
worker queue/execution and completion time. They are disabled normally; no
clock reads or per-command log records are added to ordinary rendering.

`window.c` checks actual GDI WNDOBJ clipping and process ownership. Closing the last WGL context retires its client token; a new client in the same process can reopen the surviving HWND. A valid client from a different process cannot take it over, and the old presentation binding is invalid after rebind. Private kernel interface version 6 returns an opaque process identity only to the display driver after authorizing the current client; Windows 2000 requires no new Win32k imports.
