// SPDX-License-Identifier: GPL-2.0-or-later
use crate::{capture, digest, jobs, run, write_json};
use anyhow::{Context, Result};
use serde_json::json;
use sha2::{Digest, Sha256};
use std::{env, fs, path::Path, process::Command};

pub fn launch_directory(output: &Path) -> std::path::PathBuf {
    if cfg!(target_os = "linux") {
        output.join("bin")
    } else {
        output.to_owned()
    }
}
pub fn build(root: &Path, output: &Path) -> Result<()> {
    let source = root.join("vendor/qemu");
    if !source.join("configure").is_file() {
        run(Command::new("git").arg("-C").arg(root).args([
            "submodule",
            "update",
            "--init",
            "--recursive",
            "--",
            "vendor/qemu",
        ]))?;
    }
    anyhow::ensure!(
        source.join("hw/display/dreamgpu.c").is_file(),
        "QEMU submodule lacks DreamGPU device"
    );
    for name in ["transport.h", "cursor.h"] {
        anyhow::ensure!(
            fs::read(root.join("include/dreamgpu").join(name))?
                == fs::read(source.join("include/standard-headers/dreamgpu").join(name))?,
            "SDK and QEMU {name} differ"
        );
    }
    fs::create_dir_all(output)?;
    let targets = env::var("DREAMGPU_QEMU_TARGETS")
        .unwrap_or_else(|_| "x86_64-softmmu,i386-softmmu,ppc-softmmu,m68k-softmmu".into());
    anyhow::ensure!(
        targets.split(',').all(|t| [
            "x86_64-softmmu",
            "i386-softmmu",
            "ppc-softmmu",
            "m68k-softmmu"
        ]
        .contains(&t)),
        "unsupported QEMU target list: {targets}"
    );
    let rustc = env::var_os("RUSTC").unwrap_or_else(|| "rustc".into());
    let rustc_path = capture(Command::new("which").arg(&rustc))?;
    let rustc_version = capture(Command::new(&rustc_path).arg("--version"))?;
    let mut args = vec![
        format!("--target-list={targets}"),
        format!("-Ddreamgpu_rustc={rustc_path}"),
    ];
    args.extend(
        [
            "--enable-slirp",
            "--enable-libusb",
            "--disable-werror",
            "--disable-docs",
            "--disable-guest-agent",
            "--disable-spice",
            "--disable-spice-protocol",
        ]
        .map(str::to_owned),
    );
    let host = env::consts::OS;
    let platform_args: &[&str] = match host {
        "macos" => &[
            "--disable-opengl",
            "--enable-hvf",
            "--enable-coreaudio",
            "--enable-vmnet",
            "--enable-cocoa",
            "--disable-fuse",
            "--disable-fuse-lseek",
        ],
        "linux" => &[
            "--enable-kvm",
            "--enable-opengl",
            "--enable-virglrenderer",
            "--enable-gtk",
            "--enable-pipewire",
            "--enable-alsa",
            "--enable-virtfs",
        ],
        _ => anyhow::bail!("DreamGPU native builds require macOS or Linux"),
    };
    args.extend(platform_args.iter().map(|s| (*s).to_owned()));
    if env::var_os("DREAMGPU_NATIVE_DEBUG").is_some() {
        args.extend(["--enable-debug-info".into(), "--disable-strip".into()]);
    }
    let environment: serde_json::Map<String, serde_json::Value> = [
        "CC",
        "CXX",
        "CFLAGS",
        "CXXFLAGS",
        "LDFLAGS",
        "PKG_CONFIG_PATH",
    ]
    .into_iter()
    .map(|name| (name.into(), json!(env::var(name).ok())))
    .collect();
    let fingerprint = json!({"source":source,"arguments":args,"configure_sha256":digest(&source.join("configure"))?,"environment":environment,"rustc":rustc_version});
    let stamp = output.join(".dreamgpu-configure");
    let previous = fs::read(&stamp)
        .ok()
        .and_then(|s| serde_json::from_slice::<serde_json::Value>(&s).ok());
    if env::var_os("DREAMGPU_FORCE_CONFIGURE").is_some()
        || !output.join("build.ninja").is_file()
        || previous.as_ref() != Some(&fingerprint)
    {
        run(Command::new(source.join("configure"))
            .args(&args)
            .current_dir(output))?;
        write_json(&stamp, &fingerprint)?;
    }
    // QEMU's upstream configure/Meson requires Python; DreamGPU's own build
    // graph and Rust static library compilation do not use a Python recipe.
    run(Command::new("make")
        .args(["-j", &jobs().to_string()])
        .current_dir(output))?;
    let mut binaries = serde_json::Map::new();
    let names = targets
        .split(',')
        .map(|t| format!("qemu-system-{}", t.trim_end_matches("-softmmu")))
        .chain(std::iter::once("qemu-img".into()));
    for name in names {
        let mut path = output.join(&name);
        if !path.is_file() {
            path = output.join(format!("{name}-unsigned"));
        }
        anyhow::ensure!(path.is_file(), "native build omitted {name}");
        binaries.insert(name, json!({"path":path,"sha256":digest(&path)?}));
    }
    if host == "linux" {
        let provider = crate::mesa::build(root, &output.join("mesa"))?;
        binaries = crate::native_runtime::package(root, output, &provider, &binaries)?;
    }
    let mut host_sources = serde_json::Map::new();
    record_tree(root, &root.join("crates/dreamgpu-host"), &mut host_sources)?;
    let revision = capture(
        Command::new("git")
            .arg("-C")
            .arg(&source)
            .args(["rev-parse", "HEAD"]),
    )?;
    let diff = capture(
        Command::new("git")
            .arg("-C")
            .arg(&source)
            .args(["diff", "HEAD", "--"]),
    )?;
    let untracked = capture(Command::new("git").arg("-C").arg(&source).args([
        "ls-files",
        "--others",
        "--exclude-standard",
        "-z",
    ]))?;
    let mut untracked_sources = serde_json::Map::new();
    for name in untracked.split('\0').filter(|s| !s.is_empty()) {
        let path = source.join(name);
        if path.is_file() {
            untracked_sources.insert(name.into(), json!(digest(&path)?));
        }
    }
    write_json(&output.join("manifest.json"), &json!({"schema":1,"source_root":root,"qemu_revision":revision,"qemu_diff_sha256":format!("{:x}",Sha256::digest(diff.as_bytes())),"qemu_untracked_sources":untracked_sources,"host_sources":host_sources,"rustc":capture(Command::new(env::var_os("RUSTC").unwrap_or_else(|| "rustc".into())).arg("--version"))?,"host":if host=="macos" {"Darwin"} else {"Linux"},"machine":env::consts::ARCH,"targets":targets.split(',').collect::<Vec<_>>(),"configure":fingerprint,"binaries":binaries})).context("write native provenance")
}
pub(crate) fn record_tree(
    root: &Path,
    directory: &Path,
    records: &mut serde_json::Map<String, serde_json::Value>,
) -> Result<()> {
    for item in fs::read_dir(directory)? {
        let item = item?;
        let path = item.path();
        if item.file_name() == "__pycache__"
            || item.file_name() == ".git"
            || path.extension().is_some_and(|s| s == "pyc")
        {
            continue;
        }
        if item.file_type()?.is_dir() {
            record_tree(root, &path, records)?;
        } else if path.is_file() {
            records.insert(
                path.strip_prefix(root)?.to_string_lossy().into_owned(),
                json!(digest(&path)?),
            );
        }
    }
    Ok(())
}
