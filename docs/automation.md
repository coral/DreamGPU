# Automation ownership

DreamGPU owns the serial/QMP protocol, graphics probes, timedemos, sampling and
result capture in `scripts/`. Source for the programs those controllers launch
inside Windows lives under [`tools/`](../tools/README.md): the fixed serial
runner/installer, graphics probes and game setup/run helpers. Runtime drivers
and API implementations live separately under [`guest/`](../guest/README.md).
The existing Juke application control adapter remains
in the fixture/game/lifecycle tools: callers supply its executable and fixture
manifest explicitly. This adapter does not participate in SDK/native builds.

Use absolute source/artifact paths when importing a Juke fixture manifest into
DreamGPU; historical relative paths remain relative to their original repo.
Do not reinterpret a historical manifest by merely moving it.

The standalone viewer and SDK tests operate without Juke. Full application
lifecycle tests deliberately use Juke, including its prepared-canvas capture
and window/VM controls. Use matched current host/guest packages: the namespace
migration changed textual trace, DLL and service names while preserving numeric
wire contracts.

Keep historical result ledgers in Juke. New runs should record DreamGPU and Juke
source identities together. No benchmark is run simply because a file moved.

## Preparing an installed Windows guest

Juke's UTM importer accepts both legacy `Drives`/`Images` bundles and current
`Drive`/`Data` bundles. Import into a private machine directory, preserve the
original bundle, and record both disk hashes. An imported disk is only an OS
fixture: install the matching DreamGPU package and control runner before calling
it a graphics acceptance fixture.

`scripts/fixtures/nt-install.py` stages explicitly listed files and optional directories
into an independent raw NTFS copy on Linux. It verifies source and installed-file
hashes and unmounts before producing the successor qcow2. Never stage onto a
running VM's disk. A driver installation requires a cold boot; a saved RAM
snapshot can retain the previous driver even after files have changed.

Fixture source configurations need valid `resources/fonts` and absolute local
optical-media paths. Prepare a new output directory for each candidate:

```sh
python3 scripts/fixtures/fixture.py prepare /absolute/fixture-manifest.json --output target/fixtures/candidate
python3 scripts/fixtures/fixture.py start target/fixtures/candidate --timeout 120
```

Copied QEMU executables can use an explicit, hash-pinned firmware directory in
that manifest:

```json
"firmware": {
  "path": "/absolute/qemu/pc-bios",
  "files": { "bios-256k.bin": "<lowercase SHA256 of this file>" }
}
```

List every firmware file the configured machine needs. Entries must be regular
files with safe basenames; symlink members are rejected. Preparation and startup
both verify the recorded hashes, and the generated QEMU wrapper receives `-L`
with the canonical directory. Missing or changed firmware fails before process
launch. Omitting `firmware` retains the executable's normal firmware lookup.

Startup checks exact artifact hashes, stops QEMU before guest execution, disables
the guest network link, and waits for the expected serial runner identity. The
fixture's `run.json` records its socket endpoints and owned process ID. It also
records `preparation_controller` and `startup_controller` hashes for the four
core fixture/control modules. A controller fix between preparation and startup
can be legitimate; preserve both identities so a harness change is not mistaken
for a guest or native GPU change. Keep failed starts as separate evidence.

Update the executable Windows actually starts, not just a similarly named root
copy. The accepted XP fixture starts
`C:\Documents and Settings\All Users\Start Menu\Programs\Startup\DGPUBEN.EXE`.
Updating only `C:\DGPUBEN.EXE` leaves the older controller holding COM1; launching
another copy then fails with access denied. Name the startup destination and
expected hash explicitly in the offline installation manifest. Readiness timeout
errors retain the last observed controller identity, distinguishing a stale
controller from no serial response. `C:\DGPUBEN.LOG` records serial-open failures.

## Running fixed workloads

Use the recorded serial socket for pixel, resource and desktop probes:

```sh
python3 scripts/benchmarks/halflife.py probe d3d9 --socket /tmp/recorded-serial.sock --output target/results/d3d9
python3 scripts/benchmarks/halflife.py run jrgperf --socket /tmp/recorded-serial.sock --output target/results/halflife --timeout 150
python3 scripts/benchmarks/game.py utd3d --fixture target/fixtures/candidate --output target/results/utd3d
python3 scripts/benchmarks/game.py utglide --fixture target/fixtures/candidate --output target/results/utglide
```

Retail Half-Life needs its original CD mounted on D:. The current runner checks
the disc before launching; the absence of a GL trace can otherwise be a hidden
media prompt. Unreal installation uses the fixed `utsetup` and `utdsetup` probes
with a 500-second setup deadline and the recorded original-media ISO on E:.
Game launch, readiness, provider checks, capture and cleanup use request-matched
serial messages. Screenshots are output evidence, never a click-navigation loop.

Preserve each failed attempt. Inspect its structured guest error, owned engine
log and native counters before changing code. A passing unchanged gate is reused.
Half-Life's reported engine FPS and Unreal's GPU receipt counts are different
measurements; receipt counts do not establish game FPS or input-to-display latency.
Run performance captures without concurrent builds or other active VM workloads.

Use `scripts/fixtures/fixture.py stop` for owned process cleanup. It does not by itself
prove Windows shut down cleanly; record an observed guest poweroff separately
before using a disk as a clean installation source.

## Sampling a diagnosed slowdown

On Linux, the current serial runner creates Half-Life suspended. With sampling
requested, the controller requires perf's enable acknowledgment before sending
`CONTINUE`; at `TIMEDEMO_RESULT` it disables sampling before acknowledging guest
cleanup. A missing boundary, failed profiler, or empty recording cannot claim
coverage. This needs `perf`, GNU `timeout`, and noninteractive permission to run
the bounded profiler through `sudo -n`:

```sh
python3 scripts/benchmarks/halflife.py run jrgperf \
  --socket /tmp/recorded-serial.sock \
  --sample-fixture target/fixtures/candidate/run.json \
  --output target/results/halflife-sampled --timeout 150
```

`run.json` records the control acknowledgments and, after guest cleanup, the
actual captured event count and first/last monotonic timestamps from `perf.data`.
The enabled interval includes launch, loading and console visibility delay.
Retail Half-Life exposes no timestamped first/last timedemo frames, so the exact
engine interval remains **unknown**. Recorded CPU events are not frame times.
The profiler uses software `cpu-clock` events at 499 Hz with DWARF stacks;
instrumentation affects performance and must match across compared captures.

For Unreal on macOS or Linux:

```sh
python3 scripts/benchmarks/game.py utd3d --sample \
  --fixture target/fixtures/candidate --output target/results/utd3d-sampled
```

This takes a bounded two-second sample of the exact fixture-owned QEMU inside
host-observed `MEASURING`/`MEASURED`. It is an interior rendering sample, not
coverage of the entire game or an exact engine-frame interval. `sample.json`
records tool start/exit times, recording size and containment; failed, unfinished
or empty requested samples fail that diagnostic run. Neither sampling command
builds binaries, starts a fixture, or repeats a workload automatically.

For viewport, drawable, or texture-size failures, see the opt-in
[native geometry diagnostics](geometry-diagnostics.md). Their trace overhead and
collection limits are separate from the normal graphics acceptance gates.
