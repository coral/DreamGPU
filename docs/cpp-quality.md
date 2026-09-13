# C and C++ quality gates

Use LLVM 22.1.8 for consistent formatting and analysis. The production guest
build uses the pinned MinGW GCC/G++ 16.1.1 toolchain. Clang analyzes its actual
CMake compilation databases and pinned headers; it does not replace the guest
compiler or the final PE/NE/LE link and import audits.

```sh
python3 scripts/quality/check-cpp.py format --check
python3 scripts/quality/check-cpp.py format --fix
```

The formatter covers maintained source, headers, included fragments and test
harnesses. It excludes upstream repositories except the explicit DreamGPU QEMU
file allowlist in the checker. It also excludes build outputs and ignored data.
Shared protocol header copies must stay byte-identical after formatting.

Run analysis on the host that produced the compilation database. For a source
snapshot, pass `--root` naming that same snapshot. CMake emits separate `nt5`
and `win98` databases under the guest build directory. Wine's original Make
build records individual compiler commands in `wine-compile-commands/`.

```sh
python3 scripts/quality/check-cpp.py tidy \
  --compile-db target/guest/build/nt5 \
  --compile-db target/guest/build/win98 \
  --extra-arg=--target=i686-w64-windows-gnu \
  --extra-arg=-resource-dir=/usr/lib/clang/22 \
  --extra-arg=-isystem/usr/i686-w64-mingw32/sys-root/mingw/include \
  --drop-arg=-fanalyzer \
  --drop-arg=-mpreferred-stack-boundary=2 \
  --drop-arg=-mincoming-stack-boundary=2 \
  --extra-arg=-mstack-alignment=4 \
  --jobs 4 --output target/quality/guest-tidy
```

The three removed options are GCC-specific: Clang's analyzer replaces
`-fanalyzer`, and `-mstack-alignment=4` preserves the Win98 flat bridge stack
alignment during analysis. CPU, SSE/SSE2 policy, API version, DDK includes and
language flags remain those of each real compilation. The checker keeps
separate entries for different macro/target variants and records every original
and adjusted command, LLVM version, exit status and diagnostic log. Native QEMU
analysis uses its own database without the guest target/sysroot adjustments.
GCC-only native options unsupported by Clang must be explicitly named with
`--drop-arg`; they are retained in the report.

Prepared donor translation units require an exact `--borrowed-tu` path. For
Wine, include both `ddraw/surface.c` and `wined3d/surface.c`, provide the matching
record directory, and give each `--borrowed-base PREPARED=PINNED` pair. Add the
complete maintained `dg-wine-diagnostics.h` and `dg-wine-blit-usage.h` through
`--borrowed-header`. This analyzes the real surrounding code while restricting
diagnostics to changed source ranges and our entire included fragments.
Likewise, analyze OpenGlide's `TexDB.cpp` to cover `texture-pool.cpp.inc`, plus
the generated `dg-glu.c` and `dg-new.cpp` glue. `dg-mipmap.c` is an unchanged
copy of pinned Wine GLU and is outside the maintained-code gate. Generated
Win98 `loader32-startup.c` and `setup32-startup.c` are also exact wrapper TUs
which include maintained tool source and must be selected explicitly.

## Legacy boundaries

Clang cannot parse Open Watcom's segmented pointers, register-call pragmas and
VMM/VPICD assembler dialect as the real ABI. Only the actual Win16/VxD glue in
`guest/win9x/dg-*.c` and the two NE tools `blit16.c` and `diagnose16.c` are
excluded from clang-tidy. Their surrounding C remains formatted; newline-based
assembler regions have narrow `clang-format off` markers. The real Watcom
warning gate and final NE/LE audits remain required. All flat Win98 C++ policy
and ordinary Win32 tools use the actual MinGW compilation database.

The root `.clang-tidy` disables the struct-padding suggestion and the API-name
ban which recommends Annex K `_s` replacements unavailable on these guests.
Bounds, null-pointer, lifetime, allocation and other correctness analyzers remain
enabled. Guest/tool findings are fixed in source rather than globally hidden.
An analysis pass is not a proof of memory safety, and a format pass is not a
substitute for the real compiler: it caught a Watcom instruction-line issue in
this migration.
