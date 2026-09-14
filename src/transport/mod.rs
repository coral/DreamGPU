// SPDX-License-Identifier: GPL-2.0-or-later
//! Bounded native GPU transport. Reader threads sleep in kernel receive calls;
//! completion callbacks enqueue releases and never write to sockets.

use crate::{GpuDrawableFrame, GpuFrameLease, GpuHostInfo, GpuImage, GpuImageHandle, PixelFormat};
#[cfg(target_os = "linux")]
use std::os::fd::AsFd;
use std::{
    collections::{HashMap, VecDeque},
    io::{self, Write},
    net::Shutdown,
    os::{
        fd::{AsRawFd, FromRawFd, OwnedFd},
        unix::net::{UnixListener, UnixStream},
    },
    path::{Path, PathBuf},
    sync::{
        Arc, Condvar, Mutex,
        atomic::{AtomicBool, AtomicU64, Ordering},
        mpsc,
    },
    thread::{self, JoinHandle},
};
use wire::{BYTES, Frame, MAX_DRAWABLES, MAX_SLOTS, Packet, Slot, invalid};

mod desktop;
#[cfg(target_os = "macos")]
mod macos;
pub mod wire;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum WakeReason {
    Frame,
    Work,
}
type Wake = Arc<dyn Fn(WakeReason) + Send + Sync>;
type Slots = Mutex<HashMap<(u64, u32), u64>>;

#[derive(Default)]
struct State {
    stop: AtomicBool,
    connected: AtomicBool,
    desktop_active: AtomicBool,
    latest: Mutex<Option<GpuFrameLease>>,
    latest_owner: Mutex<Option<(u32, u32, u64, u64)>>,
    drawables: Mutex<VecDeque<GpuDrawableFrame>>,
    desktop: Mutex<desktop::Desktop>,
    desktop_wait: Mutex<()>,
    desktop_available: Condvar,
    callback: Mutex<Option<Wake>>,
    client: Mutex<Option<UnixStream>>,
    error: Mutex<Option<String>>,
}
impl State {
    fn wake_desktop(&self) {
        // Closing a release lease can happen while the desktop owns its queue
        // lock. Use a separate wait handshake so close/stop cannot lose a wake
        // between the receiver's atomic predicate check and sleeping.
        let _wait = self.desktop_wait.lock().unwrap();
        self.desktop_available.notify_all();
    }
    fn wake(&self, reason: WakeReason) {
        let callback = self.callback.lock().unwrap().clone();
        if let Some(callback) = callback {
            callback(reason);
        }
    }
    fn fail(&self, error: impl std::fmt::Display) {
        if !self.stop.load(Ordering::Acquire) {
            *self.error.lock().unwrap() = Some(error.to_string());
            self.wake(WakeReason::Work);
        }
    }
}

struct Releases {
    sender: mpsc::SyncSender<Output>,
    slots: Arc<Slots>,
    stream: UnixStream,
    closed: AtomicBool,
    reply_bytes: AtomicU64,
    state: std::sync::Weak<State>,
}
enum Output {
    Release(Slot),
    Reply {
        epoch: u64,
        sequence: u64,
        token: u64,
        bytes: u64,
        result: std::result::Result<crate::FrameLease, String>,
    },
    Stop,
}
impl Releases {
    fn close(&self) {
        self.closed.store(true, Ordering::Release);
        let _ = self.stream.shutdown(Shutdown::Both);
        if let Some(state) = self.state.upgrade() {
            state.wake_desktop();
        }
    }
    fn send(&self, slot: Slot) {
        if self.closed.load(Ordering::Acquire) {
            return;
        }
        // At most MAX_SLOTS image owners exist per connection, including queued
        // releases. A full queue therefore indicates a violated producer contract.
        if self.sender.try_send(Output::Release(slot)).is_err() {
            if let Some(state) = self.state.upgrade() {
                state.fail("GPU release queue exceeded its ownership bound");
            }
            self.close();
        }
    }
    fn reply(
        &self,
        epoch: u64,
        sequence: u64,
        token: u64,
        mut result: std::result::Result<crate::FrameLease, String>,
    ) {
        if self.closed.load(Ordering::Acquire) {
            return;
        }
        let mut bytes = result
            .as_ref()
            .map(|frame| frame.bytes().len() as u64)
            .unwrap_or(0);
        if self
            .reply_bytes
            .try_update(Ordering::AcqRel, Ordering::Acquire, |pending| {
                pending
                    .checked_add(bytes)
                    .filter(|sum| *sum <= wire::MAX_CPU_BYTES)
            })
            .is_err()
        {
            result = Err("Readback reply queue exceeds its byte budget".into());
            bytes = 0;
        }
        if self
            .sender
            .try_send(Output::Reply {
                epoch,
                sequence,
                token,
                bytes,
                result,
            })
            .is_err()
        {
            self.reply_bytes.fetch_sub(bytes, Ordering::Release);
            if let Some(state) = self.state.upgrade() {
                state.fail("GPU readback completion queue is full");
            }
            self.close();
        }
    }
}

