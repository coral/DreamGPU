/* SPDX-License-Identifier: GPL-2.0-or-later
 * Swapchain buffers are drawable render targets even when DirectDraw creates
 * the front buffer with usage=0. This changes blitter selection only; resource
 * usage, location validity, CPU maps and the non-D3D fallback stay unchanged.
 */
static DWORD dg_surface_blit_usage(const struct wined3d_surface *surface) {
    const struct wined3d_swapchain *swapchain = surface->container->swapchain;
    DWORD usage = surface->resource.usage;
    unsigned int i;
    if (!swapchain)
        return usage;
    if (surface->container == swapchain->front_buffer)
        return usage | WINED3DUSAGE_RENDERTARGET;
    if (swapchain->back_buffers)
        for (i = 0; i < swapchain->desc.backbuffer_count; ++i)
            if (surface->container == swapchain->back_buffers[i])
                return usage | WINED3DUSAGE_RENDERTARGET;
    return usage;
}

static BOOL dg_surface_needs_cpu_upload(const struct wined3d_surface *source,
                                        const struct wined3d_surface *destination) {
    /* A READONLY map can add a CPU copy without invalidating GPU contents.
     * Prefer that already valid GPU copy. Writable maps/GetDC invalidate the
     * GPU locations and continue through the genuine CPU upload path. */
    return source && destination && (source->locations & WINED3D_LOCATION_SYSMEM) &&
           !(source->locations & (WINED3D_LOCATION_TEXTURE_RGB | WINED3D_LOCATION_DRAWABLE)) &&
           !(destination->locations & WINED3D_LOCATION_SYSMEM);
}
