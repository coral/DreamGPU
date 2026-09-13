// SPDX-License-Identifier: GPL-2.0-or-later
//! Reliable ordered desktop records and immutable CPU allocation ownership.

use super::*;
use crate::desktop::{DesktopBatch, DesktopOp, DesktopRect, DesktopReply};
use crate::{FrameAllocation, FrameLease, FramePixels, FrameStorage};
use std::sync::atomic::AtomicU64;

pub(super) const MAX_PENDING: usize = 64;
static NEXT_EPOCH: AtomicU64 = AtomicU64::new(1);

struct Mapping {
    address: *mut u8,
    length: usize,
}
// SAFETY: one fixed mmap is owned until the last allocation owner is dropped.
// CPU pixels are immutable until their independent slot lease is released.
unsafe impl Send for Mapping {}
unsafe impl Sync for Mapping {}
unsafe impl FrameAllocation for Mapping {
    fn base_ptr(&self) -> *mut u8 {
        self.address
    }
    fn len(&self) -> usize {
        self.length
    }
}
impl Drop for Mapping {
    fn drop(&mut self) {
        unsafe {
            libc::munmap(self.address.cast(), self.length);
        }
    }
}
struct Pixels {
    mapping: Arc<Mapping>,
    slot: Slot,
    releases: Arc<Releases>,
}
impl FramePixels for Pixels {
    fn bytes(&self) -> &[u8] {
        // SAFETY: immutable producer slot, mapped span validated against fstat.
        unsafe { std::slice::from_raw_parts(self.mapping.address, self.mapping.length) }
    }
    fn storage(&self) -> Option<FrameStorage> {
        Some(FrameStorage {
            allocation: self.mapping.clone(),
            offset: 0,
            length: self.mapping.length,
        })
    }
}
impl Drop for Pixels {
    fn drop(&mut self) {
        self.releases.send(self.slot);
    }
}

enum Operation {
    Ready(DesktopOp),
    Drop {
        client: u32,
        drawable: u32,
        cutoff: (u64, u64),
    },
    Reset,
    Resource {
        slot: Slot,
        client: u32,
        drawable: u32,
        source: DesktopRect,
        x: u32,
        y: u32,
        final_reference: bool,
        discard: bool,
    },
}
struct Pending {
    epoch: u64,
    sequence: u64,
    operation: Operation,
    returned: Option<(u64, u64)>,
    authority_revision: u64,
}

#[derive(Default)]
pub(super) struct Desktop {
    pub(super) resources: HashMap<Slot, GpuDrawableFrame>,
    pending: VecDeque<Pending>,
    wire_epoch: u64,
    authority_revision: u64,
    epoch: u64,
    sequence: u64,
    dropped: HashMap<(u32, u32), (u64, u64)>,
    received: HashMap<(u32, u32), (u64, u64)>,
    pub(super) retired: Vec<(u32, u32, (u64, u64))>,
    pub(super) legacy_anchor: Option<(u64, u64)>,
}
impl Desktop {
    pub(super) fn full(&self) -> bool {
        self.pending.len() >= MAX_PENDING
    }
    pub(super) fn clear(&mut self) {
        self.pending.clear();
        self.resources.clear();
    }
    pub(super) fn receive_image(
        &mut self,
        client: u32,
        drawable: u32,
        slot: Slot,
    ) -> io::Result<bool> {
        let key = (client, drawable);
        let identity = (slot.epoch, slot.generation);
        let dropped = self
            .dropped
            .get(&key)
            .is_some_and(|cutoff| identity <= *cutoff);
        if self
            .dropped
            .get(&key)
            .is_some_and(|cutoff| identity >= *cutoff)
        {
            // Mach images are ordered on their one sender/receiver port. Once
            // the cutoff arrives, no older image for this drawable remains.
            self.dropped.remove(&key);
            self.received.remove(&key);
        }
        if !dropped {
            if !self.received.contains_key(&key) && self.received.len() >= MAX_SLOTS {
                return Err(invalid("Too many unretired GPU resources"));
            }
            self.received.insert(key, identity);
        }
        Ok(!dropped)
    }

