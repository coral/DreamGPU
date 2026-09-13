# Source-test support

`patches.py` and `wine_prepare.py` construct exact pinned source variants for
host sanitizer and provenance tests, including optional diagnostic patches.
They are test utilities, not production build entry points.

Cargo uses `crates/dreamgpu-build/src/prepare.rs` to prepare production sources
and CMake to compile them. Both paths validate every patch and every recorded
input/output hash before publishing changed source files.
