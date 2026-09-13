// SPDX-License-Identifier: GPL-2.0-or-later
//! Exact upstream Mesa source plus checked DreamGPU patches, installed privately.
use crate::{capture, digest, jobs, run, write_json};
use anyhow::{ensure, Context, Result};
use serde_json::{json, Value};
use std::{
    fs,
    path::{Path, PathBuf},
    process::{Command, Stdio},
};

pub const DRIVERS: &str = "radeonsi,r600,r300,iris,crocus,nouveau,virgl,zink";

pub fn build(root: &Path, output: &Path) -> Result<PathBuf> {
    ensure!(
        cfg!(target_os = "linux"),
        "private Mesa is a Linux provider"
    );
    let recipe = root.join("support/native/mesa");
    let pin_path = recipe.join("source.json");
    let pin: Value = serde_json::from_slice(&fs::read(&pin_path)?)?;
    let version = pin["directory"].as_str().context("Mesa directory")?;
    ensure!(
        Path::new(version).components().count() == 1,
        "invalid Mesa directory"
    );
    fs::create_dir_all(output)?;
    let cache = fs::canonicalize(output)?;
    // A changed checked patch set gets its own source and Meson build tree.
    // Never reinterpret a previously accepted partially patched tree as the
    // new recipe's raw preimage, or alter an older runtime's source evidence.
    let output = cache.join(digest(&pin_path)?);
    fs::create_dir_all(&output)?;
    let archive = output.join(format!("{version}.tar.xz"));
    let cached_archive = cache.join(format!("{version}.tar.xz"));
    if !archive.exists() && cached_archive.is_file() {
        ensure!(
            digest(&cached_archive)? == pin["sha256"],
            "cached Mesa archive checksum mismatch"
        );
        if fs::hard_link(&cached_archive, &archive).is_err() {
            fs::copy(&cached_archive, &archive)?;
        }
    }
    if !archive.exists() {
        let temporary = archive.with_extension("download");
        run(Command::new("curl")
            .args(["--fail", "--location", "--output"])
            .arg(&temporary)
            .arg(pin["url"].as_str().context("Mesa URL")?))?;
        ensure!(
            digest(&temporary)? == pin["sha256"],
            "Mesa archive checksum mismatch"
        );
        fs::rename(temporary, &archive)?;
    }
    ensure!(
        digest(&archive)? == pin["sha256"],
        "Mesa archive checksum mismatch"
    );
    let files = pin["files"].as_object().context("Mesa file identities")?;
    let patches = pin["patches"]
        .as_object()
        .context("Mesa patch identities")?;
    for (name, sha) in patches {
        ensure!(
            digest(&recipe.join(name))? == *sha,
            "Mesa patch changed: {name}"
        );
    }
    let source = output.join(version);
    if !source.exists() {
        run(Command::new("tar")
            .arg("-xf")
            .arg(&archive)
            .arg("-C")
            .arg(&output))?;
    }
    let mut compare = Command::new("tar");
    compare
        .args(["--compare", "--file"])
        .arg(&archive)
        .arg("--directory")
        .arg(&output)
        .env("LC_ALL", "C");
    for name in files.keys() {
        compare.arg(format!("--exclude={version}/{name}"));
    }
    let compared = compare.output()?;
    let differences = String::from_utf8(compared.stdout)?;
    ensure!(
        compared.stderr.is_empty()
            && matches!(compared.status.code(), Some(0 | 1))
            && differences
                .lines()
                .all(|l| l.ends_with(": Mode differs") || l.ends_with(": Mod time differs")),
        "Mesa archive contents changed: {differences}"
    );
    let mut before = true;
    let mut after = true;
    for (name, hashes) in files {
        let hash = digest(&source.join(name))?;
        before &= hash == hashes["before"];
        after &= hash == hashes["after"];
    }
    ensure!(before || after, "mixed or foreign Mesa patch state");
    if before {
        for name in patches.keys() {
            run(Command::new("patch")
                .current_dir(&source)
                .args(["--batch", "--fuzz=0", "-p1"])
                .stdin(Stdio::from(fs::File::open(recipe.join(name))?)))?;
        }
    }
    for (name, hashes) in files {
        ensure!(
            digest(&source.join(name))? == hashes["after"],
            "Mesa patched output differs: {name}"
        );
    }
    let build = output.join("build");
    let prefix = output.join("install");
    // Mesa's own Meson/Python generator is an upstream build dependency, just
    // like QEMU's; DreamGPU orchestration stays in Cargo/Rust.
    let args = [
        "--buildtype=release",
        "-Dvulkan-drivers=",
        "-Dplatforms=x11,wayland",
        "-Dglx=dri",
        "-Degl=enabled",
        "-Dglvnd=enabled",
        "-Dgles1=disabled",
        "-Dgles2=disabled",
        "-Dgbm=enabled",
        "-Dllvm=enabled",
        "-Dshared-llvm=enabled",
        "-Dgallium-va=disabled",
        "-Dvideo-codecs=",
        "-Dbuild-tests=false",
        "-Dvalgrind=disabled",
        "--force-fallback-for=expat",
    ];
    let fingerprint = json!({"source":pin,"drivers":DRIVERS,"arguments":args,
        "meson":capture(Command::new("meson").arg("--version"))?,
        "compiler":capture(Command::new(std::env::var("CC").unwrap_or_else(|_|"cc".into())).arg("--version"))?,
        "llvm":capture(Command::new("llvm-config").arg("--version"))?});
    let stamp = output.join("configure.json");
    let previous = fs::read(&stamp)
        .ok()
        .and_then(|b| serde_json::from_slice::<Value>(&b).ok());
    if previous.as_ref() != Some(&fingerprint) || !build.join("build.ninja").is_file() {
        let mut command = Command::new("meson");
        command.arg("setup");
        if build.join("build.ninja").exists() {
            command.arg("--reconfigure");
        }
        run(command
            .arg(&build)
            .arg(&source)
            .arg(format!("--prefix={}", prefix.display()))
            .arg(format!("-Dgallium-drivers={DRIVERS}"))
            .args(args))?;
        write_json(&stamp, &fingerprint)?;
    }
    run(Command::new("ninja")
        .arg("-C")
        .arg(&build)
        .arg(format!("-j{}", jobs().min(6))))?;
    run(Command::new("meson")
        .arg("install")
        .arg("-C")
        .arg(&build)
        .arg("--no-rebuild"))?;
    write_json(&prefix.join("dreamgpu-source.json"), &fingerprint)?;
    Ok(prefix)
}