    pub(super) fn push(
        &mut self,
        packet: Packet,
        mut fds: Vec<OwnedFd>,
        releases: &Arc<Releases>,
    ) -> io::Result<bool> {
        packet.validate()?;
        if matches!(packet.kind(), wire::RESOURCE_DROP | wire::RESET) {
            if !fds.is_empty() || self.full() {
                return Err(invalid("Invalid graphics lifecycle record"));
            }
            for (offset, &value) in packet.0.iter().enumerate().skip(16) {
                let used = if packet.kind() == wire::RESET {
                    (88..104).contains(&offset)
                } else {
                    (44..52).contains(&offset) || (56..72).contains(&offset)
                };
                if !used && value != 0 {
                    return Err(invalid("Nonzero graphics lifecycle reserved field"));
                }
            }
            if packet.kind() == wire::RESOURCE_DROP {
                let client = packet.get32(44);
                let drawable = packet.get32(48);
                let cutoff = (packet.get64(56), packet.get64(64));
                if client == 0 || drawable == 0 || cutoff.0 == 0 || cutoff.1 == 0 {
                    return Err(invalid("Resource drop requires its last image identity"));
                }
                self.pending.push_back(Pending {
                    epoch: 0,
                    sequence: 0,
                    operation: Operation::Drop {
                        client,
                        drawable,
                        cutoff,
                    },
                    returned: None,
                    authority_revision: self.authority_revision,
                });
            } else {
                let anchor = (packet.get64(88), packet.get64(96));
                if anchor.0 == 0 || anchor.1 == 0 {
                    return Err(invalid("Graphics reset requires a CPU coherence anchor"));
                }
                self.authority_revision += 1;
                self.pending.push_back(Pending {
                    epoch: NEXT_EPOCH.fetch_add(1, Ordering::Relaxed),
                    sequence: 1,
                    operation: Operation::Reset,
                    returned: Some(anchor),
                    authority_revision: self.authority_revision,
                });
                self.wire_epoch = 0;
                self.sequence = 0;
            }
            return Ok(packet.kind() == wire::RESET);
        }
        let raw_epoch = packet.get64(56);
        let sequence = packet.get64(64);
        let seeded = packet.kind() == wire::CPU_DESKTOP && packet.get32(16) == 1;
        // A retained image can be released before a desktop seed or after
        // returning to CPU ownership. It consumes no desktop sequence.
        let detached = packet.kind() == wire::DESKTOP_OP
            && packet.get32(16) == 5
            && raw_epoch == 0
            && sequence == 0;
        if ((!detached) && (raw_epoch == 0 || sequence == 0)) || self.full() {
            return Err(invalid("Invalid desktop identity or queue overflow"));
        }
        if seeded {
            if raw_epoch <= self.wire_epoch || sequence != 1 {
                return Err(invalid("Desktop seed must start a fresh epoch"));
            }
        } else if !detached
            && (raw_epoch != self.wire_epoch || self.sequence.checked_add(1) != Some(sequence))
        {
            return Err(invalid(
                "Desktop sequence is missing a seed or preceding operation",
            ));
        }
        let mut returned = None;
        let operation = match packet.kind() {
            wire::CPU_DESKTOP => {
                if fds.len() != 1 {
                    return Err(invalid(
                        "CPU desktop needs exactly one allocation descriptor",
                    ));
                }
                let subtype = packet.get32(16);
                let width = packet.get32(20);
                let height = packet.get32(24);
                let stride = packet.get32(28);
                let length = packet.get64(72);
                let x = packet.get32(80);
                let y = packet.get32(84);
                let slot = Slot {
                    index: packet.get32(40),
                    epoch: raw_epoch,
                    generation: sequence,
                };
                if !(1..=3).contains(&subtype)
                    || width == 0
                    || height == 0
                    || width > 16384
                    || height > 16384
                    || stride < width * 4
                    || u64::from(stride) * u64::from(height) > length
                    || length == 0
                    || length > wire::MAX_CPU_BYTES
                    || packet.get32(32) != wire::BGRA
                    || packet.get32(36) != 0
                    || !(wire::CPU_SLOT_BASE..wire::CPU_SLOT_BASE + wire::MAX_CPU_SLOTS as u32)
                        .contains(&slot.index)
                    || packet.0[44..56]
                        .iter()
                        .chain(&packet.0[104..])
                        .any(|b| *b != 0)
                    || (subtype != 2 && (x != 0 || y != 0))
                    || (subtype != 3 && packet.0[88..104].iter().any(|b| *b != 0))
                {
                    return Err(invalid(
                        "Invalid CPU desktop allocation, geometry or reserved fields",
                    ));
                }
                if subtype == 3 {
                    let anchor = (packet.get64(88), packet.get64(96));
                    if anchor.0 == 0 || anchor.1 == 0 {
                        return Err(invalid(
                            "ReturnCpu requires a coherent legacy display anchor",
                        ));
                    }
                    returned = Some(anchor);
                }
                let fd = fds.pop().unwrap();
                let mut stat = unsafe { std::mem::zeroed::<libc::stat>() };
                if unsafe { libc::fstat(fd.as_raw_fd(), &mut stat) } != 0 {
                    return Err(io::Error::last_os_error());
                }
                if stat.st_size < 0 || stat.st_size as u64 != length {
                    return Err(invalid("CPU allocation size differs from its descriptor"));
                }
                let address = unsafe {
                    libc::mmap(
                        std::ptr::null_mut(),
                        length as usize,
                        libc::PROT_READ,
                        libc::MAP_SHARED,
                        fd.as_raw_fd(),
                        0,
                    )
                };
                if address == libc::MAP_FAILED {
                    return Err(io::Error::last_os_error());
                }
                let mapping = Arc::new(Mapping {
                    address: address.cast(),
                    length: length as usize,
                });
                {
                    let mut slots = releases.slots.lock().unwrap();
                    if slots
                        .keys()
                        .filter(|(_, i)| *i >= wire::CPU_SLOT_BASE)
                        .count()
                        >= wire::MAX_CPU_SLOTS
                        || slots.keys().any(|(_, i)| *i == slot.index)
                    {
                        return Err(invalid("CPU producer reused a live allocation slot"));
                    }
                    slots.insert((slot.epoch, slot.index), slot.generation);
                }
                let frame = FrameLease {
                    pixels: Arc::new(Pixels {
                        mapping,
                        slot,
                        releases: releases.clone(),
                    }),
                    width,
                    height,
                    stride,
                    format: PixelFormat::Bgra8,
                    generation: sequence,
                    damage: None,
                };
                Operation::Ready(match subtype {
                    1 => DesktopOp::Seed(frame),
                    2 => DesktopOp::Patch {
                        x,
                        y,
                        pixels: frame,
                    },
                    3 => DesktopOp::ReturnCpu(frame),
                    _ => unreachable!(),
                })
            }
            wire::DESKTOP_OP => {
                if !fds.is_empty() || packet.0[104..].iter().any(|b| *b != 0) {
                    return Err(invalid(
                        "Desktop operation has unexpected descriptors or reserved fields",
                    ));
                }
                let opcode = packet.get32(16);
                let x = packet.get32(20);
                let y = packet.get32(24);
                let width = packet.get32(28);
                let height = packet.get32(32);
                let source = DesktopRect {
                    x: packet.get32(36),
                    y: packet.get32(40),
                    width,
                    height,
                };
                if opcode != 5 && (width == 0 || height == 0 || width > 16384 || height > 16384) {
                    return Err(invalid("Invalid desktop operation rectangle"));
                }
                let flags = packet.get32(100);
                if (opcode != 3 && flags != 0) || flags & !1 != 0 {
                    return Err(invalid("Invalid desktop operation flags"));
                }
                for (offset, &value) in packet.0.iter().enumerate().skip(20) {
                    let used = (56..72).contains(&offset)
                        || (opcode != 5 && (20..36).contains(&offset))
                        || (matches!(opcode, 2 | 3) && (36..44).contains(&offset))
                        || (matches!(opcode, 3 | 5) && (44..88).contains(&offset))
                        || (opcode == 3 && (100..104).contains(&offset))
                        || (opcode == 4 && (88..96).contains(&offset))
                        || (opcode == 1 && (96..100).contains(&offset));
                    if !used && value != 0 {
                        return Err(invalid("Nonzero unused desktop operation field"));
                    }
                }
                if matches!(opcode, 3 | 5)
                    && (packet.get32(44) == 0
                        || packet.get32(48) == 0
                        || packet.get32(52) >= wire::MAX_SLOTS as u32
                        || packet.get64(72) == 0
                        || packet.get64(80) == 0)
                {
                    return Err(invalid("Invalid referenced GPU resource identity"));
                }
                match opcode {
                    1 => Operation::Ready(DesktopOp::Fill {
                        rect: DesktopRect {
                            x,
                            y,
                            width,
                            height,
                        },
                        bgra: packet.get32(96).to_le_bytes(),
                    }),
                    2 => Operation::Ready(DesktopOp::Copy { source, x, y }),
                    3 | 5 => Operation::Resource {
                        slot: Slot {
                            index: packet.get32(52),
                            epoch: packet.get64(72),
                            generation: packet.get64(80),
                        },
                        client: packet.get32(44),
                        drawable: packet.get32(48),
                        source,
                        x,
                        y,
                        final_reference: flags & 1 != 0 || opcode == 5,
                        discard: opcode == 5,
                    },
                    4 => {
                        let token = packet.get64(88);
                        if token == 0 {
                            return Err(invalid("Readback requires a nonzero completion token"));
                        }
                        let releases = releases.clone();
                        Operation::Ready(DesktopOp::Readback {
                            rect: DesktopRect {
                                x,
                                y,
                                width,
                                height,
                            },
                            reply: DesktopReply::callback(move |result| {
                                releases.reply(raw_epoch, sequence, token, result)
                            }),
                        })
                    }
                    _ => return Err(invalid("Unknown ordered desktop operation")),
                }
            }
            _ => return Err(invalid("Expected ordered desktop record")),
        };
        if seeded {
            self.wire_epoch = raw_epoch;
            self.epoch = NEXT_EPOCH.fetch_add(1, Ordering::Relaxed);
            self.legacy_anchor = None;
        }
        if !detached {
            self.sequence = sequence;
        }
        if seeded || returned.is_some() {
            self.authority_revision += 1;
        }
        self.pending.push_back(Pending {
            epoch: if detached { 0 } else { self.epoch },
            sequence,
            operation,
            returned,
            authority_revision: self.authority_revision,
        });
        Ok(seeded)
    }

