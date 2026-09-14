// SPDX-License-Identifier: GPL-2.0-or-later
//! Independent cursor channel. Only shape changes copy pixels (at most 32 KiB).
use crate::{NativeCursorFormat, NativeCursorShape};
use std::{
    os::fd::{AsRawFd, FromRawFd, OwnedFd},
    ptr::NonNull,
    sync::{
        Arc,
        atomic::{AtomicU32, Ordering},
    },
};

const MAGIC: u32 = 0x5255434a;
const HEADER: usize = 64;
const SLOTS: usize = 3;
const SLOT_BYTES: usize = 32832;
const MAPPING_BYTES: usize = HEADER + SLOTS * SLOT_BYTES;
pub(super) struct CursorSnapshot {
    pub shape: Arc<NativeCursorShape>,
    pub position_sequence: u64,
    pub x: i32,
    pub y: i32,
    pub flags: u8,
}
const READY: u32 = 2;
const READING: u32 = 3;

pub(super) struct CursorMapping {
    pointer: NonNull<u8>,
    _fd: OwnedFd,
    pub shape_generation: u64,
    pub position_sequence: u64,
}
impl Drop for CursorMapping {
    fn drop(&mut self) {
        unsafe {
            libc::munmap(self.pointer.as_ptr().cast(), MAPPING_BYTES);
        }
    }
}
impl CursorMapping {
    /// Takes ownership of the received descriptor, including every failure path.
    /// Producer contract: immutable header; slot CAS leases protect all shape bytes.
    pub unsafe fn from_fd(fd: i32, epoch: u64) -> Result<Self, String> {
        let fd = unsafe { OwnedFd::from_raw_fd(fd) };
        let mut stat: libc::stat = unsafe { std::mem::zeroed() };
        if unsafe { libc::fstat(fd.as_raw_fd(), &mut stat) } != 0
            || stat.st_size != MAPPING_BYTES as i64
        {
            return Err("Invalid native cursor mapping size".into());
        }
        let pointer = unsafe {
            libc::mmap(
                std::ptr::null_mut(),
                MAPPING_BYTES,
                libc::PROT_READ | libc::PROT_WRITE,
                libc::MAP_SHARED,
                fd.as_raw_fd(),
                0,
            )
        };
        if pointer == libc::MAP_FAILED {
            return Err(std::io::Error::last_os_error().to_string());
        }
        let mapping = Self {
            pointer: NonNull::new(pointer.cast()).unwrap(),
            _fd: fd,
            shape_generation: 0,
            position_sequence: 0,
        };
        let bytes = unsafe { std::slice::from_raw_parts(mapping.pointer.as_ptr(), HEADER) };
        // Do not read the producer's live generation or slot atomics as bytes.
        if u32_at(bytes, 0) != MAGIC
            || u32_at(bytes, 4) != 1
            || u32_at(bytes, 8) != 64
            || u32_at(bytes, 12) != SLOTS as u32
            || epoch == 0
            || u64_at(bytes, 16) != epoch
            || bytes[44..64].iter().any(|&byte| byte != 0)
        {
            return Err("Invalid native cursor mapping identity".into());
        }
        Ok(mapping)
    }

