// SPDX-License-Identifier: GPL-2.0-or-later
//! Pinned guest-source staging and exact, transactional patch application.
use crate::{capture, copy_tree, digest, run};
use anyhow::{bail, ensure, Context, Result};
use serde_json::{json, Value};
use std::{
    collections::BTreeSet,
    fs,
    path::{Component, Path, PathBuf},
    process::Command,
    sync::atomic::{AtomicU64, Ordering},
};

pub struct Prepared {
    pub vmdisp: PathBuf,
    pub wine: PathBuf,
    pub glide: PathBuf,
    pub patches: Value,
}

struct Temporary(PathBuf);
impl Temporary {
    fn new() -> Result<Self> {
        static NEXT: AtomicU64 = AtomicU64::new(0);
        for _ in 0..100 {
            let path = std::env::temp_dir().join(format!(
                "dreamgpu-patches-{}-{}",
                std::process::id(),
                NEXT.fetch_add(1, Ordering::Relaxed)
            ));
            match fs::create_dir(&path) {
                Ok(()) => return Ok(Self(path)),
                Err(error) if error.kind() == std::io::ErrorKind::AlreadyExists => continue,
                Err(error) => return Err(error.into()),
            }
        }
        bail!("cannot allocate a private patch transaction directory")
    }
}
impl Drop for Temporary {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.0);
    }
}
fn relative(name: &str) -> Result<PathBuf> {
    let path = Path::new(name);
    ensure!(
        !name.is_empty()
            && !name.contains('\\')
            && path
                .components()
                .all(|c| matches!(c, Component::Normal(_) | Component::CurDir))
            && path.components().any(|c| matches!(c, Component::Normal(_))),
        "invalid patch path: {name:?}"
    );
    Ok(path.to_owned())
}
fn regular(root: &Path, name: &Path) -> Result<PathBuf> {
    let mut path = root.to_owned();
    for component in name.components() {
        path.push(component);
        ensure!(
            !fs::symlink_metadata(&path)?.file_type().is_symlink(),
            "symlink in patch path: {}",
            path.display()
        );
    }
    ensure!(
        path.is_file(),
        "not a regular patch input: {}",
        path.display()
    );
    Ok(path)
}
fn files(root: &Path) -> Result<BTreeSet<PathBuf>> {
    fn walk(base: &Path, dir: &Path, paths: &mut BTreeSet<PathBuf>) -> Result<()> {
        for entry in fs::read_dir(dir)? {
            let entry = entry?;
            if entry.file_type()?.is_dir() {
                walk(base, &entry.path(), paths)?;
            } else {
                paths.insert(entry.path().strip_prefix(base)?.to_owned());
            }
        }
        Ok(())
    }
    let mut paths = BTreeSet::new();
    walk(root, root, &mut paths)?;
    Ok(paths)
}
fn string<'a>(value: &'a Value, key: &str) -> Result<&'a str> {
    value
        .get(key)
        .and_then(Value::as_str)
        .with_context(|| format!("missing string {key}"))
}
/// Validate every input, patch identity, output path, and output identity before
/// publishing any changed source. Failed checks leave the generated tree intact.
fn apply(source: &Path, manifest_path: &Path) -> Result<Value> {
    let manifest: Value = serde_json::from_slice(&fs::read(manifest_path)?)?;
    ensure!(manifest["schema"] == 1, "invalid checked-patch schema");
    let identities = manifest["files"]
        .as_object()
        .context("missing patch files")?;
    let entries = manifest["patches"]
        .as_array()
        .context("missing patch list")?;
    ensure!(
        !identities.is_empty() && !entries.is_empty(),
        "empty patch transaction"
    );
    let mut patches = Vec::new();
    for entry in entries {
        let path = regular(
            manifest_path.parent().context("manifest parent")?,
            &relative(string(entry, "file")?)?,
        )?;
        ensure!(
            digest(&path)? == string(entry, "sha256")?,
            "patch identity mismatch: {}",
            path.display()
        );
        patches.push(path.canonicalize()?);
    }
    let mut names = BTreeSet::new();
    for (name, identity) in identities {
        let name = relative(name)?;
        ensure!(
            names.insert(name.clone()),
            "duplicate normalized patch path"
        );
        let path = regular(source, &name)?;
        ensure!(
            digest(&path)? == string(identity, "before_sha256")?,
            "upstream source identity mismatch: {}",
            name.display()
        );
    }
    let stage = Temporary::new()?;
    for (name, identity) in identities {
        let name = relative(name)?;
        let output = stage.0.join(&name);
        fs::create_dir_all(output.parent().context("output parent")?)?;
        let bytes = fs::read(source.join(&name))?;
        if identity["normalize_lf"].as_bool().unwrap_or(false) {
            let normalized: Vec<u8> = bytes
                .iter()
                .enumerate()
                .filter_map(|(index, &byte)| {
                    if byte == b'\r' && bytes.get(index + 1) == Some(&b'\n') {
                        None
                    } else {
                        Some(byte)
                    }
                })
                .collect();
            fs::write(output, normalized)?;
        } else {
            fs::write(output, bytes)?;
        }
    }
    for patch in patches {
        run(Command::new("git")
            .current_dir(&stage.0)
            .args(["apply", "--no-index", "--whitespace=nowarn", "--check"])
            .arg(&patch))?;
        run(Command::new("git")
            .current_dir(&stage.0)
            .args(["apply", "--no-index", "--whitespace=nowarn"])
            .arg(&patch))?;
    }
    ensure!(
        files(&stage.0)? == names,
        "patch changes paths outside its manifest"
    );
    for (name, identity) in identities {
        let path = regular(&stage.0, &relative(name)?)?;
        ensure!(
            digest(&path)? == string(identity, "after_sha256")?,
            "patched source identity mismatch: {name}"
        );
    }
    for name in names {
        fs::copy(stage.0.join(&name), source.join(name))?;
    }
    Ok(json!({"manifest_sha256": digest(manifest_path)?, "files": manifest["files"]}))
}
fn pinned(root: &Path, lock: &Value, name: &str) -> Result<PathBuf> {
    let path = root.join("vendor").join(name);
    let revision = capture(
        Command::new("git")
            .arg("-C")
            .arg(&path)
            .args(["rev-parse", "HEAD"]),
    )?;
    ensure!(
        revision == string(&lock[name], "commit")?,
        "{name} revision differs from sources.lock.json"
    );
    ensure!(
        capture(
            Command::new("git")
                .arg("-C")
                .arg(&path)
                .args(["status", "--porcelain"])
        )?
        .is_empty(),
        "{name} source has modifications"
    );
    if let Some(submodules) = lock[name]["submodules"].as_object() {
        for (name, revision) in submodules {
            let submodule = path.join(relative(name)?);
            ensure!(
                capture(
                    Command::new("git")
                        .arg("-C")
                        .arg(submodule)
                        .args(["rev-parse", "HEAD"])
                )? == revision.as_str().context("submodule revision")?,
                "submodule revision mismatch: {name}"
            );
        }
    }
    Ok(path)
}
// Match the former recipe's text-mode reads without altering copied donor files.
fn text(path: &Path) -> Result<String> {
    Ok(fs::read_to_string(path)?
        .replace("\r\n", "\n")
        .replace('\r', "\n"))
}

