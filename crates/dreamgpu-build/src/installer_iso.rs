// SPDX-License-Identifier: GPL-2.0-or-later
//! Portable, deterministic ISO 9660 Level 1 media containing DREAMGPU.EXE.
//! Layout follows ECMA-119 sections 8.4, 9.1 and 9.4; no host tools required.
//! https://www.ecma-international.org/wp-content/uploads/ECMA-119_4th_edition_june_2019.pdf
use anyhow::{Context, Result};
use std::{
    fs::{self, File},
    io::{self, Read, Write},
    path::Path,
};

const BLOCK: usize = 2048;
const ROOT: u32 = 20;
const DATA: u32 = 21;
pub(crate) const NAME: &str = "dreamgpu-setup.iso";

pub(crate) fn build(exe: &Path, output: &Path) -> Result<()> {
    let mut source = File::open(exe)?;
    let length =
        u32::try_from(source.metadata()?.len()).context("installer exceeds ISO file limit")?;
    let temporary = output.join(".dreamgpu-setup.iso.new");
    let mut image = File::create(&temporary)?;
    write_image(&mut source, length, &mut image)?;
    image.sync_all()?;
    drop(image);
    fs::rename(temporary, output.join(NAME))?;
    eprintln!("DreamGPU: created {}", output.join(NAME).display());
    Ok(())
}

fn both16(field: &mut [u8], value: u16) {
    field[..2].copy_from_slice(&value.to_le_bytes());
    field[2..4].copy_from_slice(&value.to_be_bytes());
}

fn both32(field: &mut [u8], value: u32) {
    field[..4].copy_from_slice(&value.to_le_bytes());
    field[4..8].copy_from_slice(&value.to_be_bytes());
}

fn record(name: &[u8], sector: u32, length: u32, directory: bool) -> Vec<u8> {
    let mut record = vec![0; (33 + name.len() + 1) & !1];
    record[0] = record.len() as u8;
    both32(&mut record[2..10], sector);
    both32(&mut record[10..18], length);
    // Fixed 2000-01-01 UTC timestamps keep identical installers reproducible.
    record[18..25].copy_from_slice(&[100, 1, 1, 0, 0, 0, 0]);
    record[25] = if directory { 2 } else { 0 };
    both16(&mut record[28..32], 1);
    record[32] = name.len() as u8;
    record[33..33 + name.len()].copy_from_slice(name);
    record
}

