# Win98 VMM TLB repair

The Framework KVM guest reached Explorer but failed to load COMDLG32 before
Half-Life started. The same guest files loaded on Mac TCG. Spoofing Pentium III
CPUID did not repair the failure. The pinned [patcher9x source](https://github.com/JHRobotics/patcher9x)
repairs Win98 VMM TLB invalidation on modern physical processors.

On our private image the effective VMM is the uncompressed override
`WINDOWS/SYSTEM/VMM32/VMM.VXD`, not just the bundled `VMM32.VXD`. Preserve that
original file and every source disk. Do not replace unrelated Windows DLLs.

The source pin is in `../sources.lock.json`. Build on the Linux host:

```sh
git clone --recurse-submodules https://github.com/JHRobotics/patcher9x.git target/retro-sources/patcher9x
git -C target/retro-sources/patcher9x checkout b6e30d4b5a396dcd453b6c8e6733fd5b5cbce59e
git -C target/retro-sources/patcher9x submodule update --init --recursive
make -C target/retro-sources/patcher9x RELEASE=1 FASM=/absolute/path/to/fasm -j2
python3 scripts/diagnostics/win98-tlb.py --input-vmm /private/extracted/VMM.VXD --output target/retro-evidence/new-tlb-copy
```

The validated build used FASM 1.73.35 from Debian's
`fasm_1.73.35-2_amd64.deb`, SHA256
`33781b8f9954d3289bc781fd56f8f42f3608cb7fed589b38f8967bdbada608ca`.
Its 32-bit assembler needs the host 32-bit glibc runtime; the native static
patcher build also needs static glibc development files. No prebuilt patcher
is accepted as the source provenance.

The helper accepts only the exact validated original VMM SHA256 and verifies
its patched hash, unchanged 472,564-byte size, and exactly 58 changed bytes.
It retains original bytes, patch output, source revision, binary hashes and
command status in a new private directory. Install `VMM.VXD` into an offline
disposable disk copy at the same override path; leave original source images
and snapshots intact. Proprietary Windows bytes stay under ignored `target/`.

Linux actual evidence: `target/retro-runs/linux-win98-tlb-v1`. With KVM host CPU
retained and this patch, all 13 original retail Half-Life imports, including
COMDLG32, load successfully. The 64KiB public GL/window oracle passes. Cold
startup reaches serial READY without keys in 39.24 seconds using portable18's
bounded COM1 ownership retry. The optional MPRUI network UI diagnostic still
fails to load; it is not a Half-Life import and is retained separately rather
than treated as a graphics failure. Final audio, game, and cold-source adoption
checks remain separate gates.