enum NativeImage {
    #[cfg(target_os = "macos")]
    IoSurface(macos::SendRight),
    #[cfg(target_os = "linux")]
    DmaBuf {
        buffer: OwnedFd,
        ready: Option<OwnedFd>,
        stride: u32,
        offset: u64,
        modifier: u64,
        uuid: [u8; 16],
    },
}
struct Image {
    native: NativeImage,
    slot: Slot,
    drawable_id: u64,
    releases: Arc<Releases>,
}
// SAFETY: descriptors are received as owned rights, layout and connection order
// are validated before publication. The cooperating QEMU producer's slot remains
// immutable until RELEASE, sent only when this final image owner is dropped.
// IOSurface publication follows producer completion; DMA-BUF carries its fence.
unsafe impl GpuImage for Image {
    fn handle(&self) -> GpuImageHandle<'_> {
        match &self.native {
            #[cfg(target_os = "macos")]
            NativeImage::IoSurface(right) => GpuImageHandle::IoSurface { mach_port: right.0 },
            #[cfg(target_os = "linux")]
            NativeImage::DmaBuf {
                buffer,
                ready,
                stride,
                offset,
                modifier,
                uuid,
            } => GpuImageHandle::DmaBuf {
                fd: buffer.as_fd(),
                ready_fence: ready.as_ref().map(AsFd::as_fd),
                stride: *stride,
                offset: *offset,
                modifier: *modifier,
                device_uuid: *uuid,
                fourcc: wire::BGRA,
            },
        }
    }
}
impl Drop for Image {
    fn drop(&mut self) {
        crate::perf::event(
            "gpu.drawable.released",
            self.drawable_id,
            self.slot.generation,
        );
        self.releases.send(self.slot);
    }
}

#[derive(Default)]
struct Order {
    generations: HashMap<(u32, u32), (u64, u64)>,
    epoch: Option<u64>,
}

fn publish(
    state: &State,
    releases: &Arc<Releases>,
    order: &mut Order,
    frame: Frame,
    native: NativeImage,
) -> io::Result<()> {
    if order.epoch.is_some_and(|epoch| frame.slot.epoch < epoch) {
        return Err(invalid("Stale GPU resource epoch"));
    }
    if order.epoch != Some(frame.slot.epoch) {
        order.generations.clear();
        order.epoch = Some(frame.slot.epoch);
    }
    let key = (frame.client, frame.drawable);
    if let Some(&(epoch, generation)) = order.generations.get(&key) {
        if frame.slot.epoch == epoch && frame.slot.generation <= generation {
            return Err(invalid("GPU frame generation did not advance"));
        }
    } else if order.generations.len() >= MAX_DRAWABLES {
        return Err(invalid("Too many GPU drawables"));
    }
    {
        let mut slots = releases.slots.lock().unwrap();
        let key = (frame.slot.epoch, frame.slot.index);
        if slots
            .keys()
            .filter(|(_, i)| *i < wire::CPU_SLOT_BASE)
            .count()
            >= MAX_SLOTS
            || slots.contains_key(&key)
        {
            return Err(invalid(
                "GPU producer reused a leased slot or exceeded its export bound",
            ));
        }
        slots.insert(key, frame.slot.generation);
    }
    order
        .generations
        .insert(key, (frame.slot.epoch, frame.slot.generation));
    let image = GpuFrameLease {
        image: Arc::new(Image {
            native,
            slot: frame.slot,
            drawable_id: ((frame.client as u64) << 32) | frame.drawable as u64,
            releases: releases.clone(),
        }),
        width: frame.width,
        height: frame.height,
        format: PixelFormat::Bgra8,
        epoch: frame.slot.epoch,
        generation: frame.slot.generation,
    };
    image.validate().map_err(|e| invalid(e.to_string()))?;
    publish_lease(state, frame, image)
}