fn copy(source: impl AsRef<Path>, destination: impl AsRef<Path>) -> Result<()> {
    fs::copy(source.as_ref(), destination.as_ref()).with_context(|| {
        format!(
            "copy {} to {}",
            source.as_ref().display(),
            destination.as_ref().display()
        )
    })?;
    Ok(())
}

pub fn guest_sources(root: &Path, work: &Path, prefix: &str) -> Result<Prepared> {
    let support = root.join("support/guest");
    let lock: Value = serde_json::from_slice(&fs::read(support.join("sources.lock.json"))?)?;
    let vmdisp_source = pinned(root, &lock["sources"], "vmdisp9x")?;
    let wine_source = pinned(root, &lock["sources"], "wine9x")?;
    let glide_source = pinned(root, &lock["sources"], "qemu-xtra")?;
    let glu_source = pinned(root, &lock["sources"], "wine-glu")?.join("dlls/glu32/mipmap.c");
    fs::create_dir_all(work)?;
    let vmdisp = work.join("vmdisp9x");
    let wine = work.join("wine9x");
    let glide = work.join("openglide");
    for path in [&vmdisp, &wine, &glide] {
        ensure!(
            !path.exists(),
            "source staging must be fresh: {}",
            path.display()
        );
    }
    copy_tree(&vmdisp_source, &vmdisp)?;
    copy_tree(&wine_source, &wine)?;
    copy_tree(&glide_source.join("openglide"), &glide)?;
    for name in [
        "dg-call16.c",
        "dg-blit16.c",
        "dg-vxd.c",
        "dg-identity.h",
        "dg-identity-vxd.c",
        "dg-memory.h",
        "dg-memory-vxd.c",
        "dg-gl-vxd.c",
        "dg-timing-vxd.c",
        "dg-function-cache.h",
        "dg-owner.h",
        "dg-window9.h",
        "dg-window-vxd.c",
        "dg-window16.c",
        "dg-cursor16.c",
        "dg-cursor-vxd.c",
        "dg-cursor.h",
        "cursor32.h",
        "channel32.h",
        "blt32.h",
        "packet32.h",
    ] {
        copy(root.join("guest/win9x").join(name), vmdisp.join(name))?;
    }
    for name in ["gpu.h", "gl.h", "gl-arrays.h", "display-timing.h"] {
        copy(root.join("guest/include").join(name), vmdisp.join(name))?;
    }
    copy(
        root.join("guest/include/cursor.h"),
        vmdisp.join("dreamgpu-cursor.h"),
    )?;
    for name in [
        "dg-escape.h",
        "dg-gl-limits.h",
        "dg-gl-validate.h",
        "dg-gl-result.h",
    ] {
        copy(root.join("guest/nt/include").join(name), vmdisp.join(name))?;
    }
    copy(vmdisp.join("makefile"), vmdisp.join("makefile.dreamgpu"))?;
    let vmdisp_patches = apply(&vmdisp, &support.join("win9x/patches/base.json"))?;
    let vmdisp_timing_patches = apply(&vmdisp, &support.join("win9x/patches/timing.json"))?;
    // The checked ANSI discovery patch is part of the single production driver.
    copy(
        root.join("guest/win9x/dg-icd16.h"),
        vmdisp.join("dg-icd16.h"),
    )?;
    let vmdisp_icd_patches = apply(&vmdisp, &support.join("win9x/patches/icd.json"))?;
    let wine_patches = apply(&wine, &support.join("d3d/patches/base/manifest.json"))?;
    copy(
        root.join("guest/d3d/wine-diagnostics.h"),
        wine.join("ddraw/dg-wine-diagnostics.h"),
    )?;
    copy(
        root.join("guest/d3d/wine-blit-usage.h"),
        wine.join("wined3d/dg-wine-blit-usage.h"),
    )?;
    copy(
        root.join("guest/d3d/wine-row-copy.h"),
        wine.join("wined3d/dg-wine-row-copy.h"),
    )?;
    copy(
        root.join("guest/d3d/wine-map-policy.h"),
        wine.join("wined3d/dg-wine-map-policy.h"),
    )?;
    for name in ["vertex-pack", "vertex-array"] {
        copy(
            root.join(format!("guest/d3d/wine-{name}.h")),
            wine.join(format!("wined3d/dg-wine-{name}.h")),
        )?;
    }
    for directory in ["ddraw", "wined3d"] {
        copy(
            root.join("guest/d3d/wine-display-timing.h"),
            wine.join(directory).join("dg-wine-display-timing.h"),
        )?;
        for header in ["display-timing.h", "gpu.h"] {
            copy(
                root.join("guest/include").join(header),
                wine.join(directory).join(header),
            )?;
        }
    }
    copy(
        wine.join("ddraw/surface.c"),
        work.join("surface-diagnostic.c"),
    )?;
    copy(&glu_source, glide.join("dg-mipmap.c"))?;
    let glide_patches = apply(&glide, &support.join("glide/patches/manifest.json"))?;
    copy(
        root.join("guest/glide/texture-pool.cpp.inc"),
        glide.join("texture-pool.cpp.inc"),
    )?;
    copy(glide_source.join("LICENSE"), work.join("COPYING.LGPL-2.1"))?;
    let glu_text = text(&glu_source)?;
    let license = glu_text
        .split_once("*/")
        .context("SGI license terminator")?
        .0;
    fs::write(work.join("COPYING.SGI-B-2.0"), format!("{license}*/\n"))?;
    let mut unix = text(&glide.join("sdk2_unix.h.in"))?;
    for (name, value) in [
        ("UNSIGNED_CHAR", 1),
        ("UNSIGNED_SHORT", 2),
        ("UNSIGNED_INT", 4),
        ("UNSIGNED_LONG", 4),
        ("UNSIGNED_LONG_LONG", 8),
        ("INT_P", 4),
    ] {
        unix = unix.replace(&format!("@SIZEOF_{name}@"), &value.to_string());
    }
    fs::write(glide.join("sdk2_unix.h"), unix)?;
    fs::write(glide.join("config.h"), "#define SIZEOF_INT_P 4\n")?;
    fs::write(glide.join("dg-route.h"), GLIDE_ROUTE)?;
    fs::write(glide.join("dg-glu.c"), GLIDE_GLU)?;
    fs::write(glide.join("dg-new.cpp"), GLIDE_NEW)?;
    let frontend = text(&root.join("guest/opengl/frontend.def"))?;
    let mut imports = String::from("LIBRARY dgpugl.dll\nEXPORTS\n");
    for line in frontend.lines() {
        let Some((_, decorated)) = line.trim().split_once('=') else {
            continue;
        };
        let Some((name, bytes)) = decorated.split_once('@') else {
            continue;
        };
        ensure!(
            !name.is_empty()
                && name.chars().all(|c| c.is_ascii_alphanumeric() || c == '_')
                && !bytes.is_empty()
                && bytes.chars().all(|c| c.is_ascii_digit()),
            "invalid decorated GL export: {line}"
        );
        imports.push_str(decorated);
        imports.push('\n');
    }
    ensure!(imports.lines().count() > 2, "no decorated GL exports");
    fs::write(work.join("dg-imports.def"), imports)?;
    for target in [&wine, &glide] {
        copy(work.join("dg-imports.def"), target.join("dg-imports.def"))?;
    }
    let mut config = text(&wine.join("config.mk-sample"))?.replace("i686-w64-mingw32-", prefix);
    config.push_str("\nVERSION_BUILD=45\nTUNE=-march=pentium3 -mno-sse -mno-sse2 -mtune=core2 -fdata-sections -ffunction-sections\nTUNE_LD=,--no-insert-timestamp\n");
    fs::write(wine.join("config.mk"), config)?;
    for name in ["COPYING.LGPL-2.1", "COPYING.SGI-B-2.0"] {
        copy(work.join(name), glide.join(name))?;
    }
    for (source, name) in [
        (wine_source.join("nocrt/licence.txt"), "LICENSE.nocrt"),
        (
            wine_source.join("pthread9x/licence.txt"),
            "LICENSE.pthread9x",
        ),
        (root.join("guest/nt/COPYING"), "COPYING.GPL-2.0"),
    ] {
        copy(source, wine.join(name))?;
    }
    let mut notices = Vec::new();
    for name in files(&wine_source.join("pthread9x/src"))? {
        if name.extension().is_some_and(|ext| ext == "c") {
            let text = text(&wine_source.join("pthread9x/src").join(&name))?;
            let text = text.trim_start();
            if text.starts_with("/*") {
                if let Some(end) = text.find("*/") {
                    notices.push(format!(
                        "pthread9x/src/{}\n{}",
                        name.display(),
                        &text[..end + 2]
                    ));
                }
            }
        }
    }
    fs::write(
        wine.join("LICENSE.pthread9x-source-notices"),
        format!("{}\n", notices.join("\n\n")),
    )?;

    Ok(Prepared {
        vmdisp,
        wine,
        glide,
        patches: json!({"vmdisp9x": vmdisp_patches, "vmdisp9x-timing": vmdisp_timing_patches, "vmdisp9x-icd": vmdisp_icd_patches, "wine9x": {"base": wine_patches}, "openglide": glide_patches}),
    })
}