fn write_image(source: &mut impl Read, length: u32, output: &mut impl Write) -> io::Result<()> {
    // System area, primary descriptor, terminator, L/M path tables, root directory.
    let mut metadata = vec![0; DATA as usize * BLOCK];
    let primary = &mut metadata[16 * BLOCK..17 * BLOCK];
    primary[..7].copy_from_slice(b"\x01CD001\x01");
    primary[8..72].fill(b' ');
    primary[40..54].copy_from_slice(b"DREAMGPU_SETUP");
    let sectors = DATA + length.div_ceil(BLOCK as u32);
    both32(&mut primary[80..88], sectors);
    both16(&mut primary[120..124], 1);
    both16(&mut primary[124..128], 1);
    both16(&mut primary[128..132], BLOCK as u16);
    both32(&mut primary[132..140], 10);
    primary[140..144].copy_from_slice(&18u32.to_le_bytes());
    primary[148..152].copy_from_slice(&19u32.to_be_bytes());
    primary[156..190].copy_from_slice(&record(&[0], ROOT, BLOCK as u32, true));
    primary[190..813].fill(b' ');
    // Unspecified volume dates: sixteen ASCII zeroes and a zero UTC offset.
    for start in [813, 830, 847, 864] {
        primary[start..start + 16].fill(b'0');
    }
    primary[881] = 1;
    metadata[17 * BLOCK..17 * BLOCK + 7].copy_from_slice(b"\xffCD001\x01");
    for (sector, extent, parent) in [
        (18, ROOT.to_le_bytes(), 1u16.to_le_bytes()),
        (19, ROOT.to_be_bytes(), 1u16.to_be_bytes()),
    ] {
        let table = &mut metadata[sector * BLOCK..sector * BLOCK + 10];
        table[0] = 1;
        table[2..6].copy_from_slice(&extent);
        table[6..8].copy_from_slice(&parent);
    }
    let mut offset = ROOT as usize * BLOCK;
    for entry in [
        record(&[0], ROOT, BLOCK as u32, true),
        record(&[1], ROOT, BLOCK as u32, true),
        record(b"DREAMGPU.EXE;1", DATA, length, false),
    ] {
        metadata[offset..offset + entry.len()].copy_from_slice(&entry);
        offset += entry.len();
    }
    output.write_all(&metadata)?;
    let copied = io::copy(&mut source.take(u64::from(length) + 1), output)?;
    if copied != u64::from(length) {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "installer size changed while creating ISO",
        ));
    }
    let padding = (BLOCK - length as usize % BLOCK) % BLOCK;
    output.write_all(&[0; BLOCK][..padding])
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn media_preserves_files_at_sector_boundaries() {
        for size in [0, 1, BLOCK - 1, BLOCK, BLOCK + 1, 3 * BLOCK + 17] {
            let input: Vec<_> = (0..size).map(|n| n as u8).collect();
            let mut image = Vec::new();
            write_image(&mut &input[..], size as u32, &mut image).unwrap();
            assert_eq!(image.len() % BLOCK, 0);
            let primary = &image[16 * BLOCK..17 * BLOCK];
            assert_eq!(&primary[..7], b"\x01CD001\x01");
            for count in [
                u32::from_le_bytes(primary[80..84].try_into().unwrap()),
                u32::from_be_bytes(primary[84..88].try_into().unwrap()),
            ] {
                assert_eq!(count as usize * BLOCK, image.len());
            }
            // Follow the directory's extent and file records as a reader does.
            let root = u32::from_le_bytes(primary[158..162].try_into().unwrap()) as usize;
            let mut offset = root * BLOCK;
            for _ in 0..2 {
                offset += image[offset] as usize;
            }
            let file = &image[offset..offset + image[offset] as usize];
            assert_eq!(&file[33..33 + file[32] as usize], b"DREAMGPU.EXE;1");
            let extent = u32::from_le_bytes(file[2..6].try_into().unwrap()) as usize * BLOCK;
            let length = u32::from_le_bytes(file[10..14].try_into().unwrap()) as usize;
            assert_eq!(length, size);
            assert_eq!(&image[extent..extent + length], input);
            assert!(image[extent + length..].iter().all(|&byte| byte == 0));
        }
    }

    #[test]
    fn changed_input_length_is_rejected() {
        for length in [2, 4] {
            let error = write_image(&mut &b"abc"[..], length, &mut Vec::new()).unwrap_err();
            assert_eq!(error.kind(), io::ErrorKind::InvalidData);
        }
    }

    #[test]
    fn rebuild_replaces_media_and_identical_inputs_are_reproducible() -> Result<()> {
        let directory =
            std::env::temp_dir().join(format!("dreamgpu-installer-iso-{}", std::process::id()));
        fs::create_dir(&directory)?;
        struct Cleanup(std::path::PathBuf);
        impl Drop for Cleanup {
            fn drop(&mut self) {
                let _ = fs::remove_dir_all(&self.0);
            }
        }
        let _cleanup = Cleanup(directory.clone());
        let exe = directory.join("dreamgpu.exe");
        fs::write(&exe, b"first installer")?;
        build(&exe, &directory)?;
        let original = fs::read(directory.join(NAME))?;
        build(&exe, &directory)?;
        assert_eq!(fs::read(directory.join(NAME))?, original);
        fs::write(&exe, b"updated installer")?;
        build(&exe, &directory)?;
        assert_ne!(fs::read(directory.join(NAME))?, original);
        assert!(!directory.join(".dreamgpu-setup.iso.new").exists());
        Ok(())
    }
}
