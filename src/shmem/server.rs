// SPDX-License-Identifier: GPL-2.0-or-later
//! Sleeping CPU display and cursor transport.
use super::display::ShmemDisplay;
use std::os::{
    fd::RawFd,
    unix::net::{UnixListener, UnixStream},
};
use tracing::warn;

// Display transport uses a sleeping worker. The GUI only drains received epochs;
// neither descriptor readiness nor frame arrival requires a redraw polling loop.
use super::input::{InputQueue, InputSender};
use std::{
    io::{Read, Write},
    os::fd::AsRawFd,
    sync::{
        Arc, Mutex,
        atomic::{AtomicBool, Ordering},
    },
    thread::JoinHandle,
};
type WakeCallback = Arc<dyn Fn() + Send + Sync>;

pub struct ShmemServer {
    cursor: Arc<Mutex<super::cursor::CursorMailbox>>,
    socket_path: String,
    inner: Option<ShmemDisplay>,
    received: Arc<Mutex<Option<ShmemDisplay>>>,
    callback: Arc<Mutex<Option<WakeCallback>>>,
    input: InputSender,
    stop: Arc<AtomicBool>,
    worker: Option<JoinHandle<()>>,
}
impl ShmemServer {
    pub fn new(path: &str) -> std::io::Result<Self> {
        let _ = std::fs::remove_file(path);
        let listener = UnixListener::bind(path)?;
        listener.set_nonblocking(true)?;
        let (wake, receiver) = UnixStream::pair()?;
        wake.set_nonblocking(true)?;
        receiver.set_nonblocking(true)?;
        let input = InputSender {
            queue: Arc::new(Mutex::new(InputQueue::default())),
            wake: Arc::new(wake),
            acknowledgments: Arc::new((Mutex::new((0, false)), std::sync::Condvar::new())),
            next_barrier: Arc::new(std::sync::atomic::AtomicU64::new(1)),
        };
        let received = Arc::new(Mutex::new(None));
        let cursor = Arc::new(Mutex::new(super::cursor::CursorMailbox::default()));
        let callback = Arc::new(Mutex::new(None));
        let stop = Arc::new(AtomicBool::new(false));
        let worker = {
            let received = received.clone();
            let cursor = cursor.clone();
            let callback = callback.clone();
            let stop = stop.clone();
            let input = input.clone();
            std::thread::Builder::new()
                .name("qemu-display-io".into())
                .spawn(move || {
                    display_worker(listener, receiver, input, received, callback, stop, cursor)
                })?
        };
        Ok(Self {
            cursor,
            socket_path: path.into(),
            inner: None,
            received,
            callback,
            input,
            stop,
            worker: Some(worker),
        })
    }
    /// Update desktop refresh pacing from the monitor containing the consumer
    /// window. The rate is retained across native reconnects, independent of
    /// guest-selected modes and native GPU submission throughput.
    pub fn set_host_refresh_millihz(&self, millihz: u32) {
        self.input.set_host_refresh(millihz);
    }
    pub fn set_callback(&mut self, callback: WakeCallback) {
        *self.callback.lock().unwrap() = Some(callback);
    }
    pub fn poll(&mut self) -> bool {
        if let Some(display) = self.received.lock().unwrap().take() {
            self.inner = Some(display);
        }
        self.inner.is_some()
    }
    pub fn display(&self) -> Option<&ShmemDisplay> {
        self.inner.as_ref()
    }
    pub fn native_cursor(&self) -> Option<crate::NativeCursor> {
        self.cursor.lock().unwrap().state.clone()
    }
    pub fn take_cursor_error(&self) -> Option<String> {
        self.cursor.lock().unwrap().error.take()
    }
    pub fn display_mut(&mut self) -> Option<&mut ShmemDisplay> {
        self.inner.as_mut()
    }
    pub fn take_display(&mut self) -> Option<ShmemDisplay> {
        self.inner.take()
    }
    pub fn socket_path(&self) -> &str {
        &self.socket_path
    }
}
impl Drop for ShmemServer {
    fn drop(&mut self) {
        self.stop.store(true, Ordering::Release);
        self.input.wake();
        if let Some(worker) = self.worker.take() {
            let _ = worker.join();
        }
        let _ = std::fs::remove_file(&self.socket_path);
    }
}

