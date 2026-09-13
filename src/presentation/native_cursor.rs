// SPDX-License-Identifier: GPL-2.0-or-later
//! Compose only the cursor rectangle on the GPU, reading the untouched desktop.
//! AND/XOR operates on the guest's encoded RGB bytes, including inversion.
use crate::{NativeCursorFormat, NativeCursorShape};
use wgpu::util::DeviceExt;

const SHADER: &str = r#"
struct Params { origin: vec2<i32>, size: vec2<u32>, mode: u32, srgb: u32, padding: vec2<u32> }
@group(0) @binding(0) var desktop: texture_2d<f32>;
@group(0) @binding(1) var<storage, read> pixels: array<vec2<u32>>;
@group(0) @binding(2) var<uniform> params: Params;
fn encode(v: vec3<f32>) -> vec3<f32> {
    return select(1.055 * pow(max(v, vec3<f32>(0)), vec3<f32>(1.0/2.4)) - 0.055, 12.92*v, v <= vec3<f32>(0.0031308));
}
fn decode(v: vec3<f32>) -> vec3<f32> {
    return select(pow((v+0.055)/1.055,vec3<f32>(2.4)),v/12.92,v <= vec3<f32>(0.04045));
}
fn rgb(word: u32) -> vec3<u32> { return vec3<u32>((word>>16u)&255u,(word>>8u)&255u,word&255u); }
@vertex fn vertex(@builtin(vertex_index) index: u32) -> @builtin(position) vec4<f32> {
    let x = f32((index << 1u) & 2u); let y = f32(index & 2u);
    return vec4<f32>(x*2.0-1.0,1.0-y*2.0,0.0,1.0);
}
@fragment fn fragment(@builtin(position) point: vec4<f32>) -> @location(0) vec4<f32> {
    let p = vec2<u32>(point.xy); let at = params.origin + vec2<i32>(p);
    let size = vec2<i32>(textureDimensions(desktop));
    if any(at < vec2<i32>(0)) || any(at >= size) { discard; }
    let pair = pixels[p.y*params.size.x+p.x];
    let loaded = textureLoad(desktop,at,0).rgb;
    let encoded = select(loaded,encode(loaded),params.srgb!=0u);
    let background = vec3<u32>(round(clamp(encoded,vec3<f32>(0),vec3<f32>(1))*255.0));
    var result: vec3<u32>;
    if params.mode == 1u {
        let alpha = pair.x >> 24u;
        if alpha == 0u && params.padding.x == 0u { discard; }
        result = min(rgb(pair.x) + (background*(255u-alpha)+127u)/255u,vec3<u32>(255));
    } else {
        if pair.x == 0x00ffffffu && pair.y == 0u && params.padding.x == 0u { discard; }
        result = (background & rgb(pair.x)) ^ rgb(pair.y);
    }
    return vec4<f32>(decode(vec3<f32>(result)/255.0),1.0);
}
"#;

