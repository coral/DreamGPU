// SPDX-License-Identifier: GPL-2.0-or-later
//! Propagate the exact checkout and generate bindings from the small canonical ABI.
use std::{collections::HashSet, fmt::Write, path::PathBuf};
fn main() {
    let root = std::env::var("CARGO_MANIFEST_DIR").expect("Cargo provides the source root");
    println!("cargo::metadata=root={root}");
    dreamgpu_build::build(std::path::Path::new(&root)).expect("DreamGPU component build failed");
    println!("cargo::rerun-if-changed=build.rs");
    println!("cargo::rerun-if-changed=include/dreamgpu/transport.h");
    let header = std::fs::read_to_string(PathBuf::from(&root).join("include/dreamgpu/transport.h"))
        .expect("DreamGPU's canonical transport ABI header is required");
    let mut generated =
        String::from("// Generated from include/dreamgpu/transport.h; do not edit.\n");
    let mut names = HashSet::new();
    for line in header.lines() {
        let mut words = line.split_whitespace();
        if words.next() != Some("#define") {
            continue;
        }
        let Some(name) = words.next().filter(|s| s.starts_with("DG_TRANSPORT_")) else {
            continue;
        };
        assert!(
            name.bytes()
                .all(|c| c.is_ascii_uppercase() || c.is_ascii_digit() || c == b'_'),
            "Invalid ABI constant identifier: {name}"
        );
        assert!(names.insert(name), "Duplicate ABI constant: {name}");
        let literal = words
            .next()
            .expect("ABI constants must have an unsigned integer value");
        let value: u32 = if let Some(hex) = literal.strip_prefix("0x") {
            u32::from_str_radix(hex, 16).expect("ABI hexadecimal constant")
        } else {
            literal.parse().expect("ABI integer constant")
        };
        // This header intentionally uses literal constants. Reject expressions rather
        // than accidentally accepting a changed ABI using only its first token.
        if let Some(next) = words.next() {
            assert!(
                next.starts_with("/*"),
                "ABI constants must be literals: {name}"
            );
        }
        writeln!(generated, "pub const {name}: u32 = {value};").unwrap();
    }
    assert!(
        names.contains("DG_TRANSPORT_VERSION") && names.contains("DG_TRANSPORT_PACKET_BYTES"),
        "Incomplete transport ABI header"
    );
    std::fs::write(
        PathBuf::from(std::env::var_os("OUT_DIR").unwrap()).join("gpu_transport.rs"),
        generated,
    )
    .expect("write generated ABI bindings");
}
