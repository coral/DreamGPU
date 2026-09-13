// SPDX-License-Identifier: GPL-2.0-or-later
//! Minimal native GPU presentation consumer. No application integration or guest media required.
//! `cargo run --example viewer --features presentation -- /tmp/dreamgpu.sock`
//! `--smoke` presents a synthetic ordered desktop once and exits.
use dreamgpu::{
    desktop::{DesktopBatch, DesktopOp},
    presentation::desktop::DesktopCanvas,
    transport::GpuServer,
    GpuHostInfo,
};
use std::{
    path::PathBuf,
    sync::{
        atomic::{AtomicUsize, Ordering},
        Arc,
    },
    time::{Duration, Instant},
};
use winit::{
    application::ApplicationHandler,
    event::WindowEvent,
    event_loop::{ActiveEventLoop, ControlFlow, EventLoop, EventLoopProxy},
    window::{Window, WindowId},
};

fn block_on<T>(future: impl std::future::Future<Output = T>) -> T {
    struct Wake(std::thread::Thread);
    impl std::task::Wake for Wake {
        fn wake(self: Arc<Self>) {
            self.0.unpark();
        }
    }
    let waker = std::task::Waker::from(Arc::new(Wake(std::thread::current())));
    let mut cx = std::task::Context::from_waker(&waker);
    let mut future = std::pin::pin!(future);
    loop {
        match future.as_mut().poll(&mut cx) {
            std::task::Poll::Ready(v) => return v,
            std::task::Poll::Pending => std::thread::park(),
        }
    }
}

struct Viewer {
    socket: PathBuf,
    smoke: bool,
    proxy: EventLoopProxy<()>,
    state: Option<State>,
    failed: bool,
    smoke_deadline: Instant,
}
struct State {
    window: Arc<Window>,
    surface: wgpu::Surface<'static>,
    config: wgpu::SurfaceConfiguration,
    device: wgpu::Device,
    queue: wgpu::Queue,
    server: GpuServer,
    canvas: DesktopCanvas,
    pipeline: wgpu::RenderPipeline,
    sampler: wgpu::Sampler,
    // Native images retain their producer lease until the consumer submission completes.
    #[cfg(target_os = "macos")]
    image: Option<dreamgpu::presentation::gpu_texture::GpuTextureFrame>,
    #[cfg(target_os = "linux")]
    image: Option<wgpu::Texture>,
    minimized: bool,
    pending: Arc<AtomicUsize>,
}

