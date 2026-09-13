# Glide provider scope

The current DreamGPU provider is `glide2x.dll`, translated through the same
DreamGPU OpenGL channel as WineD3D. The installer must put the provider and its
shared runtime in the Windows system directory. Games using that API use normal
Windows DLL loading; application-local deployment is a diagnostic fixture only.

| API | Source available | Current package | Remaining work |
| --- | --- | --- | --- |
| Glide2x | Pinned `vendor/qemu-xtra/openglide`, with checked DreamGPU adaptations | `glide2x.dll` | Finish installer ownership/removal and final OS/host acceptance |
| Glide3x | Donor `openglide/Glide3x.def`, `g3wrap.cpp`, or `g2xwrap/wr301dll.cpp` | Not shipped as a supported provider | Audit layout conversion and state/query semantics, implement missing behavior, add normal-loader pixel and lifecycle probes, then integrate packaging |
| Glide 2.11 (`glide.dll`) | Donor `g2xwrap/wr211dll.cpp` and `gl211wrp.def` | Not shipped as a supported provider | Audit the older ABI and forwarding exports, add legacy compiler/import checks and a real public-API probe before packaging |

The current CMake source list includes `g3wrap.cpp`, but links the **Glide2x export
table**. Compiling that source does not establish a Glide3x ABI. In the pinned
donor, `grFinish` and `grFlush` are empty, and vertex-layout offset handling needs
bounds and disabled-attribute review. Renaming the existing DLL to `glide3x.dll`
would therefore be an incorrect support claim.

The donor G2X wrapper Makefile defaults to `-march=x86-64-v2`; that build recipe is
not suitable for DreamGPU's 32-bit legacy guests. A future wrapper must use the
existing audited Cargo/CMake toolchain and retain its original notices and source
provenance. These are separate compatibility extensions, not prerequisites for
activating the tested Glide2x provider.

`tools/glide/probe.cpp` is shared by diagnostic `DGGLIDE.EXE` and ordinary-system
`DGSYSGR.EXE`. The system variant rejects neighboring providers, verifies loaded
system paths, then checks 2,304 pixels, eight swaps and context recreation. Its
results establish those tested behaviors only; they do not establish all Glide
versions or every game's compatibility.
