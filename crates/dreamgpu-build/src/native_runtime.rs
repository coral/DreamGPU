// SPDX-License-Identifier: GPL-2.0-or-later
//! Content-addressed native execution closure and its authenticated Rust launcher.
use crate::{capture, digest, run, write_json};
use anyhow::{ensure, Context, Result};
use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use std::{
    collections::{BTreeMap, BTreeSet, VecDeque},
    fs,
    os::unix::fs::PermissionsExt,
    path::{Path, PathBuf},
    process::Command,
};

fn add(
    source: &Path,
    relative: &str,
    staging: &Path,
    files: &mut serde_json::Map<String, Value>,
) -> Result<()> {
    let source = source
        .canonicalize()
        .with_context(|| format!("runtime source {}", source.display()))?;
    if files.get(relative).is_some_and(|previous| {
        previous["source"]
            .as_str()
            .is_some_and(|p| Path::new(p) == source)
    }) {
        return Ok(());
    }
    let sha = digest(&source)?;
    if let Some(previous) = files.get(relative) {
        ensure!(
            previous["sha256"] == sha,
            "different libraries share runtime name {relative}"
        );
        return Ok(());
    }
    let target = staging.join(relative);
    fs::create_dir_all(target.parent().context("runtime member parent")?)?;
    fs::copy(&source, &target)?;
    let metadata = fs::metadata(&target)?;
    files.insert(relative.into(),json!({"sha256":sha,"size":metadata.len(),"mode":metadata.permissions().mode() & 0o777,"source":source}));
    Ok(())
}

fn dependencies(path: &Path, library: &Path) -> Result<Vec<PathBuf>> {
    let output = Command::new("ldd")
        .arg(path)
        .env("LC_ALL", "C")
        .env("LD_LIBRARY_PATH", library)
        .env_remove("LD_PRELOAD")
        .env_remove("LD_AUDIT")
        .output()?;
    ensure!(
        output.status.success(),
        "cannot resolve ELF dependencies: {}",
        path.display()
    );
    let text = String::from_utf8(output.stdout)?;
    ensure!(
        !text.contains("not found"),
        "missing ELF dependency: {text}"
    );
    Ok(text
        .lines()
        .filter_map(|line| {
            line.split_whitespace()
                .find(|part| part.starts_with('/'))
                .map(PathBuf::from)
        })
        .collect())
}

fn regular_tree(directory: &Path) -> Result<Vec<PathBuf>> {
    let mut output = Vec::new();
    for item in fs::read_dir(directory)? {
        let path = item?.path();
        if path.is_dir() {
            output.extend(regular_tree(&path)?);
        } else if path.is_file() {
            output.push(path);
        }
    }
    output.sort();
    Ok(output)
}

