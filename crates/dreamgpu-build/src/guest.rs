// SPDX-License-Identifier: GPL-2.0-or-later
//! Cross compilation and OS-specific guest packages.
#[path = "unreal_audit.rs"]
mod unreal_audit;
use crate::{capture, copy_tree, digest, jobs, run, write_json};
use anyhow::{Context, Result};
use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use std::{
    env, fs,
    path::{Path, PathBuf},
    process::Command,
};

pub fn build(root: &Path, output: &Path) -> Result<()> {
    if let Ok(host) = env::var("DREAMGPU_GUEST_HOST") {
        return remote(root, output, &host);
    }
    anyhow::ensure!(cfg!(all(target_os="linux", target_arch="x86_64")), "The pinned Win98 Watcom toolchain needs x86-64 Linux. Set DREAMGPU_GUEST_HOST to an SSH build host, or run the documented guest toolchain container.");
    fs::create_dir_all(output)?;
    let prefix = env::var("DREAMGPU_CROSS_COMPILE").unwrap_or_else(|_| "i686-w64-mingw32-".into());
    let toolchain: Value =
        serde_json::from_slice(&fs::read(root.join("support/guest/toolchain.json"))?)?;
    for compiler in ["gcc", "g++"] {
        let program = format!("{prefix}{compiler}");
        let target = capture(Command::new(&program).arg("-dumpmachine"))?;
        let version = capture(Command::new(&program).arg("-dumpfullversion"))?;
        anyhow::ensure!(
            toolchain["cross_compiler"]["target"] == target
                && toolchain["cross_compiler"]["version"] == version,
            "{program} is {target} {version}; expected pinned {} {}",
            toolchain["cross_compiler"]["target"],
            toolchain["cross_compiler"]["version"]
        );
    }
    let lock: Value =
        serde_json::from_slice(&fs::read(root.join("support/guest/sources.lock.json"))?)?;
    for name in ["vmdisp9x", "wine9x", "qemu-xtra", "wine-glu"] {
        source(root, name, &lock["sources"][name])?;
    }
    let watcom = watcom(root, output, &lock["tools"]["open-watcom"])?;
    let mut inputs = serde_json::Map::new();
    for name in [
        "guest",
        "tools",
        "support/guest",
        "support/attribution",
        "LICENSES",
        "crates/dreamgpu-build",
    ] {
        crate::native::record_tree(root, &root.join(name), &mut inputs)?;
    }
    for name in ["LICENSE", "ATTRIBUTION.md"] {
        inputs.insert(name.into(), json!(digest(&root.join(name))?));
    }
    inputs.insert(
        "@build-configuration".into(),
        json!({"prefix":prefix,"toolchain":toolchain}),
    );
    // Stable content identities make repeated Cargo builds incremental. A changed
    // recipe gets a fresh staging tree; accepted packages are never relabelled.
    let identity = format!("{:x}", Sha256::digest(serde_json::to_vec(&inputs)?));
    // Keep compiler outputs stable across edits. Only donor preparation gets a
    // content-addressed directory; a helper edit must not rebuild all of Wine.
    let work = output.join("build");
    fs::create_dir_all(&work)?;
    let mut preparation_inputs = serde_json::Map::new();
    for name in [
        "guest/include",
        "guest/nt/include",
        "guest/win9x",
        "guest/d3d",
        "guest/glide",
        "support/guest/win9x/patches",
        "support/guest/d3d/patches",
        "support/guest/glide/patches",
    ] {
        crate::native::record_tree(root, &root.join(name), &mut preparation_inputs)?;
    }
    for name in [
        "guest/opengl/frontend.def",
        "crates/dreamgpu-build/src/prepare.rs",
        "support/guest/sources.lock.json",
    ] {
        preparation_inputs.insert(name.into(), json!(digest(&root.join(name))?));
    }
    preparation_inputs.insert("@prefix".into(), json!(prefix));
    let preparation_identity = format!(
        "{:x}",
        Sha256::digest(serde_json::to_vec(&preparation_inputs)?)
    );
    let preparation_work = work.join("prepared").join(preparation_identity);
    fs::create_dir_all(&preparation_work)?;
    let source_work = preparation_work.join("source");
    let preparation_receipt = preparation_work.join("prepared.json");
    let cached = fs::read(&preparation_receipt)
        .ok()
        .and_then(|bytes| serde_json::from_slice::<Value>(&bytes).ok());
    let valid_cache = cached.as_ref().is_some_and(|receipt| {
        receipt["files"].as_object().is_some_and(|files| {
            !files.is_empty()
                && files.iter().all(|(name, expected)| {
                    let path = source_work.join(name);
                    path.is_file()
                        && !path.is_symlink()
                        && digest(&path).ok().as_deref() == expected.as_str()
                })
        })
    });
    let prepared = if valid_cache {
        crate::prepare::Prepared {
            vmdisp: source_work.join("vmdisp9x"),
            vmdisp_icd: source_work.join("vmdisp9x-icd"),
            wine: source_work.join("wine9x"),
            glide: source_work.join("openglide"),
            patches: cached.unwrap()["patches"].clone(),
        }
    } else {
        if source_work.exists() {
            fs::remove_dir_all(&source_work)?;
        }
        let prepared = crate::prepare::guest_sources(root, &source_work, &prefix)?;
        let mut files = serde_json::Map::new();
        crate::native::record_tree(&source_work, &source_work, &mut files)?;
        write_json(
            &preparation_receipt,
            &json!({"schema":1,"patches":prepared.patches,"files":files}),
        )?;
        prepared
    };
    for (os, package) in [("nt5", "windows2000-xp"), ("win98", "win98")] {
        let binary = work.join(os);
        let stage = work.join(format!("package-{package}"));
        run(Command::new("cmake")
            .arg("-S")
            .arg(root.join("guest"))
            .arg("-B")
            .arg(&binary)
            .arg("-G")
            .arg("Ninja")
            .arg(format!(
                "-DCMAKE_TOOLCHAIN_FILE={}",
                root.join("support/guest/cmake/mingw-i686.cmake").display()
            ))
            .arg(format!("-DDREAMGPU_MINGW_PREFIX={prefix}"))
            .arg(format!("-DDREAMGPU_GUEST_OS={os}"))
            .arg(format!("-DDREAMGPU_RUNNER_ID={identity}"))
            .arg(format!(
                "-DDREAMGPU_VMDISP_SOURCE={}",
                prepared.vmdisp.display()
            ))
            .arg(format!(
                "-DDREAMGPU_VMDISP_ICD_SOURCE={}",
                prepared.vmdisp_icd.display()
            ))
            .arg(format!(
                "-DDREAMGPU_WINE_SOURCE={}",
                prepared.wine.display()
            ))
            .arg(format!(
                "-DDREAMGPU_GLIDE_SOURCE={}",
                prepared.glide.display()
            ))
            .arg(format!("-DDREAMGPU_WATCOM_ROOT={}", watcom.display()))
            .arg(format!("-DCMAKE_INSTALL_PREFIX={}", stage.display())))?;
        run(Command::new("cmake").arg("--build").arg(&binary).args([
            "--target",
            "dreamgpu-guest",
            "--parallel",
            &jobs().to_string(),
        ]))?;
        // Install into an empty staging package so stale payloads or previous
        // manifests cannot recursively enter the new package identity.
        if stage.exists() {
            fs::remove_dir_all(&stage)?;
        }
        run(Command::new("cmake").arg("--install").arg(&binary))?;
        let imports = audit_package(&stage, &prefix)?;
        let mut files = serde_json::Map::new();
        crate::native::record_tree(&stage, &stage, &mut files)?;
        crate::package_metadata::write(&stage, &files, &identity)?;
        write_json(
            &stage.join("manifest.json"),
            &json!({"schema":1,"guest_os":package,"build_identity":identity,"runner_identity":identity,"sources":lock,"toolchain":toolchain,"patches":prepared.patches,"inputs":inputs,"files":files,"imports":imports,"status":"Source build and binary audit passed; runtime acceptance is not implied"}),
        )?;
        // Publish only after compilation, install and audits all succeed.
        fs::create_dir_all(output.join("packages"))?;
        let destination = output.join("packages").join(package);
        let temporary = output.join("packages").join(format!(".{package}.new"));
        if temporary.exists() {
            fs::remove_dir_all(&temporary)?;
        }
        copy_tree(&stage, &temporary)?;
        if destination.exists() {
            fs::remove_dir_all(&destination)?;
        }
        fs::rename(temporary, destination)?;
    }
    crate::installer::build(root, output, &prefix)?;
    Ok(())
}
fn source(root: &Path, name: &str, pin: &Value) -> Result<()> {
    let source = root.join("vendor").join(name);
    let commit = pin["commit"]
        .as_str()
        .context("source pin missing commit")?;
    if !source.join(".git").exists() {
        if root.join(".git").exists() {
            run(Command::new("git")
                .arg("-C")
                .arg(root)
                .args(["submodule", "update", "--init", "--recursive", "--"])
                .arg(format!("vendor/{name}")))?;
        } else {
            if source.exists() {
                anyhow::ensure!(
                    fs::read_dir(&source)?.next().is_none(),
                    "refusing to replace unversioned source {}",
                    source.display()
                );
            }
            run(Command::new("git")
                .args(["clone", "--filter=blob:none", "--no-checkout"])
                .arg(pin["url"].as_str().context("source URL")?)
                .arg(&source))?;
            if let Some(paths) = pin["sparse"].as_array() {
                let paths: Vec<_> = paths.iter().filter_map(Value::as_str).collect();
                run(Command::new("git")
                    .arg("-C")
                    .arg(&source)
                    .args(["sparse-checkout", "set", "--cone"])
                    .args(paths))?;
            }
            run(Command::new("git")
                .arg("-C")
                .arg(&source)
                .args(["checkout", "--detach", commit]))?;
            run(Command::new("git").arg("-C").arg(&source).args([
                "submodule",
                "update",
                "--init",
                "--recursive",
            ]))?;
        }
    }
    anyhow::ensure!(
        capture(
            Command::new("git")
                .arg("-C")
                .arg(&source)
                .args(["rev-parse", "HEAD"])
        )? == commit,
        "{name}: source differs from locked commit"
    );
    anyhow::ensure!(
        capture(
            Command::new("git")
                .arg("-C")
                .arg(&source)
                .args(["status", "--porcelain"])
        )?
        .is_empty(),
        "{name}: pinned donor contains modifications"
    );
    if let Some(children) = pin["submodules"].as_object() {
        for (child, revision) in children {
            anyhow::ensure!(
                capture(
                    Command::new("git")
                        .arg("-C")
                        .arg(source.join(child))
                        .args(["rev-parse", "HEAD"])
                )? == revision.as_str().unwrap_or_default(),
                "{name}/{child}: nested pin mismatch"
            );
        }
    }
    Ok(())
}
fn watcom(_root: &Path, output: &Path, pin: &Value) -> Result<PathBuf> {
    let cache = env::var_os("DREAMGPU_TOOL_CACHE")
        .map(PathBuf::from)
        .unwrap_or_else(|| output.join("toolchain"));
    fs::create_dir_all(&cache)?;
    let archive = cache.join("ow-snapshot.tar.xz");
    if !archive.is_file() {
        let temporary = archive.with_extension("xz.download");
        run(Command::new("curl")
            .args(["--fail", "--location", "--retry", "3", "--output"])
            .arg(&temporary)
            .arg(pin["url"].as_str().context("Watcom URL")?))?;
        fs::rename(temporary, &archive)?;
    }
    anyhow::ensure!(
        Some(digest(&archive)?.as_str()) == pin["sha256"].as_str(),
        "Open Watcom archive checksum mismatch"
    );
    let destination = env::var_os("DREAMGPU_WATCOM_ROOT")
        .map(PathBuf::from)
        .unwrap_or_else(|| cache.join("watcom"));
    if !destination.join("binl64/wmake").is_file() {
        fs::create_dir_all(&destination)?;
        run(Command::new("tar")
            .arg("-xJf")
            .arg(&archive)
            .arg("-C")
            .arg(&destination))?;
    }
    Ok(destination)
}
fn remote(root: &Path, output: &Path, host: &str) -> Result<()> {
    anyhow::ensure!(
        !host.is_empty()
            && !host.starts_with('-')
            && host
                .chars()
                .all(|c| c.is_ascii_alphanumeric() || "@._-".contains(c)),
        "invalid SSH build host"
    );
    let destination = match env::var("DREAMGPU_GUEST_ROOT") {
        Ok(path) => path,
        Err(_) => capture(Command::new("ssh").arg(host).arg(
            "mkdir -p ~/.cache/dreamgpu/cargo-guest && cd ~/.cache/dreamgpu/cargo-guest && pwd",
        ))?,
    };
    anyhow::ensure!(
        destination.starts_with('/') && !destination.contains('\n'),
        "remote build root must be absolute"
    );
    let mkdir = format!("mkdir -p {0} && cd {0} && test ! -e .git && (test -f .dreamgpu-build-cache || test -z \"$(ls -A)\") && touch .dreamgpu-build-cache", quote(&destination));
    run(Command::new("ssh").arg(host).arg(mkdir))?;
    for name in [
        "guest",
        "tools",
        "support",
        "LICENSES",
        "LICENSE",
        "ATTRIBUTION.md",
        "crates/dreamgpu-build",
    ] {
        let parent = Path::new(name).parent().unwrap();
        run(Command::new("ssh").arg(host).arg(format!(
            "mkdir -p {}",
            quote(&format!("{destination}/{}", parent.display()))
        )))?;
        run(Command::new("rsync")
            .args(["-a", "--delete", "--exclude=__pycache__", "--exclude=*.pyc"])
            .arg(root.join(name))
            .arg(format!(
                "{host}:{}/",
                quote(&format!("{destination}/{}", parent.display()))
            )))?;
    }
    // A small independent workspace avoids transmitting consumer sources or VM data.
    let workspace = "[workspace]\nresolver = \"2\"\nmembers = [\"crates/dreamgpu-build\"]\n";
    run(Command::new("ssh").arg(host).arg(format!(
        "printf %s {} > {}/Cargo.toml",
        quote(workspace),
        quote(&destination)
    )))?;
    let mut overrides = String::new();
    for name in [
        "DREAMGPU_CROSS_COMPILE",
        "DREAMGPU_TOOL_CACHE",
        "DREAMGPU_WATCOM_ROOT",
    ] {
        if let Ok(value) = env::var(name) {
            overrides.push_str(&format!(" {name}={}", quote(&value)));
        }
    }
    let command=format!("cd {} && PATH=\"$HOME/.cargo/bin:$PATH\" env{} cargo run --release -p dreamgpu-build -- guest --root {} --output {}",quote(&destination),overrides,quote(&destination),quote(&format!("{destination}/target/guest")));
    run(Command::new("ssh").arg(host).arg(command))?;
    fs::create_dir_all(output)?;
    run(Command::new("rsync")
        .args(["-a", "--delete"])
        .arg(format!(
            "{host}:{}/",
            quote(&format!("{destination}/target/guest/packages"))
        ))
        .arg(output.join("packages")))?;
    for name in ["dreamgpu.exe", "installer-manifest.json"] {
        run(Command::new("rsync")
            .arg("-a")
            .arg(format!(
                "{host}:{}",
                quote(&format!("{destination}/target/guest/{name}"))
            ))
            .arg(output.join(name)))?;
    }
    Ok(())
}
fn quote(value: &str) -> String {
    format!("'{}'", value.replace('\'', "'\\''"))
}