    pub(super) fn take(&mut self) -> io::Result<Option<(DesktopBatch, bool)>> {
        loop {
            // Lifecycle records share the reliable FIFO so a destroy cannot remove
            // an image before a preceding queued clipped blit takes its own lease.
            while let Some(Pending {
                operation:
                    Operation::Drop {
                        client,
                        drawable,
                        cutoff,
                    },
                ..
            }) = self.pending.front()
            {
                let (client, drawable, cutoff) = (*client, *drawable, *cutoff);
                if !self.dropped.contains_key(&(client, drawable))
                    && self.dropped.len() >= MAX_SLOTS
                {
                    return Err(invalid("GPU resource tombstone bound exceeded"));
                }
                let cutoff = *self
                    .dropped
                    .entry((client, drawable))
                    .and_modify(|old| *old = (*old).max(cutoff))
                    .or_insert(cutoff);
                self.resources.retain(|slot, image| {
                    image.client != client
                        || image.drawable != drawable
                        || (slot.epoch, slot.generation) > cutoff
                });
                self.retired.push((client, drawable, cutoff));
                if self
                    .received
                    .get(&(client, drawable))
                    .is_some_and(|last| *last >= cutoff)
                {
                    self.dropped.remove(&(client, drawable));
                    self.received.remove(&(client, drawable));
                }
                self.pending.pop_front();
            }
            // Release only the exact retained frame, in FIFO order. A detached
            // discard never creates a renderer batch or changes CPU authority.
            if let Some(Pending {
                epoch: 0,
                sequence: 0,
                operation:
                    Operation::Resource {
                        slot,
                        client,
                        drawable,
                        discard: true,
                        ..
                    },
                ..
            }) = self.pending.front()
            {
                let Some(resource) = self.resources.get(slot) else {
                    return Ok(None);
                };
                if resource.client != *client || resource.drawable != *drawable {
                    return Err(invalid("Discard referenced another client's GPU resource"));
                }
                self.resources.remove(slot);
                self.pending.pop_front();
                continue;
            }
            break;
        }
        let Some(head) = self.pending.front() else {
            return Ok(None);
        };
        let resolved = if let Operation::Resource {
            slot,
            client,
            drawable,
            source,
            x,
            y,
            final_reference,
            discard,
        } = &head.operation
        {
            let Some(resource) = self.resources.get(slot) else {
                return Ok(None);
            };
            if resource.client != *client || resource.drawable != *drawable {
                return Err(invalid("Desktop referenced another client's GPU resource"));
            }
            let operation = if *discard {
                DesktopOp::Barrier
            } else {
                DesktopOp::Blit {
                    image: resource.frame.clone(),
                    source: *source,
                    x: *x,
                    y: *y,
                }
            };
            if *final_reference {
                self.resources.remove(slot);
            }
            Some(operation)
        } else {
            None
        };
        let pending = self.pending.pop_front().unwrap();
        let operation = resolved.unwrap_or_else(|| match pending.operation {
            Operation::Ready(op) => op,
            Operation::Reset => {
                self.received.clear();
                for (slot, resource) in self.resources.drain() {
                    self.dropped
                        .entry((resource.client, resource.drawable))
                        .and_modify(|old| *old = (*old).max((slot.epoch, slot.generation)))
                        .or_insert((slot.epoch, slot.generation));
                }
                DesktopOp::Reset
            }
            _ => unreachable!(),
        });
        let returned =
            pending.returned.is_some() && pending.authority_revision == self.authority_revision;
        if returned {
            self.legacy_anchor = pending.returned;
        }
        Ok(Some((
            DesktopBatch {
                epoch: pending.epoch,
                sequence: pending.sequence,
                operations: vec![operation],
            },
            returned,
        )))
    }
}