/// Publish launchers without replacing Ninja's real QEMU outputs.
pub fn package(
    root: &Path,
    output: &Path,
    provider: &Path,
    raw: &serde_json::Map<String, Value>,
) -> Result<serde_json::Map<String, Value>> {
    let staging = output.join(".runtime-staging");
    // This scratch directory is never a published runtime or an input.
    if staging.exists() {
        fs::remove_dir_all(&staging)?;
    }
    fs::create_dir(&staging)?;
    let library = if provider.join("lib64").is_dir() {
        provider.join("lib64")
    } else {
        provider.join("lib")
    };
    let mut files = serde_json::Map::new();
    let mut work = VecDeque::new();
    for (name, binary) in raw {
        let path = Path::new(binary["path"].as_str().context("raw native path")?);
        add(path, &format!("programs/{name}"), &staging, &mut files)?;
        work.push_back(path.to_owned());
    }
    for path in regular_tree(&library)? {
        let name = path
            .file_name()
            .context("Mesa library name")?
            .to_str()
            .context("UTF8 Mesa name")?;
        if name.contains(".so") {
            let relative = if path.parent().is_some_and(|p| p.ends_with("gbm")) {
                format!("gbm/{name}")
            } else if path.parent().is_some_and(|p| p.ends_with("dri")) {
                format!("dri/{name}")
            } else {
                format!("lib/{name}")
            };
            add(&path, &relative, &staging, &mut files)?;
            work.push_back(path);
        }
    }
    add(
        &provider.join("dreamgpu-source.json"),
        "provenance/mesa.json",
        &staging,
        &mut files,
    )?;
    let mesa_source = provider
        .parent()
        .context("Mesa build parent")?
        .join("mesa-26.1.8");
    for path in regular_tree(&mesa_source.join("licenses"))? {
        add(
            &path,
            &format!(
                "licenses/mesa/{}",
                path.file_name().context("license name")?.to_string_lossy()
            ),
            &staging,
            &mut files,
        )?;
    }
    add(
        &mesa_source.join("docs/license.rst"),
        "licenses/mesa/license.rst",
        &staging,
        &mut files,
    )?;
    add(
        &mesa_source.join("subprojects/expat-2.5.0/COPYING"),
        "licenses/expat/COPYING",
        &staging,
        &mut files,
    )?;
    // Preserve GLVND's platform vendor choices. Mesa's entry selects our private
    // libEGL_mesa; proprietary entries remain installed-platform requirements.
    let mut vendors = BTreeMap::new();
    for directory in [
        Path::new("/etc/glvnd/egl_vendor.d"),
        Path::new("/usr/share/glvnd/egl_vendor.d"),
    ] {
        if directory.is_dir() {
            for path in regular_tree(directory)? {
                if path.extension().is_some_and(|e| e == "json") {
                    vendors.insert(path.file_name().context("vendor name")?.to_owned(), path);
                }
            }
        }
    }
    vendors.insert(
        "50_mesa.json".into(),
        provider.join("share/glvnd/egl_vendor.d/50_mesa.json"),
    );
    for (name, path) in vendors {
        add(
            &path,
            &format!("egl/{}", name.to_string_lossy()),
            &staging,
            &mut files,
        )?;
    }
    for item in fs::read_dir(root.join("vendor/qemu/pc-bios"))? {
        let path = item?.path();
        if path.is_file()
            && (path
                .extension()
                .is_some_and(|e| e == "bin" || e == "rom" || e == "dtb" || e == "img" || e == "fd")
                || path
                    .file_name()
                    .is_some_and(|n| n.to_string_lossy().starts_with("openbios-")))
        {
            add(
                &path,
                &format!(
                    "firmware/{}",
                    path.file_name().context("firmware name")?.to_string_lossy()
                ),
                &staging,
                &mut files,
            )?;
        }
    }
    for name in ["COPYING", "COPYING.LIB"] {
        add(
            &root.join("vendor/qemu").join(name),
            &format!("licenses/qemu/{name}"),
            &staging,
            &mut files,
        )?;
    }
    let mut seen = BTreeSet::new();
    let mut packages = BTreeMap::new();
    let rpm_inventory =
        capture(Command::new("rpm").args(["-qa", "--qf", "%{NAME}\t%{SOURCERPM}\n"])).ok();
    let debian = rpm_inventory.is_none();
    let rpm_inventory = rpm_inventory.unwrap_or_default();
    let mut source_packages: BTreeMap<String, Vec<String>> = BTreeMap::new();
    for line in rpm_inventory.lines() {
        if let Some((name, source)) = line.split_once('\t') {
            source_packages
                .entry(source.into())
                .or_default()
                .push(name.into());
        }
    }
    let mut platform = serde_json::Map::new();
    while let Some(path) = work.pop_front() {
        let resolved = path.canonicalize()?;
        if !seen.insert(resolved.clone()) {
            continue;
        }
        for dependency in dependencies(&resolved, &library)? {
            let name = dependency
                .file_name()
                .context("dependency name")?
                .to_string_lossy();
            // PT_INTERP is selected by the kernel before LD_LIBRARY_PATH. Record
            // and verify its exact host identity instead of pretending it moved.
            if name.starts_with("ld-linux") || name.starts_with("ld-musl") {
                platform.insert(
                    dependency.to_string_lossy().into_owned(),
                    json!({"sha256":digest(&dependency)?}),
                );
            }
            add(&dependency, &format!("lib/{name}"), &staging, &mut files)?;
            work.push_back(dependency);
        }
        if !resolved.starts_with(provider)
            && !raw
                .values()
                .any(|r| r["path"].as_str().is_some_and(|p| Path::new(p) == resolved))
        {
            if debian {
                let owner =
                    capture(Command::new("dpkg-query").arg("-S").arg(&resolved)).or_else(|_| {
                        capture(
                            Command::new("dpkg-query")
                                .arg("-S")
                                .arg(resolved.strip_prefix("/usr").unwrap_or(&resolved)),
                        )
                    })?;
                let name = owner
                    .lines()
                    .next()
                    .and_then(|l| l.split_once(": "))
                    .map(|(n, _)| n)
                    .context("Debian dependency owner")?;
                if let std::collections::btree_map::Entry::Vacant(entry) =
                    packages.entry(name.to_owned())
                {
                    let record = capture(Command::new("dpkg-query").args(["-W", "-f=${binary:Package}\n${Version}\n${source:Package} ${source:Version}\n${Homepage}\n"]).arg(name))?;
                    let doc_name = name.split(':').next().context("Debian package name")?;
                    add(
                        &Path::new("/usr/share/doc").join(doc_name).join("copyright"),
                        &format!("licenses/packages/{doc_name}/copyright"),
                        &staging,
                        &mut files,
                    )?;
                    entry.insert(record);
                }
                continue;
            }
            let query = Command::new("rpm")
                .args([
                    "-qf",
                    "--qf",
                    "%{NAME}\n%{VERSION}-%{RELEASE}\n%{SOURCERPM}\n%{LICENSE}\n",
                ])
                .arg(&resolved)
                .output();
            match query {
                Ok(result) if result.status.success() => {
                    let record = String::from_utf8(result.stdout)?;
                    let name = record.lines().next().context("RPM package name")?.to_owned();
                    if let std::collections::btree_map::Entry::Vacant(entry) = packages.entry(name.clone()) {
                        let source_rpm = record.lines().nth(2).context("source RPM")?;
                        let related = source_packages.get(source_rpm).context("installed source package inventory")?;
                        let mut license_count = 0;
                        for package in related {
                            let license_paths = capture(Command::new("rpm").args(["-ql"]).arg(package))?;
                            for path in license_paths.lines().map(Path::new).filter(|p| p.is_file() && (p.starts_with("/usr/share/licenses") || p.file_name().is_some_and(|n| { let n=n.to_string_lossy().to_ascii_uppercase(); ["COPYING","COPYRIGHT","LICENSE","LICENCE","NOTICE"].iter().any(|prefix|n.starts_with(prefix)) }))) {
                                add(path, &format!("licenses/packages/{package}/{}",path.strip_prefix("/")?.display()), &staging, &mut files)?;
                                license_count += 1;
                            }
                        }
                        if license_count == 0 {
                            // Some distributions ship the project's complete copyright
                            // permissions in installed source headers, not a LICENSE file.
                            for package in related {
                                let paths = capture(Command::new("rpm").arg("-ql").arg(package))?;
                                for path in paths.lines().map(Path::new).filter(|p| p.is_file() && p.starts_with("/usr/include") && p.extension().is_some_and(|e|e=="h")) {
                                    let text = fs::read_to_string(path)?;
                                    if text.contains("Permission is hereby granted") || text.contains("Redistribution and use in source and binary forms") {
                                        add(path,&format!("licenses/source-headers/{package}/{}",path.strip_prefix("/")?.display()),&staging,&mut files)?;
                                        license_count += 1;
                                    }
                                }
                            }
                        }
                        ensure!(license_count > 0, "no installed license text for source package {source_rpm}");
                        entry.insert(record);
                    }
                }
                _ => anyhow::bail!("cannot establish source package/license provenance for {}; install RPM provenance tools or add a checked platform inventory adapter",resolved.display()),
            }
        }
    }
    let rust_metadata: Value = serde_json::from_str(&capture(
        Command::new(std::env::var_os("CARGO").unwrap_or_else(|| "cargo".into()))
            .args([
                "metadata",
                "--locked",
                "--format-version=1",
                "--manifest-path",
            ])
            .arg(root.join("support/native/launcher/Cargo.toml")),
    )?)?;
    let mut rust_packages = Vec::new();
    for package in rust_metadata["packages"]
        .as_array()
        .context("launcher dependency metadata")?
    {
        let name = package["name"].as_str().context("Rust package name")?;
        let source = Path::new(
            package["manifest_path"]
                .as_str()
                .context("Rust package source")?,
        )
        .parent()
        .context("Rust source parent")?;
        rust_packages.push(json!({"name":name,"version":package["version"],"source":package["source"],"license":package["license"],"repository":package["repository"]}));
        if package["source"].is_null() {
            continue;
        }
        let mut count = 0;
        for item in fs::read_dir(source)? {
            let path = item?.path();
            if path.is_file()
                && path.file_name().is_some_and(|n| {
                    let n = n.to_string_lossy().to_ascii_uppercase();
                    ["LICENSE", "COPYING", "UNLICENSE", "NOTICE"]
                        .iter()
                        .any(|p| n.starts_with(p))
                })
            {
                add(
                    &path,
                    &format!(
                        "licenses/rust/{name}/{}",
                        path.file_name()
                            .context("Rust license name")?
                            .to_string_lossy()
                    ),
                    &staging,
                    &mut files,
                )?;
                count += 1;
            }
        }
        ensure!(count > 0, "Rust dependency license text missing: {name}");
    }
    for name in ["Cargo.toml", "Cargo.lock", "src/main.rs"] {
        add(
            &root.join("support/native/launcher").join(name),
            &format!("provenance/launcher/{name}"),
            &staging,
            &mut files,
        )?;
    }
    add(
        &root.join("LICENSE"),
        "licenses/launcher/LICENSE",
        &staging,
        &mut files,
    )?;
    let manifest = json!({"schema":1,"provider":"Mesa","firmware":true,"source":serde_json::from_slice::<Value>(&fs::read(provider.join("dreamgpu-source.json"))?)?,
        "files":files,"rust_packages":rust_packages,"platform_files":platform,"packages":packages,"package_format":if debian {"dpkg"}else{"rpm"},
        "platform_contract":"Kernel DRM, display server, proprietary GLVND vendor implementations and ELF interpreter remain host facilities. Bundled ELF dependencies are byte-pinned; hardware acceptance currently AMD radeonsi only."});
    let bytes = serde_json::to_vec_pretty(&manifest)?;
    let id = format!("{:x}", Sha256::digest(&bytes));
    let runtime = output.join("runtime").join(&id);
    fs::create_dir_all(runtime.parent().context("runtime parent")?)?;
    fs::write(staging.join("manifest.json"), &bytes)?;
    if runtime.exists() {
        ensure!(
            fs::read(runtime.join("manifest.json"))? == bytes,
            "existing runtime manifest differs"
        );
        for (name, record) in files {
            let metadata = fs::symlink_metadata(runtime.join(&name))?;
            ensure!(
                metadata.is_file()
                    && record["size"] == metadata.len()
                    && record["mode"] == metadata.permissions().mode() & 0o777,
                "existing runtime type/size/mode changed: {name}"
            );
            ensure!(
                digest(&runtime.join(&name))? == record["sha256"],
                "existing runtime changed: {name}"
            );
        }
        fs::remove_dir_all(&staging)?;
    } else {
        fs::rename(&staging, &runtime)?;
    }
    let bin = output.join("bin");
    fs::create_dir_all(&bin)?;
    let launcher_source = output.join("launcher-source");
    fs::create_dir_all(launcher_source.join("src"))?;
    for name in ["Cargo.toml", "Cargo.lock", "src/main.rs"] {
        let source = root.join("support/native/launcher").join(name);
        if source.exists() {
            fs::copy(source, launcher_source.join(name))?;
        }
    }
    let mut result = serde_json::Map::new();
    for (name, binary) in raw {
        let target = bin.join(name);
        run(
            Command::new(std::env::var_os("CARGO").unwrap_or_else(|| "cargo".into()))
                .args(["build", "--release", "--locked", "--manifest-path"])
                .arg(launcher_source.join("Cargo.toml"))
                .arg("--target-dir")
                .arg(output.join("launcher-target"))
                .env("DREAMGPU_RUNTIME_MANIFEST", runtime.join("manifest.json"))
                .env("DREAMGPU_RUNTIME_DIRECTORY", &runtime)
                .env("DREAMGPU_RUNTIME_PROGRAM", name)
                .env_remove("CARGO_ENCODED_RUSTFLAGS")
                .env_remove("RUSTFLAGS"),
        )?;
        fs::copy(
            output.join("launcher-target/release/dreamgpu-native-launcher"),
            &target,
        )?;
        result.insert(
            name.clone(),
            json!({"path":target,"sha256":digest(&target)?,"execution":binary,
            "runtime":{"path":runtime.join("manifest.json"),"sha256":id,"directory":runtime},
            "execution_path":runtime.join("programs").join(name)}),
        );
    }
    write_json(
        &bin.join("manifest.json"),
        &json!({"schema":2,"binaries":result}),
    )?;
    Ok(result)
}

/// Repackage already-built outputs for a bounded launcher/provider validation.
pub fn package_existing(root: &Path, output: &Path) -> Result<()> {
    let mut raw = serde_json::Map::new();
    for name in [
        "qemu-system-i386",
        "qemu-system-x86_64",
        "qemu-system-ppc",
        "qemu-system-m68k",
        "qemu-img",
    ] {
        let path = output.join(name);
        if path.is_file() {
            raw.insert(name.into(), json!({"path":path,"sha256":digest(&path)?}));
        }
    }
    ensure!(!raw.is_empty(), "no built native programs to package");
    package(root, output, &output.join("mesa/install"), &raw)?;
    Ok(())
}