fn audit_package(stage: &Path, prefix: &str) -> Result<Value> {
    let mut files = Vec::new();
    collect_binaries(stage, &mut files)?;
    anyhow::ensure!(!files.is_empty(), "empty guest package");
    let mut evidence = serde_json::Map::new();
    for path in files {
        let data = fs::read(&path)?;
        let header = read32(&data, 0x3c)? as usize;
        let signature = data
            .get(header..header + 2)
            .context("truncated executable signature")?;
        let relative = path.strip_prefix(stage)?.to_string_lossy().into_owned();
        if signature == b"LE" {
            audit_vxd(&data)?;
            evidence.insert(
                relative,
                json!({"format":"LE","audit":"DDB export checked"}),
            );
            continue;
        }
        if signature == b"NE" {
            audit_ne(&data)?;
            evidence.insert(
                relative,
                json!({"format":"NE","audit":"segment relocation targets checked"}),
            );
            continue;
        }
        anyhow::ensure!(
            signature == b"PE" && read16(&data, header + 4)? == 0x14c,
            "{}: not an i386 PE image",
            path.display()
        );
        let optional = header + 24;
        anyhow::ensure!(
            read16(&data, optional)? == 0x10b
                && read16(&data, optional + 40)? <= 5
                && read16(&data, optional + 48)? <= 5,
            "{}: unsupported PE OS/subsystem version",
            path.display()
        );
        let details = capture(
            Command::new(format!("{prefix}objdump"))
                .arg("-p")
                .arg(&path),
        )?;
        let libraries: Vec<String> = details
            .lines()
            .filter_map(|line| {
                line.trim()
                    .strip_prefix("DLL Name:")
                    .map(|s| s.trim().to_lowercase())
            })
            .collect();
        let name = path.file_name().unwrap().to_string_lossy().to_lowercase();
        let allowed: &[&str] = match name.as_str() {
            "dgpumini.sys" => &["videoprt.sys", "ntoskrnl.exe", "hal.dll"],
            "dgpudisp.dll" => &["win32k.sys"],
            "dgpugl.dll" => &["kernel32.dll", "user32.dll", "gdi32.dll"],
            "glide2x.dll" => &[
                "dgpugl.dll",
                "gdi32.dll",
                "user32.dll",
                "kernel32.dll",
                "msvcrt.dll",
                "winmm.dll",
            ],
            _ if relative.starts_with("application/") || relative.starts_with("switchers/") => &[
                "kernel32.dll",
                "user32.dll",
                "gdi32.dll",
                "advapi32.dll",
                "msvcrt.dll",
                "dgpugl.dll",
                "wined3d.dll",
            ],
            // Retained Win98 diagnostics use the legacy system CRT. All
            // other maintained helper executables are freestanding.
            "dgcd.exe" => &["kernel32.dll", "user32.dll", "msvcrt.dll", "winmm.dll"],
            "dgchan9.exe" | "dgwgl9.exe" | "dg9mode.exe" | "dgid.exe" | "dgacc.exe" => {
                &["kernel32.dll", "user32.dll", "gdi32.dll", "msvcrt.dll"]
            }
            _ => &[
                "kernel32.dll",
                "user32.dll",
                "gdi32.dll",
                "advapi32.dll",
                "winmm.dll",
                "setupapi.dll",
                "cfgmgr32.dll",
                "shell32.dll",
                "version.dll",
                "comctl32.dll",
            ],
        };
        anyhow::ensure!(
            libraries.iter().all(|s| allowed.contains(&s.as_str())),
            "{relative}: unexpected runtime dependency for this component: {libraries:?}"
        );
        if name == "wined3d.dll" || name == "glide2x.dll" {
            anyhow::ensure!(
                libraries.iter().any(|s| s == "dgpugl.dll"),
                "{relative} must import DreamGPU OpenGL"
            );
        }
        if name == "wined3d.dll" {
            for symbol in [
                "ChoosePixelFormat",
                "DescribePixelFormat",
                "GetPixelFormat",
                "SetPixelFormat",
                "SwapBuffers",
            ] {
                anyhow::ensure!(
                    !details.split_whitespace().any(|s| s == symbol),
                    "Wine retained system GDI GL binding {symbol}"
                );
            }
        }
        let mut result = json!({"format":"PE32","imports":libraries});
        if unreal_audit::applies(&name) {
            let assembly = capture(
                Command::new(format!("{prefix}objdump"))
                    .args(["-d", "-M", "att"])
                    .arg(&path),
            )?;
            result["unreal_contract"] = unreal_audit::audit(&data, &details, &assembly)
                .with_context(|| format!("Unreal helper audit: {relative}"))?;
        }
        evidence.insert(relative, result);
    }
    Ok(Value::Object(evidence))
}
fn collect_binaries(dir: &Path, files: &mut Vec<PathBuf>) -> Result<()> {
    for item in fs::read_dir(dir)? {
        let path = item?.path();
        if path.is_dir() {
            collect_binaries(&path, files)?;
        } else if path.extension().is_some_and(|s| {
            ["exe", "dll", "drv", "vxd", "sys"]
                .iter()
                .any(|e| s.eq_ignore_ascii_case(e))
        }) {
            files.push(path);
        }
    }
    Ok(())
}
fn read16(data: &[u8], offset: usize) -> Result<u16> {
    Ok(u16::from_le_bytes(
        data.get(offset..offset + 2)
            .context("truncated executable u16")?
            .try_into()?,
    ))
}
fn read32(data: &[u8], offset: usize) -> Result<u32> {
    Ok(u32::from_le_bytes(
        data.get(offset..offset + 4)
            .context("truncated executable u32")?
            .try_into()?,
    ))
}
fn audit_vxd(data: &[u8]) -> Result<()> {
    anyhow::ensure!(data.get(..2) == Some(b"MZ"), "VxD DOS signature missing");
    let h = read32(data, 0x3c)? as usize;
    let field = |offset| read32(data, h + offset).map(|n| n as usize);
    let entry = h + field(0x5c)?;
    anyhow::ensure!(
        data.get(entry..entry + 2) == Some(&[1, 3]),
        "VxD DDB must be a 32-bit ordinal 1 export"
    );
    let object = read16(data, entry + 2)? as usize;
    let offset = read32(data, entry + 5)? as usize;
    anyhow::ensure!(
        (1..=field(0x44)?).contains(&object),
        "DDB object out of bounds"
    );
    let table = h + field(0x40)? + (object - 1) * 24;
    let size = read32(data, table)? as usize;
    let flags = read32(data, table + 8)?;
    let first = read32(data, table + 12)? as usize;
    let page_size = field(0x28)?;
    anyhow::ensure!(
        page_size > 0 && offset + 80 <= size && flags & 0x2000 != 0 && first > 0,
        "invalid DDB extent"
    );
    let page = h + field(0x48)? + (first - 1 + offset / page_size) * 4;
    let bytes = data.get(page..page + 3).context("DDB page table")?;
    let number = ((bytes[0] as usize) << 16) | ((bytes[1] as usize) << 8) | bytes[2] as usize;
    anyhow::ensure!(number > 0, "invalid DDB page");
    let position = field(0x80)? + (number - 1) * page_size + offset % page_size;
    let ddb = data.get(position..position + 80).context("truncated DDB")?;
    anyhow::ensure!(
        ddb[..4] == [0; 4] && &ddb[12..20] == b"DREAMGPU" && read16(ddb, 4)? == 0x400,
        "invalid DreamGPU DDB identity"
    );
    let resident = h + field(0x58)?;
    anyhow::ensure!(
        data.get(resident..resident + 10) == Some(b"\x08DREAMGPU\0"),
        "LE module name differs from device"
    );
    Ok(())
}
fn audit_ne(data: &[u8]) -> Result<()> {
    anyhow::ensure!(data.get(..2) == Some(b"MZ"), "NE DOS signature missing");
    let h = read32(data, 0x3c)? as usize;
    let segments = read16(data, h + 0x1c)? as usize;
    let modules = read16(data, h + 0x1e)? as usize;
    let table = h + read16(data, h + 0x22)? as usize;
    let shift = read16(data, h + 0x32)?;
    anyhow::ensure!(
        shift <= 16 && segments > 0,
        "invalid NE segment alignment/count"
    );
    for index in 0..segments {
        let entry = table + index * 8;
        let sector = read16(data, entry)? as usize;
        let size = match read16(data, entry + 2)? {
            0 => 65536,
            n => n as usize,
        };
        if read16(data, entry + 4)? & 0x100 == 0 {
            continue;
        }
        let relocation = (sector << shift) + size;
        let count = read16(data, relocation)? as usize;
        for n in 0..count {
            let entry = relocation + 2 + n * 8;
            let kind = *data.get(entry + 1).context("NE relocation")? & 3;
            let offset = read16(data, entry + 2)? as usize;
            let target = read16(data, entry + 4)? as usize;
            anyhow::ensure!(offset < size, "NE relocation source out of bounds");
            if kind == 0 && target != 255 {
                anyhow::ensure!(
                    (1..=segments).contains(&target),
                    "NE missing segment {target}"
                );
            }
            if kind == 1 || kind == 2 {
                anyhow::ensure!((1..=modules).contains(&target), "NE missing import module");
            }
        }
    }
    Ok(())
}

#[cfg(test)]
#[path = "audit_tests.rs"]
mod audit_tests;