const GLIDE_ROUTE: &str = r#"#define ChoosePixelFormat wglChoosePixelFormat
#define DescribePixelFormat wglDescribePixelFormat
#define SetPixelFormat wglSetPixelFormat
#define GetPixelFormat wglGetPixelFormat
#define SwapBuffers wglSwapBuffers
#include <windows.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glu.h>
"#;

const GLIDE_GLU: &str = r#"#include <windows.h>
#include <GL/gl.h>
#include <GL/glu.h>
const GLubyte * WINAPI gluErrorString(GLenum error) {
    switch (error) {
    case GL_NO_ERROR: return (const GLubyte *)"no error";
    case GL_INVALID_ENUM: return (const GLubyte *)"invalid enum";
    case GL_INVALID_VALUE: return (const GLubyte *)"invalid value";
    case GL_INVALID_OPERATION: return (const GLubyte *)"invalid operation";
    case GL_OUT_OF_MEMORY: return (const GLubyte *)"out of memory";
    default: return (const GLubyte *)"OpenGL error";
    }
}
"#;

const GLIDE_NEW: &str = r#"#include <windows.h>
#include <stddef.h>
void *operator new(size_t bytes) {
    void *p = HeapAlloc(GetProcessHeap(), 0, bytes ? bytes : 1);
    if (!p) ExitProcess(ERROR_NOT_ENOUGH_MEMORY);
    return p;
}
void *operator new[](size_t bytes) { return operator new(bytes); }
void operator delete(void *p) noexcept { if (p) HeapFree(GetProcessHeap(), 0, p); }
void operator delete[](void *p) noexcept { operator delete(p); }
void operator delete(void *p, size_t) noexcept { operator delete(p); }
void operator delete[](void *p, size_t) noexcept { operator delete(p); }
"#;