struct DisplayConnection {
    stream: UnixStream,
    bytes: [u8; 24],
    used: usize,
    fd: Option<RawFd>,
    output: [u8; 24],
    out_used: usize,
    out_len: usize,
}
impl Drop for DisplayConnection {
    fn drop(&mut self) {
        if let Some(fd) = self.fd.take() {
            unsafe {
                libc::close(fd);
            }
        }
    }
}
impl DisplayConnection {
    fn receive(&mut self) -> std::io::Result<Option<([u8; 24], Option<RawFd>)>> {
        let mut iov = libc::iovec {
            iov_base: self.bytes[self.used..].as_mut_ptr().cast(),
            iov_len: 24 - self.used,
        };
        // usize storage provides cmsghdr alignment on both supported platforms.
        let mut control = [0usize; 8];
        let mut msg: libc::msghdr = unsafe { std::mem::zeroed() };
        msg.msg_iov = &mut iov;
        msg.msg_iovlen = 1;
        msg.msg_control = control.as_mut_ptr().cast();
        msg.msg_controllen = size_of_val(&control) as _;
        let n = unsafe { libc::recvmsg(self.stream.as_raw_fd(), &mut msg, 0) };
        if n < 0 {
            let e = std::io::Error::last_os_error();
            if e.kind() == std::io::ErrorKind::WouldBlock {
                return Ok(None);
            }
            return Err(e);
        }
        if n == 0 {
            return Err(std::io::Error::from(std::io::ErrorKind::UnexpectedEof));
        }
        unsafe {
            let mut c = libc::CMSG_FIRSTHDR(&msg);
            while !c.is_null() {
                if (*c).cmsg_level == libc::SOL_SOCKET && (*c).cmsg_type == libc::SCM_RIGHTS {
                    let count =
                        ((*c).cmsg_len as usize - libc::CMSG_LEN(0) as usize) / size_of::<RawFd>();
                    for i in 0..count {
                        let fd = *(libc::CMSG_DATA(c).cast::<RawFd>().add(i));
                        libc::fcntl(fd, libc::F_SETFD, libc::FD_CLOEXEC);
                        if self.fd.is_none() {
                            self.fd = Some(fd)
                        } else {
                            libc::close(fd);
                        }
                    }
                }
                c = libc::CMSG_NXTHDR(&msg, c);
            }
        }
        if msg.msg_flags & libc::MSG_CTRUNC != 0 {
            return Err(std::io::Error::from(std::io::ErrorKind::InvalidData));
        }
        self.used += n as usize;
        if self.used == 24 {
            self.used = 0;
            Ok(Some((self.bytes, self.fd.take())))
        } else {
            Ok(None)
        }
    }
}