/// Resource arrival can unblock ordered work without changing any desktop
/// pixels. In particular, a late Mach image must wake a waiting Unix BLIT.
fn publish_lease(state: &State, frame: Frame, image: GpuFrameLease) -> io::Result<()> {
    let mut desktop = state.desktop.lock().unwrap();
    if !desktop.receive_image(frame.client, frame.drawable, frame.slot)? {
        return Ok(());
    }
    crate::perf::event(
        "gpu.drawable.received",
        ((frame.client as u64) << 32) | frame.drawable as u64,
        frame.slot.generation,
    );
    if frame.kind == wire::FRAME {
        state.desktop_active.store(true, Ordering::Release);
        *state.latest_owner.lock().unwrap() = Some((
            frame.client,
            frame.drawable,
            frame.slot.epoch,
            frame.slot.generation,
        ));
        let previous = state.latest.lock().unwrap().replace(image);
        drop(previous);
    } else if frame.retain {
        if desktop.resources.len() >= MAX_SLOTS
            || desktop
                .resources
                .insert(
                    frame.slot,
                    GpuDrawableFrame {
                        client: frame.client,
                        drawable: frame.drawable,
                        frame: image,
                    },
                )
                .is_some()
        {
            return Err(invalid(
                "Retained GPU resource registry overflow or duplicate",
            ));
        }
    } else {
        let mut pending = state.drawables.lock().unwrap();
        if pending.len() >= MAX_SLOTS {
            return Err(invalid("GPU drawable queue is full"));
        }
        pending.push_back(GpuDrawableFrame {
            client: frame.client,
            drawable: frame.drawable,
            frame: image,
        });
    }
    drop(desktop);
    state.wake(if frame.kind == wire::FRAME {
        WakeReason::Frame
    } else {
        WakeReason::Work
    });
    Ok(())
}

fn publish_desktop(
    state: &State,
    packet: Packet,
    fds: Vec<OwnedFd>,
    releases: &Arc<Releases>,
) -> io::Result<bool> {
    let mut desktop = state.desktop.lock().unwrap();
    while desktop.full()
        && !state.stop.load(Ordering::Acquire)
        && !releases.closed.load(Ordering::Acquire)
    {
        let wait = state.desktop_wait.lock().unwrap();
        if state.stop.load(Ordering::Acquire) || releases.closed.load(Ordering::Acquire) {
            break;
        }
        // Retain the queue lock until the wait handshake is held: consumers
        // cannot free capacity and signal before this receiver is ready.
        drop(desktop);
        drop(state.desktop_available.wait(wait).unwrap());
        desktop = state.desktop.lock().unwrap();
    }
    if state.stop.load(Ordering::Acquire) || releases.closed.load(Ordering::Acquire) {
        return Ok(false);
    }
    let seeded = desktop.push(packet, fds, releases)?;
    if seeded {
        state.desktop_active.store(true, Ordering::Release);
    }
    drop(desktop);
    state.wake(WakeReason::Work);
    Ok(true)
}

/// Owns the listener and worker lifetime. Connection is lazy on QEMU's first 3D request.
pub struct GpuServer {
    path: PathBuf,
    state: Arc<State>,
    thread: Option<JoinHandle<()>>,
}
impl GpuServer {
    /// Diagnostic snapshot. Each returned frame temporarily retains its producer lease.
    /// This is intended for acceptance tools, not the presentation hot path.
    pub fn retained_drawables(&self) -> Vec<GpuDrawableFrame> {
        self.state
            .desktop
            .lock()
            .unwrap()
            .resources
            .values()
            .cloned()
            .collect()
    }

    /// Current coherent CPU handoff marker, for lifecycle diagnostics.
    pub fn legacy_anchor(&self) -> Option<(u64, u64)> {
        self.state.desktop.lock().unwrap().legacy_anchor
    }