#[cfg(test)]
mod tests {
    use super::*;
    fn fixture() -> (Temporary, PathBuf, PathBuf) {
        let temp = Temporary::new().unwrap();
        let source = temp.0.join("source");
        fs::create_dir(&source).unwrap();
        fs::write(source.join("input.c"), b"before\r\n").unwrap();
        let patch = temp.0.join("change.patch");
        fs::write(&patch, b"diff --git a/input.c b/input.c\n--- a/input.c\n+++ b/input.c\n@@ -1 +1 @@\n-before\n+after\n").unwrap();
        let after = temp.0.join("after");
        fs::write(&after, b"after\n").unwrap();
        let manifest = temp.0.join("manifest.json");
        let value = json!({"schema":1,"patches":[{"file":"change.patch","sha256":digest(&patch).unwrap()}],"files":{"input.c":{"before_sha256":digest(&source.join("input.c")).unwrap(),"after_sha256":digest(&after).unwrap(),"normalize_lf":true}}});
        fs::write(&manifest, serde_json::to_vec(&value).unwrap()).unwrap();
        (temp, source, manifest)
    }
    #[test]
    fn exact_patch_normalization_and_reapplication_rejection() {
        let (_temp, source, manifest) = fixture();
        let evidence = apply(&source, &manifest).unwrap();
        assert_eq!(evidence["manifest_sha256"], digest(&manifest).unwrap());
        assert_eq!(fs::read(source.join("input.c")).unwrap(), b"after\n");
        assert!(apply(&source, &manifest).is_err());
        assert_eq!(fs::read(source.join("input.c")).unwrap(), b"after\n");
    }
    #[test]
    fn failed_output_or_patch_identity_never_changes_source() {
        for invalid_patch in [false, true] {
            let (_temp, source, manifest) = fixture();
            let mut value: Value = serde_json::from_slice(&fs::read(&manifest).unwrap()).unwrap();
            if invalid_patch {
                value["patches"][0]["sha256"] = json!("wrong");
            } else {
                value["files"]["input.c"]["after_sha256"] = json!("wrong");
            }
            fs::write(&manifest, serde_json::to_vec(&value).unwrap()).unwrap();
            assert!(apply(&source, &manifest).is_err());
            assert_eq!(fs::read(source.join("input.c")).unwrap(), b"before\r\n");
        }
    }
    #[test]
    fn rejects_unrecorded_patch_output_before_publishing() {
        let (temp, source, manifest) = fixture();
        let patch = temp.0.join("change.patch");
        let mut text = fs::read_to_string(&patch).unwrap();
        text.push_str("diff --git a/extra.c b/extra.c\nnew file mode 100644\n--- /dev/null\n+++ b/extra.c\n@@ -0,0 +1 @@\n+unrecorded\n");
        fs::write(&patch, text).unwrap();
        let mut value: Value = serde_json::from_slice(&fs::read(&manifest).unwrap()).unwrap();
        value["patches"][0]["sha256"] = json!(digest(&patch).unwrap());
        fs::write(&manifest, serde_json::to_vec(&value).unwrap()).unwrap();
        assert!(apply(&source, &manifest).is_err());
        assert_eq!(fs::read(source.join("input.c")).unwrap(), b"before\r\n");
        assert!(!source.join("extra.c").exists());
    }
    #[test]
    fn rejects_escape_and_symlink_inputs() {
        for name in ["", ".", "../escape", "/absolute", "a/../../b", "a\\b"] {
            assert!(relative(name).is_err(), "{name}");
        }
        #[cfg(unix)]
        {
            let (temp, source, manifest) = fixture();
            fs::remove_file(source.join("input.c")).unwrap();
            std::os::unix::fs::symlink(temp.0.join("after"), source.join("input.c")).unwrap();
            assert!(apply(&source, &manifest).is_err());
        }
    }
    #[test]
    #[ignore = "requires initialized pinned vendor checkouts; stages source only"]
    fn pinned_guest_sources() {
        let root = Path::new(env!("CARGO_MANIFEST_DIR"))
            .parent()
            .unwrap()
            .parent()
            .unwrap();
        let output = std::env::var_os("DREAMGPU_PREPARE_TEST_OUTPUT")
            .map(PathBuf::from)
            .unwrap_or_else(|| root.join("target/prepare-rust-parity"));
        let output = if output.is_absolute() {
            output
        } else {
            root.join(output)
        };
        let prepared = guest_sources(root, &output, "i686-w64-mingw32-").unwrap();
        crate::write_json(&output.join("patch-evidence.json"), &prepared.patches).unwrap();
        assert!(prepared.vmdisp.join("makefile.dreamgpu").is_file());
        assert!(text(&prepared.vmdisp.join("control.c"))
            .unwrap()
            .contains("DgIcdGetInfo16(lpOutput)"));
        assert!(prepared.vmdisp.join("dg-icd16.h").is_file());
        assert!(prepared.wine.join("config.mk").is_file());
        assert!(prepared.glide.join("dg-imports.def").is_file());
        for (identity, debug) in [(true, false), (false, true), (true, true)] {
            let variant = output.join(format!("vmdisp-identity-{identity}-debug-{debug}"));
            copy_tree(&prepared.vmdisp, &variant).unwrap();
            // Historical identity instrumentation has its own checked control.c
            // patch. Test that independent recipe against its pinned donor input;
            // it is not an alternate production driver or package target.
            copy(
                root.join("vendor/vmdisp9x/control.c"),
                variant.join("control.c"),
            )
            .unwrap();
            let patches = root.join("support/guest/win9x/patches");
            if identity {
                apply(&variant, &patches.join("identity.json")).unwrap();
            }
            if debug {
                let name = if identity {
                    "identity-debug.json"
                } else {
                    "debug.json"
                };
                apply(&variant, &patches.join(name)).unwrap();
            }
        }
    }
}
