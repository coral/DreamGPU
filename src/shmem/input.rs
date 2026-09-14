// SPDX-License-Identifier: GPL-2.0-or-later
//! Ordered socket input independent of display epochs, with bounded backpressure.
use crate::perf;
use std::{
    collections::VecDeque,
    io::Write,
    os::unix::net::UnixStream,
    sync::{
        Arc, Condvar, Mutex,
        atomic::{AtomicU64, Ordering},
    },
};
pub const DREAMGPU_INPUT_MOUSE_REL: u8 = 1;
pub const DREAMGPU_INPUT_MOUSE_ABS: u8 = 2;
pub const DREAMGPU_INPUT_MOUSE_BTN: u8 = 3;
pub const DREAMGPU_INPUT_KEY: u8 = 4;
pub const DREAMGPU_INPUT_RESET: u8 = 5;
pub const DREAMGPU_INPUT_REFRESH: u8 = 6;
/// Consumer monitor rate in millihertz; never a guest render-rate limit.
pub const DREAMGPU_HOST_REFRESH: u8 = 7;
const CAPACITY: usize = 1024;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct DreamGpuInputEvent {
    pub event_type: u8,
    pub button: u8,
    pub pressed: u8,
    pub reserved: u8,
    pub x: i32,
    pub y: i32,
    pub padding: u32,
    pub id: u64,
}
impl DreamGpuInputEvent {
    pub fn encode(self) -> [u8; 24] {
        let mut b = [0; 24];
        b[0] = self.event_type;
        b[1] = self.button;
        b[2] = self.pressed;
        b[4..8].copy_from_slice(&self.x.to_ne_bytes());
        b[8..12].copy_from_slice(&self.y.to_ne_bytes());
        b[16..24].copy_from_slice(&self.id.to_ne_bytes());
        b
    }
}

#[derive(Default)]
pub(super) struct InputQueue {
    pub events: VecDeque<DreamGpuInputEvent>,
    pub connected: bool,
    pub host_refresh_millihz: Option<u32>,
}
impl InputQueue {
    /// Coalescing is restricted to consecutive motion; buttons/keys remain
    /// ordered relative to all preceding and following movement.
    fn push(&mut self, event: DreamGpuInputEvent) -> bool {
        if let Some(tail) = self.events.back_mut() {
            if tail.event_type == event.event_type {
                match event.event_type {
                    DREAMGPU_INPUT_MOUSE_REL => {
                        if let (Some(x), Some(y)) =
                            (tail.x.checked_add(event.x), tail.y.checked_add(event.y))
                        {
                            tail.x = x;
                            tail.y = y;
                            tail.id = event.id;
                            perf::event("input.coalesced", event.id, 1);
                            return false;
                        }
                    }
                    DREAMGPU_INPUT_MOUSE_ABS => {
                        *tail = event;
                        perf::event("input.coalesced", event.id, 1);
                        return false;
                    }
                    _ => {}
                }
            }
        }
        if self.events.len() >= CAPACITY {
            // Explicit fail-safe recovery. Do not overwrite unread transitions
            // and leave keys held: reset the emulator before resuming delivery.
            perf::event("input.overflow", event.id, self.events.len() as u64);
            self.events.clear();
            self.events.push_back(DreamGpuInputEvent {
                event_type: DREAMGPU_INPUT_RESET,
                ..Default::default()
            });
            self.events.push_back(event);
            return true;
        }
        self.events.push_back(event);
        false
    }
}