    pub fn new(path: &Path, host: GpuHostInfo) -> io::Result<Self> {
        let listener = UnixListener::bind(path)?;
        let state = Arc::new(State::default());
        let worker_state = state.clone();
        let thread = thread::Builder::new()
            .name("dreamgpu_gpu_rx".into())
            .spawn(move || match listener.accept() {
                Ok((stream, _)) if !worker_state.stop.load(Ordering::Acquire) => {
                    if let Err(error) = connection(stream, &host, &worker_state) {
                        worker_state.fail(error);
                    }
                    if let Some(client) = worker_state.client.lock().unwrap().take() {
                        let _ = client.shutdown(Shutdown::Both);
                    }
                }
                Err(error) => worker_state.fail(error),
                _ => {}
            })?;
        Ok(Self {
            path: path.to_owned(),
            state,
            thread: Some(thread),
        })
    }
    pub fn set_callback(&self, callback: Wake) {
        *self.state.callback.lock().unwrap() = Some(callback);
    }
    pub fn take_frame(&self) -> Option<GpuFrameLease> {
        self.state.latest.lock().unwrap().take()
    }
    pub fn take_drawables(&self) -> Vec<GpuDrawableFrame> {
        self.state.drawables.lock().unwrap().drain(..).collect()
    }
    pub fn desktop_active(&self) -> bool {
        self.state.desktop_active.load(Ordering::Acquire)
    }
    pub fn connected(&self) -> bool {
        self.state.connected.load(Ordering::Acquire)
    }
    pub fn take_error(&self) -> Option<String> {
        self.state.error.lock().unwrap().take()
    }
    pub fn take_desktop_update(&self) -> Option<crate::desktop::DesktopBatch> {
        if self.state.stop.load(Ordering::Acquire) {
            return None;
        }
        let result = {
            let mut desktop = self.state.desktop.lock().unwrap();
            let result = desktop.take();
            for (client, drawable, cutoff) in desktop.retired.drain(..) {
                self.state.drawables.lock().unwrap().retain(|image| {
                    image.client != client
                        || image.drawable != drawable
                        || (image.frame.epoch, image.frame.generation) > cutoff
                });
                let mut owner = self.state.latest_owner.lock().unwrap();
                if owner.is_some_and(|(c, d, epoch, generation)| {
                    c == client && d == drawable && (epoch, generation) <= cutoff
                }) {
                    self.state.latest.lock().unwrap().take();
                    *owner = None;
                }
            }
            result
        };
        match result {
            Ok(Some((batch, returned))) => {
                if returned {
                    self.state.desktop_active.store(false, Ordering::Release);
                    self.state.latest.lock().unwrap().take();
                    self.state.drawables.lock().unwrap().clear();
                }
                self.state.wake_desktop();
                Some(batch)
            }
            Ok(None) => {
                self.state.wake_desktop();
                None
            }
            Err(error) => {
                self.state.fail(error);
                None
            }
        }
    }
    pub fn permits_cpu_frame(&self, epoch: u64, generation: u64) -> bool {
        if self.desktop_active() {
            return false;
        }
        let mut desktop = self.state.desktop.lock().unwrap();
        if let Some(anchor) = desktop.legacy_anchor {
            if (epoch, generation) < anchor {
                return false;
            }
            desktop.legacy_anchor = None;
        }
        true
    }
    pub fn fail_desktop(&self) {
        // Disconnect without joining on the UI thread. Owned image leases stay
        // immutable while outstanding GPU work finishes; shutdown joins later.
        self.state.stop.store(true, Ordering::Release);
        self.state.wake_desktop();
        if let Some(client) = self.state.client.lock().unwrap().as_ref() {
            let _ = client.shutdown(Shutdown::Both);
        }
        self.state.desktop.lock().unwrap().clear();
    }
}
impl Drop for GpuServer {
    fn drop(&mut self) {
        self.state.stop.store(true, Ordering::Release);
        self.state.wake_desktop();
        if let Some(client) = self.state.client.lock().unwrap().as_ref() {
            let _ = client.shutdown(Shutdown::Both);
        }
        // Wake a listener that never received a guest connection. This connect is
        // local and immediately dropped; connected readers are interrupted above.
        let _ = UnixStream::connect(&self.path);
        if let Some(worker) = self.thread.take() {
            let _ = worker.join();
        }
        self.state.latest.lock().unwrap().take();
        self.state.drawables.lock().unwrap().clear();
        self.state.desktop.lock().unwrap().clear();
        let _ = std::fs::remove_file(&self.path);
    }
}

