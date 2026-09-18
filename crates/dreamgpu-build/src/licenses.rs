// SPDX-License-Identifier: GPL-2.0-or-later
//! Consolidate the guest's notices before its package identity is computed.
use anyhow::{ensure, Result};
use std::{collections::BTreeMap, fs, path::Path};

fn collect(root: &Path, directory: &Path, files: &mut BTreeMap<String, Vec<u8>>) -> Result<()> {
    for entry in fs::read_dir(directory)? {
        let entry = entry?;
        let kind = entry.file_type()?;
        let path = entry.path();
        ensure!(!kind.is_symlink(), "symlink license: {}", path.display());
        if kind.is_dir() {
            collect(root, &path, files)?;
        } else {
            ensure!(kind.is_file(), "non-file license: {}", path.display());
            let name = path
                .strip_prefix(root)?
                .to_str()
                .ok_or_else(|| anyhow::anyhow!("non-UTF8 license path: {}", path.display()))?;
            files.insert(name.replace('\\', "/"), fs::read(&path)?);
        }
    }
    Ok(())
}

pub(crate) fn consolidate(stage: &Path) -> Result<()> {
    let directory = stage.join("licenses");
    ensure!(
        !directory.is_symlink() && directory.is_dir(),
        "guest package is missing its license directory"
    );
    let mut files = BTreeMap::new();
    collect(stage, &directory, &mut files)?;
    ensure!(!files.is_empty(), "guest package has no license notices");
    let destination = stage.join("LICENSES.txt");
    ensure!(!destination.exists(), "guest license bundle already exists");
    let mut bundle = b"DreamGPU licenses and third-party notices\r\n\r\n\
        All packaged license texts and attribution records follow, unchanged.\r\n\
        Section labels preserve their original package paths; references to\r\n\
        separate notice files refer to the corresponding sections below.\r\n"
        .to_vec();
    for (name, contents) in files {
        bundle.extend_from_slice(format!("\r\n===== {name} =====\r\n\r\n").as_bytes());
        bundle.extend_from_slice(&contents);
        bundle.extend_from_slice(b"\r\n");
    }
    fs::write(destination, bundle)?;
    // This is the unpublished, freshly installed package staging directory.
    // Hash and publish only the consolidated file, not the input notice tree.
    fs::remove_dir_all(directory)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bundle_preserves_every_notice_and_label_in_stable_order() -> Result<()> {
        let stage = std::env::temp_dir().join(format!("dg-licenses-{}", std::process::id()));
        fs::create_dir(&stage)?;
        struct Cleanup(std::path::PathBuf);
        impl Drop for Cleanup {
            fn drop(&mut self) {
                let _ = fs::remove_dir_all(&self.0);
            }
        }
        let _cleanup = Cleanup(stage.clone());
        fs::create_dir_all(stage.join("licenses/wine"))?;
        fs::write(
            stage.join("licenses/wine/NOTICE"),
            b"Copyright example\r\nNo final newline",
        )?;
        fs::write(stage.join("licenses/ATTRIBUTION.md"), b"See wine/NOTICE\n")?;
        fs::write(stage.join("payload.dll"), b"unchanged")?;
        consolidate(&stage)?;
        let bundle = fs::read_to_string(stage.join("LICENSES.txt"))?;
        assert!(bundle.contains("===== licenses/ATTRIBUTION.md =====\r\n\r\nSee wine/NOTICE\n\r\n"));
        assert!(bundle.ends_with(
            "===== licenses/wine/NOTICE =====\r\n\r\nCopyright example\r\nNo final newline\r\n"
        ));
        assert!(!stage.join("licenses").exists());
        assert_eq!(fs::read(stage.join("payload.dll"))?, b"unchanged");
        assert_eq!(fs::read_dir(&stage)?.count(), 2);
        // A missing source tree must never silently publish an empty bundle.
        assert!(consolidate(&stage).is_err());
        Ok(())
    }
}