#[derive(Clone)]
pub(super) struct InputSender {
    pub queue: Arc<Mutex<InputQueue>>,
    pub wake: Arc<UnixStream>,
    pub acknowledgments: Arc<(Mutex<(u64, bool)>, Condvar)>,
    pub next_barrier: Arc<AtomicU64>,
}
impl InputSender {
    pub fn set_host_refresh(&self, millihz: u32) {
        let millihz = millihz.clamp(10_000, 500_000);
        let mut queue = self.queue.lock().unwrap();
        if queue.host_refresh_millihz == Some(millihz) {
            return;
        }
        queue.host_refresh_millihz = Some(millihz);
        if queue.connected {
            queue.push(DreamGpuInputEvent {
                event_type: DREAMGPU_HOST_REFRESH,
                x: millihz as i32,
                ..Default::default()
            });
        }
        drop(queue);
        self.wake();
    }
    pub fn connection_changed(&self, connected: bool) {
        if connected {
            let mut queue = self.queue.lock().unwrap();
            if let Some(millihz) = queue.host_refresh_millihz {
                queue.push(DreamGpuInputEvent {
                    event_type: DREAMGPU_HOST_REFRESH,
                    x: millihz as i32,
                    ..Default::default()
                });
            }
        }
        let (lock, changed) = &*self.acknowledgments;
        lock.lock().unwrap().1 = connected;
        changed.notify_all();
    }
    pub fn acknowledge(&self, id: u64) {
        if id & (1u64 << 63) == 0 {
            return;
        }
        let (lock, changed) = &*self.acknowledgments;
        lock.lock().unwrap().0 = id;
        changed.notify_all();
    }
    pub fn reset_and_wait(&self, timeout: std::time::Duration) -> std::io::Result<()> {
        let id = (1u64 << 63) | self.next_barrier.fetch_add(1, Ordering::Relaxed);
        self.send(DreamGpuInputEvent {
            event_type: DREAMGPU_INPUT_RESET,
            id,
            ..Default::default()
        });
        let (lock, changed) = &*self.acknowledgments;
        let (state, result) = changed
            .wait_timeout_while(lock.lock().unwrap(), timeout, |state| {
                state.1 && state.0 < id
            })
            .unwrap();
        if !state.1 {
            return Err(std::io::ErrorKind::NotConnected.into());
        }
        if result.timed_out() && state.0 < id {
            return Err(std::io::ErrorKind::TimedOut.into());
        }
        Ok(())
    }
    pub fn send(&self, event: DreamGpuInputEvent) {
        let mut q = self.queue.lock().unwrap();
        if !q.connected {
            perf::event("input.disconnected", event.id, 1);
            return;
        }
        let was_empty = q.events.is_empty();
        let overflow = q.push(event);
        perf::event("input.queued", event.id, q.events.len() as u64);
        drop(q);
        if overflow {
            tracing::warn!("QEMU input queue overloaded; resetting held keys and buttons");
        }
        if was_empty {
            self.wake();
        }
    }
    pub fn wake(&self) {
        // Nonblocking socketpair: EAGAIN already represents a pending wakeup.
        // UnixStream::write also suppresses SIGPIPE if the worker has exited.
        let _ = (&*self.wake).write(&[1]);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn ev(kind: u8, x: i32) -> DreamGpuInputEvent {
        DreamGpuInputEvent {
            event_type: kind,
            x,
            ..Default::default()
        }
    }
    #[test]
    fn coalescing_preserves_discrete_order() {
        let mut q = InputQueue::default();
        q.push(ev(1, 2));
        q.push(ev(1, 3));
        q.push(ev(4, 30));
        q.push(ev(1, 7));
        q.push(ev(1, 1));
        assert_eq!(
            q.events
                .iter()
                .map(|e| (e.event_type, e.x))
                .collect::<Vec<_>>(),
            vec![(1, 5), (4, 30), (1, 8)]
        );
    }
    #[test]
    fn overload_resets_instead_of_overwriting_release() {
        let mut q = InputQueue::default();
        for _ in 0..CAPACITY {
            q.push(ev(4, 30));
        }
        assert!(q.push(ev(4, 31)));
        assert_eq!(q.events.len(), 2);
        assert_eq!(q.events[0].event_type, DREAMGPU_INPUT_RESET);
        assert_eq!(q.events[1].x, 31);
    }
    #[test]
    fn overflowing_motion_does_not_wrap() {
        let mut q = InputQueue::default();
        q.push(ev(1, i32::MAX));
        q.push(ev(1, 1));
        assert_eq!(q.events.len(), 2);
    }
    #[test]
    fn monitor_rate_is_retained_for_connect_and_changes_are_deduplicated() {
        let (wake, _reader) = UnixStream::pair().unwrap();
        let sender = InputSender {
            queue: Arc::new(Mutex::new(InputQueue::default())),
            wake: Arc::new(wake),
            acknowledgments: Arc::new((Mutex::new((0, false)), Condvar::new())),
            next_barrier: Arc::new(AtomicU64::new(1)),
        };
        sender.set_host_refresh(120_000);
        assert!(sender.queue.lock().unwrap().events.is_empty());
        sender.queue.lock().unwrap().connected = true;
        sender.connection_changed(true);
        sender.set_host_refresh(120_000);
        {
            let mut queue = sender.queue.lock().unwrap();
            assert_eq!(queue.events.len(), 1);
            let event = queue.events.pop_front().unwrap();
            assert_eq!(
                (event.event_type, event.x),
                (DREAMGPU_HOST_REFRESH, 120_000)
            );
        }
        sender.set_host_refresh(59_940);
        let mut queue = sender.queue.lock().unwrap();
        assert_eq!(queue.events.pop_front().unwrap().x, 59_940);
    }
    #[test]
    fn wire_layout() {
        assert_eq!(size_of::<DreamGpuInputEvent>(), 24);
    }
}