pub struct NativeCursorComposer {
    layout: wgpu::BindGroupLayout,
    pipeline: wgpu::RenderPipeline,
    pixels: wgpu::Buffer,
    params: wgpu::Buffer,
    texture: wgpu::Texture,
    mode: u32,
}
impl NativeCursorComposer {
    pub fn new(device: &wgpu::Device, shape: &NativeCursorShape) -> Result<Self, String> {
        shape.validate()?;
        let layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("native cursor composition"),
            entries: &[
                wgpu::BindGroupLayoutEntry {
                    binding: 0,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Texture {
                        sample_type: wgpu::TextureSampleType::Float { filterable: false },
                        view_dimension: wgpu::TextureViewDimension::D2,
                        multisampled: false,
                    },
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 1,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Buffer {
                        ty: wgpu::BufferBindingType::Storage { read_only: true },
                        has_dynamic_offset: false,
                        min_binding_size: None,
                    },
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 2,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Buffer {
                        ty: wgpu::BufferBindingType::Uniform,
                        has_dynamic_offset: false,
                        min_binding_size: None,
                    },
                    count: None,
                },
            ],
        });
        let pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("native cursor composition"),
            bind_group_layouts: &[Some(&layout)],
            immediate_size: 0,
        });
        let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("native cursor RGB algebra"),
            source: wgpu::ShaderSource::Wgsl(SHADER.into()),
        });
        let pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("native cursor RGB algebra"),
            layout: Some(&pipeline_layout),
            vertex: wgpu::VertexState {
                module: &shader,
                entry_point: Some("vertex"),
                buffers: &[],
                compilation_options: Default::default(),
            },
            fragment: Some(wgpu::FragmentState {
                module: &shader,
                entry_point: Some("fragment"),
                targets: &[Some(wgpu::ColorTargetState {
                    format: wgpu::TextureFormat::Bgra8UnormSrgb,
                    blend: None,
                    write_mask: wgpu::ColorWrites::ALL,
                })],
                compilation_options: Default::default(),
            }),
            primitive: Default::default(),
            depth_stencil: None,
            multisample: Default::default(),
            multiview_mask: None,
            cache: None,
        });
        let pixels = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("native cursor shape"),
            contents: bytemuck::cast_slice(&shape.pixels),
            usage: wgpu::BufferUsages::STORAGE,
        });
        let params = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("native cursor position"),
            size: 32,
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        let texture = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("composed native cursor rectangle"),
            size: wgpu::Extent3d {
                width: shape.width.into(),
                height: shape.height.into(),
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Bgra8UnormSrgb,
            usage: wgpu::TextureUsages::TEXTURE_BINDING
                | wgpu::TextureUsages::RENDER_ATTACHMENT
                | wgpu::TextureUsages::COPY_SRC,
            view_formats: &[],
        });
        Ok(Self {
            layout,
            pipeline,
            pixels,
            params,
            texture,
            mode: match shape.format {
                NativeCursorFormat::PremultipliedArgb => 1,
                NativeCursorFormat::AndXor => 2,
            },
        })
    }
    pub fn texture(&self) -> &wgpu::Texture {
        &self.texture
    }
    pub fn set_shape(
        &mut self,
        device: &wgpu::Device,
        shape: &NativeCursorShape,
    ) -> Result<(), String> {
        shape.validate()?;
        self.pixels = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("native cursor shape"),
            contents: bytemuck::cast_slice(&shape.pixels),
            usage: wgpu::BufferUsages::STORAGE,
        });
        self.mode = match shape.format {
            NativeCursorFormat::PremultipliedArgb => 1,
            NativeCursorFormat::AndXor => 2,
        };
        if self.texture.width() != u32::from(shape.width)
            || self.texture.height() != u32::from(shape.height)
        {
            self.texture = device.create_texture(&wgpu::TextureDescriptor {
                label: Some("composed native cursor rectangle"),
                size: wgpu::Extent3d {
                    width: shape.width.into(),
                    height: shape.height.into(),
                    depth_or_array_layers: 1,
                },
                mip_level_count: 1,
                sample_count: 1,
                dimension: wgpu::TextureDimension::D2,
                format: wgpu::TextureFormat::Bgra8UnormSrgb,
                usage: wgpu::TextureUsages::TEXTURE_BINDING
                    | wgpu::TextureUsages::RENDER_ATTACHMENT
                    | wgpu::TextureUsages::COPY_SRC,
                view_formats: &[],
            });
        }
        Ok(())
    }
    pub fn compose(
        &self,
        device: &wgpu::Device,
        queue: &wgpu::Queue,
        encoder: &mut wgpu::CommandEncoder,
        desktop: &wgpu::Texture,
        x: i32,
        y: i32,
    ) {
        self.compose_inner(device, queue, encoder, desktop, x, y, false);
    }
    #[expect(
        clippy::too_many_arguments,
        reason = "Carries independent GPU resources and cursor placement into one render pass."
    )]
    fn compose_inner(
        &self,
        device: &wgpu::Device,
        queue: &wgpu::Queue,
        encoder: &mut wgpu::CommandEncoder,
        desktop: &wgpu::Texture,
        x: i32,
        y: i32,
        preserve_background: bool,
    ) {
        let params = [
            x as u32,
            y as u32,
            self.texture.width(),
            self.texture.height(),
            self.mode,
            desktop.format().is_srgb() as u32,
            preserve_background as u32,
            0,
        ];
        queue.write_buffer(&self.params, 0, bytemuck::cast_slice(&params));
        let source = desktop.create_view(&Default::default());
        let target = self.texture.create_view(&Default::default());
        let bindings = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("native cursor source"),
            layout: &self.layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::TextureView(&source),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: self.pixels.as_entire_binding(),
                },
                wgpu::BindGroupEntry {
                    binding: 2,
                    resource: self.params.as_entire_binding(),
                },
            ],
        });
        let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
            label: Some("compose native cursor rectangle"),
            color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                view: &target,
                resolve_target: None,
                depth_slice: None,
                ops: wgpu::Operations {
                    load: wgpu::LoadOp::Clear(wgpu::Color::TRANSPARENT),
                    store: wgpu::StoreOp::Store,
                },
            })],
            depth_stencil_attachment: None,
            timestamp_writes: None,
            occlusion_query_set: None,
            multiview_mask: None,
        });
        pass.set_pipeline(&self.pipeline);
        pass.set_bind_group(0, &bindings, &[]);
        pass.draw(0..3, 0..1);
    }
    /// Explicit diagnostic snapshot only. Full desktop copy is never presented.
    pub fn snapshot(
        &self,
        device: &wgpu::Device,
        queue: &wgpu::Queue,
        desktop: &wgpu::Texture,
        x: i32,
        y: i32,
    ) -> Result<wgpu::Texture, String> {
        if u64::from(desktop.width()) * u64::from(desktop.height()) * 4 > 64 * 1024 * 1024 {
            return Err("Cursor diagnostic exceeds the 64 MiB readback bound".into());
        }
        let image = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("explicit desktop and cursor diagnostic"),
            size: desktop.size(),
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: desktop.format(),
            usage: wgpu::TextureUsages::COPY_SRC | wgpu::TextureUsages::COPY_DST,
            view_formats: &[],
        });
        let mut encoder = device.create_command_encoder(&Default::default());
        encoder.copy_texture_to_texture(
            desktop.as_image_copy(),
            image.as_image_copy(),
            desktop.size(),
        );
        let left = i64::from(x).max(0);
        let top = i64::from(y).max(0);
        let right =
            (i64::from(x) + i64::from(self.texture.width())).min(i64::from(desktop.width()));
        let bottom =
            (i64::from(y) + i64::from(self.texture.height())).min(i64::from(desktop.height()));
        if right > left && bottom > top {
            self.compose_inner(device, queue, &mut encoder, desktop, x, y, true);
            encoder.copy_texture_to_texture(
                wgpu::TexelCopyTextureInfo {
                    texture: &self.texture,
                    mip_level: 0,
                    origin: wgpu::Origin3d {
                        x: (left - i64::from(x)) as u32,
                        y: (top - i64::from(y)) as u32,
                        z: 0,
                    },
                    aspect: wgpu::TextureAspect::All,
                },
                wgpu::TexelCopyTextureInfo {
                    texture: &image,
                    mip_level: 0,
                    origin: wgpu::Origin3d {
                        x: left as u32,
                        y: top as u32,
                        z: 0,
                    },
                    aspect: wgpu::TextureAspect::All,
                },
                wgpu::Extent3d {
                    width: (right - left) as u32,
                    height: (bottom - top) as u32,
                    depth_or_array_layers: 1,
                },
            );
        }
        queue.submit([encoder.finish()]);
        Ok(image)
    }
}
