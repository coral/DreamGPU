// SPDX-License-Identifier: GPL-2.0-or-later
//! Guest-positioned cursor plane, independent of desktop framebuffer ownership.
use std::sync::Arc;

/// Canonical cursor wire contract for native adapter conformance checks.
pub const WIRE_HEADER: &str = include_str!("../include/dreamgpu/cursor.h");

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum NativeCursorFormat {
    PremultipliedArgb,
    AndXor,
}

#[derive(Clone, Debug)]
pub struct NativeCursorShape {
    pub width: u16,
    pub height: u16,
    pub hot_x: u16,
    pub hot_y: u16,
    pub format: NativeCursorFormat,
    /// Packed top-left rows: [0xAARRGGBB, 0], or [RGB AND mask, RGB XOR color].
    pub pixels: Vec<[u32; 2]>,
}

impl NativeCursorShape {
    pub fn validate(&self) -> Result<(), String> {
        if self.width == 0
            || self.height == 0
            || self.width > 64
            || self.height > 64
            || self.hot_x >= self.width
            || self.hot_y >= self.height
            || self.pixels.len() != usize::from(self.width) * usize::from(self.height)
        {
            return Err("Invalid native cursor dimensions, hotspot or pixel count".into());
        }
        for &[first, second] in &self.pixels {
            let valid = match self.format {
                NativeCursorFormat::PremultipliedArgb => {
                    let alpha = first >> 24;
                    second == 0
                        && (first & 255) <= alpha
                        && ((first >> 8) & 255) <= alpha
                        && ((first >> 16) & 255) <= alpha
                }
                NativeCursorFormat::AndXor => (first | second) & 0xff000000 == 0,
            };
            if !valid {
                return Err("Invalid native cursor pixel encoding".into());
            }
        }
        Ok(())
    }
}

#[derive(Clone, Debug, Default)]
pub struct NativeCursor {
    /// Host-local revision; changes on shape, position, visibility or reconnect.
    pub revision: u64,
    pub shape: Option<Arc<NativeCursorShape>>,
    pub x: i32,
    pub y: i32,
    pub visible: bool,
    pub enabled: bool,
}

impl NativeCursor {
    /// Revisions advance for accepted transport updates, including identical moves.
    /// Those must not schedule another GPU presentation on an idle desktop.
    pub fn same_presentation(&self, other: &Self) -> bool {
        if self.enabled != other.enabled {
            return false;
        }
        if !self.enabled {
            return true;
        }
        if self.visible != other.visible {
            return false;
        }
        if !self.visible {
            return true;
        }
        self.x == other.x
            && self.y == other.y
            && match (&self.shape, &other.shape) {
                (Some(a), Some(b)) => Arc::ptr_eq(a, b),
                (None, None) => true,
                _ => false,
            }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn invisible_motion_does_not_schedule_presentation_but_showing_does() {
        let hidden = NativeCursor {
            enabled: true,
            ..Default::default()
        };
        let moved = NativeCursor {
            x: 100,
            y: 200,
            revision: 2,
            ..hidden.clone()
        };
        assert!(hidden.same_presentation(&moved));
        let visible = NativeCursor {
            visible: true,
            ..moved.clone()
        };
        assert!(!moved.same_presentation(&visible));
        assert!(!visible.same_presentation(&NativeCursor {
            x: 101,
            ..visible.clone()
        }));
    }
}
