// SPDX-License-Identifier: GPL-2.0-or-later
//! Single-plane EGL DMA-BUF images consumed by the Vulkan render queue.
//!
//! Each incoming image is acquired from the foreign queue, copied on the GPU
//! into an ordinary compositor texture, and released in the same submission.
//! The producer slot is retained until that submission completes. No pixel
//! mapping or CPU readback is involved.

use std::ffi::CStr;
use std::os::fd::{IntoRawFd, OwnedFd};
use std::sync::Arc;

use crate::{Error, GpuFrameLease, GpuImageHandle, PixelFormat, Result};
use ash::{khr, vk};

const INTEROP_FEATURES: wgpu::Features = wgpu::Features::VULKAN_EXTERNAL_MEMORY_DMA_BUF;
const DRM_ARGB8888: u32 = u32::from_le_bytes(*b"AR24");
const DRM_ABGR8888: u32 = u32::from_le_bytes(*b"AB24");

fn error(message: impl std::fmt::Display) -> Error {
    Error::Render(format!("DMA-BUF interop: {message}"))
}

/// Identity negotiated with the EGL producer before exporting allocations.
pub fn device_uuid(device: &wgpu::Device) -> Result<[u8; 16]> {
    let native = unsafe { device.as_hal::<wgpu::hal::api::Vulkan>() }
        .ok_or_else(|| error("renderer is not using Vulkan"))?;
    let mut identity = vk::PhysicalDeviceIDProperties::default();
    let mut properties = vk::PhysicalDeviceProperties2::default().push_next(&mut identity);
    // SAFETY: output structs belong to this call and the physical device to the
    // retained instance. Vulkan 1.1 properties are part of wgpu's Vulkan baseline.
    unsafe {
        native
            .shared_instance()
            .raw_instance()
            .get_physical_device_properties2(native.raw_physical_device(), &mut properties);
    }
    Ok(identity.device_uuid)
}

/// Select the EGL producer's render node from the renderer's physical device.
pub fn render_node(device: &wgpu::Device) -> Result<String> {
    let native = unsafe { device.as_hal::<wgpu::hal::api::Vulkan>() }
        .ok_or_else(|| error("renderer is not using Vulkan"))?;
    let instance = native.shared_instance().raw_instance();
    let extensions =
        unsafe { instance.enumerate_device_extension_properties(native.raw_physical_device()) }
            .map_err(error)?;
    if !extensions.iter().any(|property| unsafe {
        CStr::from_ptr(property.extension_name.as_ptr()) == ash::ext::physical_device_drm::NAME
    }) {
        return Err(error("physical GPU does not expose a DRM render node"));
    }
    let mut drm = vk::PhysicalDeviceDrmPropertiesEXT::default();
    let mut properties = vk::PhysicalDeviceProperties2::default().push_next(&mut drm);
    unsafe {
        instance.get_physical_device_properties2(native.raw_physical_device(), &mut properties)
    };
    if drm.has_render == 0 || drm.render_minor < 0 {
        return Err(error("physical GPU has no DRM render node"));
    }
    Ok(format!("/dev/dri/renderD{}", drm.render_minor))
}

/// Consume a producer lease into the canonical GPU desktop source texture.
/// Call on the render worker, serialized with other queue submissions.
pub fn copy_lease(
    device: &wgpu::Device,
    queue: &wgpu::Queue,
    frame: &GpuFrameLease,
) -> Result<wgpu::Texture> {
    frame.validate()?;
    let GpuImageHandle::DmaBuf {
        fd,
        fourcc,
        modifier,
        stride,
        offset,
        device_uuid: source_uuid,
        ready_fence,
    } = frame.image.handle()
    else {
        return Err(error("image is not a DMA-BUF"));
    };
    if source_uuid == [0; 16] || source_uuid != device_uuid(device)? {
        return Err(error("producer and renderer use different physical GPUs"));
    }
    let expected = match frame.format {
        PixelFormat::Bgra8 => DRM_ARGB8888,
        PixelFormat::Rgba8 => DRM_ABGR8888,
        _ => return Err(error("unsupported GPU pixel format")),
    };
    if fourcc != expected {
        return Err(error("frame pixel format disagrees with DMA-BUF format"));
    }
    let buffer = fd.try_clone_to_owned()?;
    let ready = ready_fence.map(|fd| fd.try_clone_to_owned()).transpose()?;
    let layout = DmaBufLayout {
        width: frame.width,
        height: frame.height,
        fourcc,
        modifier,
        stride,
        offset,
    };
    // SAFETY: GpuImage's unsafe contract provides an immutable allocation with
    // truthful layout/fence. Physical identity and pixel format checked above.
    // RenderWorker serializes this queue. The retained frame delays producer reuse.
    unsafe {
        copy_frame(
            device,
            queue,
            buffer,
            ready,
            layout,
            Arc::new(frame.clone()),
        )
    }
}

