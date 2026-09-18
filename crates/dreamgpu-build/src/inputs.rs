// SPDX-License-Identifier: GPL-2.0-or-later
//! Watch native sources without recursively watching build-generated files.
use crate::capture;
use anyhow::Result;
use std::{
    collections::BTreeSet,
    path::{Path, PathBuf},
    process::Command,
};

fn git_sources(root: &Path, paths: &mut BTreeSet<PathBuf>) -> Result<()> {
    // A directory-level Cargo watch includes ignored Meson state (.wraplock)
    // and Python bytecode. Meson writes that state during an otherwise no-op
    // build, making the build script invalidate itself indefinitely.
    let files = capture(Command::new("git").arg("-C").arg(root).args([
        "ls-files",
        "--cached",
        "--others",
        "--exclude-standard",
        "-z",
    ]))?;
    for name in files.split('\0').filter(|name| !name.is_empty()) {
        let path = root.join(name);
        if path.is_dir() && path.join(".git").exists() {
            git_sources(&path, paths)?;
        } else if path.is_file() || path.is_symlink() {
            paths.insert(path);
        }
    }
    // New compilation units are referenced by watched Meson/Make definitions.
    // Deliberately do not watch Git's index: read-only Git operations may refresh
    // it, including the provenance collection performed by the native build.
    Ok(())
}

pub(crate) fn watch_git_sources(root: &Path) -> Result<()> {
    let mut paths = BTreeSet::new();
    git_sources(root, &mut paths)?;
    for path in paths {
        println!("cargo::rerun-if-changed={}", path.display());
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    #[test]
    fn watches_sources_and_nested_repositories_without_generated_state() {
        let root = std::env::temp_dir().join(format!("dreamgpu-inputs-{}", std::process::id()));
        fs::create_dir(&root).unwrap();
        struct Cleanup(PathBuf);
        impl Drop for Cleanup {
            fn drop(&mut self) {
                let _ = fs::remove_dir_all(&self.0);
            }
        }
        let _cleanup = Cleanup(root.clone());
        let git = |directory: &Path, args: &[&str]| {
            capture(Command::new("git").arg("-C").arg(directory).args(args)).unwrap()
        };
        git(&root, &["init", "-q"]);
        fs::create_dir(root.join("subprojects")).unwrap();
        fs::write(
            root.join(".gitignore"),
            "subprojects/.wraplock\n__pycache__/\n",
        )
        .unwrap();
        fs::write(root.join("meson.build"), "# build definition\n").unwrap();
        fs::write(root.join("device.c"), "/* tracked input */\n").unwrap();
        git(&root, &["add", "."]);
        fs::write(root.join("new-device.c"), "/* untracked input */\n").unwrap();
        let nested = root.join("firmware");
        fs::create_dir(&nested).unwrap();
        git(&nested, &["init", "-q"]);
        fs::write(nested.join("bios.c"), "/* nested input */\n").unwrap();
        git(&nested, &["add", "."]);
        git(
            &nested,
            &[
                "-c",
                "user.name=Test",
                "-c",
                "user.email=test@example.invalid",
                "-c",
                "commit.gpgsign=false",
                "commit",
                "-qm",
                "fixture",
            ],
        );
        git(&root, &["add", "firmware"]);
        let mut before = BTreeSet::new();
        git_sources(&root, &mut before).unwrap();
        for name in ["meson.build", "device.c", "new-device.c", "firmware/bios.c"] {
            assert!(before.contains(&root.join(name)), "missing {name}");
        }
        fs::write(root.join("subprojects/.wraplock"), "").unwrap();
        fs::create_dir(root.join("__pycache__")).unwrap();
        fs::write(root.join("__pycache__/script.pyc"), "generated").unwrap();
        let mut after = BTreeSet::new();
        git_sources(&root, &mut after).unwrap();
        assert_eq!(before, after);
        assert!(after.iter().all(|path| path.is_file()));
    }
}
