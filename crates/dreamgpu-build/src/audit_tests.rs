// SPDX-License-Identifier: GPL-2.0-or-later
//! Exercise the actual executable audits with bounded, synthetic structures.
//! These are parser contracts, not substitutes for linking the real drivers.
use super::{audit_ne, audit_vxd};
fn put16(bytes: &mut [u8], offset: usize, value: u16) {
    bytes[offset..offset + 2].copy_from_slice(&value.to_le_bytes());
}
fn put32(bytes: &mut [u8], offset: usize, value: u32) {
    bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
}
fn ne() -> Vec<u8> {
    let mut bytes = vec![0; 512];
    bytes[..2].copy_from_slice(b"MZ");
    put32(&mut bytes, 0x3c, 0x40);
    bytes[0x40..0x42].copy_from_slice(b"NE");
    put16(&mut bytes, 0x40 + 0x1c, 1); // One segment.
    put16(&mut bytes, 0x40 + 0x1e, 1); // One imported module.
    put16(&mut bytes, 0x40 + 0x22, 0x40); // Segment table at 0x80.
    put16(&mut bytes, 0x40 + 0x32, 4); // Sector unit is 16 bytes.
    put16(&mut bytes, 0x80, 0x10); // Segment bytes at 0x100.
    put16(&mut bytes, 0x82, 16);
    put16(&mut bytes, 0x84, 0x100); // Relocations follow segment.
    put16(&mut bytes, 0x110, 1);
    bytes[0x112] = 3; // 16:16 pointer relocation.
    put16(&mut bytes, 0x114, 0); // Source within the segment.
    put16(&mut bytes, 0x116, 1); // Internal segment target.
    bytes
}
fn le() -> Vec<u8> {
    let mut bytes = vec![0; 1024];
    bytes[..2].copy_from_slice(b"MZ");
    put32(&mut bytes, 0x3c, 0x40);
    bytes[0x40..0x42].copy_from_slice(b"LE");
    put32(&mut bytes, 0x40 + 0x28, 4096);
    put32(&mut bytes, 0x40 + 0x40, 0xc0); // Object table at 0x100.
    put32(&mut bytes, 0x40 + 0x44, 1);
    put32(&mut bytes, 0x40 + 0x48, 0xe0); // Page table at 0x120.
    put32(&mut bytes, 0x40 + 0x58, 0x100); // Resident names at 0x140.
    put32(&mut bytes, 0x40 + 0x5c, 0x140); // Entry table at 0x180.
    put32(&mut bytes, 0x40 + 0x80, 0x200); // File-absolute data pages.
    bytes[0x180] = 1;
    bytes[0x181] = 3;
    put16(&mut bytes, 0x182, 1);
    put32(&mut bytes, 0x185, 0); // DDB offset in object.
    put32(&mut bytes, 0x100, 4096);
    put32(&mut bytes, 0x108, 0x2000); // Preloaded object.
    put32(&mut bytes, 0x10c, 1); // First page-map entry.
    bytes[0x122] = 1; // Big-endian 24-bit physical page number.
    bytes[0x140..0x14a].copy_from_slice(b"\x08DREAMGPU\0");
    put16(&mut bytes, 0x204, 0x400);
    bytes[0x20c..0x214].copy_from_slice(b"DREAMGPU");
    bytes
}
#[test]
fn ne_internal_and_import_relocations_require_live_indices_and_source_bounds() {
    let original = ne();
    audit_ne(&original).unwrap();
    let mut imported = original.clone();
    imported[0x113] = 1;
    audit_ne(&imported).unwrap();
    put16(&mut imported, 0x116, 2);
    assert!(audit_ne(&imported).is_err());
    for (offset, value) in [(0x116, 2), (0x114, 16), (0x72, 17), (0x5c, 0)] {
        let mut malformed = original.clone();
        put16(&mut malformed, offset, value);
        assert!(audit_ne(&malformed).is_err(), "field {offset:x}");
    }
    for length in [0, 63, 0x85, 0x111, 0x115, 0x117] {
        assert!(audit_ne(&original[..length]).is_err(), "length {length}");
    }
}
#[test]
fn le_ddb_requires_present_object_extent_page_and_module_identity() {
    let original = le();
    audit_vxd(&original).unwrap();
    for (offset, value) in [
        (0x68, 0),     // Zero page size.
        (0x100, 79),   // Object too small for the DDB.
        (0x108, 0),    // Missing preload flag.
        (0x10c, 0),    // Missing page map.
        (0x185, 4096), // DDB beyond object.
    ] {
        let mut malformed = original.clone();
        put32(&mut malformed, offset, value);
        assert!(audit_vxd(&malformed).is_err(), "field {offset:x}");
    }
    for offset in [
        0x180, 0x181, 0x182, 0x122, 0x140, 0x141, 0x200, 0x205, 0x20c,
    ] {
        let mut malformed = original.clone();
        malformed[offset] ^= 0x80;
        assert!(audit_vxd(&malformed).is_err(), "byte {offset:x}");
    }
    for length in [0, 63, 0x181, 0x188, 0x10f, 0x122, 0x249] {
        assert!(audit_vxd(&original[..length]).is_err(), "length {length}");
    }
}