/// Open the renderer's Vulkan device with external memory and fence support.
/// `None` means this adapter can only serve ordinary DreamGPU framebuffers.
pub fn create_device(
    adapter: &wgpu::Adapter,
    descriptor: &wgpu::DeviceDescriptor<'_>,
) -> Result<Option<(wgpu::Device, wgpu::Queue)>> {
    if !adapter.features().contains(INTEROP_FEATURES) {
        return Ok(None);
    }
    // SAFETY: only inspect the adapter and create a device belonging to it.
    let Some(hal) = (unsafe { adapter.as_hal::<wgpu::hal::api::Vulkan>() }) else {
        return Ok(None);
    };
    let instance = hal.shared_instance().raw_instance();
    let available =
        unsafe { instance.enumerate_device_extension_properties(hal.raw_physical_device()) }
            .map_err(error)?;
    let required = [
        khr::external_semaphore_fd::NAME,
        ash::ext::queue_family_foreign::NAME,
    ];
    for extension in required {
        if !available.iter().any(|property| {
            // Vulkan returns a NUL-terminated fixed-size extension name.
            unsafe { CStr::from_ptr(property.extension_name.as_ptr()) == extension }
        }) {
            return Ok(None);
        }
    }
    let mut desc = descriptor.clone();
    desc.required_features |= INTEROP_FEATURES;
    // SAFETY: the callback only adds extensions verified above, preserving all
    // wgpu features, limits and queue creation parameters.
    let native = unsafe {
        hal.open_with_callback(
            desc.required_features,
            &desc.required_limits,
            &desc.memory_hints,
            Some(Box::new(move |args| {
                for extension in required {
                    if !args.extensions.contains(&extension) {
                        args.extensions.push(extension);
                    }
                }
            })),
        )
    }
    .map_err(error)?;
    drop(hal);
    // SAFETY: native was created from this exact adapter and descriptor.
    unsafe { adapter.create_device_from_hal::<wgpu::hal::api::Vulkan>(native, &desc) }
        .map(Some)
        .map_err(error)
}

/// The producer's negotiated single-plane layout. Multi-plane modifiers must
/// be resolved into a supported export allocation by the producer first.
#[derive(Debug, Clone, Copy)]
pub struct DmaBufLayout {
    pub width: u32,
    pub height: u32,
    pub fourcc: u32,
    pub modifier: u64,
    pub stride: u32,
    pub offset: u64,
}

impl DmaBufLayout {
    fn format(self, maximum_dimension: u32) -> Result<wgpu::TextureFormat> {
        if self.width == 0
            || self.height == 0
            || self.width > maximum_dimension
            || self.height > maximum_dimension
            || self
                .width
                .checked_mul(4)
                .is_none_or(|row| row > self.stride)
            || self
                .offset
                .checked_add(u64::from(self.stride) * u64::from(self.height))
                .is_none()
            || self.modifier == u64::MAX
        {
            return Err(error("invalid dimensions, stride, offset or modifier"));
        }
        match self.fourcc {
            DRM_ARGB8888 => Ok(wgpu::TextureFormat::Bgra8UnormSrgb),
            DRM_ABGR8888 => Ok(wgpu::TextureFormat::Rgba8UnormSrgb),
            _ => Err(error(
                "only single-plane ARGB8888/ABGR8888 exports are supported",
            )),
        }
    }
}

struct ImportedSemaphore {
    // Keep the logical device alive until after semaphore destruction.
    _device: wgpu::Device,
    raw: ash::Device,
    semaphore: vk::Semaphore,
}

impl Drop for ImportedSemaphore {
    fn drop(&mut self) {
        // Owned by the submission completion callback after successful submit.
        unsafe { self.raw.destroy_semaphore(self.semaphore, None) };
    }
}