    pub fn latest_shape(&mut self) -> Result<Option<CursorSnapshot>, String> {
        let mut latest = None;
        let mut generation = self.shape_generation;
        for index in 0..SLOTS {
            let slot = unsafe {
                &*self
                    .pointer
                    .as_ptr()
                    .add(32 + index * 4)
                    .cast::<AtomicU32>()
            };
            if slot
                .compare_exchange(READY, READING, Ordering::AcqRel, Ordering::Acquire)
                .is_err()
            {
                continue;
            }
            let bytes = unsafe {
                std::slice::from_raw_parts(
                    self.pointer.as_ptr().add(HEADER + index * SLOT_BYTES),
                    SLOT_BYTES,
                )
            };
            let next = u64_at(bytes, 0);
            let shape = if next > generation {
                decode_shape(bytes).map(Some)
            } else {
                Ok(None)
            };
            // No GPU/consumer object retains a cursor slot, even after malformed input.
            slot.store(0, Ordering::Release);
            if let Some(shape) = shape? {
                latest = Some(shape);
                generation = next;
            }
        }
        self.shape_generation = generation;
        Ok(latest)
    }
}
fn u32_at(bytes: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes(bytes[offset..offset + 4].try_into().unwrap())
}
fn u64_at(bytes: &[u8], offset: usize) -> u64 {
    u64::from_le_bytes(bytes[offset..offset + 8].try_into().unwrap())
}
fn decode_shape(bytes: &[u8]) -> Result<CursorSnapshot, String> {
    let (width, height, hot_x, hot_y) = (
        u32_at(bytes, 8),
        u32_at(bytes, 12),
        u32_at(bytes, 16),
        u32_at(bytes, 20),
    );
    if width == 0 || height == 0 || width > 64 || height > 64 || hot_x >= width || hot_y >= height {
        return Err("Invalid native cursor geometry".into());
    }
    let end = 64 + width as usize * height as usize * 8;
    if u32_at(bytes, 28) != 0
        || bytes[52..64]
            .iter()
            .chain(bytes[end..].iter())
            .any(|&byte| byte != 0)
    {
        return Err("Native cursor reserved bytes are nonzero".into());
    }
    let format = match u32_at(bytes, 24) {
        1 => NativeCursorFormat::PremultipliedArgb,
        2 => NativeCursorFormat::AndXor,
        _ => return Err("Unknown native cursor pixel format".into()),
    };
    let pixels = bytes[64..end]
        .as_chunks::<8>()
        .0
        .iter()
        .map(|pixel| [u32_at(pixel, 0), u32_at(pixel, 4)])
        .collect();
    let shape = NativeCursorShape {
        width: width as u16,
        height: height as u16,
        hot_x: hot_x as u16,
        hot_y: hot_y as u16,
        format,
        pixels,
    };
    shape.validate()?;
    let position_sequence = u64_at(bytes, 32);
    let flags = u32_at(bytes, 48);
    if position_sequence == 0 || flags & !3 != 0 {
        return Err("Invalid native cursor position identity/flags".into());
    }
    Ok(CursorSnapshot {
        shape: Arc::new(shape),
        position_sequence,
        x: u32_at(bytes, 40) as i32,
        y: u32_at(bytes, 44) as i32,
        flags: flags as u8,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    fn mapping() -> CursorMapping {
        let path = std::env::temp_dir().join(format!("dg-cursor-{}", uuid::Uuid::new_v4()));
        let mut file = std::fs::OpenOptions::new()
            .create_new(true)
            .read(true)
            .write(true)
            .open(&path)
            .unwrap();
        let mut bytes = vec![0; MAPPING_BYTES];
        for (offset, value) in [(0, MAGIC), (4, 1), (8, 64), (12, 3), (16, 7)] {
            bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
        }
        file.write_all(&bytes).unwrap();
        std::fs::remove_file(path).unwrap();
        use std::os::fd::IntoRawFd;
        unsafe { CursorMapping::from_fd(file.into_raw_fd(), 7) }.unwrap()
    }
    fn publish(mapping: &CursorMapping, index: usize, generation: u64, rgb: u32) {
        let bytes = unsafe {
            std::slice::from_raw_parts_mut(
                mapping.pointer.as_ptr().add(HEADER + index * SLOT_BYTES),
                SLOT_BYTES,
            )
        };
        bytes.fill(0);
        bytes[0..8].copy_from_slice(&generation.to_le_bytes());
        for (offset, value) in [
            (8, 1u32),
            (12, 1),
            (24, 2),
            (32, generation as u32),
            (48, 3),
            (64, 0xffffff),
            (68, rgb),
        ] {
            bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
        }
        unsafe {
            &*mapping
                .pointer
                .as_ptr()
                .add(32 + index * 4)
                .cast::<AtomicU32>()
        }
        .store(READY, Ordering::Release);
    }
    #[test]
    fn coalesced_shapes_keep_newest_pixels_and_release_every_slot() {
        let mut mapping = mapping();
        publish(&mapping, 0, 3, 0x010203);
        publish(&mapping, 1, 7, 0x090807);
        publish(&mapping, 2, 5, 0x030405);
        let shape = mapping.latest_shape().unwrap().unwrap();
        assert_eq!(shape.shape.pixels, vec![[0xffffff, 0x090807]]);
        assert_eq!(mapping.shape_generation, 7);
        assert!(mapping.latest_shape().unwrap().is_none());
        // Reuse all slots immediately while the previous shape remains alive.
        for i in 0..3 {
            publish(&mapping, i, 8 + i as u64, 0x112233);
        }
        assert_eq!(shape.shape.pixels, vec![[0xffffff, 0x090807]]);
        assert_eq!(
            mapping.latest_shape().unwrap().unwrap().shape.pixels,
            vec![[0xffffff, 0x112233]]
        );
    }
    #[test]
    fn malformed_shape_does_not_pin_slot_or_advance_generation() {
        let mut mapping = mapping();
        publish(&mapping, 0, 1, 0x1000000);
        assert!(mapping.latest_shape().is_err());
        assert_eq!(mapping.shape_generation, 0);
        assert_eq!(
            unsafe { &*mapping.pointer.as_ptr().add(32).cast::<AtomicU32>() }
                .load(Ordering::Acquire),
            0
        );
        publish(&mapping, 0, 2, 0xffffff);
        assert!(mapping.latest_shape().unwrap().is_some());
    }
    #[test]
    fn atomic_shape_position_rejects_old_moves_and_reconnect_clears_shape() {
        let map = mapping();
        let mailbox = Arc::new(std::sync::Mutex::new(CursorMailbox::default()));
        let mut receiver = CursorReceiver::new(mailbox.clone());
        let mut packet = [0u8; 24];
        packet[0] = b'C';
        packet[8..16].copy_from_slice(&7u64.to_le_bytes());
        receiver
            .receive(packet, Some(unsafe { libc::dup(map._fd.as_raw_fd()) }))
            .unwrap();
        publish(&map, 0, 50, 0x123456);
        packet[0] = b'S';
        packet[8..16].copy_from_slice(&50u64.to_le_bytes());
        receiver.receive(packet, None).unwrap();
        let accepted = mailbox.lock().unwrap().state.clone().unwrap();
        packet[0] = b'P';
        packet[1] = 3;
        packet[8..16].copy_from_slice(&49u64.to_le_bytes());
        packet[16..20].copy_from_slice(&(-123i32).to_le_bytes());
        assert!(!receiver.receive(packet, None).unwrap());
        assert!(accepted.same_presentation(mailbox.lock().unwrap().state.as_ref().unwrap()));
        packet[8..16].copy_from_slice(&51u64.to_le_bytes());
        receiver.receive(packet, None).unwrap();
        let moved = mailbox.lock().unwrap().state.clone().unwrap();
        assert_eq!(moved.x, -123);
        assert!(Arc::ptr_eq(
            accepted.shape.as_ref().unwrap(),
            moved.shape.as_ref().unwrap()
        ));
        // A repeated accepted position advances the transport revision but not
        // presentation identity, so the event loop can remain idle.
        packet[8..16].copy_from_slice(&52u64.to_le_bytes());
        receiver.receive(packet, None).unwrap();
        assert!(moved.same_presentation(mailbox.lock().unwrap().state.as_ref().unwrap()));
        receiver.disconnect();
        assert!(!mailbox.lock().unwrap().state.as_ref().unwrap().enabled);
        let fresh = mapping();
        packet = [0; 24];
        packet[0] = b'C';
        packet[8..16].copy_from_slice(&7u64.to_le_bytes());
        receiver
            .receive(packet, Some(unsafe { libc::dup(fresh._fd.as_raw_fd()) }))
            .unwrap();
        let reset = mailbox.lock().unwrap().state.clone().unwrap();
        assert!(reset.shape.is_none());
        assert!(!reset.enabled);
        assert!(reset.revision > moved.revision);
        publish(&fresh, 0, 1, 0xabcdef);
        packet[0] = b'S';
        packet[8..16].copy_from_slice(&1u64.to_le_bytes());
        receiver.receive(packet, None).unwrap();
        assert_eq!(
            mailbox
                .lock()
                .unwrap()
                .state
                .as_ref()
                .unwrap()
                .shape
                .as_ref()
                .unwrap()
                .pixels,
            vec![[0xffffff, 0xabcdef]]
        );
    }
    #[test]
    fn cursor_layout_matches_native_header() {
        let header = crate::cursor::WIRE_HEADER;
        for (name, value) in [
            ("DG_CURSOR_TRANSPORT_HEADER_BYTES", HEADER),
            ("DG_CURSOR_TRANSPORT_SLOT_COUNT", SLOTS),
            ("DG_CURSOR_TRANSPORT_SLOT_BYTES", SLOT_BYTES),
            ("DG_CURSOR_TRANSPORT_MAPPING_BYTES", MAPPING_BYTES),
        ] {
            let line = header
                .lines()
                .find(|line| line.starts_with(&format!("#define {name} ")))
                .unwrap();
            assert_eq!(
                line.split_whitespace()
                    .nth(2)
                    .unwrap()
                    .parse::<usize>()
                    .unwrap(),
                value
            );
        }
    }
}

#[derive(Default)]
pub(super) struct CursorMailbox {
    pub state: Option<crate::NativeCursor>,
    pub error: Option<String>,
}

pub(super) struct CursorReceiver {
    mapping: Option<CursorMapping>,
    mailbox: Arc<std::sync::Mutex<CursorMailbox>>,
}
impl CursorReceiver {
    pub fn new(mailbox: Arc<std::sync::Mutex<CursorMailbox>>) -> Self {
        Self {
            mapping: None,
            mailbox,
        }
    }
    pub fn receive(&mut self, packet: [u8; 24], fd: Option<i32>) -> Result<bool, String> {
        if packet[0] == b'C' {
            let fd = fd.ok_or("Native cursor mapping is missing its descriptor")?;
            // from_fd consumes the descriptor before any packet validation fails.
            let mapping = unsafe { CursorMapping::from_fd(fd, u64_at(&packet, 8)) }?;
            if packet[1..8].iter().any(|&x| x != 0) {
                return Err("Malformed cursor mapping notification".into());
            }
            self.mapping = Some(mapping);
            let mut mailbox = self.mailbox.lock().unwrap();
            let revision = mailbox
                .state
                .as_ref()
                .map_or(1, |s| s.revision.wrapping_add(1));
            mailbox.state = Some(crate::NativeCursor {
                revision,
                ..Default::default()
            });
            return Ok(true);
        }
        if let Some(fd) = fd {
            unsafe {
                libc::close(fd);
            }
            return Err("Unexpected descriptor on cursor notification".into());
        }
        let mapping = self
            .mapping
            .as_mut()
            .ok_or("Cursor update arrived before its mapping")?;
        let mut mailbox = self.mailbox.lock().unwrap();
        let state = mailbox.state.as_mut().ok_or("Cursor update has no state")?;
        match packet[0] {
            b'S' => {
                if packet[1..8].iter().any(|&x| x != 0) || u64_at(&packet, 8) == 0 {
                    return Err("Malformed cursor shape notification".into());
                }
                if let Some(snapshot) = mapping.latest_shape()? {
                    state.shape = Some(snapshot.shape);
                    if snapshot.position_sequence > mapping.position_sequence {
                        state.x = snapshot.x;
                        state.y = snapshot.y;
                        state.visible = snapshot.flags & 1 != 0;
                        state.enabled = snapshot.flags & 2 != 0;
                        mapping.position_sequence = snapshot.position_sequence;
                    }
                } else {
                    return Ok(false);
                }
            }
            b'P' => {
                let sequence = u64_at(&packet, 8);
                if packet[1] & !3 != 0 || packet[2..8].iter().any(|&x| x != 0) || sequence == 0 {
                    return Err("Malformed cursor position notification".into());
                }
                if sequence <= mapping.position_sequence {
                    return Ok(false);
                }
                state.x = u32_at(&packet, 16) as i32;
                state.y = u32_at(&packet, 20) as i32;
                state.visible = packet[1] & 1 != 0;
                state.enabled = packet[1] & 2 != 0;
                mapping.position_sequence = sequence;
            }
            _ => return Err("Unknown native cursor notification".into()),
        }
        state.revision = state.revision.wrapping_add(1);
        Ok(true)
    }
    pub fn fail(&self, error: String) {
        self.mailbox.lock().unwrap().error.get_or_insert(error);
    }
    pub fn disconnect(&mut self) {
        self.mapping = None;
        if let Some(state) = self.mailbox.lock().unwrap().state.as_mut() {
            state.visible = false;
            state.enabled = false;
            state.revision = state.revision.wrapping_add(1);
        }
    }
}
