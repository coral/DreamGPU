# Private Linux OpenGL provider

Cargo pins the complete Mesa release in `source.json`, checks every patch and
before/after source identity, and builds upstream Meson into the native output
tree. No system Mesa installation is modified.

The patches modify Mesa's own implementation: integer pixel-transfer parameters
avoid a lossy float conversion, and GPU Draw/CopyPixels retain fractional raster
origins. Original file copyright and license headers remain in the verified
archive. The patches are DreamGPU modifications to those named upstream files;
they do not replace their licenses.

Source: https://archive.mesa3d.org/mesa-26.1.8.tar.xz, release 26.1.8; exact SHA256
and each modified path are in `source.json`. Mesa's full license documentation
and source archive accompany the runtime provenance. Build dependencies and
libraries copied from the build host are recorded separately by exact identity.

The changed provider passed the preserved extreme INDEX_OFFSET and fractional
negative PixelZoom GPU oracles through DreamGPU, plus ordinary image IPC and
release-slot checks. This fixes those two provider failures. Legacy 1D texture
borders remain unresolved; no border dimensions or pixels are fabricated.
