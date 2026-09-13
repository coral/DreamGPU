# Installer foundation

Cargo packages one `dreamgpu.exe` containing both existing guest packages. It
selects Windows98 or NT5 (2000/XP), requires exactly one present PCI1234:1113
adapter, validates every selected embedded payload SHA256 before filesystem
changes, and has no dynamic CRT or C++ exception dependency.

System-provider integration is unfinished. Normal execution returns exit30,
`system_provider_not_ready`, without changing disk or registry. This is not yet
a completed system GPU installer. The diagnostic `/stage` operation creates a
fresh `%WINDIR%\DGSETUP.NEW`, records PREPARE.json, writes/read-verifies files,
then renames to `%WINDIR%\DreamGPU` only after RESULT.json is durable. Existing
destinations are never adopted or overwritten. Stage exit10 means staging only.

`/silent` suppresses UI; JSON is emitted to inherited stdout and debug output,
with an explicit process exit code. Staged receipts also live inside the owned
directory. Exit20 unsupported OS,21 missing/ambiguous device,22 invalid payload,
23 arguments,24 occupied/unavailable destination,25 failed staging rolled back,
26 incomplete rollback,30 unavailable system provider. No code reports installed
success. A crash leaves its fresh staging directory for explicit recovery; a
later run refuses it. Initial private-preparation cleanup and actual system-provider activation remain
unfinished; journalled lifecycle operations are described below.

The modern owned logic is C++23 with Win32 RAII. Current atomic ownership tests
exercise the actual transaction template and SHA256 vectors under ASan/UBSan.
The future driver/ICD activation transaction must retain this preflight and
receipt model; it must not replace NT protected system DLLs.

## Journalled lifecycle

`lifecycle.h` owns a fixed, pointer-free v1 journal: file, registry-key and
registry-value operations record immediate-before, desired and first-install
original identities. Each phase is appended with SHA256 and flushed before its
mutation. Recovery validates records and trims only an incomplete final record;
corrupt complete records are rejected. The journal has a16MiB limit.

`win32-lifecycle.h` supplies the actual syscall adapter. Before backups and
restored temporary copies are flushed/read-verified. Windows98-compatible file
replacement records the intent, moves the original into an owned private slot,
then publishes the verified new file. A journalled rename gap is reconciled
only with its exact backup. Current bytes that disagree with both known states
are an ownership conflict. A named installer mutex prevents competing installer
processes. This is process-interruption recovery using Windows file APIs, not a
claim of filesystem atomicity under host power loss or concurrent hostile edits.

`/stage` now also captures the current shared-library originals into its private
journal generation. It does not change the system libraries. `/rollback`
reconciles and restores a generation; `/continue` resumes recorded installation,
removal, or rollback work; `/upgrade` inherits first-install originals only when
current files equal the previous owned result; `/uninstall` creates a reverse
transaction and captures its own immediate-before backup for rollback. Native
files that already equalled the requested bytes before initial installation are
borrowed, never newly owned. Backups/receipts remain available as evidence.

Global plans name `dgpugl.dll`, `glide2x.dll`, `wined3d.dll`, `winedd.dll`,
`wined8.dll`, and `wined9.dll` in the OS system directory. The separate ICD hook
uses a typed NT DGPUICD key plus four values, or the Win98 named value. Its
production-readiness flag is false; the diagnostic ICD is not registered.
Microsoft `opengl32.dll`, `ddraw.dll`, `d3d8.dll` and `d3d9.dll` are absent from
the adapter's allowed destinations. A completed mutation list cannot produce
`activated`: the real activation verifier still deliberately returns false.

Additional receipt exits are11 pending_reboot,12 rolled_back,13 removed,
27 staged_journal_failed,28 installer_busy,29 ownership_conflict. Exit0 is
reserved for independently verified activation, currently unreachable because
provider readiness and activation verification remain false. Default execution
still fails before writes. Upgrade is disabled until providers are ready.

## Remaining provider work

The existing Win98 `tools/win9x/driver32.cpp` already selects exactly one present
PCI1234:1113 node, verifies adjacent DG9X.INF's provider/section/hardware identity,
uses V1 SetupAPI structures and DIF_INSTALLDEVICE, then audits the selected driver
pair. Reuse that checked implementation as a library or extract its existing
helper alongside the INF in the private driver directory. The OS binding change
needs its own saved previous-driver identity and reboot verification; a child
process exit alone is insufficient. The NT equivalent is `tools/nt/install.cpp`
using dynamically resolved newdev; do not import that NT API into the PE4
bootstrap. Neither is activated by this lifecycle foundation yet.

Still required: owned driver-binding rollback, production ICD/capability proof,
NT5 system Direct3D HAL/integration, activation verifier, checked automatic reboot
continuation registration, recovery/cleanup of interrupted initial private
preparation, and removal of archived private payloads after verified uninstall.
A pending journal can be resumed explicitly without a busy retry loop. These
limitations are explicit; no system-install success is inferred from staging.

Host tests compile the actual policy, engine and Win32 adapter under ASan/UBSan.
The adapter syscall seam injects failures into capture, journal append/flush,
copy/rename, registry mutation and rollback, and verifies exact original bytes,
foreign-edit refusal, key ownership and uninstall's own rollback backup. No
mock duplicates the ordering or reconciliation policy.

The optional `dreamgpu-setup-verify` CMake target requires
`DREAMGPU_SETUP_VERIFY_HASH` naming one audited installer SHA256. Its fixed
`DGSETTST.EXE` helper runs only in an independent Windows 98/2000/XP fixture: default
execution must refuse activation without private state, `/stage` must preserve
six global originals in its journal, and `/rollback` must leave all fourteen
observed graphics files unchanged, including the Win98 display-driver pair.
It checks the Win98 ICD value or the four NT ICD values according to the detected
OS. Supported helper targets are not a claim of completed runtime acceptance on
each OS. The serial `setupcheck`
route collects `C:\DGSETTST.LOG`; it never enables providers or claims system
installation. This targeted helper is separate from the normal installer.

The combined installer links CfgMgr32 before SetupAPI. MinGW's SetupAPI import
library also offers `CM_Get_Device_IDA`, but Windows 98's actual SetupAPI DLL
does not export it. The Cargo artifact audit requires this function to resolve
from CfgMgr32; checking DLL names alone would miss this loader incompatibility.