/// Acquire an immutable producer image and return a GPU copy ready for use on
/// this same queue. The producer owner is released only after GPU completion.
///
/// # Safety
/// - `buffer` is a single-plane DMA-BUF on this physical GPU with exactly the
///   supplied layout, fully initialized by the producer.
/// - The producer has released the image for foreign access in GENERAL layout;
///   `ready` is its SYNC_FD completion fence. Without a fence, completion must
///   already have been observed by the transport's completion worker.
/// - `producer` prevents slot writes/reallocation for its entire lifetime.
/// - `device` and `queue` belong to the same logical device. Calls submitting
///   work to `queue` are serialized with this function.
pub unsafe fn copy_frame(
    device: &wgpu::Device,
    queue: &wgpu::Queue,
    buffer: OwnedFd,
    ready: Option<OwnedFd>,
    layout: DmaBufLayout,
    producer: Arc<dyn Send + Sync>,
) -> Result<wgpu::Texture> {
    let format = layout.format(device.limits().max_texture_dimension_2d)?;
    let hal = unsafe { device.as_hal::<wgpu::hal::api::Vulkan>() }
        .ok_or_else(|| error("renderer is not using Vulkan"))?;
    {
        let native_queue = unsafe { queue.as_hal::<wgpu::hal::api::Vulkan>() }
            .ok_or_else(|| error("queue is not Vulkan"))?;
        if native_queue.raw_device().handle() != hal.raw_device().handle() {
            return Err(error("queue and texture belong to different devices"));
        }
    }
    if !device.features().contains(INTEROP_FEATURES)
        || !hal
            .enabled_device_extensions()
            .contains(&khr::external_semaphore_fd::NAME)
        || !hal
            .enabled_device_extensions()
            .contains(&ash::ext::queue_family_foreign::NAME)
    {
        return Err(error(
            "renderer device does not support external images and fences",
        ));
    }
    let size = wgpu::Extent3d {
        width: layout.width,
        height: layout.height,
        depth_or_array_layers: 1,
    };
    let descriptor = wgpu::TextureDescriptor {
        label: Some("DreamGPU imported EGL image"),
        size,
        mip_level_count: 1,
        sample_count: 1,
        dimension: wgpu::TextureDimension::D2,
        format,
        usage: wgpu::TextureUsages::COPY_SRC,
        view_formats: &[],
    };
    let native_descriptor = wgpu::hal::TextureDescriptor {
        label: descriptor.label,
        size,
        mip_level_count: 1,
        sample_count: 1,
        dimension: descriptor.dimension,
        format,
        usage: wgpu::TextureUses::COPY_SRC,
        memory_flags: wgpu::hal::MemoryFlags::empty(),
        view_formats: vec![],
    };
    let raw = hal.raw_device().clone();
    let family = hal.queue_family_index();
    let semaphore = if let Some(fd) = ready {
        let semaphore = unsafe { raw.create_semaphore(&vk::SemaphoreCreateInfo::default(), None) }
            .map_err(error)?;
        let owner = ImportedSemaphore {
            _device: device.clone(),
            raw: raw.clone(),
            semaphore,
        };
        let extension =
            khr::external_semaphore_fd::Device::new(hal.shared_instance().raw_instance(), &raw);
        use std::os::fd::AsRawFd;
        let info = vk::ImportSemaphoreFdInfoKHR::default()
            .semaphore(semaphore)
            .flags(vk::SemaphoreImportFlags::TEMPORARY)
            .handle_type(vk::ExternalSemaphoreHandleTypeFlags::SYNC_FD)
            .fd(fd.as_raw_fd());
        unsafe { extension.import_semaphore_fd(&info) }.map_err(error)?;
        // Vulkan owns the FD only after successful import.
        let _ = fd.into_raw_fd();
        Some(owner)
    } else {
        None
    };
    let imported = unsafe {
        hal.texture_from_dmabuf_fd(
            buffer,
            &native_descriptor,
            layout.modifier,
            u64::from(layout.stride),
            layout.offset,
        )
    }
    .map_err(error)?;
    let raw_image = unsafe { imported.raw_handle() };
    drop(hal);
    // The raw acquire barrier below establishes this initial COPY_SRC state.
    // The texture is never reused after the release barrier.
    let imported = unsafe {
        device.create_texture_from_hal::<wgpu::hal::api::Vulkan>(
            imported,
            &descriptor,
            wgpu::TextureUses::COPY_SRC,
        )
    };
    let destination = device.create_texture(&wgpu::TextureDescriptor {
        label: Some("DreamGPU GPU desktop source"),
        usage: wgpu::TextureUsages::COPY_DST
            | wgpu::TextureUsages::COPY_SRC
            | wgpu::TextureUsages::TEXTURE_BINDING,
        ..descriptor
    });
    // wgpu deliberately forbids mixing native and portable commands in one
    // encoder. Keep the foreign ownership barriers in separate command buffers
    // and submit the acquire/copy/release buffers as one ordered submission.
    let mut acquire = device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
        label: Some("DreamGPU foreign GPU image acquire"),
    });
    unsafe { ownership_barrier(&mut acquire, &raw, raw_image, family, true) };
    let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
        label: Some("DreamGPU GPU image copy"),
    });
    encoder.copy_texture_to_texture(imported.as_image_copy(), destination.as_image_copy(), size);
    let mut release = device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
        label: Some("DreamGPU foreign GPU image release"),
    });
    unsafe { ownership_barrier(&mut release, &raw, raw_image, family, false) };
    let commands = [acquire.finish(), encoder.finish(), release.finish()];
    if let Some(ref fence) = semaphore {
        // Queue submissions are serialized by the render worker. No unrelated
        // submission may consume this semaphore between staging and submit.
        let native_queue = unsafe { queue.as_hal::<wgpu::hal::api::Vulkan>() }
            .ok_or_else(|| error("queue is not Vulkan"))?;
        native_queue.add_wait_semaphore(fence.semaphore, None, vk::PipelineStageFlags::TOP_OF_PIPE);
    }
    queue.submit(commands);
    queue.on_submitted_work_done(move || {
        drop(imported);
        drop(semaphore);
        drop(producer);
    });
    Ok(destination)
}

