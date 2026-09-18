// SPDX-License-Identifier: GPL-2.0-or-later
//! Native and legacy guest builds, owned and sequenced by Cargo.
use anyhow::{Context, Result};
use sha2::{Digest, Sha256};
use std::{
    env, fs,
    io::Read,
    path::{Path, PathBuf},
    process::{Command, Stdio},
};
pub mod guest;
mod inputs;
pub mod installer;
pub mod mesa;
pub mod native;
pub mod native_runtime;
pub mod package_metadata;
pub mod prepare;

pub fn run(command: &mut Command) -> Result<()> {
    eprintln!("DreamGPU: {command:?}");
    let status = command
        .stdout(Stdio::inherit())
        .stderr(Stdio::inherit())
        .status()
        .with_context(|| format!("start {command:?}"))?;
    anyhow::ensure!(status.success(), "command failed ({status}): {command:?}");
    Ok(())
}
pub fn capture(command: &mut Command) -> Result<String> {
    let result = command
        .stderr(Stdio::inherit())
        .output()
        .with_context(|| format!("start {command:?}"))?;
    anyhow::ensure!(result.status.success(), "command failed: {command:?}");
    Ok(String::from_utf8(result.stdout)?.trim().to_owned())
}
pub fn digest(path: &Path) -> Result<String> {
    let mut file = fs::File::open(path).with_context(|| format!("read {}", path.display()))?;
    let mut hasher = Sha256::new();
    let mut buffer = [0u8; 65536];
    loop {
        let size = file.read(&mut buffer)?;
        if size == 0 {
            break;
        }
        hasher.update(&buffer[..size]);
    }
    Ok(format!("{:x}", hasher.finalize()))
}
pub fn copy_tree(source: &Path, destination: &Path) -> Result<()> {
    fs::create_dir_all(destination)?;
    for item in fs::read_dir(source)? {
        let item = item?;
        if item.file_name() == ".git" || item.file_name() == "__pycache__" {
            continue;
        }
        let target = destination.join(item.file_name());
        let kind = item.file_type()?;
        if kind.is_dir() {
            copy_tree(&item.path(), &target)?;
        } else if kind.is_symlink() {
            #[cfg(unix)]
            std::os::unix::fs::symlink(fs::read_link(item.path())?, &target)?;
            #[cfg(not(unix))]
            anyhow::bail!("source symlinks require a Unix build host");
        } else {
            fs::copy(item.path(), &target)?;
        }
    }
    Ok(())
}
pub fn write_json(path: &Path, value: &serde_json::Value) -> Result<()> {
    let temporary = path.with_extension("json.tmp");
    fs::write(
        &temporary,
        format!("{}\n", serde_json::to_string_pretty(value)?),
    )?;
    fs::rename(temporary, path)?;
    Ok(())
}
pub fn jobs() -> usize {
    env::var("NUM_JOBS")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or_else(|| std::thread::available_parallelism().map_or(1, usize::from))
        .max(1)
}
pub fn build(root: &Path) -> Result<()> {
    for name in [
        "DREAMGPU_BUILD",
        "DREAMGPU_OUTPUT",
        "DREAMGPU_QEMU_TARGETS",
        "DREAMGPU_NATIVE_DEBUG",
        "DREAMGPU_GUEST_HOST",
        "DREAMGPU_GUEST_ROOT",
        "DREAMGPU_CROSS_COMPILE",
        "DREAMGPU_WATCOM_ROOT",
        "DREAMGPU_TOOL_CACHE",
        "DREAMGPU_FORCE_CONFIGURE",
        "CC",
        "CXX",
        "CFLAGS",
        "CXXFLAGS",
        "LDFLAGS",
        "PKG_CONFIG_PATH",
        "RUSTC",
    ] {
        println!("cargo::rerun-if-env-changed={name}");
    }
    let selection = env::var("DREAMGPU_BUILD").unwrap_or_else(|_| "native".into());
    anyhow::ensure!(
        ["all", "native", "guest", "sdk"].contains(&selection.as_str()),
        "DREAMGPU_BUILD must be all, native, guest, or sdk"
    );
    let output = env::var_os("DREAMGPU_OUTPUT")
        .map(PathBuf::from)
        .unwrap_or_else(|| root.join("target"));
    let output = if output.is_absolute() {
        output
    } else {
        root.join(output)
    };
    fs::create_dir_all(&output)?;
    println!(
        "cargo::rustc-env=DREAMGPU_NATIVE_DIR={}",
        native::launch_directory(&output.join("qemu-build")).display()
    );
    if selection == "native" || selection == "all" {
        for path in [
            "crates/dreamgpu-host",
            "include/dreamgpu",
            "support/native",
        ] {
            println!("cargo::rerun-if-changed={}", root.join(path).display());
        }
        native::build(root, &output.join("qemu-build"))?;
        inputs::watch_git_sources(&root.join("vendor/qemu"))?;
        println!(
            "cargo::metadata=native_dir={}",
            native::launch_directory(&output.join("qemu-build")).display()
        );
    }
    if selection == "guest" || selection == "all" {
        for path in [
            "guest",
            "icons",
            "tools",
            "support/guest",
            "support/attribution",
            "LICENSE",
            "LICENSES",
            "ATTRIBUTION.md",
            ".gitmodules",
            "vendor/vmdisp9x",
            "vendor/wine9x",
            "vendor/qemu-xtra",
            "vendor/wine-glu",
        ] {
            println!("cargo::rerun-if-changed={}", root.join(path).display());
        }
        guest::build(root, &output.join("guest"))?;
        println!(
            "cargo::metadata=guest_dir={}",
            output.join("guest/packages").display()
        );
    }
    Ok(())
}
