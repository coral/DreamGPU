// SPDX-License-Identifier: GPL-2.0-or-later
//! Bindings to the canonical include/dreamgpu/transport.h ABI.
//! Numeric constants are generated at build time; wire encoding remains explicit little-endian.

use crate::GpuHostInfo;
use std::io;

pub mod abi {
    include!(concat!(env!("OUT_DIR"), "/gpu_transport.rs"));
}

pub const MAGIC: u32 = abi::DG_TRANSPORT_MAGIC;
pub const VERSION: u32 = abi::DG_TRANSPORT_VERSION;
pub const BYTES: usize = abi::DG_TRANSPORT_PACKET_BYTES as usize;
pub const HELLO: u32 = abi::DG_TRANSPORT_KIND_HELLO;
pub const FRAME: u32 = abi::DG_TRANSPORT_KIND_FRAME;
pub const RELEASE: u32 = abi::DG_TRANSPORT_KIND_RELEASE;
pub const ERROR: u32 = abi::DG_TRANSPORT_KIND_ERROR;
pub const DRAWABLE: u32 = abi::DG_TRANSPORT_KIND_DRAWABLE;
pub const CPU_DESKTOP: u32 = abi::DG_TRANSPORT_KIND_CPU_DESKTOP;
pub const DESKTOP_OP: u32 = abi::DG_TRANSPORT_KIND_DESKTOP_OP;
pub const DESKTOP_REPLY: u32 = abi::DG_TRANSPORT_KIND_DESKTOP_REPLY;
pub const RESOURCE_DROP: u32 = abi::DG_TRANSPORT_KIND_RESOURCE_DROP;
pub const RESET: u32 = abi::DG_TRANSPORT_KIND_RESET;
pub const TOP_LEFT: u32 = abi::DG_TRANSPORT_FLAG_TOP_LEFT;
pub const READY_FENCE: u32 = abi::DG_TRANSPORT_FLAG_READY_FENCE;
pub const RETAIN_FOR_DESKTOP: u32 = abi::DG_TRANSPORT_FLAG_RETAIN_FOR_DESKTOP;
pub const BGRA: u32 = abi::DG_TRANSPORT_FORMAT_ARGB8888;
pub const MAX_SLOTS: usize = abi::DG_TRANSPORT_MAX_EXPORT_SLOTS as usize;
pub const MAX_DRAWABLES: usize = abi::DG_TRANSPORT_MAX_DRAWABLES as usize;
pub const CPU_SLOT_BASE: u32 = abi::DG_TRANSPORT_CPU_SLOT_BASE;
pub const MAX_CPU_SLOTS: usize = abi::DG_TRANSPORT_MAX_CPU_SLOTS as usize;
pub const MAX_CPU_BYTES: u64 = abi::DG_TRANSPORT_MAX_CPU_BYTES as u64;

pub fn invalid(message: impl Into<String>) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidData, message.into())
}

#[derive(Clone, Debug)]
pub struct Packet(pub [u8; BYTES]);