fn connection(mut stream: UnixStream, host: &GpuHostInfo, state: &Arc<State>) -> io::Result<()> {
    *state.client.lock().unwrap() = Some(stream.try_clone()?);
    if state.stop.load(Ordering::Acquire) {
        return Ok(());
    }
    let (sender, receiver) =
        mpsc::sync_channel::<Output>(MAX_SLOTS + wire::MAX_CPU_SLOTS + desktop::MAX_PENDING + 8);
    let releases = Arc::new(Releases {
        sender,
        slots: Arc::default(),
        stream: stream.try_clone()?,
        closed: AtomicBool::new(false),
        reply_bytes: AtomicU64::new(0),
        state: Arc::downgrade(state),
    });
    let mut writer = stream.try_clone()?;
    #[cfg(target_os = "macos")]
    let endpoint = macos::Endpoint::new()?;
    #[cfg(target_os = "macos")]
    let service = Some(endpoint.name());
    #[cfg(not(target_os = "macos"))]
    let service = None;
    stream.write_all(&Packet::hello(host, service)?.0)?;
    let writer_releases = releases.clone();
    let writer_state = state.clone();
    let writer_thread = thread::Builder::new()
        .name("dreamgpu_gpu_release".into())
        .spawn(move || {
            while let Ok(output) = receiver.recv() {
                if writer_releases.closed.load(Ordering::Acquire) {
                    break;
                }
                let result = match output {
                    Output::Release(slot) => {
                        writer_releases
                            .slots
                            .lock()
                            .unwrap()
                            .remove(&(slot.epoch, slot.index));
                        writer.write_all(&slot.release().0)
                    }
                    Output::Reply {
                        epoch,
                        sequence,
                        token,
                        bytes,
                        result,
                    } => {
                        let result =
                            desktop::send_reply(&mut writer, epoch, sequence, token, result);
                        writer_releases
                            .reply_bytes
                            .fetch_sub(bytes, Ordering::Release);
                        result
                    }
                    Output::Stop => break,
                };
                if let Err(error) = result {
                    writer_state.fail(error);
                    writer_releases.closed.store(true, Ordering::Release);
                    let _ = writer.shutdown(Shutdown::Both);
                    writer_state.wake_desktop();
                    break;
                }
            }
        })?;
    #[allow(unused_mut)]
    let mut workers = Workers {
        releases: releases.clone(),
        writer: Some(writer_thread),
        #[cfg(target_os = "macos")]
        endpoint: Some(endpoint),
        #[cfg(target_os = "macos")]
        mach: None,
    };
    state.connected.store(true, Ordering::Release);
    #[cfg(target_os = "macos")]
    {
        let state = state.clone();
        let releases = releases.clone();
        let port = workers.endpoint.as_ref().unwrap().port();
        workers.mach = Some(
            thread::Builder::new()
                .name("dreamgpu_gpu_mach".into())
                .spawn(move || {
                    let mut order = Order::default();
                    loop {
                        let result = macos::receive(port).and_then(|(packet, right)| {
                            let frame = Frame::parse(&packet)?;
                            if frame.ready_fence
                                || frame.offset != 0
                                || frame.modifier != 0
                                || frame.uuid != [0; 16]
                            {
                                return Err(invalid(
                                    "IOSurface frame has DMA-BUF-only layout or fence flags",
                                ));
                            }
                            macos::validate_surface(
                                right.0,
                                frame.width,
                                frame.height,
                                frame.stride,
                            )?;
                            publish(
                                &state,
                                &releases,
                                &mut order,
                                frame,
                                NativeImage::IoSurface(right),
                            )
                        });
                        if let Err(error) = result {
                            if !releases.closed.load(Ordering::Acquire) {
                                state.fail(error);
                                let _ = releases.stream.shutdown(Shutdown::Both);
                                state.wake_desktop();
                            }
                            break;
                        }
                    }
                })?,
        );
    }
    #[cfg(target_os = "linux")]
    let mut order = Order::default();
    let result = (|| {
        while let Some((packet, mut fds)) = receive_record(&stream)? {
            packet.validate()?;
            if packet.kind() == wire::ERROR {
                return Err(invalid("QEMU GPU producer reported an error"));
            }
            if matches!(
                packet.kind(),
                wire::CPU_DESKTOP | wire::DESKTOP_OP | wire::RESOURCE_DROP | wire::RESET
            ) {
                if !publish_desktop(state, packet, fds, &releases)? {
                    break;
                }
                continue;
            }
            #[cfg(target_os = "linux")]
            {
                let frame = Frame::parse(&packet)?;
                if frame.uuid != host.device_uuid || frame.uuid == [0; 16] {
                    return Err(invalid("GPU producer selected a different physical device"));
                }
                if fds.len() != 1 + usize::from(frame.ready_fence) {
                    return Err(invalid("GPU record has an invalid descriptor count"));
                }
                let ready = if frame.ready_fence { fds.pop() } else { None };
                let native = NativeImage::DmaBuf {
                    buffer: fds.pop().unwrap(),
                    ready,
                    stride: frame.stride,
                    offset: u64::from(frame.offset),
                    modifier: frame.modifier,
                    uuid: frame.uuid,
                };
                publish(state, &releases, &mut order, frame, native)?;
            }
            #[cfg(not(target_os = "linux"))]
            {
                let _ = &mut fds;
                return Err(invalid("Mac GPU image records must transfer Mach rights"));
            }
        }
        Ok(())
    })();
    drop(workers);
    state.client.lock().unwrap().take();
    result
}

