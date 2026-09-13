// SPDX-License-Identifier: GPL-2.0-or-later
//! Explicit offscreen GPU readback for diagnostics, never part of presentation.

use crate::GpuFrameLease;
use std::{sync::mpsc, time::Duration};

pub struct PendingReadback {
    device: wgpu::Device,
    buffer: wgpu::Buffer,
    width: u32,
    height: u32,
    stride: u32,
}

/// Submit on the render thread. This does not bind or replace any VM canvas.
pub fn begin_readback(
    device: &wgpu::Device,
    queue: &wgpu::Queue,
    frame: &GpuFrameLease,
) -> Result<PendingReadback, String> {
    frame.validate().map_err(|error| error.to_string())?;
    #[cfg(target_os = "macos")]
    let imported = crate::presentation::gpu_texture::import_iosurface(device, frame)?;
    #[cfg(target_os = "macos")]
    let texture = imported.texture();
    #[cfg(target_os = "linux")]
    let imported = crate::presentation::linux_gpu_texture::copy_lease(device, queue, frame)
        .map_err(|error| error.to_string())?;
    #[cfg(target_os = "linux")]
    let texture = &imported;
    let pending = begin_texture_readback(device, queue, texture)?;
    #[cfg(target_os = "macos")]
    imported.retain_until_submitted_work_done(queue, || {});
    Ok(pending)
}

/// Copy a renderer-owned canonical BGRA image on explicit diagnostic request.
pub fn begin_texture_readback(
    device: &wgpu::Device,
    queue: &wgpu::Queue,
    texture: &wgpu::Texture,
) -> Result<PendingReadback, String> {
    if !matches!(
        texture.format(),
        wgpu::TextureFormat::Bgra8Unorm | wgpu::TextureFormat::Bgra8UnormSrgb
    ) || !texture.usage().contains(wgpu::TextureUsages::COPY_SRC)
        || texture.depth_or_array_layers() != 1
        || texture.sample_count() != 1
    {
        return Err("Diagnostic capture requires a copyable single-layer BGRA texture".into());
    }
    let stride = (u64::from(texture.width()) * 4).div_ceil(256) * 256;
    let size = stride * u64::from(texture.height());
    if size > 64 * 1024 * 1024 || stride > u64::from(u32::MAX) {
        return Err("Diagnostic drawable exceeds the 64 MiB readback bound".into());
    }
    let buffer = device.create_buffer(&wgpu::BufferDescriptor {
        label: Some("Explicit drawable diagnostic readback"),
        size,
        usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
        mapped_at_creation: false,
    });
    let mut encoder = device.create_command_encoder(&Default::default());
    encoder.copy_texture_to_buffer(
        texture.as_image_copy(),
        wgpu::TexelCopyBufferInfo {
            buffer: &buffer,
            layout: wgpu::TexelCopyBufferLayout {
                offset: 0,
                bytes_per_row: Some(stride as u32),
                rows_per_image: Some(texture.height()),
            },
        },
        texture.size(),
    );
    queue.submit([encoder.finish()]);
    Ok(PendingReadback {
        device: device.clone(),
        buffer,
        width: texture.width(),
        height: texture.height(),
        stride: stride as u32,
    })
}

impl PendingReadback {
    pub fn dimensions(&self) -> (u32, u32) {
        (self.width, self.height)
    }

    /// Wait only on a diagnostic/test thread, then pack canonical top-left BGRA.
    pub fn finish(self) -> Result<Vec<u8>, String> {
        let (sender, receiver) = mpsc::sync_channel(1);
        self.buffer
            .slice(..)
            .map_async(wgpu::MapMode::Read, move |result| {
                let _ = sender.try_send(result);
            });
        self.device
            .poll(wgpu::PollType::Wait {
                submission_index: None,
                timeout: Some(Duration::from_secs(10)),
            })
            .map_err(|error| error.to_string())?;
        receiver
            .recv_timeout(Duration::from_secs(1))
            .map_err(|error| error.to_string())?
            .map_err(|error| error.to_string())?;
        let pixels = self
            .buffer
            .slice(..)
            .get_mapped_range()
            .map_err(|error| error.to_string())?;
        let row = self.width as usize * 4;
        let mut packed = Vec::with_capacity(row * self.height as usize);
        for y in 0..self.height as usize {
            packed.extend_from_slice(
                &pixels[y * self.stride as usize..y * self.stride as usize + row],
            );
        }
        drop(pixels);
        self.buffer.unmap();
        Ok(packed)
    }
}