impl Packet {
    pub fn new(kind: u32) -> Self {
        let mut packet = Self([0; BYTES]);
        packet.set32(0, MAGIC);
        packet.set32(4, VERSION);
        packet.set32(8, kind);
        packet.set32(12, BYTES as u32);
        packet
    }
    pub fn get32(&self, offset: usize) -> u32 {
        u32::from_le_bytes(self.0[offset..offset + 4].try_into().unwrap())
    }
    pub fn get64(&self, offset: usize) -> u64 {
        u64::from_le_bytes(self.0[offset..offset + 8].try_into().unwrap())
    }
    pub fn set32(&mut self, offset: usize, value: u32) {
        self.0[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
    }
    pub fn set64(&mut self, offset: usize, value: u64) {
        self.0[offset..offset + 8].copy_from_slice(&value.to_le_bytes());
    }
    pub fn kind(&self) -> u32 {
        self.get32(8)
    }
    pub fn validate(&self) -> io::Result<()> {
        if self.get32(0) != MAGIC || self.get32(4) != VERSION || self.get32(12) != BYTES as u32 {
            return Err(invalid("Unsupported GPU transport header"));
        }
        Ok(())
    }
    pub fn hello(host: &GpuHostInfo, service: Option<&str>) -> io::Result<Self> {
        let mut packet = Self::new(HELLO);
        if let Some(service) = service {
            packet.set32(16, 1);
            put_string(&mut packet.0[24..80], service)?;
        } else {
            packet.set32(16, 2);
            if host.device_uuid == [0; 16] {
                return Err(invalid(
                    "GPU renderer did not supply its physical device UUID",
                ));
            }
            packet.0[80..96].copy_from_slice(&host.device_uuid);
            put_string(
                &mut packet.0[96..128],
                host.render_node
                    .as_deref()
                    .ok_or_else(|| invalid("GPU renderer did not supply its DRM render node"))?,
            )?;
        }
        Ok(packet)
    }
}

fn put_string(destination: &mut [u8], value: &str) -> io::Result<()> {
    if value.len() >= destination.len() || value.as_bytes().contains(&0) {
        return Err(invalid("GPU handshake string is too long or contains NUL"));
    }
    destination[..value.len()].copy_from_slice(value.as_bytes());
    Ok(())
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Slot {
    pub index: u32,
    pub epoch: u64,
    pub generation: u64,
}

impl Slot {
    pub fn release(self) -> Packet {
        let mut packet = Packet::new(RELEASE);
        packet.set32(40, self.index);
        packet.set64(56, self.epoch);
        packet.set64(64, self.generation);
        packet
    }
}

#[derive(Debug)]
pub struct Frame {
    pub kind: u32,
    pub width: u32,
    pub height: u32,
    pub stride: u32,
    pub offset: u32,
    pub modifier: u64,
    pub ready_fence: bool,
    pub retain: bool,
    pub slot: Slot,
    pub client: u32,
    pub drawable: u32,
    pub uuid: [u8; 16],
}

impl Frame {
    pub fn parse(packet: &Packet) -> io::Result<Self> {
        packet.validate()?;
        if !matches!(packet.kind(), FRAME | DRAWABLE) {
            return Err(invalid("Expected GPU frame record"));
        }
        let flags = packet.get32(16);
        let width = packet.get32(20);
        let height = packet.get32(24);
        let stride = packet.get32(28);
        if flags & TOP_LEFT == 0
            || flags & !(TOP_LEFT | READY_FENCE | RETAIN_FOR_DESKTOP) != 0
            || (packet.kind() == FRAME && flags & RETAIN_FOR_DESKTOP != 0)
            || width == 0
            || height == 0
            || width > 16384
            || height > 16384
            || width.checked_mul(4).is_none_or(|row| row > stride)
            || stride > 1024 * 1024
            || packet.get32(32) != BGRA
            || packet.get32(40) >= MAX_SLOTS as u32
            || packet.0[52..56]
                .iter()
                .chain(&packet.0[96..128])
                .any(|byte| *byte != 0)
        {
            return Err(invalid(
                "Invalid GPU image geometry, flags, slot or reserved fields",
            ));
        }
        Ok(Self {
            kind: packet.kind(),
            width,
            height,
            stride,
            offset: packet.get32(36),
            modifier: packet.get64(72),
            ready_fence: flags & READY_FENCE != 0,
            retain: flags & RETAIN_FOR_DESKTOP != 0,
            slot: Slot {
                index: packet.get32(40),
                epoch: packet.get64(56),
                generation: packet.get64(64),
            },
            client: packet.get32(44),
            drawable: packet.get32(48),
            uuid: packet.0[80..96].try_into().unwrap(),
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn rust_layout_matches_authoritative_qemu_header() {
        let header = include_str!("../../include/dreamgpu/transport.h");
        for (name, expected) in [
            ("DG_TRANSPORT_PLATFORM_MACOS", 1),
            ("DG_TRANSPORT_PLATFORM_LINUX", 2),
            ("DG_TRANSPORT_HELLO_PLATFORM", 16),
            ("DG_TRANSPORT_HELLO_MACH_SERVICE", 24),
            ("DG_TRANSPORT_HELLO_MACH_SERVICE_LEN", 56),
            ("DG_TRANSPORT_HELLO_RENDER_NODE", 96),
            ("DG_TRANSPORT_HELLO_RENDER_NODE_LEN", 32),
            ("DG_TRANSPORT_CPU_SEED", 1),
            ("DG_TRANSPORT_CPU_PATCH", 2),
            ("DG_TRANSPORT_CPU_RETURN", 3),
            ("DG_TRANSPORT_CPU_OFF_SUBTYPE", 16),
            ("DG_TRANSPORT_CPU_OFF_ALLOCATION", 72),
            ("DG_TRANSPORT_CPU_OFF_DST_X", 80),
            ("DG_TRANSPORT_CPU_OFF_DST_Y", 84),
            ("DG_TRANSPORT_CPU_OFF_LEGACY_EPOCH", 88),
            ("DG_TRANSPORT_CPU_OFF_LEGACY_FRAME", 96),
            ("DG_TRANSPORT_DESKTOP_FILL", 1),
            ("DG_TRANSPORT_DESKTOP_COPY", 2),
            ("DG_TRANSPORT_DESKTOP_BLIT", 3),
            ("DG_TRANSPORT_DESKTOP_READBACK", 4),
            ("DG_TRANSPORT_DESKTOP_DISCARD", 5),
            ("DG_TRANSPORT_DESKTOP_BLIT_RELEASE", 1),
            ("DG_TRANSPORT_DESKTOP_OFF_OPCODE", 16),
            ("DG_TRANSPORT_DESKTOP_OFF_DST_X", 20),
            ("DG_TRANSPORT_DESKTOP_OFF_DST_Y", 24),
            ("DG_TRANSPORT_DESKTOP_OFF_WIDTH", 28),
            ("DG_TRANSPORT_DESKTOP_OFF_HEIGHT", 32),
            ("DG_TRANSPORT_DESKTOP_OFF_SRC_X", 36),
            ("DG_TRANSPORT_DESKTOP_OFF_SRC_Y", 40),
            ("DG_TRANSPORT_DESKTOP_OFF_GPU_SLOT", 52),
            ("DG_TRANSPORT_DESKTOP_OFF_GPU_EPOCH", 72),
            ("DG_TRANSPORT_DESKTOP_OFF_GPU_FRAME", 80),
            ("DG_TRANSPORT_DESKTOP_OFF_TOKEN", 88),
            ("DG_TRANSPORT_DESKTOP_OFF_COLOR", 96),
            ("DG_TRANSPORT_DESKTOP_OFF_FLAGS", 100),
            ("DG_TRANSPORT_MAGIC", MAGIC),
            ("DG_TRANSPORT_VERSION", VERSION),
            ("DG_TRANSPORT_PACKET_BYTES", BYTES as u32),
            ("DG_TRANSPORT_MAX_EXPORT_SLOTS", MAX_SLOTS as u32),
            ("DG_TRANSPORT_MAX_DRAWABLES", MAX_DRAWABLES as u32),
            ("DG_TRANSPORT_KIND_HELLO", HELLO),
            ("DG_TRANSPORT_KIND_FRAME", FRAME),
            ("DG_TRANSPORT_KIND_RELEASE", RELEASE),
            ("DG_TRANSPORT_KIND_ERROR", ERROR),
            ("DG_TRANSPORT_KIND_DRAWABLE", DRAWABLE),
            ("DG_TRANSPORT_KIND_CPU_DESKTOP", CPU_DESKTOP),
            ("DG_TRANSPORT_KIND_DESKTOP_OP", DESKTOP_OP),
            ("DG_TRANSPORT_KIND_DESKTOP_REPLY", DESKTOP_REPLY),
            ("DG_TRANSPORT_KIND_RESOURCE_DROP", RESOURCE_DROP),
            ("DG_TRANSPORT_KIND_RESET", RESET),
            ("DG_TRANSPORT_CPU_SLOT_BASE", CPU_SLOT_BASE),
            ("DG_TRANSPORT_MAX_CPU_SLOTS", MAX_CPU_SLOTS as u32),
            ("DG_TRANSPORT_MAX_CPU_BYTES", MAX_CPU_BYTES as u32),
            ("DG_TRANSPORT_FLAG_TOP_LEFT", TOP_LEFT),
            ("DG_TRANSPORT_FLAG_READY_FENCE", READY_FENCE),
            ("DG_TRANSPORT_FLAG_RETAIN_FOR_DESKTOP", RETAIN_FOR_DESKTOP),
            ("DG_TRANSPORT_FORMAT_ARGB8888", BGRA),
            ("DG_TRANSPORT_OFF_MAGIC", 0),
            ("DG_TRANSPORT_OFF_VERSION", 4),
            ("DG_TRANSPORT_OFF_KIND", 8),
            ("DG_TRANSPORT_OFF_SIZE", 12),
            ("DG_TRANSPORT_OFF_FLAGS", 16),
            ("DG_TRANSPORT_OFF_WIDTH", 20),
            ("DG_TRANSPORT_OFF_HEIGHT", 24),
            ("DG_TRANSPORT_OFF_STRIDE", 28),
            ("DG_TRANSPORT_OFF_FOURCC", 32),
            ("DG_TRANSPORT_OFF_OFFSET", 36),
            ("DG_TRANSPORT_OFF_SLOT", 40),
            ("DG_TRANSPORT_OFF_CLIENT", 44),
            ("DG_TRANSPORT_OFF_DRAWABLE", 48),
            ("DG_TRANSPORT_OFF_EPOCH", 56),
            ("DG_TRANSPORT_OFF_GENERATION", 64),
            ("DG_TRANSPORT_OFF_MODIFIER", 72),
            ("DG_TRANSPORT_OFF_DEVICE_UUID", 80),
        ] {
            let value = header
                .lines()
                .find_map(|line| {
                    let mut words = line.split_whitespace();
                    (words.next() == Some("#define") && words.next() == Some(name))
                        .then(|| words.next().unwrap())
                })
                .unwrap_or_else(|| panic!("Missing {name}"));
            let actual = if let Some(hex) = value.strip_prefix("0x") {
                u32::from_str_radix(hex, 16).unwrap()
            } else {
                value.parse().unwrap()
            };
            assert_eq!(actual, expected, "{name}");
        }
    }
    #[test]
    fn release_preserves_epoch_and_generation_without_stray_payload() {
        let slot = Slot {
            index: 5,
            epoch: 0x123456789,
            generation: u64::MAX,
        };
        let packet = slot.release();
        packet.validate().unwrap();
        assert_eq!(packet.kind(), RELEASE);
        assert_eq!(packet.get32(40), slot.index);
        assert_eq!(packet.get64(56), slot.epoch);
        assert_eq!(packet.get64(64), slot.generation);
        assert!(packet.0[16..40]
            .iter()
            .chain(&packet.0[72..])
            .all(|b| *b == 0));
    }

    #[test]
    fn frame_validation_rejects_wrong_orientation_formats_bounds_and_headers() {
        let mut packet = Packet::new(DRAWABLE);
        packet.set32(16, TOP_LEFT);
        packet.set32(20, 32);
        packet.set32(24, 16);
        packet.set32(28, 128);
        packet.set32(32, BGRA);
        Frame::parse(&packet).unwrap();
        for (offset, value) in [
            (0, 0),
            (4, VERSION + 1),
            (12, 127),
            (16, 0),
            (16, 4),
            (20, 0),
            (24, 16385),
            (28, 127),
            (32, 0),
            (40, 96),
            (52, 1),
            (96, 1),
        ] {
            let mut broken = packet.clone();
            broken.set32(offset, value);
            assert!(
                Frame::parse(&broken).is_err(),
                "offset {offset}, value {value}"
            );
        }
    }
}
