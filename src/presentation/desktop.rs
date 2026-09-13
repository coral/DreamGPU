// SPDX-License-Identifier: GPL-2.0-or-later
//! GPU canvas for ordered mixed CPU/GPU desktops. Ordinary CPU frames bypass it.

use crate::{
    desktop::{
        DesktopBatch, DesktopOp, DesktopRect, MAX_DESKTOP_OPERATIONS, MAX_DESKTOP_PIXEL_BYTES,
    },
    FrameLease, PixelFormat,
};
use std::sync::{
    atomic::{AtomicUsize, Ordering},
    Arc,
};
use wgpu::util::DeviceExt;

type Result<T> = std::result::Result<T, String>;

#[derive(Clone, Copy, Default)]
struct State {
    epoch: u64,
    sequence: u64,
    width: u32,
    height: u32,
    mixed: bool,
}

/// Owned by the render worker, one per VM. Processing does not acquire a window
/// surface: offscreen/minimized VMs must still satisfy guest readbacks.
#[derive(Default)]
pub struct DesktopCanvas {
    state: State,
    texture: Option<wgpu::Texture>,
    scratch: Option<wgpu::Texture>,
    fill: Option<wgpu::RenderPipeline>,
    #[cfg(target_os = "macos")]
    cpu_cache: crate::presentation::shared_texture::DirectTextureCache,
    pending: Arc<AtomicUsize>,
}
impl DesktopCanvas {
    pub fn texture(&self) -> Option<&wgpu::Texture> {
        self.texture.as_ref()
    }
    pub fn is_mixed(&self) -> bool {
        self.state.mixed
    }
    pub fn generation(&self) -> u64 {
        self.state.sequence
    }
    pub fn has_pending_completions(&self) -> bool {
        self.pending.load(Ordering::Acquire) != 0
    }

    /// CPU authority keeps only the sequence tombstone after outstanding GPU
    /// work completes. A later mixed epoch must still begin with a fresh seed.
    pub fn release_cpu_resources(&mut self) {
        if !self.is_mixed() && !self.has_pending_completions() {
            self.texture = None;
            self.scratch = None;
            self.fill = None;
            #[cfg(target_os = "macos")]
            self.cpu_cache.clear();
        }
    }

    /// Validation is atomic. Native resources are also imported before changing
    /// the canvas. CPU uploads use encoder copies, preserving their order relative
    /// to GPU operations (queue.write_texture would move them ahead of the batch).
    pub fn apply(
        &mut self,
        device: &wgpu::Device,
        queue: &wgpu::Queue,
        batch: DesktopBatch,
    ) -> Result<()> {
        let result = self.apply_inner(device, queue, &batch);
        if let Err(error) = &result {
            for op in &batch.operations {
                if let DesktopOp::Readback { reply, .. } = op {
                    reply.complete(Err(error.clone()));
                }
            }
        }
        result
    }

