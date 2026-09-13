// SPDX-License-Identifier: GPL-2.0-or-later
//! Immutable CPU frame contracts shared by producers and GPU consumers.
use std::sync::Arc;

/// A rectangular region that has been modified
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct DirtyRect {
    pub x: u32,
    pub y: u32,
    pub width: u32,
    pub height: u32,
}

impl DirtyRect {
    /// Create a new dirty rect
    pub fn new(x: u32, y: u32, width: u32, height: u32) -> Self {
        Self {
            x,
            y,
            width,
            height,
        }
    }

    /// Create a dirty rect covering the entire framebuffer
    pub fn full(width: u32, height: u32) -> Self {
        Self {
            x: 0,
            y: 0,
            width,
            height,
        }
    }

    /// Merge another dirty rect into this one (union of bounding boxes)
    pub fn merge(&mut self, other: &DirtyRect) {
        let x1 = self.x.min(other.x);
        let y1 = self.y.min(other.y);
        let x2 = (self.x + self.width).max(other.x + other.width);
        let y2 = (self.y + self.height).max(other.y + other.height);
        self.x = x1;
        self.y = y1;
        self.width = x2 - x1;
        self.height = y2 - y1;
    }

    /// Check if this rect covers the entire framebuffer
    pub fn is_full(&self, fb_width: u32, fb_height: u32) -> bool {
        self.x == 0 && self.y == 0 && self.width >= fb_width && self.height >= fb_height
    }
}

/// Pixel format for framebuffer data
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PixelFormat {
    /// 32-bit BGRA (blue, green, red, alpha)
    Bgra8,
    /// 32-bit RGBA (red, green, blue, alpha)
    Rgba8,
    /// 24-bit BGR (blue, green, red)
    Bgr8,
    /// 24-bit RGB (red, green, blue)
    Rgb8,
}

impl PixelFormat {
    /// Bytes per pixel for this format
    pub fn bytes_per_pixel(&self) -> usize {
        match self {
            PixelFormat::Bgra8 | PixelFormat::Rgba8 => 4,
            PixelFormat::Bgr8 | PixelFormat::Rgb8 => 3,
        }
    }
}

/// Owned storage that may be imported by a native GPU backend without a copy.
///
/// # Safety
/// `base_ptr()` and `len()` must describe the same live, nonempty contiguous
/// backing allocation for the owner's entire lifetime. The span must contain no
/// holes and occupy one virtual-memory region; it must not be unmapped, resized,
/// or moved until the last owner is dropped. The pointer and length are stable.
/// Ownership alone does NOT prevent a producer writing individual frame slots.
/// Callers must retain the corresponding FrameLease until all GPU reads finish.
/// Accessing the raw pointer requires separate synchronization with producers.
pub unsafe trait FrameAllocation: Send + Sync {
    fn base_ptr(&self) -> *mut u8;
    fn len(&self) -> usize;

    fn is_empty(&self) -> bool {
        self.len() == 0
    }
}

/// A plane within a native allocation. `length` may include initialized row or
/// page padding beyond FramePixels::bytes(). Consumers must validate bounds and
/// alignment before importing. Retaining this object keeps memory mapped, but
/// does not retain the frame's producer read lease.
#[derive(Clone)]
pub struct FrameStorage {
    pub allocation: Arc<dyn FrameAllocation>,
    pub offset: usize,
    pub length: usize,
}

/// Immutable pixels whose owner prevents producer reuse for the lease lifetime.
pub trait FramePixels: Send + Sync {
    fn bytes(&self) -> &[u8];

    /// Optional native backing, with offset pointing to the first pixel byte.
    /// Keep this FramePixels owner alive throughout CPU/GPU access to the plane.
    fn storage(&self) -> Option<FrameStorage> {
        None
    }
}
impl FramePixels for Vec<u8> {
    fn bytes(&self) -> &[u8] {
        self
    }
}

/// A stable frame that can cross from the backend to the render worker.
/// `damage` covers changes since the immediately preceding generation. Consumers
/// skipping a generation must upload the complete image.
#[derive(Clone)]
pub struct FrameLease {
    pub pixels: Arc<dyn FramePixels>,
    pub width: u32,
    pub height: u32,
    pub stride: u32,
    pub format: PixelFormat,
    pub generation: u64,
    pub damage: Option<DirtyRect>,
}
impl FrameLease {
    pub fn bytes(&self) -> &[u8] {
        self.pixels.bytes()
    }
    pub fn is_valid(&self) -> bool {
        let row = self.width as usize * self.format.bytes_per_pixel();
        self.width > 0
            && self.height > 0
            && self.stride as usize >= row
            && (self.stride as usize)
                .checked_mul(self.height as usize - 1)
                .and_then(|n| n.checked_add(row))
                .is_some_and(|n| n <= self.bytes().len())
    }
    pub fn packed_copy(&self) -> Option<Vec<u8>> {
        if !self.is_valid() {
            return None;
        }
        let row = self.width as usize * self.format.bytes_per_pixel();
        let mut pixels = Vec::with_capacity(row * self.height as usize);
        for y in 0..self.height as usize {
            let offset = y * self.stride as usize;
            pixels.extend_from_slice(&self.bytes()[offset..offset + row]);
        }
        Some(pixels)
    }
}
