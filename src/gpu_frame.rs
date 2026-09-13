// SPDX-License-Identifier: GPL-2.0-or-later
//! Native GPU images have an explicit producer lease; they never expose CPU bytes.

use std::sync::Arc;

use crate::{Error, PixelFormat, Result};

/// Physical GPU chosen by the renderer, negotiated before native export begins.
#[derive(Clone, Debug, Default)]
pub struct GpuHostInfo {
    pub device_uuid: [u8; 16],
    pub render_node: Option<String>,
}

/// A complete offscreen drawable, not a replacement for the guest desktop.
#[derive(Clone)]
pub struct GpuDrawableFrame {
    pub client: u32,
    pub drawable: u32,
    pub frame: GpuFrameLease,
}

/// Borrowed native image description. Retain its `GpuImage` throughout import and GPU use.
#[derive(Debug)]
pub enum GpuImageHandle<'a> {
    /// A live IOSurface Mach send right, retained by the image owner.
    /// The producer has completed all rendering before publishing this handle.
    IoSurface { mach_port: u32 },
    /// A single-plane image exported from the renderer's physical GPU.
    #[cfg(unix)]
    DmaBuf {
        fd: std::os::fd::BorrowedFd<'a>,
        fourcc: u32,
        modifier: u64,
        stride: u32,
        offset: u64,
        device_uuid: [u8; 16],
        /// Wait on this sync_file before acquiring the foreign image. If absent,
        /// producer completion must already have been observed before publication.
        ready_fence: Option<std::os::fd::BorrowedFd<'a>>,
    },
    #[cfg(not(unix))]
    Unsupported(std::marker::PhantomData<&'a ()>),
}

/// A lease preventing producer reuse of an exported GPU image.
///
/// # Safety
/// The returned handle and backing allocation must remain valid and stable until
/// this owner is dropped. Image pixels are initialized. Producer work has either
/// completed before publication or is covered by the returned completion fence.
/// No producer may modify/reallocate the image until the last owner is dropped.
/// Dropping the owner must be safe on a GPU completion thread. Consumers must keep
/// an owner until every GPU submission accessing the image completes, including
/// any required foreign ownership release. Descriptor values must truthfully
/// describe the exported image; consumers additionally validate their bounds.
pub unsafe trait GpuImage: Send + Sync {
    fn handle(&self) -> GpuImageHandle<'_>;
}

/// One complete immutable image; ordered desktop updates use separate commands.
/// Pixels have the display's top-left origin, matching CPU FrameLease images.
/// A bottom-left GL renderer must flip during its GPU export blit, never on the CPU.
#[derive(Clone)]
pub struct GpuFrameLease {
    pub image: Arc<dyn GpuImage>,
    pub width: u32,
    pub height: u32,
    pub format: PixelFormat,
    /// Changes on mode changes, reset or producer reconnection.
    pub epoch: u64,
    pub generation: u64,
}

impl GpuFrameLease {
    pub fn validate(&self) -> Result<()> {
        if self.width == 0 || self.height == 0 {
            return Err(Error::Render("GPU frame has empty dimensions".into()));
        }
        if !matches!(self.format, PixelFormat::Bgra8 | PixelFormat::Rgba8) {
            return Err(Error::Render(
                "GPU frames require BGRA8 or RGBA8 pixels".into(),
            ));
        }
        match self.image.handle() {
            GpuImageHandle::IoSurface { mach_port: 0 } => {
                return Err(Error::Render(
                    "GPU frame has a null IOSurface Mach port".into(),
                ));
            }
            #[cfg(unix)]
            GpuImageHandle::DmaBuf { stride, offset, .. } => {
                let row = u64::from(self.width) * 4;
                if u64::from(stride) < row
                    || offset
                        .checked_add(u64::from(stride) * u64::from(self.height - 1))
                        .and_then(|last| last.checked_add(row))
                        .is_none()
                {
                    return Err(Error::Render("GPU frame DMA-BUF layout is invalid".into()));
                }
            }
            #[cfg(not(unix))]
            GpuImageHandle::Unsupported(_) => {
                return Err(Error::Render(
                    "Native GPU image is unsupported on this host".into(),
                ));
            }
            _ => {}
        }
        Ok(())
    }
}