    fn apply_inner(
        &mut self,
        device: &wgpu::Device,
        queue: &wgpu::Queue,
        batch: &DesktopBatch,
    ) -> Result<()> {
        let state = validate(self.state, batch, device.limits().max_texture_dimension_2d)?;
        if matches!(batch.operations.as_slice(), [DesktopOp::Reset]) {
            self.texture = None;
            self.scratch = None;
            self.fill = None;
            #[cfg(target_os = "macos")]
            self.cpu_cache.clear();
            self.state = state;
            return Ok(());
        }
        let mut cpu_images = Vec::new();
        for op in &batch.operations {
            if let DesktopOp::Seed(pixels) | DesktopOp::Patch { pixels, .. } = op {
                #[cfg(target_os = "macos")]
                let imported = self.cpu_cache.import(device, pixels)?.map(CpuImage::Shared);
                #[cfg(not(target_os = "macos"))]
                let imported: Option<CpuImage> = {
                    let _ = pixels;
                    None
                };
                cpu_images.push(imported.unwrap_or(CpuImage::Upload));
            }
        }
        let mut imports = Vec::new();
        for op in &batch.operations {
            if let DesktopOp::Blit { image, .. } = op {
                imports.push(import(device, queue, image)?);
            }
        }
        if self
            .texture
            .as_ref()
            .is_none_or(|t| t.width() != state.width || t.height() != state.height)
        {
            self.texture = Some(texture(
                device,
                state.width,
                state.height,
                "DreamGPU mixed desktop",
            ));
            self.scratch = None;
        }
        let desktop = self.texture.as_ref().unwrap();
        let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
            label: Some("DreamGPU ordered desktop"),
        });
        let mut imported = imports.iter();
        let mut cpu_image = cpu_images.iter();
        let mut reads = Vec::new();
        for op in &batch.operations {
            match op {
                DesktopOp::Reset => unreachable!("reset is validated as a standalone batch"),
                DesktopOp::Barrier => {}
                DesktopOp::Seed(pixels) => upload_cpu(
                    device,
                    &mut encoder,
                    desktop,
                    pixels,
                    0,
                    0,
                    cpu_image.next().unwrap(),
                ),
                // The renderer resumes its CPU path using this exact coherent lease.
                DesktopOp::ReturnCpu(_) => {}
                DesktopOp::Patch { x, y, pixels } => upload_cpu(
                    device,
                    &mut encoder,
                    desktop,
                    pixels,
                    *x,
                    *y,
                    cpu_image.next().unwrap(),
                ),
                DesktopOp::Blit { source, x, y, .. } => {
                    let image = imported.next().unwrap();
                    encoder.copy_texture_to_texture(
                        copy_info(&image.texture, source.x, source.y),
                        copy_info(desktop, *x, *y),
                        extent(source.width, source.height),
                    );
                }
                DesktopOp::Copy { source, x, y } => {
                    let scratch = self.scratch.get_or_insert_with(|| {
                        texture(
                            device,
                            state.width,
                            state.height,
                            "DreamGPU overlapping desktop copy",
                        )
                    });
                    encoder.copy_texture_to_texture(
                        copy_info(desktop, source.x, source.y),
                        copy_info(scratch, 0, 0),
                        extent(source.width, source.height),
                    );
                    encoder.copy_texture_to_texture(
                        copy_info(scratch, 0, 0),
                        copy_info(desktop, *x, *y),
                        extent(source.width, source.height),
                    );
                }
                DesktopOp::Fill { rect, bgra } => {
                    let pipeline = self.fill.get_or_insert_with(|| fill_pipeline(device));
                    let rgba = [bgra[2], bgra[1], bgra[0], bgra[3]].map(|c| f32::from(c) / 255.0);
                    let uniform = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
                        label: Some("desktop fill color"),
                        contents: bytemuck::cast_slice(&rgba),
                        usage: wgpu::BufferUsages::UNIFORM,
                    });
                    let bind = device.create_bind_group(&wgpu::BindGroupDescriptor {
                        label: None,
                        layout: &pipeline.get_bind_group_layout(0),
                        entries: &[wgpu::BindGroupEntry {
                            binding: 0,
                            resource: uniform.as_entire_binding(),
                        }],
                    });
                    // Fill bytes are already encoded guest colors, so disable sRGB conversion.
                    let view = desktop.create_view(&wgpu::TextureViewDescriptor {
                        format: Some(wgpu::TextureFormat::Bgra8Unorm),
                        ..Default::default()
                    });
                    let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                        label: Some("desktop solid fill"),
                        color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                            view: &view,
                            resolve_target: None,
                            depth_slice: None,
                            ops: wgpu::Operations {
                                load: wgpu::LoadOp::Load,
                                store: wgpu::StoreOp::Store,
                            },
                        })],
                        depth_stencil_attachment: None,
                        timestamp_writes: None,
                        occlusion_query_set: None,
                        multiview_mask: None,
                    });
                    pass.set_pipeline(pipeline);
                    pass.set_bind_group(0, &bind, &[]);
                    pass.set_scissor_rect(rect.x, rect.y, rect.width, rect.height);
                    pass.draw(0..3, 0..1);
                }
                DesktopOp::Readback { rect, reply } => {
                    let stride = aligned_stride(rect.width);
                    let buffer = device.create_buffer(&wgpu::BufferDescriptor {
                        label: Some("guest pixel readback"),
                        size: u64::from(stride) * u64::from(rect.height),
                        usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
                        mapped_at_creation: false,
                    });
                    encoder.copy_texture_to_buffer(
                        copy_info(desktop, rect.x, rect.y),
                        wgpu::TexelCopyBufferInfo {
                            buffer: &buffer,
                            layout: wgpu::TexelCopyBufferLayout {
                                offset: 0,
                                bytes_per_row: Some(stride),
                                rows_per_image: Some(rect.height),
                            },
                        },
                        extent(rect.width, rect.height),
                    );
                    reads.push((buffer, *rect, stride, reply.clone()));
                }
            }
        }
        queue.submit([encoder.finish()]);
        // Keep native handles, their producer leases, and ownership barriers alive
        // until this submission finishes, independently of host presentation.
        self.pending.fetch_add(1, Ordering::AcqRel);
        let pending = self.pending.clone();
        queue.on_submitted_work_done(move || {
            drop(imports);
            drop(cpu_images);
            pending.fetch_sub(1, Ordering::Release);
        });
        for (buffer, rect, stride, reply) in reads {
            self.pending.fetch_add(1, Ordering::AcqRel);
            let pending = self.pending.clone();
            let mapped = buffer.clone();
            let sequence = batch.sequence;
            buffer.map_async(wgpu::MapMode::Read, .., move |status| {
                let result = status.map_err(|error| error.to_string()).and_then(|_| {
                    let data = mapped.get_mapped_range(..).map_err(|e| e.to_string())?;
                    let mut bytes =
                        Vec::with_capacity(rect.width as usize * rect.height as usize * 4);
                    for row in data.chunks_exact(stride as usize) {
                        bytes.extend_from_slice(&row[..rect.width as usize * 4]);
                    }
                    drop(data);
                    mapped.unmap();
                    Ok(FrameLease {
                        pixels: Arc::new(bytes),
                        width: rect.width,
                        height: rect.height,
                        stride: rect.width * 4,
                        format: PixelFormat::Bgra8,
                        generation: sequence,
                        damage: None,
                    })
                });
                reply.complete(result);
                pending.fetch_sub(1, Ordering::Release);
            });
        }
        self.state = state;
        Ok(())
    }
}