fn display_worker(
    listener: UnixListener,
    mut wake: UnixStream,
    input: InputSender,
    received: Arc<Mutex<Option<ShmemDisplay>>>,
    callback: Arc<Mutex<Option<WakeCallback>>>,
    stop: Arc<AtomicBool>,
    cursor: Arc<Mutex<super::cursor::CursorMailbox>>,
) {
    let mut cursor = super::cursor::CursorReceiver::new(cursor);
    let mut client: Option<DisplayConnection> = None;
    while !stop.load(Ordering::Acquire) {
        let output_pending = client.as_ref().is_some_and(|c| c.out_len > 0)
            || !input.queue.lock().unwrap().events.is_empty();
        let mut fds = [
            libc::pollfd {
                fd: wake.as_raw_fd(),
                events: libc::POLLIN,
                revents: 0,
            },
            libc::pollfd {
                fd: if client.is_none() {
                    listener.as_raw_fd()
                } else {
                    -1
                },
                events: libc::POLLIN,
                revents: 0,
            },
            libc::pollfd {
                fd: client.as_ref().map_or(-1, |c| c.stream.as_raw_fd()),
                events: libc::POLLIN | if output_pending { libc::POLLOUT } else { 0 },
                revents: 0,
            },
        ];
        // Infinite blocking wait, interrupted by input, frames, connect or stop.
        let n = unsafe { libc::poll(fds.as_mut_ptr(), fds.len() as _, -1) };
        if n < 0 {
            if std::io::Error::last_os_error().kind() == std::io::ErrorKind::Interrupted {
                continue;
            }
            break;
        }
        if fds[0].revents != 0 {
            let mut b = [0; 128];
            while wake.read(&mut b).is_ok_and(|n| n > 0) {}
        }
        if stop.load(Ordering::Acquire) {
            break;
        }
        if fds[1].revents & libc::POLLIN != 0
            && let Ok((stream, _)) = listener.accept()
        {
            if stream.set_nonblocking(true).is_err() {
                continue;
            }
            #[cfg(target_os = "macos")]
            unsafe {
                let on: libc::c_int = 1;
                libc::setsockopt(
                    stream.as_raw_fd(),
                    libc::SOL_SOCKET,
                    libc::SO_NOSIGPIPE,
                    (&on as *const libc::c_int).cast(),
                    size_of_val(&on) as _,
                );
            }
            input.queue.lock().unwrap().connected = true;
            input.connection_changed(true);
            client = Some(DisplayConnection {
                stream,
                bytes: [0; 24],
                used: 0,
                fd: None,
                output: [0; 24],
                out_used: 0,
                out_len: 0,
            });
        }
        let mut disconnected = false;
        let mut changed = false;
        if let Some(c) = client.as_mut() {
            if fds[2].revents & (libc::POLLIN | libc::POLLHUP | libc::POLLERR) != 0 {
                for _ in 0..256 {
                    match c.receive() {
                        Ok(Some((message, fd))) => {
                            if matches!(message[0], b'C' | b'S' | b'P') {
                                match cursor.receive(message, fd) {
                                    Ok(update) => changed |= update,
                                    Err(error) => {
                                        warn!("Native cursor transport failed: {error}");
                                        cursor.fail(error);
                                        disconnected = true;
                                        break;
                                    }
                                }
                                continue;
                            }
                            let id = u64::from_ne_bytes(message[8..16].try_into().unwrap());
                            let ts = u64::from_ne_bytes(message[16..24].try_into().unwrap());
                            if message[0] == b'D' {
                                if let Some(fd) = fd {
                                    unsafe {
                                        let mut stat: libc::stat = std::mem::zeroed();
                                        if libc::fstat(fd, &mut stat) == 0 && stat.st_size > 0 {
                                            if let Some(mut d) =
                                                ShmemDisplay::from_fd(fd, stat.st_size as usize)
                                            {
                                                d.attach_input(input.clone());
                                                *received.lock().unwrap() = Some(d);
                                                changed = true;
                                            }
                                        } else {
                                            libc::close(fd);
                                        }
                                    }
                                }
                            } else {
                                if let Some(fd) = fd {
                                    unsafe {
                                        libc::close(fd);
                                    }
                                }
                                match message[0] {
                                    b'F' => {
                                        crate::perf::record_at("frame.published", id, 0, ts, 0);
                                        changed = true;
                                    }
                                    b'A' => {
                                        input.acknowledge(id);
                                        crate::perf::record_at(
                                            "input.consumed",
                                            id,
                                            message[1] as u64,
                                            ts,
                                            0,
                                        )
                                    }
                                    _ => {
                                        disconnected = true;
                                        break;
                                    }
                                }
                            }
                        }
                        Ok(None) => break,
                        Err(e) if e.kind() == std::io::ErrorKind::Interrupted => continue,
                        Err(_) => {
                            disconnected = true;
                            break;
                        }
                    }
                }
            }
            if !disconnected {
                for _ in 0..256 {
                    if c.out_len == 0 {
                        let Some(event) = input.queue.lock().unwrap().events.pop_front() else {
                            break;
                        };
                        c.output = event.encode();
                        c.out_len = 24;
                        c.out_used = 0;
                    }
                    match c.stream.write(&c.output[c.out_used..c.out_len]) {
                        Ok(0) => {
                            disconnected = true;
                            break;
                        }
                        Ok(n) => {
                            c.out_used += n;
                            if c.out_used == c.out_len {
                                c.out_len = 0
                            }
                        }
                        Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => break,
                        Err(e) if e.kind() == std::io::ErrorKind::Interrupted => continue,
                        Err(_) => {
                            disconnected = true;
                            break;
                        }
                    }
                }
            }
        }
        if disconnected {
            cursor.disconnect();
            client = None;
            input.connection_changed(false);
            let mut q = input.queue.lock().unwrap();
            q.connected = false;
            q.events.clear();
            crate::perf::event("input.disconnected", 0, 1);
            changed = true;
        }
        if changed {
            let cb = callback.lock().unwrap().clone();
            if let Some(cb) = cb {
                cb();
            }
        }
    }
    input.queue.lock().unwrap().connected = false;
    input.connection_changed(false);
}

#[cfg(test)]
mod display_tests {
    use super::*;
    #[test]
    fn socket_records_survive_fragmentation_and_eof() {
        let (stream, mut writer) = UnixStream::pair().unwrap();
        stream.set_nonblocking(true).unwrap();
        let mut c = DisplayConnection {
            stream,
            bytes: [0; 24],
            used: 0,
            fd: None,
            output: [0; 24],
            out_used: 0,
            out_len: 0,
        };
        let mut b = [0; 24];
        b[0] = b'A';
        b[8..16].copy_from_slice(&123u64.to_ne_bytes());
        writer.write_all(&b[..7]).unwrap();
        assert!(c.receive().unwrap().is_none());
        writer.write_all(&b[7..]).unwrap();
        assert_eq!(c.receive().unwrap().unwrap().0, b);
        assert!(c.receive().unwrap().is_none());
        drop(writer);
        assert_eq!(
            c.receive().unwrap_err().kind(),
            std::io::ErrorKind::UnexpectedEof
        );
    }
    #[test]
    fn idle_worker_shutdown_is_interruptible() {
        let path =
            std::path::Path::new("/tmp").join(format!("dg-io-{}.sock", uuid::Uuid::new_v4()));
        let server = ShmemServer::new(path.to_str().unwrap()).unwrap();
        let start = std::time::Instant::now();
        drop(server);
        assert!(start.elapsed() < std::time::Duration::from_secs(2));
        assert!(!path.exists());
    }
}
