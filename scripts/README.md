# Runtime automation and validation

Cargo owns builds and guest packaging. These Python tools prepare private
fixtures, drive bounded guest commands, measure performance and inspect evidence.
They are not build dependencies.

| Directory | Responsibility | Main commands |
| --- | --- | --- |
| `automation/` | Shared QMP, host input and compositor control | `vm.py`; supporting `host_input.py`, `kwin.py` |
| `fixtures/` | Disposable fixtures, offline disk updates and installation media | `fixture.py`, `adopt.py`, `app-install.py`, `nt-install.py`, `tool-media.py`, `ut-media.py`, `win9x-stage.py` |
| `benchmarks/` | Timed game runs, frame measurements and process sampling | `bench.py`, `game.py`, `halflife.py`, `sample.py` |
| `diagnostics/` | Graphics traces, counters, lifecycle and Win98 prerequisite checks | `gl-trace.py`, `trace.py`, `lifecycle.py`, `dual.py`, `cursor-check.py`, `win98-probe.py` |
| `quality/` | Source formatting, static analysis and native numerical checks | `check-cpp.py`, `test-softfloat-f32-x80.py` |
| `tests/` | Host automation unit tests and the native numerical test source | `test_*.py`, `softfloat-f32-x80.c` |

Commands remain directly executable by path from any working directory:

```sh
python3 /path/to/dreamgpu/scripts/fixtures/fixture.py --help
python3 /path/to/dreamgpu/scripts/benchmarks/game.py --help
python3 /path/to/dreamgpu/scripts/quality/check-cpp.py --help
```

Shared implementation imports use the `scripts.<directory>` package namespace.
Each directly executable module adds its checkout root only when Python has not
loaded it as a package. There are no forwarding files at the old script paths.
Copy the entire `scripts/` tree when freezing a source-independent controller;
individual entry points depend on modules in neighboring directories.

Run the automation tests from the checkout root:

```sh
python3 -m unittest discover -s scripts/tests -p 'test_*.py'
python3 -m unittest discover -s tests -p 'test_*.py'
```

Use fixed, bounded serial/QMP commands and process-owned cleanup. Avoid menu
click/screenshot/wait loops. Save artifact identities, settings and outcomes in
machine-readable records; repeat successful benchmarks only after a relevant
change or to investigate a failure or evidence gap.

Audio diagnostics belong to the consuming application and live in Juke.
Original OS/game media, keys, fixture disks, generated builds and profiling dumps
remain ignored local artifacts.