fn validate(mut state: State, batch: &DesktopBatch, limit: u32) -> Result<State> {
    if batch.epoch == 0
        || batch.sequence == 0
        || batch.operations.is_empty()
        || batch.operations.len() > MAX_DESKTOP_OPERATIONS
    {
        return Err("Invalid desktop batch identity or count".into());
    }
    if batch
        .operations
        .iter()
        .any(|op| matches!(op, DesktopOp::Reset))
    {
        if batch.operations.len() != 1 || batch.epoch <= state.epoch || batch.sequence != 1 {
            return Err("Desktop reset requires a standalone fresh epoch and sequence 1".into());
        }
        return Ok(State {
            epoch: batch.epoch,
            sequence: 1,
            ..State::default()
        });
    }
    let seeded = matches!(batch.operations.first(), Some(DesktopOp::Seed(_)));
    if seeded {
        if batch.epoch <= state.epoch || batch.sequence != 1 {
            return Err("Desktop seed requires a fresh epoch and sequence 1".into());
        }
    } else if batch.epoch != state.epoch
        || state.sequence.checked_add(1) != Some(batch.sequence)
        || !state.mixed
    {
        return Err("Desktop batch is missing its seed or a preceding sequence".into());
    }
    let mut bytes = 0u64;
    for (index, op) in batch.operations.iter().enumerate() {
        let work = match op {
            DesktopOp::Reset => unreachable!("reset was validated above"),
            DesktopOp::Barrier => 0,
            DesktopOp::Seed(frame) => {
                if index != 0 {
                    return Err("Desktop seed must be first".into());
                }
                check_pixels(frame, limit)?;
                state.width = frame.width;
                state.height = frame.height;
                state.mixed = true;
                u64::from(aligned_stride(frame.width)) * u64::from(frame.height)
            }
            DesktopOp::ReturnCpu(frame) => {
                check_pixels(frame, limit)?;
                if index + 1 != batch.operations.len()
                    || frame.width != state.width
                    || frame.height != state.height
                {
                    return Err("ReturnCpu requires a final coherent full desktop".into());
                }
                state.mixed = false;
                u64::from(aligned_stride(frame.width)) * u64::from(frame.height)
            }
            DesktopOp::Patch { x, y, pixels } => {
                check_pixels(pixels, limit)?;
                check_rect(
                    DesktopRect {
                        x: *x,
                        y: *y,
                        width: pixels.width,
                        height: pixels.height,
                    },
                    state,
                )?;
                u64::from(aligned_stride(pixels.width)) * u64::from(pixels.height)
            }
            DesktopOp::Blit {
                image,
                source,
                x,
                y,
            } => {
                image.validate().map_err(|e| e.to_string())?;
                if image.format != PixelFormat::Bgra8
                    || image.width > limit
                    || image.height > limit
                    || !source.fits(image.width, image.height)
                {
                    return Err("Invalid GPU drawable format or region".into());
                }
                check_rect(
                    DesktopRect {
                        x: *x,
                        y: *y,
                        ..*source
                    },
                    state,
                )?;
                u64::from(image.width) * u64::from(image.height) * 4
            }
            DesktopOp::Copy { source, x, y } => {
                check_rect(*source, state)?;
                check_rect(
                    DesktopRect {
                        x: *x,
                        y: *y,
                        ..*source
                    },
                    state,
                )?;
                u64::from(source.width) * u64::from(source.height) * 8
            }
            DesktopOp::Fill { rect, .. } | DesktopOp::Readback { rect, .. } => {
                check_rect(*rect, state)?;
                u64::from(aligned_stride(rect.width)) * u64::from(rect.height)
            }
        };
        bytes += work;
        if bytes > MAX_DESKTOP_PIXEL_BYTES {
            return Err("Desktop batch exceeds its pixel budget".into());
        }
    }
    state.epoch = batch.epoch;
    state.sequence = batch.sequence;
    Ok(state)
}
fn check_pixels(frame: &FrameLease, limit: u32) -> Result<()> {
    if !frame.is_valid()
        || frame.format != PixelFormat::Bgra8
        || frame.width > limit
        || frame.height > limit
    {
        Err("Desktop CPU pixels must be valid bounded BGRA8".into())
    } else {
        Ok(())
    }
}
fn check_rect(rect: DesktopRect, state: State) -> Result<()> {
    if rect.fits(state.width, state.height) {
        Ok(())
    } else {
        Err("Desktop rectangle is outside its canvas".into())
    }
}
fn aligned_stride(width: u32) -> u32 {
    (width * 4).div_ceil(wgpu::COPY_BYTES_PER_ROW_ALIGNMENT) * wgpu::COPY_BYTES_PER_ROW_ALIGNMENT
}
fn extent(width: u32, height: u32) -> wgpu::Extent3d {
    wgpu::Extent3d {
        width,
        height,
        depth_or_array_layers: 1,
    }
}
fn copy_info(texture: &wgpu::Texture, x: u32, y: u32) -> wgpu::TexelCopyTextureInfo<'_> {
    wgpu::TexelCopyTextureInfo {
        texture,
        mip_level: 0,
        origin: wgpu::Origin3d { x, y, z: 0 },
        aspect: wgpu::TextureAspect::All,
    }
}
fn texture(device: &wgpu::Device, width: u32, height: u32, label: &str) -> wgpu::Texture {
    device.create_texture(&wgpu::TextureDescriptor {
        label: Some(label),
        size: extent(width, height),
        mip_level_count: 1,
        sample_count: 1,
        dimension: wgpu::TextureDimension::D2,
        format: wgpu::TextureFormat::Bgra8UnormSrgb,
        view_formats: &[wgpu::TextureFormat::Bgra8Unorm],
        usage: wgpu::TextureUsages::COPY_SRC
            | wgpu::TextureUsages::COPY_DST
            | wgpu::TextureUsages::TEXTURE_BINDING
            | wgpu::TextureUsages::RENDER_ATTACHMENT,
    })
}
enum CpuImage {
    Upload,
    #[cfg(target_os = "macos")]
    Shared(crate::presentation::shared_texture::SharedTextureFrame),
}
fn upload_cpu(
    device: &wgpu::Device,
    encoder: &mut wgpu::CommandEncoder,
    texture: &wgpu::Texture,
    pixels: &FrameLease,
    x: u32,
    y: u32,
    image: &CpuImage,
) {
    #[cfg(target_os = "macos")]
    if let CpuImage::Shared(frame) = image {
        encoder.copy_texture_to_texture(
            frame.texture().as_image_copy(),
            copy_info(texture, x, y),
            extent(pixels.width, pixels.height),
        );
        crate::perf::event(
            "desktop.cpu_direct_import",
            pixels.generation,
            u64::from(pixels.width) * u64::from(pixels.height) * 4,
        );
        return;
    }
    let _ = image;
    upload(device, encoder, texture, pixels, x, y);
}
fn upload(
    device: &wgpu::Device,
    encoder: &mut wgpu::CommandEncoder,
    texture: &wgpu::Texture,
    pixels: &FrameLease,
    x: u32,
    y: u32,
) {
    let stride = aligned_stride(pixels.width);
    let buffer = device.create_buffer(&wgpu::BufferDescriptor {
        label: Some("immutable desktop pixels"),
        size: u64::from(stride) * u64::from(pixels.height),
        usage: wgpu::BufferUsages::COPY_SRC,
        mapped_at_creation: true,
    });
    // Copy straight into the upload allocation. Building a padded Vec and then
    // create_buffer_init would copy every CPU patch a second time. wgpu zeros
    // new buffers, including the row padding left untouched here.
    {
        let mut staging = buffer
            .get_mapped_range_mut(..)
            .expect("new desktop upload buffer is fully mapped");
        for row in 0..pixels.height as usize {
            staging
                .slice(row * stride as usize..row * stride as usize + pixels.width as usize * 4)
                .copy_from_slice(
                    &pixels.bytes()[row * pixels.stride as usize
                        ..row * pixels.stride as usize + pixels.width as usize * 4],
                );
        }
    }
    buffer.unmap();
    encoder.copy_buffer_to_texture(
        wgpu::TexelCopyBufferInfo {
            buffer: &buffer,
            layout: wgpu::TexelCopyBufferLayout {
                offset: 0,
                bytes_per_row: Some(stride),
                rows_per_image: Some(pixels.height),
            },
        },
        copy_info(texture, x, y),
        extent(pixels.width, pixels.height),
    );
}