struct Workers {
    releases: Arc<Releases>,
    writer: Option<JoinHandle<()>>,
    #[cfg(target_os = "macos")]
    endpoint: Option<macos::Endpoint>,
    #[cfg(target_os = "macos")]
    mach: Option<JoinHandle<()>>,
}
impl Drop for Workers {
    fn drop(&mut self) {
        self.releases.close();
        let _ = self.releases.sender.try_send(Output::Stop);
        #[cfg(target_os = "macos")]
        {
            self.endpoint.take();
            if let Some(thread) = self.mach.take() {
                let _ = thread.join();
            }
        }
        if let Some(thread) = self.writer.take() {
            let _ = thread.join();
        }
    }
}

/// Read exactly one fixed-size record, retaining ancillary descriptors through
/// arbitrary stream fragmentation. Reject handles attached after its first byte.
fn receive_record(stream: &UnixStream) -> io::Result<Option<(Packet, Vec<OwnedFd>)>> {
    let mut packet = Packet([0; BYTES]);
    let mut used = 0;
    let mut fds = Vec::new();
    while used < BYTES {
        let mut control = [0u64; 16];
        let mut iov = libc::iovec {
            iov_base: packet.0[used..].as_mut_ptr().cast(),
            iov_len: BYTES - used,
        };
        let mut message: libc::msghdr = unsafe { std::mem::zeroed() };
        message.msg_iov = &mut iov;
        message.msg_iovlen = 1;
        message.msg_control = control.as_mut_ptr().cast();
        message.msg_controllen = std::mem::size_of_val(&control) as _;
        #[cfg(target_os = "linux")]
        let flags = libc::MSG_CMSG_CLOEXEC;
        #[cfg(not(target_os = "linux"))]
        let flags = 0;
        let received = unsafe { libc::recvmsg(stream.as_raw_fd(), &mut message, flags) };
        if received < 0 {
            let error = io::Error::last_os_error();
            if error.kind() == io::ErrorKind::Interrupted {
                continue;
            }
            return Err(error);
        }
        if received == 0 {
            return if used == 0 {
                Ok(None)
            } else {
                Err(invalid("Truncated GPU stream record"))
            };
        }
        let mut ancillary_invalid = false;
        unsafe {
            let mut cmsg = libc::CMSG_FIRSTHDR(&message);
            while !cmsg.is_null() {
                if (*cmsg).cmsg_level == libc::SOL_SOCKET && (*cmsg).cmsg_type == libc::SCM_RIGHTS {
                    let bytes = (*cmsg).cmsg_len as usize - libc::CMSG_LEN(0) as usize;
                    let count = bytes / std::mem::size_of::<i32>();
                    ancillary_invalid |=
                        used != 0 || !bytes.is_multiple_of(std::mem::size_of::<i32>());
                    for index in 0..count {
                        let raw = std::ptr::read_unaligned(
                            libc::CMSG_DATA(cmsg).cast::<i32>().add(index),
                        );
                        let fd = OwnedFd::from_raw_fd(raw);
                        #[cfg(not(target_os = "linux"))]
                        if libc::fcntl(fd.as_raw_fd(), libc::F_SETFD, libc::FD_CLOEXEC) < 0 {
                            return Err(io::Error::last_os_error());
                        }
                        fds.push(fd);
                    }
                } else {
                    ancillary_invalid = true;
                }
                cmsg = libc::CMSG_NXTHDR(&message, cmsg);
            }
        }
        if message.msg_flags & (libc::MSG_CTRUNC | libc::MSG_TRUNC) != 0
            || ancillary_invalid
            || fds.len() > 2
        {
            return Err(invalid("Invalid or truncated GPU ancillary descriptors"));
        }
        used += received as usize;
    }
    Ok(Some((packet, fds)))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Read;

    fn send_fd(stream: &UnixStream, data: &[u8], fd: i32) {
        let mut control = [0u64; 4];
        let mut vector = libc::iovec {
            iov_base: data.as_ptr().cast_mut().cast(),
            iov_len: data.len(),
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
            assert_eq!(
                libc::sendmsg(stream.as_raw_fd(), &message, 0),
                data.len() as isize
            );
        }
    }

    #[test]
    fn fragmented_records_keep_owned_close_on_exec_descriptors() {
        let (mut sender, receiver) = UnixStream::pair().unwrap();
        let file = std::fs::File::open("/dev/null").unwrap();
        let packet = Packet::new(wire::FRAME);
        let bytes = packet.0;
        let writer = thread::spawn(move || {
            send_fd(&sender, &bytes[..1], file.as_raw_fd());
            for chunk in bytes[1..].chunks(7) {
                sender.write_all(chunk).unwrap();
            }
        });
        let (received, mut fds) = receive_record(&receiver).unwrap().unwrap();
        assert_eq!(received.0, packet.0);
        assert_eq!(fds.len(), 1);
        let fd = fds.pop().unwrap();
        let raw = fd.as_raw_fd();
        assert_ne!(
            unsafe { libc::fcntl(raw, libc::F_GETFD) } & libc::FD_CLOEXEC,
            0
        );
        drop(fd);
        assert_eq!(unsafe { libc::fcntl(raw, libc::F_GETFD) }, -1);
        writer.join().unwrap();
        assert!(receive_record(&receiver).unwrap().is_none());
    }

    #[test]
    fn truncated_record_reports_error_and_idle_server_shutdown_finishes() {
        let (mut sender, receiver) = UnixStream::pair().unwrap();
        sender.write_all(&[1, 2, 3]).unwrap();
        drop(sender);
        assert_eq!(
            receive_record(&receiver).unwrap_err().kind(),
            io::ErrorKind::InvalidData
        );
        let path = std::env::temp_dir().join(format!(
            "dg-gpu-test-{}.sock",
            uuid::Uuid::new_v4().simple()
        ));
        let server = GpuServer::new(&path, GpuHostInfo::default()).unwrap();
        drop(server);
        assert!(!path.exists());
    }

    #[test]
    fn connected_server_shutdown_interrupts_stream_and_mach_receivers() {
        let path = std::env::temp_dir().join(format!(
            "dg-gpu-test-{}.sock",
            uuid::Uuid::new_v4().simple()
        ));
        let host = GpuHostInfo {
            device_uuid: [1; 16],
            render_node: Some("/dev/dri/renderD128".into()),
        };
        let server = GpuServer::new(&path, host).unwrap();
        let mut client = UnixStream::connect(&path).unwrap();
        let mut hello = [0; BYTES];
        client.read_exact(&mut hello).unwrap();
        assert_eq!(Packet(hello).kind(), wire::HELLO);
        drop(server);
        assert!(!path.exists());
        let mut byte = [0];
        assert_eq!(client.read(&mut byte).unwrap(), 0);
    }
}
