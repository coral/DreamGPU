// SPDX-License-Identifier: GPL-2.0-or-later
//! Authenticated process-private native runtime. The parent environment is never changed.
use sha2::{Digest, Sha256};
use std::os::unix::{fs::PermissionsExt, process::CommandExt};
use std::{
    env, fs,
    io::{self, Read},
    path::{Component, Path, PathBuf},
    process::Command,
};

const MANIFEST: &str = include_str!(env!("DREAMGPU_RUNTIME_MANIFEST"));
const FALLBACK: &str = env!("DREAMGPU_RUNTIME_DIRECTORY");
const PROGRAM: &str = env!("DREAMGPU_RUNTIME_PROGRAM");

fn safe(path: &str) -> bool {
    !path.is_empty()
        && Path::new(path)
            .components()
            .all(|p| matches!(p, Component::Normal(_)))
}
fn error(message: impl Into<String>) -> io::Error {
    io::Error::other(message.into())
}
fn hash(path: &Path) -> io::Result<String> {
    let mut input = fs::File::open(path)?;
    let mut digest = Sha256::new();
    let mut buffer = [0u8; 131072];
    loop {
        let count = input.read(&mut buffer)?;
        if count == 0 {
            break;
        }
        digest.update(&buffer[..count]);
    }
    Ok(format!("{:x}", digest.finalize()))
}
fn verify(directory: &Path, manifest: &serde_json::Value) -> io::Result<PathBuf> {
    let directory = directory.canonicalize()?;
    if fs::read(directory.join("manifest.json"))? != MANIFEST.as_bytes() {
        return Err(error("runtime sidecar differs from embedded manifest"));
    }
    for (path, record) in manifest["platform_files"]
        .as_object()
        .ok_or_else(|| error("invalid platform inventory"))?
    {
        if hash(Path::new(path))? != record["sha256"] {
            return Err(error(format!("platform identity changed: {path}")));
        }
    }
    let files = manifest["files"]
        .as_object()
        .ok_or_else(|| error("invalid runtime files"))?;
    for (name, record) in files {
        if !safe(name) {
            return Err(error("unsafe runtime member"));
        }
        let path = directory.join(name);
        let metadata = fs::symlink_metadata(&path)?;
        if !metadata.is_file()
            || !path.canonicalize()?.starts_with(&directory)
            || Some(metadata.len()) != record["size"].as_u64()
            || hash(&path)? != record["sha256"]
            || Some(u64::from(metadata.permissions().mode() & 0o777)) != record["mode"].as_u64()
        {
            return Err(error(format!("runtime identity mismatch: {name}")));
        }
    }
    let name = format!("programs/{PROGRAM}");
    if !safe(PROGRAM) || !files.contains_key(&name) {
        return Err(error("unrecorded native program"));
    }
    Ok(directory)
}
fn main() {
    if let Err(error) = launch() {
        eprintln!("DreamGPU native runtime: {error}");
        std::process::exit(126);
    }
}
fn launch() -> io::Result<()> {
    let manifest: serde_json::Value =
        serde_json::from_str(MANIFEST).map_err(|e| error(e.to_string()))?;
    let digest = format!("{:x}", Sha256::digest(MANIFEST.as_bytes()));
    let exe = env::current_exe()?;
    let adjacent = exe
        .parent()
        .and_then(Path::parent)
        .ok_or_else(|| error("launcher has no parent"))?
        .join("runtime")
        .join(&digest);
    // A present but changed adjacent package must fail; never silently fall back.
    let directory = if adjacent.try_exists()? {
        adjacent
    } else {
        PathBuf::from(FALLBACK)
    };
    let directory = verify(&directory, &manifest)?;
    let program = directory.join("programs").join(PROGRAM);
    if env::args_os().nth(1).as_deref() == Some(std::ffi::OsStr::new("--dreamgpu-runtime")) {
        println!(
            "{}",
            serde_json::json!({"schema":1,"manifest_sha256":digest,"directory":directory,"program":program,"program_sha256":manifest["files"][format!("programs/{PROGRAM}")]["sha256"]})
        );
        return Ok(());
    }
    let mut child = Command::new(&program);
    if PROGRAM.starts_with("qemu-system-") && manifest["firmware"] == true {
        child.arg("-L").arg(directory.join("firmware"));
    }
    child.args(env::args_os().skip(1));
    child
        .env("LD_LIBRARY_PATH", directory.join("lib"))
        .env("LIBGL_DRIVERS_PATH", directory.join("dri"))
        .env("GBM_BACKENDS_PATH", directory.join("gbm"))
        .env("__EGL_VENDOR_LIBRARY_DIRS", directory.join("egl"));
    // Keep GLVND's vendor selection, including installed proprietary vendors.
    // A caller's old override must not bypass this authenticated Mesa package.
    for key in [
        "LD_PRELOAD",
        "LD_AUDIT",
        "__EGL_VENDOR_LIBRARY_FILENAMES",
        "__GLX_VENDOR_LIBRARY_NAME",
        "MESA_LOADER_DRIVER_OVERRIDE",
        "GALLIUM_DRIVER",
        "LIBGL_ALWAYS_SOFTWARE",
    ] {
        child.env_remove(key);
    }
    Err(child.exec())
}
