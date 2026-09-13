# DreamGPU guest Glide

This source-built OpenGLide library translates legacy Glide2x calls into the
same DreamGPU GL channel used by OpenGL and WineD3D. The host GPU renders the work;
normal swap/presentation does not read the image through the guest CPU.

Runtime adaptations stay in this directory. Exact patches live in `support/guest/glide/`. Rust prepares the pinned sources,
`support/guest/cmake/glide.cmake` compiles them, and the public probe lives in `tools/glide/`.

Build on a host with i686 MinGW C/C++ tools:

```sh
DREAMGPU_BUILD=guest cargo build --release
```

The source lock pins OpenGLide and SGI mipmap code. The builder retains source
and licenses, produces a DLL/probe hash manifest and audits imports. Ship the
corresponding DreamGPU `dgpugl.dll` in the game's folder alongside `glide2x.dll`.
Do not install over the operating system's OpenGL library. The translator's
actual packed pixel formats are converted by the bounded DreamGPU frontend;
unsupported global GL extensions are not advertised to satisfy a string gate.
The donor's optional annotation overlay is removed; DreamGPU owns instrumentation.
Local SGI mipmap code avoids a system GLU call entering another GL driver.

`DGGLIDE.EXE` exercises public Glide calls, RGB565 texture upload and drawing,
2304 exact GPU pixels including same-address texture replacement and combining before alpha/lightmap blending, eight swaps, context shutdown, and reopening a context on the same HWND. Its serial command is
`PROBE <request-id> glide`; `scripts/benchmarks/halflife.py probe glide` collects the
fresh log and exit status. The first Mac gate is retained in
`benchmarks/retro-gpu/glide-acceptance.json`. This does not establish real-game
compatibility, every texture/LFB format, or Windows98/XP support.

The shared C++23 probe source also builds `DGSYSGR.EXE` for system-provider
installation checks. Run it as `C:\DGSYSGR.EXE` with working directory `C:\`
and no neighboring `glide2x.dll` or `dgpugl.dll`. It uses normal
`LoadLibraryA("glide2x.dll")` search, then requires the actual Glide module and
its already-loaded DreamGPU OpenGL dependency to reside in `GetSystemDirectoryA`.
It runs the same 2304-pixel oracle and writes `C:\DGSYSGR.LOG`; the app-local
`DGGLIDE.EXE` remains a separate diagnostic mode. Building either tool proves
neither global installation nor a runtime pass. The fixed `sysglide` runner route
must be installed before invoking it through automation.

The original LGPL OpenGLide and SGI Free Software B headers remain in the
pinned/generated source. Distribution must retain their license notices and
provide corresponding source as required. Game media stays outside Git.

The wrapper reuses a window's existing pixel format and checks each WGL initialization step. Its unused ARB pixel-format probe context has been removed: this DreamGPU frontend supplies the checked legacy format. A failed bind stops initialization instead of continuing with null GL strings. `DG_FIRST_PRESENT_ACCEPTED` is written once per context after a successful WGL swap, allowing the fixed game helper to distinguish initialization from presentation without per-frame logging.

The buffer alignment cleanup preserves the donor's actual byte layout. An initial suspicion of heap corruption was disproved by the actual-source ASan/UBSan test: the donor macro's unparenthesized cast already forced byte arithmetic. Parenthesizing the macro together with explicit matching offsets makes that layout clear; no corruption fix or performance gain is claimed for this cleanup.

The donor inverted its SGIS capability flag into `BuildMipMaps` even when mipmaps were disabled. On hosts without that extension this caused unnecessary CPU mipmap generation; the upload macro also sent level zero twice. The builder now gates generation on the existing enabled setting, and selects either SGI generation or the ordinary image upload. `test_mipmaps.py` compiles the patched donor decision and macro with ASan/UBSan and checks all enabled/support combinations. This does not advertise SGIS support or change the selected texture filter.

DreamGPU builds now use the implemented core client-array ABI directly, without changing the frontend's advertised GL version or extension strings. The donor's version gate otherwise selected millions of immediate per-vertex calls. The array adaptation guards the absent secondary-color callback and fixes second-pass color pointers to address the allocated arrays. `test_arrays.py` exercises actual donor setup and second-pass code with six packed vertices under ASan/UBSan.

The texture database preserves the donor's address-overlap and palette-hash invalidation. Retired records leave lookup immediately; up to 64 private single-texture records can retain GPU storage, with an 8 MiB cap that conservatively includes complete generated mip chains. A compatible new texture reuses that storage through a complete `glTexSubImage2D` upload. Changes to level dimensions or internal format redefine the level normally. The pool is released on database clear or destruction; dual-texture extension records are deleted normally. Object-local wrap and filter values suppress repeated identical parameter calls.

`test_pool.py` compiles the actual patched database under ASan/UBSan. It checks overlapping invalidation, palette changes, pixel replacement, shape/internal-format/mip-level redefinition, null data, parameter changes, count and byte limits, dual-texture deletion, and final cleanup. Runtime acceptance still requires the public Glide pixel probe and one recorded game attempt with the exact package.

Glide math builds for Pentium III SSE1 (`-msse -mno-sse2 -mfpmath=sse`), including source-built mipmap helpers. This removes much of the translator's x87 extended-precision work under TCG without fast-math. Binary32 operations round differently from x87 excess-precision intermediates; this is not a bit-identical arithmetic claim. The guest OS must preserve SSE state (`CR4.OSFXSR`), and SIMD exceptions remain masked. The frontend's guest packing and x87 calling ABI are unchanged. Public pixel checks include normalized fractional alpha as well as texture-plus-local-color addition and multiplicative blending.

With negotiated secondary color, local RGB is added after texturing and before blending. The donor fallback instead drew a second unconditionally additive pass, so fully transparent geometry or a multiplicative lightmap could incorrectly brighten the destination. The public probe distinguishes these equations using known pixels, rather than treating a brighter game screenshot as a correctness oracle. Glide fog-coordinate support remains a separate unsupported capability; no fog acceptance is claimed by these blending checks.