pub(super) fn send_reply(
    stream: &mut UnixStream,
    epoch: u64,
    sequence: u64,
    token: u64,
    result: std::result::Result<FrameLease, String>,
) -> io::Result<()> {
    let mut packet = Packet::new(wire::DESKTOP_REPLY);
    packet.set64(56, epoch);
    packet.set64(64, sequence);
    packet.set64(88, token);
    let Ok(frame) = result else {
        packet.set32(16, 1);
        return stream.write_all(&packet.0);
    };
    if !frame.is_valid() || frame.format != PixelFormat::Bgra8 {
        packet.set32(16, 1);
        return stream.write_all(&packet.0);
    }
    let stride = frame.width * 4;
    let length = u64::from(stride) * u64::from(frame.height);
    if length > wire::MAX_CPU_BYTES {
        return Err(invalid("Readback reply exceeds bounded allocation"));
    }
    let name = std::ffi::CString::new(format!(
        "/jr-{}",
        &uuid::Uuid::new_v4().simple().to_string()[..24]
    ))
    .unwrap();
    let raw = unsafe {
        libc::shm_open(
            name.as_ptr(),
            libc::O_CREAT | libc::O_EXCL | libc::O_RDWR,
            0o600,
        )
    };
    if raw < 0 {
        return Err(io::Error::last_os_error());
    }
    let file = unsafe { std::fs::File::from_raw_fd(raw) };
    unsafe {
        libc::shm_unlink(name.as_ptr());
        libc::fcntl(raw, libc::F_SETFD, libc::FD_CLOEXEC);
    }
    file.set_len(length)?;
    let address = unsafe {
        libc::mmap(
            std::ptr::null_mut(),
            length as usize,
            libc::PROT_READ | libc::PROT_WRITE,
            libc::MAP_SHARED,
            raw,
            0,
        )
    };
    if address == libc::MAP_FAILED {
        return Err(io::Error::last_os_error());
    }
    // This explicit guest CPU readback is the only path copying GPU results
    // into a reply allocation. Ordinary present/import never maps pixels.
    for y in 0..frame.height as usize {
        unsafe {
            std::ptr::copy_nonoverlapping(
                frame.bytes().as_ptr().add(y * frame.stride as usize),
                address.cast::<u8>().add(y * stride as usize),
                stride as usize,
            );
        }
    }
    unsafe {
        libc::munmap(address, length as usize);
    }
    packet.set32(20, frame.width);
    packet.set32(24, frame.height);
    packet.set32(28, stride);
    packet.set64(72, length);
    send_fd_record(stream, &packet, file.as_raw_fd())
}

