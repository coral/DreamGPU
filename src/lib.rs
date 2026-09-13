// SPDX-License-Identifier: GPL-2.0-or-later
#![recursion_limit = "256"]
//! DreamGPU's application-independent frame ownership, native transport and presentation.
//!
//! Producers retain native allocations until every CPU/GPU consumer lease completes.
//! Reliable desktop operations and latest complete frames deliberately use separate queues.
pub mod cursor;
pub mod desktop;
pub mod frame;
pub mod gpu_frame;
pub mod native;
pub mod perf;
#[cfg(feature = "presentation")]
pub mod presentation;
#[cfg(all(feature = "transport", unix))]
pub mod shmem;
#[cfg(all(feature = "transport", unix))]
pub mod transport;

pub use cursor::{NativeCursor, NativeCursorFormat, NativeCursorShape};
pub use frame::{DirtyRect, FrameAllocation, FrameLease, FramePixels, FrameStorage, PixelFormat};
pub use gpu_frame::{GpuDrawableFrame, GpuFrameLease, GpuHostInfo, GpuImage, GpuImageHandle};

#[derive(thiserror::Error, Debug)]
pub enum Error {
    #[error("Render error: {0}")]
    Render(String),
    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
}
pub type Result<T> = std::result::Result<T, Error>;