impl Viewer {
    fn start(&self, event_loop: &ActiveEventLoop) -> Result<State, Box<dyn std::error::Error>> {
        let window = Arc::new(
            event_loop.create_window(
                Window::default_attributes()
                    .with_title("DreamGPU reference viewer")
                    .with_inner_size(winit::dpi::LogicalSize::new(960, 720)),
            )?,
        );
        #[cfg(target_os = "macos")]
        let backends = wgpu::Backends::METAL;
        #[cfg(target_os = "linux")]
        let backends = wgpu::Backends::VULKAN;
        let instance = wgpu::Instance::new(wgpu::InstanceDescriptor {
            backends,
            ..wgpu::InstanceDescriptor::new_without_display_handle()
        });
        let surface = instance.create_surface(window.clone())?;
        let adapter = block_on(instance.request_adapter(&wgpu::RequestAdapterOptions {
            compatible_surface: Some(&surface),
            ..Default::default()
        }))?;
        #[cfg(target_os = "macos")]
        let (device, queue) = block_on(adapter.request_device(&Default::default()))?;
        #[cfg(target_os = "linux")]
        let (device, queue) = dreamgpu::presentation::linux_gpu_texture::create_device(
            &adapter,
            &Default::default(),
        )?
        .ok_or("GPU lacks required DMA-BUF interop")?;
        #[cfg(target_os = "macos")]
        let host = GpuHostInfo::default();
        #[cfg(target_os = "linux")]
        let host = GpuHostInfo {
            device_uuid: dreamgpu::presentation::linux_gpu_texture::device_uuid(&device)?,
            render_node: Some(dreamgpu::presentation::linux_gpu_texture::render_node(
                &device,
            )?),
        };
        let size = window.inner_size();
        let mut config = surface
            .get_default_config(&adapter, size.width.max(1), size.height.max(1))
            .ok_or("Surface has no compatible configuration")?;
        // Encoded guest RGB is sampled and written without implicit sRGB conversion.
        if let Some(format) = surface
            .get_capabilities(&adapter)
            .formats
            .into_iter()
            .find(|f| !f.is_srgb())
        {
            config.format = format;
        }
        config.desired_maximum_frame_latency = 1;
        surface.configure(&device, &config);
        let shader=device.create_shader_module(wgpu::ShaderModuleDescriptor { label:Some("DreamGPU viewer"), source:wgpu::ShaderSource::Wgsl(r#"
            @group(0) @binding(0) var image: texture_2d<f32>;
            @group(0) @binding(1) var image_sampler: sampler;
            struct Vertex { @builtin(position) position: vec4<f32>, @location(0) uv: vec2<f32> }
            @vertex fn vs(@builtin(vertex_index) i: u32) -> Vertex {
                let uv=vec2<f32>(f32((i << 1u) & 2u),f32(i & 2u));
                var v:Vertex; v.position=vec4<f32>(uv.x*2.-1.,1.-uv.y*2.,0.,1.);v.uv=uv;return v;
            }
            @fragment fn fs(v:Vertex) -> @location(0) vec4<f32> { return textureSample(image,image_sampler,v.uv); }
        "#.into()) });
        let pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("DreamGPU viewer"),
            layout: None,
            vertex: wgpu::VertexState {
                module: &shader,
                entry_point: Some("vs"),
                buffers: &[],
                compilation_options: Default::default(),
            },
            fragment: Some(wgpu::FragmentState {
                module: &shader,
                entry_point: Some("fs"),
                targets: &[Some(config.format.into())],
                compilation_options: Default::default(),
            }),
            primitive: Default::default(),
            depth_stencil: None,
            multisample: Default::default(),
            multiview_mask: None,
            cache: None,
        });
        let sampler = device.create_sampler(&Default::default());
        let server = GpuServer::new(&self.socket, host)?;
        let proxy = self.proxy.clone();
        server.set_callback(Arc::new(move |_| {
            let _ = proxy.send_event(());
        }));
        let mut canvas = DesktopCanvas::default();
        if self.smoke {
            let frame = dreamgpu::FrameLease {
                width: 64,
                height: 64,
                stride: 256,
                format: dreamgpu::PixelFormat::Bgra8,
                generation: 1,
                damage: None,
                pixels: Arc::new([0x90, 0x50, 0x20, 0xff].repeat(64 * 64)),
            };
            canvas.apply(
                &device,
                &queue,
                DesktopBatch {
                    epoch: 1,
                    sequence: 1,
                    operations: vec![DesktopOp::Seed(frame)],
                },
            )?;
        }
        eprintln!(
            "DreamGPU viewer ready: {} ({})",
            self.socket.display(),
            adapter.get_info().name
        );
        window.request_redraw();
        Ok(State {
            window,
            surface,
            config,
            device,
            queue,
            server,
            canvas,
            pipeline,
            sampler,
            image: None,
            minimized: false,
            pending: Arc::new(AtomicUsize::new(0)),
        })
    }
    fn fail(&mut self, event_loop: &ActiveEventLoop, error: impl std::fmt::Display) {
        eprintln!("DreamGPU viewer: {error}");
        self.failed = true;
        event_loop.exit();
    }
}
impl State {
    fn service(&mut self) -> Result<(), String> {
        self.device
            .poll(wgpu::PollType::Poll)
            .map_err(|e| e.to_string())?;
        if let Some(error) = self.server.take_error() {
            return Err(error);
        }
        // Bounded transport capacity limits work per event. A completion wake continues draining.
        for _ in 0..64 {
            let Some(batch) = self.server.take_desktop_update() else {
                break;
            };
            self.canvas.apply(&self.device, &self.queue, batch)?;
        }
        if let Some(frame) = self.server.take_frame() {
            #[cfg(target_os = "macos")]
            {
                self.image = Some(dreamgpu::presentation::gpu_texture::import_iosurface(
                    &self.device,
                    &frame,
                )?);
            }
            #[cfg(target_os = "linux")]
            {
                self.image = Some(
                    dreamgpu::presentation::linux_gpu_texture::copy_lease(
                        &self.device,
                        &self.queue,
                        &frame,
                    )
                    .map_err(|e| e.to_string())?,
                );
            }
        }
        // Drawable mailbox references are not desktop authority. The ordered stream owns them.
        drop(self.server.take_drawables());
        Ok(())
    }
    fn render(&mut self) -> Result<bool, String> {
        if self.minimized {
            return Ok(false);
        }
        let output = match self.surface.get_current_texture() {
            wgpu::CurrentSurfaceTexture::Success(t)
            | wgpu::CurrentSurfaceTexture::Suboptimal(t) => t,
            wgpu::CurrentSurfaceTexture::Outdated | wgpu::CurrentSurfaceTexture::Lost => {
                self.surface.configure(&self.device, &self.config);
                self.window.request_redraw();
                return Ok(false);
            }
            wgpu::CurrentSurfaceTexture::Timeout | wgpu::CurrentSurfaceTexture::Occluded => {
                return Ok(false)
            }
            other => return Err(format!("Surface acquisition failed: {other:?}")),
        };
        let target = output.texture.create_view(&Default::default());
        let mut encoder = self.device.create_command_encoder(&Default::default());
        #[cfg(target_os = "macos")]
        let image = self.image.as_ref().map(|i| i.texture());
        #[cfg(target_os = "linux")]
        let image = self.image.as_ref();
        let texture = if self.canvas.is_mixed() {
            self.canvas.texture()
        } else {
            image.or(self.canvas.texture())
        };
        let group = texture.map(|t| {
            self.device.create_bind_group(&wgpu::BindGroupDescriptor {
                label: None,
                layout: &self.pipeline.get_bind_group_layout(0),
                entries: &[
                    wgpu::BindGroupEntry {
                        binding: 0,
                        resource: wgpu::BindingResource::TextureView(
                            &t.create_view(&Default::default()),
                        ),
                    },
                    wgpu::BindGroupEntry {
                        binding: 1,
                        resource: wgpu::BindingResource::Sampler(&self.sampler),
                    },
                ],
            })
        });
        {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: None,
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &target,
                    depth_slice: None,
                    resolve_target: None,
                    ops: wgpu::Operations {
                        load: wgpu::LoadOp::Clear(wgpu::Color::BLACK),
                        store: wgpu::StoreOp::Store,
                    },
                })],
                depth_stencil_attachment: None,
                timestamp_writes: None,
                occlusion_query_set: None,
                multiview_mask: None,
            });
            if let Some(group) = &group {
                pass.set_pipeline(&self.pipeline);
                pass.set_bind_group(0, group, &[]);
                pass.draw(0..3, 0..1);
            }
        }
        self.pending.fetch_add(1, Ordering::Release);
        self.queue.submit([encoder.finish()]);
        let pending = self.pending.clone();
        self.queue.on_submitted_work_done(move || {
            pending.fetch_sub(1, Ordering::Release);
        });
        #[cfg(target_os = "macos")]
        if let Some(image) = &self.image {
            image.retain_until_submitted_work_done(&self.queue, || {});
        }
        self.window.pre_present_notify();
        self.queue.present(output);
        Ok(true)
    }
}
impl ApplicationHandler<()> for Viewer {
    fn resumed(&mut self, e: &ActiveEventLoop) {
        if self.state.is_none() {
            match self.start(e) {
                Ok(s) => self.state = Some(s),
                Err(error) => self.fail(e, error),
            }
        }
    }
    fn user_event(&mut self, e: &ActiveEventLoop, _: ()) {
        if let Some(s) = &mut self.state {
            match s.service() {
                Ok(()) => s.window.request_redraw(),
                Err(error) => self.fail(e, error),
            }
        }
    }
    fn window_event(&mut self, e: &ActiveEventLoop, _: WindowId, event: WindowEvent) {
        let Some(s) = &mut self.state else { return };
        match event {
            WindowEvent::CloseRequested => e.exit(),
            WindowEvent::Occluded(false) => s.window.request_redraw(),
            WindowEvent::Resized(size) => {
                s.minimized = size.width == 0 || size.height == 0;
                if !s.minimized {
                    s.config.width = size.width;
                    s.config.height = size.height;
                    s.surface.configure(&s.device, &s.config);
                    s.window.request_redraw();
                }
            }
            WindowEvent::RedrawRequested => match s.service().and_then(|_| s.render()) {
                Ok(true) if self.smoke => {
                    eprintln!("DreamGPU viewer smoke passed");
                    e.exit()
                }
                Ok(_) => {}
                Err(error) => self.fail(e, error),
            },
            _ => {}
        }
    }
    fn about_to_wait(&mut self, e: &ActiveEventLoop) {
        if self.smoke && Instant::now() >= self.smoke_deadline {
            self.fail(e, "Smoke presentation timed out (window may be occluded)");
            return;
        }
        if let Some(s) = &mut self.state {
            if self.smoke {
                s.window.request_redraw();
            }
            if let Err(error) = s.service() {
                self.fail(e, error);
                return;
            }
            // Pump pending readback completions without a busy loop or idle polling.
            e.set_control_flow(
                if self.smoke
                    || s.canvas.has_pending_completions()
                    || s.pending.load(Ordering::Acquire) != 0
                {
                    ControlFlow::WaitUntil(Instant::now() + Duration::from_millis(4))
                } else {
                    ControlFlow::Wait
                },
            );
        }
    }
    fn exiting(&mut self, _: &ActiveEventLoop) {
        if let Some(s) = self.state.take() {
            let _ = s.device.poll(wgpu::PollType::wait_indefinitely());
            drop(s);
        }
    }
}
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<_> = std::env::args().skip(1).collect();
    if args.iter().any(|a| a == "--help") {
        println!("viewer [SOCKET] [--smoke]\nConsumes DreamGPU native image/ordered desktop transport. This viewer presents native GPU images; a VM application supplies input routing and CPU-display integration.");
        return Ok(());
    }
    let smoke = args.iter().any(|a| a == "--smoke");
    let socket = args
        .iter()
        .find(|a| !a.starts_with('-'))
        .map(PathBuf::from)
        .unwrap_or_else(|| {
            std::env::temp_dir().join(format!("dreamgpu-viewer-{}.sock", std::process::id()))
        });
    let event_loop = EventLoop::<()>::with_user_event().build()?;
    let mut viewer = Viewer {
        socket,
        smoke,
        proxy: event_loop.create_proxy(),
        state: None,
        failed: false,
        smoke_deadline: Instant::now() + Duration::from_secs(10),
    };
    event_loop.run_app(&mut viewer)?;
    if viewer.failed {
        return Err("Viewer failed".into());
    }
    Ok(())
}
