# Fixed Win98 DreamGPU installation

`driver32.cpp` builds two C++23 fixture controls. They use the actual Windows 98 SetupAPI V1 ABI, without exceptions, RTTI, a guest CRT, or direct registry replacement.

```sh
DREAMGPU_BUILD=guest cargo build --release
```

Cargo and CMake include these controls in `target/guest/packages/win98/tools/`.
The build checks PE/NE/LE structure and runtime import allowlists. Ordinary
source builds require no Windows media. Original Windows DLLs remain private
inputs to any additional OS export audit; they are never package contents.

Put `DG9INST.EXE`, `DG9X.INF`, `DGPUMINI.DRV`, and `DGPUMINI.VXD` together on fixture drive E:. The runner's fixed `win9xinstall` command invokes that installer. It selects exactly one present `PCI\VEN_1234&DEV_1113` devnode, verifies the adjacent INF's provider, section, and hardware ID, and requests a quiet `DIF_INSTALLDEVICE` installation. It reads back the selected adapter's `DEFAULT` driver pair before reporting success. Win98's HardwareID property is a legacy REG_SZ with unreliable RequiredSize; identity selection uses bounded `CM_Get_Device_IDA`, not NT registry-property packing.

Keep the original Windows 98 installation CD on D: while completing device installation. Cleanly power off the disposable fixture and cold boot an independent copy. The fixed `win9xdiag` command runs `C:\SIERRA\Half-Life\DG9AUDIT.EXE`; it requires a started devnode with no problem code, the renamed driver pair, an open loaded `\\.\DREAMGPU` VxD, and a successful versioned channel OPEN/CLOSE transaction. An installation log alone is not activation evidence.

`scripts/fixtures/win9x-stage.py` prepares the initial stopped, hash-pinned FAT32 source copy. Its manifest permits explicit app/tool files outside `WINDOWS` and one checked startup migration in WIN.INI. It never installs drivers, edits device registry keys, or modifies the original source. Every staged file and startup value is read back exactly before producing a fresh qcow2 image. Existing live or paused fixtures are never staging sources.

The named C++ driver candidate passed this cold activation on Linux and macOS. Linux evidence is retained privately under `target/validation/linux-dgpu-win98-v1`; graphical correctness and game acceptance remain separate recorded gates.

After OS driver setup finishes, mount the original retail Half-Life CUE on D: for Half-Life tests. Keeping the Windows setup CD there leaves retail build742 waiting in its custom launcher prompt before loading `hw.dll`. Its later timeout cleanup can produce a secondary null-call fault. The shared runner now checks the `HALF_LIFE` volume and the original `SIERRA.INF` / `DATA1.CAB` markers before launching. This is a media prerequisite, not a renderer fault. Preserve the game CUE path in the stopped fixture's configuration for its cold successors.
