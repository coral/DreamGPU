// SPDX-License-Identifier: GPL-2.0-or-later
//! Native artifact discovery for the exact checkout selected by Cargo.
use std::path::{Path, PathBuf};

/// The SDK source checkout, independent of the application's working directory.
pub fn source_root() -> &'static Path {
    Path::new(env!("CARGO_MANIFEST_DIR"))
}

/// Resolve signed and unsigned build artifacts without searching system PATH.
pub fn binary_in(directory: &Path, name: &str) -> PathBuf {
    let binary = directory.join(name);
    if binary.is_file() {
        return binary;
    }
    let unsigned = directory.join(format!("{name}-unsigned"));
    if unsigned.is_file() { unsigned } else { binary }
}

/// Native output selected when Cargo built this SDK.
pub fn build_directory() -> PathBuf {
    option_env!("DREAMGPU_NATIVE_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|| source_root().join("target/qemu-build"))
}