unsafe fn ownership_barrier(
    encoder: &mut wgpu::CommandEncoder,
    device: &ash::Device,
    image: vk::Image,
    family: u32,
    acquire: bool,
) {
    let (old, new, source, destination, source_access, destination_access, before, after) =
        if acquire {
            (
                vk::ImageLayout::GENERAL,
                vk::ImageLayout::TRANSFER_SRC_OPTIMAL,
                vk::QUEUE_FAMILY_FOREIGN_EXT,
                family,
                vk::AccessFlags::empty(),
                vk::AccessFlags::TRANSFER_READ,
                vk::PipelineStageFlags::TOP_OF_PIPE,
                vk::PipelineStageFlags::TRANSFER,
            )
        } else {
            (
                vk::ImageLayout::TRANSFER_SRC_OPTIMAL,
                vk::ImageLayout::GENERAL,
                family,
                vk::QUEUE_FAMILY_FOREIGN_EXT,
                vk::AccessFlags::TRANSFER_READ,
                vk::AccessFlags::empty(),
                vk::PipelineStageFlags::TRANSFER,
                vk::PipelineStageFlags::BOTTOM_OF_PIPE,
            )
        };
    let barrier = vk::ImageMemoryBarrier::default()
        .old_layout(old)
        .new_layout(new)
        .src_queue_family_index(source)
        .dst_queue_family_index(destination)
        .src_access_mask(source_access)
        .dst_access_mask(destination_access)
        .image(image)
        .subresource_range(vk::ImageSubresourceRange {
            aspect_mask: vk::ImageAspectFlags::COLOR,
            base_mip_level: 0,
            level_count: 1,
            base_array_layer: 0,
            layer_count: 1,
        });
    unsafe {
        encoder.as_hal_mut::<wgpu::hal::api::Vulkan, _, _>(|native| {
            let native = native.expect("device and encoder belong to the Vulkan backend");
            device.cmd_pipeline_barrier(
                native.raw_handle(),
                before,
                after,
                vk::DependencyFlags::empty(),
                &[],
                &[],
                &[barrier],
            );
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn invalid_external_layouts_are_rejected_before_native_import() {
        let valid = DmaBufLayout {
            width: 640,
            height: 480,
            fourcc: DRM_ARGB8888,
            modifier: 0,
            stride: 2560,
            offset: 0,
        };
        assert_eq!(
            valid.format(8192).unwrap(),
            wgpu::TextureFormat::Bgra8UnormSrgb
        );
        for bad in [
            DmaBufLayout { width: 0, ..valid },
            DmaBufLayout {
                height: 8193,
                ..valid
            },
            DmaBufLayout {
                stride: 2559,
                ..valid
            },
            DmaBufLayout {
                offset: u64::MAX,
                ..valid
            },
            DmaBufLayout {
                modifier: u64::MAX,
                ..valid
            },
            DmaBufLayout {
                fourcc: u32::from_le_bytes(*b"NV12"),
                ..valid
            },
        ] {
            assert!(bad.format(8192).is_err());
        }
    }
}
