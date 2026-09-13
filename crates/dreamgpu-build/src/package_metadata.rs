// SPDX-License-Identifier: GPL-2.0-or-later
//! Payload identities shared with the fixture tooling; source identity alone is
//! never accepted as proof of the packaged executable bytes.
use crate::{digest, write_json};
use anyhow::{ensure, Context, Result};
use serde_json::{json, Map, Value};
use sha2::{Digest, Sha256};
use std::{
    fs,
    path::{Component, Path},
};

const IDENTITY_SCHEME: &str = "sha256-json-utf8-sorted-compact";
const RUNNER: &str = "tools/common/DGPUBEN.EXE";
fn hash(value: &str) -> bool {
    value.len() == 64
        && value
            .bytes()
            .all(|c| c.is_ascii_digit() || (b'a'..=b'f').contains(&c))
}

pub fn write(stage: &Path, files: &Map<String, Value>, identity: &str) -> Result<()> {
    ensure!(hash(identity), "invalid runner build identity");
    ensure!(!files.is_empty(), "cannot publish empty package metadata");
    for (name, expected) in files {
        let relative = Path::new(name);
        ensure!(
            !name.is_empty()
                && !name.contains('\\')
                && relative
                    .components()
                    .all(|c| matches!(c, Component::Normal(_))),
            "invalid package member {name:?}"
        );
        let expected = expected.as_str().context("package hash must be a string")?;
        ensure!(hash(expected), "invalid package hash for {name}");
        let mut path = stage.to_owned();
        for part in relative.components() {
            path.push(part);
            ensure!(
                !fs::symlink_metadata(&path)?.file_type().is_symlink(),
                "symlink in package member {name}"
            );
        }
        ensure!(
            path.is_file() && digest(&path)? == expected,
            "package member changed before publication: {name}"
        );
    }
    let runner_hash = files
        .get(RUNNER)
        .context("package is missing its portable runner")?;
    let runner = json!({"schema":1,"runner_identity":identity,"runner_path":RUNNER,"runner_sha256":runner_hash,"status":"Build and payload identity only; guest installation has not been observed"});
    write_json(&stage.join("runner_manifest.json"), &runner)?;
    let mut payloads = files.clone();
    payloads.insert(
        "runner_manifest.json".into(),
        json!(digest(&stage.join("runner_manifest.json"))?),
    );
    // serde_json's default Map is sorted. Keep the ordering explicit even if a
    // future workspace enables preserve_order through feature unification.
    let sorted: std::collections::BTreeMap<_, _> = payloads.iter().collect();
    let payload_identity = format!("{:x}", Sha256::digest(serde_json::to_vec(&sorted)?));
    write_json(
        &stage.join("package.json"),
        &json!({"schema":2,"package":"dreamgpu-windows-os","identity_scheme":IDENTITY_SCHEME,"identity":payload_identity,"runner_identity":identity,"hardware_profile_version":1,"native_gpu_ipc_version":2,"files":payloads,"validation":"Source build and payload hashes; not runtime acceptance"}),
    )?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn canonical_identity_matches_runtime_reader_bytes_and_detects_drift() {
        let root = std::env::temp_dir().join(format!("dg-package-metadata-{}", std::process::id()));
        fs::create_dir(&root).unwrap();
        fs::create_dir_all(root.join("tools/common")).unwrap();
        fs::write(root.join(RUNNER), b"runner").unwrap();
        let mut files = Map::new();
        files.insert(RUNNER.into(), json!(digest(&root.join(RUNNER)).unwrap()));
        let identity = "a".repeat(64);
        write(&root, &files, &identity).unwrap();
        let manifest: Value =
            serde_json::from_slice(&fs::read(root.join("package.json")).unwrap()).unwrap();
        let runner: Value =
            serde_json::from_slice(&fs::read(root.join("runner_manifest.json")).unwrap()).unwrap();
        assert_eq!(runner["runner_identity"], identity);
        let canonical = format!(
            "{{\"runner_manifest.json\":\"{}\",\"{RUNNER}\":\"{}\"}}",
            digest(&root.join("runner_manifest.json")).unwrap(),
            digest(&root.join(RUNNER)).unwrap()
        );
        assert_eq!(
            manifest["identity"],
            format!("{:x}", Sha256::digest(canonical.as_bytes()))
        );
        fs::write(root.join(RUNNER), b"changed").unwrap();
        assert!(write(&root, &files, &identity).is_err());
        fs::remove_dir_all(root).unwrap();
    }
    #[test]
    fn rejects_unbound_runner_and_unsafe_names() {
        let mut files = Map::new();
        files.insert("../escape".into(), json!("a".repeat(64)));
        assert!(write(Path::new("/unused"), &files, &"b".repeat(64)).is_err());
        assert!(write(Path::new("/unused"), &Map::new(), &"b".repeat(64)).is_err());
        assert!(write(Path::new("/unused"), &files, "not a hash").is_err());
    }
}
