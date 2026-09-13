// SPDX-License-Identifier: GPL-2.0-or-later
//! wgpu desktop composition and native image import, independent of any window toolkit.
pub mod desktop;
pub mod gpu_diagnostic;
#[cfg(target_os = "macos")]
pub mod gpu_texture;
#[cfg(target_os = "linux")]
pub mod linux_gpu_texture;
pub mod native_cursor;
#[cfg(target_os = "macos")]
pub mod shared_texture;