pub(super) fn send_fd_record(stream: &mut UnixStream, packet: &Packet, fd: i32) -> io::Result<()> {
    let mut control = [0u64; 4];
    let mut vector = libc::iovec {
        iov_base: packet.0.as_ptr().cast_mut().cast(),
        iov_len: 1,
    };
    let mut message: libc::msghdr = unsafe { std::mem::zeroed() };
    message.msg_iov = &mut vector;
    message.msg_iovlen = 1;
    message.msg_control = control.as_mut_ptr().cast();
    message.msg_controllen = unsafe { libc::CMSG_SPACE(4) as _ };
    unsafe {
        let header = libc::CMSG_FIRSTHDR(&message);
        (*header).cmsg_level = libc::SOL_SOCKET;
        (*header).cmsg_type = libc::SCM_RIGHTS;
        (*header).cmsg_len = libc::CMSG_LEN(4) as _;
        std::ptr::write_unaligned(libc::CMSG_DATA(header).cast::<i32>(), fd);
    }
    #[cfg(target_os = "macos")]
    unsafe {
        let enabled = 1i32;
        libc::setsockopt(
            stream.as_raw_fd(),
            libc::SOL_SOCKET,
            libc::SO_NOSIGPIPE,
            (&enabled as *const i32).cast(),
            4,
        );
    }
    loop {
        #[cfg(target_os = "linux")]
        let flags = libc::MSG_NOSIGNAL;
        #[cfg(not(target_os = "linux"))]
        let flags = 0;
        let written = unsafe { libc::sendmsg(stream.as_raw_fd(), &message, flags) };
        if written == 1 {
            break;
        }
        let error = io::Error::last_os_error();
        if error.kind() != io::ErrorKind::Interrupted {
            return Err(error);
        }
    }
    stream.write_all(&packet.0[1..])
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::{Seek, SeekFrom};

    fn releases() -> (Arc<Releases>, mpsc::Receiver<Output>) {
        let (stream, _peer) = UnixStream::pair().unwrap();
        let (sender, receiver) = mpsc::sync_channel(128);
        (
            Arc::new(Releases {
                sender,
                slots: Arc::default(),
                stream,
                closed: AtomicBool::new(false),
                reply_bytes: AtomicU64::new(0),
                state: std::sync::Weak::new(),
            }),
            receiver,
        )
    }
    fn cpu_packet(subtype: u32, sequence: u64) -> (Packet, OwnedFd) {
        let path =
            std::env::temp_dir().join(format!("dg-cpu-test-{}", uuid::Uuid::new_v4().simple()));
        let mut file = std::fs::OpenOptions::new()
            .create_new(true)
            .read(true)
            .write(true)
            .open(&path)
            .unwrap();
        std::fs::remove_file(path).unwrap();
        file.set_len(65536).unwrap();
        file.write_all(&[1, 2, 3, 255, 4, 5, 6, 255]).unwrap();
        file.seek(SeekFrom::Start(256)).unwrap();
        file.write_all(&[7, 8, 9, 255, 10, 11, 12, 255]).unwrap();
        let mut packet = Packet::new(wire::CPU_DESKTOP);
        for (offset, value) in [
            (16, subtype),
            (20, 2),
            (24, 2),
            (28, 256),
            (32, wire::BGRA),
            (40, wire::CPU_SLOT_BASE),
        ] {
            packet.set32(offset, value);
        }
        packet.set64(56, 1);
        packet.set64(64, sequence);
        packet.set64(72, 65536);
        if subtype == 3 {
            packet.set64(88, 7);
            packet.set64(96, 13);
        }
        (packet, file.into())
    }
    fn fill(sequence: u64) -> Packet {
        let mut packet = Packet::new(wire::DESKTOP_OP);
        packet.set32(16, 1);
        packet.set32(28, 1);
        packet.set32(32, 1);
        packet.set32(96, 0xff123456);
        packet.set64(56, 1);
        packet.set64(64, sequence);
        packet
    }

    #[test]
    fn cpu_fd_lease_releases_slot_while_cached_allocation_remains_mapped() {
        let (releases, output) = releases();
        let mut desktop = Desktop::default();
        let (packet, fd) = cpu_packet(1, 1);
        assert!(desktop.push(packet, vec![fd], &releases).unwrap());
        let (batch, returned) = desktop.take().unwrap().unwrap();
        assert!(!returned);
        let DesktopOp::Seed(ref frame) = batch.operations[0] else {
            panic!("seed expected")
        };
        let storage = frame.pixels.storage().unwrap();
        assert_eq!(
            frame.packed_copy().unwrap(),
            [1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255, 10, 11, 12, 255]
        );
        drop(batch);
        let Output::Release(slot) = output.try_recv().unwrap() else {
            panic!("release expected")
        };
        assert_eq!((slot.index, slot.epoch, slot.generation), (96, 1, 1));
        assert_eq!(
            unsafe { *storage.allocation.base_ptr() },
            1,
            "cache keeps mmap without retaining producer credit"
        );
    }

    struct Unimported;
    unsafe impl GpuImage for Unimported {
        fn handle(&self) -> GpuImageHandle<'_> {
            panic!("ownership-only test")
        }
    }

    #[test]
    fn resource_and_blit_arrival_orders_wake_work_without_premature_frames() {
        for image_first in [true, false] {
            let state = State::default();
            let wakes = Arc::new(Mutex::new(Vec::new()));
            let received = wakes.clone();
            *state.callback.lock().unwrap() = Some(Arc::new(move |reason| {
                received.lock().unwrap().push(reason);
            }));
            let (releases, _output) = releases();
            let (seed, fd) = cpu_packet(1, 1);
            assert!(publish_desktop(&state, seed, vec![fd], &releases).unwrap());
            drop(state.desktop.lock().unwrap().take().unwrap());
            let mut image_packet = Packet::new(wire::DRAWABLE);
            for (offset, value) in [
                (16, wire::TOP_LEFT | wire::RETAIN_FOR_DESKTOP),
                (20, 1),
                (24, 1),
                (28, 4),
                (32, wire::BGRA),
                (40, 5),
                (44, 7),
                (48, 8),
            ] {
                image_packet.set32(offset, value);
            }
            image_packet.set64(56, 100);
            image_packet.set64(64, 42);
            let mut blit = Packet::new(wire::DESKTOP_OP);
            for (offset, value) in [
                (16, 3),
                (28, 1),
                (32, 1),
                (44, 7),
                (48, 8),
                (52, 5),
                (100, 1),
            ] {
                blit.set32(offset, value);
            }
            blit.set64(56, 1);
            blit.set64(64, 2);
            blit.set64(72, 100);
            blit.set64(80, 42);
            let image = Arc::new(Unimported);
            let publish_image = |packet: &Packet| {
                // Exercise publication/ownership without importing a fake GPU handle.
                publish_lease(
                    &state,
                    Frame::parse(packet).unwrap(),
                    GpuFrameLease {
                        image: image.clone(),
                        width: 1,
                        height: 1,
                        format: PixelFormat::Bgra8,
                        epoch: 100,
                        generation: packet.get64(64),
                    },
                )
                .unwrap();
            };
            if image_first {
                publish_image(&image_packet);
                assert!(state.desktop.lock().unwrap().take().unwrap().is_none());
            }
            assert!(publish_desktop(&state, blit, vec![], &releases).unwrap());
            if !image_first {
                assert!(
                    state.desktop.lock().unwrap().take().unwrap().is_none(),
                    "a Unix BLIT must wait for its late Mach image"
                );
                publish_image(&image_packet);
            }
            assert_eq!(*wakes.lock().unwrap(), [WakeReason::Work; 3]);
            assert!(
                state.latest.lock().unwrap().is_none(),
                "a retained drawable is never a complete desktop frame"
            );
            let (batch, _) = state.desktop.lock().unwrap().take().unwrap().unwrap();
            assert_eq!(batch.sequence, 2);
            assert!(matches!(batch.operations[0], DesktopOp::Blit { .. }));
            assert!(state.desktop.lock().unwrap().resources.is_empty());
            assert_eq!(Arc::strong_count(&image), 2);
            drop(batch);
            assert_eq!(
                Arc::strong_count(&image),
                1,
                "the queued GPU operation holds the final producer reference"
            );

            image_packet.set32(8, wire::FRAME);
            image_packet.set32(16, wire::TOP_LEFT);
            image_packet.set64(64, 43);
            publish_image(&image_packet);
            assert_eq!(wakes.lock().unwrap().last(), Some(&WakeReason::Frame));
            assert!(state.latest.lock().unwrap().take().is_some());
        }
    }

    #[test]
    fn detached_discard_waits_for_image_without_acquiring_desktop() {
        for returned in [false, true] {
            let (releases, _output) = releases();
            let mut desktop = Desktop::default();
            if returned {
                let (packet, fd) = cpu_packet(1, 1);
                desktop.push(packet, vec![fd], &releases).unwrap();
                drop(desktop.take().unwrap());
                let (mut packet, fd) = cpu_packet(3, 2);
                packet.set32(40, wire::CPU_SLOT_BASE + 1);
                desktop.push(packet, vec![fd], &releases).unwrap();
                drop(desktop.take().unwrap());
            }
            let before = (
                desktop.wire_epoch,
                desktop.epoch,
                desktop.sequence,
                desktop.authority_revision,
                desktop.legacy_anchor,
            );
            let slot = Slot {
                index: 5,
                epoch: 100,
                generation: 42,
            };
            let mut discard = Packet::new(wire::DESKTOP_OP);
            for (offset, value) in [(16, 5), (44, 7), (48, 8), (52, 5)] {
                discard.set32(offset, value);
            }
            discard.set64(72, 100);
            discard.set64(80, 42);
            desktop.push(discard, vec![], &releases).unwrap();
            assert!(desktop.take().unwrap().is_none());
            assert_eq!(desktop.pending.len(), 1, "wait for the cross-channel image");
            let image = Arc::new(Unimported);
            desktop.resources.insert(
                slot,
                GpuDrawableFrame {
                    client: 7,
                    drawable: 8,
                    frame: GpuFrameLease {
                        image: image.clone(),
                        width: 1,
                        height: 1,
                        format: PixelFormat::Bgra8,
                        epoch: 100,
                        generation: 42,
                    },
                },
            );
            assert!(
                desktop.take().unwrap().is_none(),
                "no renderer batch for a release"
            );
            assert!(desktop.pending.is_empty() && desktop.resources.is_empty());
            assert_eq!(Arc::strong_count(&image), 1, "release exact retained lease");
            assert_eq!(
                before,
                (
                    desktop.wire_epoch,
                    desktop.epoch,
                    desktop.sequence,
                    desktop.authority_revision,
                    desktop.legacy_anchor
                )
            );
        }
    }

    #[test]
    fn ordered_blit_waits_for_cross_channel_resource_and_final_reference_releases_registry() {
        let (releases, _output) = releases();
        let mut desktop = Desktop::default();
        let (packet, fd) = cpu_packet(1, 1);
        desktop.push(packet, vec![fd], &releases).unwrap();
        drop(desktop.take().unwrap());
        let slot = Slot {
            index: 5,
            epoch: 100,
            generation: 42,
        };
        let mut blit = Packet::new(wire::DESKTOP_OP);
        for (offset, value) in [
            (16, 3),
            (28, 1),
            (32, 1),
            (44, 7),
            (48, 8),
            (52, 5),
            (100, 1),
        ] {
            blit.set32(offset, value);
        }
        blit.set64(56, 1);
        blit.set64(64, 2);
        blit.set64(72, 100);
        blit.set64(80, 42);
        desktop.push(blit, vec![], &releases).unwrap();
        desktop.push(fill(3), vec![], &releases).unwrap();
        assert!(
            desktop.take().unwrap().is_none(),
            "a later CPU write must not pass a missing Mach image"
        );
        let image = Arc::new(Unimported);
        desktop.resources.insert(
            slot,
            GpuDrawableFrame {
                client: 7,
                drawable: 8,
                frame: GpuFrameLease {
                    image: image.clone(),
                    width: 1,
                    height: 1,
                    format: PixelFormat::Bgra8,
                    epoch: 100,
                    generation: 42,
                },
            },
        );
        let (batch, _) = desktop.take().unwrap().unwrap();
        assert_eq!(batch.sequence, 2);
        assert!(matches!(batch.operations[0], DesktopOp::Blit { .. }));
        assert!(desktop.resources.is_empty());
        assert_eq!(
            Arc::strong_count(&image),
            2,
            "in-flight blit still owns its final producer reference"
        );
        assert_eq!(desktop.take().unwrap().unwrap().0.sequence, 3);
        drop(batch);
        assert_eq!(Arc::strong_count(&image), 1);
    }

    #[test]
    fn malformed_cpu_allocations_and_noncontiguous_records_do_not_advance_order() {
        let (releases, _output) = releases();
        let mut desktop = Desktop::default();
        let (mut bad, fd) = cpu_packet(1, 1);
        bad.set64(72, 32768);
        assert!(desktop.push(bad, vec![fd], &releases).is_err());
        let (good, fd) = cpu_packet(1, 1);
        desktop.push(good, vec![fd], &releases).unwrap();
        assert!(desktop.push(fill(3), vec![], &releases).is_err());
        let mut unknown = fill(2);
        unknown.set32(100, 0x80);
        assert!(desktop.push(unknown, vec![], &releases).is_err());
        desktop.push(fill(2), vec![], &releases).unwrap();
        assert_eq!(desktop.sequence, 2);
    }

    #[test]
    fn readback_reply_transfers_packed_pixels_and_error_reply_has_no_descriptor() {
        let (mut sender, receiver) = UnixStream::pair().unwrap();
        let writer = thread::spawn(move || {
            let pixels = vec![1, 2, 3, 255, 99, 99, 99, 99, 4, 5, 6, 255];
            send_reply(
                &mut sender,
                7,
                9,
                11,
                Ok(FrameLease {
                    pixels: Arc::new(pixels),
                    width: 1,
                    height: 2,
                    stride: 8,
                    format: PixelFormat::Bgra8,
                    generation: 1,
                    damage: None,
                }),
            )
            .unwrap();
            send_reply(&mut sender, 7, 10, 12, Err("canceled".into())).unwrap();
        });
        let (packet, mut fds) = receive_record(&receiver).unwrap().unwrap();
        assert_eq!(
            (
                packet.kind(),
                packet.get64(56),
                packet.get64(64),
                packet.get64(88)
            ),
            (8, 7, 9, 11)
        );
        assert_eq!(
            (packet.get32(16), packet.get32(28), packet.get64(72)),
            (0, 4, 8)
        );
        let fd = fds.pop().unwrap();
        let address = unsafe {
            libc::mmap(
                std::ptr::null_mut(),
                8,
                libc::PROT_READ,
                libc::MAP_SHARED,
                fd.as_raw_fd(),
                0,
            )
        };
        assert_ne!(address, libc::MAP_FAILED);
        assert_eq!(
            unsafe { std::slice::from_raw_parts(address.cast::<u8>(), 8) },
            [1, 2, 3, 255, 4, 5, 6, 255]
        );
        unsafe {
            libc::munmap(address, 8);
        }
        let (error, fds) = receive_record(&receiver).unwrap().unwrap();
        assert_ne!(error.get32(16), 0);
        assert_eq!(error.get64(88), 12);
        assert!(fds.is_empty());
        writer.join().unwrap();
    }

    #[test]
    fn lifecycle_cutoffs_handle_late_images_and_reset_starts_a_new_host_epoch() {
        let (releases, _output) = releases();
        let mut desktop = Desktop::default();
        let (packet, fd) = cpu_packet(1, 1);
        desktop.push(packet, vec![fd], &releases).unwrap();
        let original_epoch = desktop.take().unwrap().unwrap().0.epoch;
        let mut drop_packet = Packet::new(wire::RESOURCE_DROP);
        drop_packet.set32(44, 7);
        drop_packet.set32(48, 8);
        drop_packet.set64(56, 100);
        drop_packet.set64(64, 42);
        desktop.push(drop_packet, vec![], &releases).unwrap();
        assert!(desktop.take().unwrap().is_none());
        assert!(!desktop
            .receive_image(
                7,
                8,
                Slot {
                    index: 5,
                    epoch: 100,
                    generation: 41
                }
            )
            .unwrap());
        assert!(!desktop
            .receive_image(
                7,
                8,
                Slot {
                    index: 6,
                    epoch: 100,
                    generation: 42
                }
            )
            .unwrap());
        assert!(
            desktop.dropped.is_empty(),
            "ordered final image retires the tombstone without an unbounded history"
        );
        assert!(desktop
            .receive_image(
                7,
                8,
                Slot {
                    index: 5,
                    epoch: 101,
                    generation: 1
                }
            )
            .unwrap());
        let mut reset = Packet::new(wire::RESET);
        reset.set64(88, 9);
        reset.set64(96, 2);
        desktop.push(reset, vec![], &releases).unwrap();
        let (reset, return_cpu) = desktop.take().unwrap().unwrap();
        assert!(reset.epoch > original_epoch && reset.sequence == 1 && return_cpu);
        assert!(matches!(reset.operations.as_slice(), [DesktopOp::Reset]));
        assert_eq!(desktop.legacy_anchor, Some((9, 2)));
        let (packet, fd) = cpu_packet(1, 1);
        // Simulate the writer having delivered the first CPU release.
        releases.slots.lock().unwrap().clear();
        desktop.push(packet, vec![fd], &releases).unwrap();
        assert!(desktop.take().unwrap().unwrap().0.epoch > reset.epoch);
    }

    #[test]
    fn older_return_cannot_reopen_cpu_delivery_over_a_newer_queued_seed() {
        let (releases, _output) = releases();
        let mut desktop = Desktop::default();
        let (packet, fd) = cpu_packet(1, 1);
        desktop.push(packet, vec![fd], &releases).unwrap();
        drop(desktop.take().unwrap());
        releases.slots.lock().unwrap().clear();
        let (packet, fd) = cpu_packet(3, 2);
        desktop.push(packet, vec![fd], &releases).unwrap();
        let (mut packet, fd) = cpu_packet(1, 1);
        packet.set64(56, 2);
        packet.set32(40, 97);
        desktop.push(packet, vec![fd], &releases).unwrap();
        let (old_return, permit_cpu) = desktop.take().unwrap().unwrap();
        assert!(matches!(old_return.operations[0], DesktopOp::ReturnCpu(_)));
        assert!(
            !permit_cpu,
            "newer queued Seed still owns desktop authority"
        );
        assert!(desktop.legacy_anchor.is_none());
    }

    #[test]
    fn full_reliable_queue_shutdown_wakes_its_sleeping_reader() {
        use std::io::Read;
        let path =
            std::env::temp_dir().join(format!("dg-full-{}.sock", uuid::Uuid::new_v4().simple()));
        let server = GpuServer::new(
            &path,
            GpuHostInfo {
                device_uuid: [1; 16],
                render_node: Some("/dev/dri/renderD128".into()),
            },
        )
        .unwrap();
        let mut client = UnixStream::connect(&path).unwrap();
        client.read_exact(&mut [0u8; BYTES]).unwrap();
        let (packet, fd) = cpu_packet(1, 1);
        send_fd_record(&mut client, &packet, fd.as_raw_fd()).unwrap();
        for sequence in 2..=MAX_PENDING as u64 + 1 {
            client.write_all(&fill(sequence).0).unwrap();
        }
        let deadline = std::time::Instant::now() + std::time::Duration::from_secs(2);
        while !server.state.desktop.lock().unwrap().full() {
            assert!(std::time::Instant::now() < deadline);
            std::thread::yield_now();
        }
        drop(server);
        assert!(!path.exists());
    }
}
