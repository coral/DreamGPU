# Generated attribution records

[../../ATTRIBUTION.md](../../ATTRIBUTION.md) is the reviewed human-readable license
and provenance summary. These JSON files preserve factual dependency metadata;
they do not grant rights or calculate legal compatibility.

Regenerate from the repository root with Python 3.11 or later:

```sh
python3 tools/licensing/inventory.py
```

This optional documentation tool calls `cargo metadata --locked --all-features
--format-version 1`; it is not a build dependency. Cargo may populate its download
cache. `--metadata FILE` accepts a previously captured metadata result from the
same checkout/lockfile. All eight top-level source dependencies should be
initialized to collect their notices; absent nested repositories remain marked
uninitialized, without invented license evidence.

- `cargo.json`: all-target/all-feature normal, build and development dependency
  superset (267 external packages at this update). Includes direct workspace
  declarations, transitive edges, raw declared license expressions, repository
  URLs, authors, registry checksums, available VCS pins and collected notices.
- `cargo-native-launcher.json`: the separate locked native-launcher Cargo graph,
  with the same package, dependency, notice and checksum metadata. Use
  `python3 tools/licensing/inventory.py --launcher-only` to refresh just this graph.
- `repositories.json`: parent gitlinks, observed checkout revisions, repository
  URLs, initialization state, root notice hashes and every QEMU Meson wrap.
  A declared or initialized dependency is not necessarily compiled or shipped.
- `notices/<sha256>`: exact collected root license/notice bytes, deduplicated by
  SHA-256. Each JSON `notice_files` map associates original relative filename
  with the corresponding content file. These retain upstream terms, not the
  DreamGPU default license. Source headers and nested notices may impose
  additional terms; their absence here does not imply their absence upstream.

Cargo metadata covers this workspace, not QEMU's separate optional Rust/Meson
graph, OS packages, firmware binaries or compiler runtime internals. Root license
files alone are not a per-file license audit. Keep the selected release's source,
patches, build receipts and full applicable notices. Refresh this inventory after
source-pin or lockfile changes; immutable historical receipts should stay intact.

Mesa is a pinned source archive, not one of the eight gitlinks. Its exact archive
and per-file patch identities live in `../native/mesa/source.json`; native runtime
receipts retain the source license tree and copied platform-library inventory.
