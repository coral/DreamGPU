// SPDX-License-Identifier: GPL-2.0-or-later
//! Ordered changes to a guest desktop with CPU and GPU producers.
//!
//! A batch is reliable: it must never enter the latest-complete-frame mailbox.
//! Seed and patch pixels are immutable leases captured at the command's position
//! in the guest stream, not a later read of live VRAM. A GPU drawable alone says
//! nothing about its position or visibility on the desktop.

use crate::{FrameLease, GpuFrameLease};
use std::sync::{mpsc::SyncSender, Arc, Mutex};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct DesktopRect {
    pub x: u32,
    pub y: u32,
    pub width: u32,
    pub height: u32,
}
impl DesktopRect {
    pub fn fits(self, width: u32, height: u32) -> bool {
        self.width > 0
            && self.height > 0
            && self.x.checked_add(self.width).is_some_and(|x| x <= width)
            && self.y.checked_add(self.height).is_some_and(|y| y <= height)
    }
}

/// One bounded, indivisible sequence of dependent desktop operations.
/// Start each epoch with sequence 1 and Seed. Thereafter sequences are contiguous.
/// Mode changes/reset require a fresh epoch and seed. ReturnCpu must be last and
/// must carry the full coherent image, including every preceding GPU write.
pub struct DesktopBatch {
    pub epoch: u64,
    pub sequence: u64,
    pub operations: Vec<DesktopOp>,
}

pub enum DesktopOp {
    /// Explicit device reset discards desktop contents. Standalone, fresh epoch, sequence 1.
    Reset,
    /// Advance stream order without changing pixels, e.g. a drawable discard.
    Barrier,
    Seed(FrameLease),
    Patch {
        x: u32,
        y: u32,
        pixels: FrameLease,
    },
    /// A clipped, unscaled drawable blit. The lease names the exact swap being
    /// referenced and pins it until the GPU has consumed it.
    Blit {
        image: GpuFrameLease,
        source: DesktopRect,
        x: u32,
        y: u32,
    },
    Fill {
        rect: DesktopRect,
        bgra: [u8; 4],
    },
    Copy {
        source: DesktopRect,
        x: u32,
        y: u32,
    },
    /// Only an actual guest CPU pixel read needs this operation. Completion is
    /// one-shot and never waits for presentation. Channel replies need capacity one.
    Readback {
        rect: DesktopRect,
        reply: DesktopReply,
    },
    ReturnCpu(FrameLease),
}

pub const MAX_DESKTOP_OPERATIONS: usize = 64;
pub const MAX_DESKTOP_PIXEL_BYTES: u64 = 64 * 1024 * 1024;

/// One-shot completion shared by transport, validation and GPU callbacks.
/// Callbacks must be nonblocking and safe on any thread: enqueue a reply to an
/// existing I/O worker, never perform socket I/O or wait here.
#[derive(Clone)]
pub struct DesktopReply(Arc<ReplyState>);
type ReadResult = std::result::Result<FrameLease, String>;
enum Destination {
    Channel(SyncSender<ReadResult>),
    Callback(Box<dyn FnOnce(ReadResult) + Send>),
}
impl Destination {
    fn complete(self, result: ReadResult) {
        match self {
            Self::Channel(sender) => {
                let _ = sender.try_send(result);
            }
            Self::Callback(callback) => callback(result),
        }
    }
}
struct ReplyState(Mutex<Option<Destination>>);
impl Drop for ReplyState {
    fn drop(&mut self) {
        if let Some(reply) = self
            .0
            .get_mut()
            .unwrap_or_else(|error| error.into_inner())
            .take()
        {
            reply.complete(Err("Desktop readback was canceled".into()));
        }
    }
}
impl DesktopReply {
    pub fn callback(reply: impl FnOnce(ReadResult) + Send + 'static) -> Self {
        Self(Arc::new(ReplyState(Mutex::new(Some(
            Destination::Callback(Box::new(reply)),
        )))))
    }
    pub fn complete(&self, result: ReadResult) {
        let reply = self
            .0
             .0
            .lock()
            .unwrap_or_else(|error| error.into_inner())
            .take();
        // Release the lock before invoking user code; recursive completion is harmless.
        if let Some(reply) = reply {
            reply.complete(result);
        }
    }
}
impl From<SyncSender<ReadResult>> for DesktopReply {
    fn from(sender: SyncSender<ReadResult>) -> Self {
        Self(Arc::new(ReplyState(Mutex::new(Some(
            Destination::Channel(sender),
        )))))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn completion_is_once_and_cancellation_wakes_the_receiver() {
        let (tx, rx) = std::sync::mpsc::sync_channel(1);
        let reply = DesktopReply::callback(move |value| {
            assert!(tx.try_send(value).is_ok());
        });
        let clone = reply.clone();
        reply.complete(Err("first".into()));
        clone.complete(Err("second".into()));
        drop(reply);
        drop(clone);
        assert_eq!(rx.recv().unwrap().err().unwrap(), "first");
        assert!(rx.try_recv().is_err());
        let (tx, rx) = std::sync::mpsc::sync_channel(1);
        let reply = DesktopReply::from(tx);
        let clone = reply.clone();
        drop(reply);
        assert!(rx.try_recv().is_err());
        drop(clone);
        assert!(rx.recv().unwrap().err().unwrap().contains("canceled"));
    }
}