struct Imported {
    texture: wgpu::Texture,
    #[cfg(target_os = "macos")]
    _owner: crate::presentation::gpu_texture::GpuTextureFrame,
}
fn import(
    device: &wgpu::Device,
    queue: &wgpu::Queue,
    image: &crate::GpuFrameLease,
) -> Result<Imported> {
    #[cfg(target_os = "macos")]
    {
        let _ = queue;
        let owner = crate::presentation::gpu_texture::import_iosurface(device, image)?;
        Ok(Imported {
            texture: owner.texture().clone(),
            _owner: owner,
        })
    }
    #[cfg(target_os = "linux")]
    {
        Ok(Imported {
            texture: crate::presentation::linux_gpu_texture::copy_lease(device, queue, image)
                .map_err(|e| e.to_string())?,
        })
    }
    #[cfg(not(any(target_os = "macos", target_os = "linux")))]
    {
        let _ = (device, queue, image);
        Err("Native GPU desktop requires macOS or Linux".into())
    }
}
fn fill_pipeline(device: &wgpu::Device) -> wgpu::RenderPipeline {
    let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some("desktop fill"),
        source: wgpu::ShaderSource::Wgsl(
            r#"
        @group(0) @binding(0) var<uniform> color: vec4<f32>;
        @vertex fn vs(@builtin(vertex_index) id: u32) -> @builtin(position) vec4<f32> {
            let p = array<vec2<f32>, 3>(vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
            return vec4(p[id], 0.0, 1.0);
        }
        @fragment fn fs() -> @location(0) vec4<f32> { return color; }
    "#
            .into(),
        ),
    });
    device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
        label: Some("desktop solid fill"),
        layout: None,
        vertex: wgpu::VertexState {
            module: &shader,
            entry_point: Some("vs"),
            compilation_options: Default::default(),
            buffers: &[],
        },
        fragment: Some(wgpu::FragmentState {
            module: &shader,
            entry_point: Some("fs"),
            compilation_options: Default::default(),
            targets: &[Some(wgpu::ColorTargetState {
                format: wgpu::TextureFormat::Bgra8Unorm,
                blend: None,
                write_mask: wgpu::ColorWrites::ALL,
            })],
        }),
        primitive: Default::default(),
        depth_stencil: None,
        multisample: Default::default(),
        multiview_mask: None,
        cache: None,
    })
}
