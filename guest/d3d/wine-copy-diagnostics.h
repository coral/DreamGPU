/* SPDX-License-Identifier: GPL-2.0-or-later
 * Diagnostic Wine build only. Wine's existing device serialization protects
 * these counters. No per-call I/O; fixed measurement events delimit counts.
 * Boundaries include readback entries because front-buffer GL flushes can
 * publish without calling Wine swapchain Present. They are not frame counts.
 */
#define DG_COPY_KEYS 64
struct dg_copy_key {
    unsigned int kind, source, line, root_source, root_line;
    unsigned int width, height, format, usage, pool, locations, wanted, role;
};
struct dg_copy_count {
    struct dg_copy_key key;
    unsigned long count;
};
static struct dg_copy_count dg_copy_counts[DG_COPY_KEYS];
static unsigned int dg_copy_used, dg_copy_overflow, dg_copy_source, dg_copy_line;
static unsigned int dg_copy_root_source, dg_copy_root_line, dg_copy_depth;
static unsigned int dg_copy_state, dg_copy_presents;
static HANDLE dg_copy_start, dg_copy_end;
static DWORD dg_copy_started;
static void dg_copy_record(unsigned int kind, const struct wined3d_surface *surface, DWORD wanted) {
    struct dg_copy_key key;
    struct wined3d_swapchain *swapchain;
    unsigned int i;
    if (dg_copy_state != 1 || !surface)
        return;
    memset(&key, 0, sizeof(key));
    key.kind = kind;
    key.source = dg_copy_source;
    key.line = dg_copy_line;
    key.root_source = dg_copy_depth ? dg_copy_root_source : 0;
    key.root_line = dg_copy_depth ? dg_copy_root_line : 0;
    key.width = surface->resource.width;
    key.height = surface->resource.height;
    key.format = surface->resource.format->id;
    key.usage = surface->resource.usage;
    key.pool = surface->resource.pool;
    key.locations = surface->locations;
    key.wanted = wanted;
    swapchain = surface->container->swapchain;
    key.role = !swapchain                                                                    ? 0
               : surface->container == swapchain->front_buffer                               ? 1
               : swapchain->back_buffers && surface->container == swapchain->back_buffers[0] ? 2
                                                                                             : 3;
    for (i = 0; i < dg_copy_used; ++i)
        if (!memcmp(&key, &dg_copy_counts[i].key, sizeof(key))) {
            ++dg_copy_counts[i].count;
            return;
        }
    if (dg_copy_used == DG_COPY_KEYS) {
        ++dg_copy_overflow;
        return;
    }
    dg_copy_counts[dg_copy_used].key = key;
    dg_copy_counts[dg_copy_used++].count = 1;
}
static void dg_copy_dump(BOOL final) {
    HANDLE file;
    DWORD written;
    unsigned int i;
    char line[512];
    int length;
    DWORD saved = GetLastError();
    file = CreateFileA("C:\\DGWCOPY.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0,
                       NULL);
    if (file != INVALID_HANDLE_VALUE) {
        length = wsprintfA(
            line,
            "WINE_COPY schema=1 final=%u elapsed_ms=%lu boundaries=%u tuples=%u overflow=%u\r\n",
            final, GetTickCount() - dg_copy_started, dg_copy_presents, dg_copy_used,
            dg_copy_overflow);
        WriteFile(file, line, length, &written, NULL);
        for (i = 0; i < dg_copy_used; ++i) {
            const struct dg_copy_count *entry = &dg_copy_counts[i];
            const struct dg_copy_key *k = &entry->key;
            length = wsprintfA(
                line,
                "COPY kind=%u source=%u line=%u root_source=%u root_line=%u width=%u height=%u "
                "format=%u usage=%08x pool=%u locations=%08x wanted=%08x role=%u count=%lu\r\n",
                k->kind, k->source, k->line, k->root_source, k->root_line, k->width, k->height,
                k->format, k->usage, k->pool, k->locations, k->wanted, k->role, entry->count);
            WriteFile(file, line, length, &written, NULL);
        }
        CloseHandle(file);
    }
    SetLastError(saved);
}
void dg_copy_present(void) {
    if (dg_copy_state == 2)
        return;
    if (!dg_copy_start) {
        dg_copy_start = OpenEventA(SYNCHRONIZE, FALSE, "DreamGPUGameMeasuring");
        dg_copy_end = OpenEventA(SYNCHRONIZE, FALSE, "DreamGPUGameMeasured");
        if (!dg_copy_start || !dg_copy_end) {
            if (dg_copy_start)
                CloseHandle(dg_copy_start);
            if (dg_copy_end)
                CloseHandle(dg_copy_end);
            dg_copy_start = dg_copy_end = NULL;
            dg_copy_state = 2;
            return;
        }
    }
    if (!dg_copy_state && WaitForSingleObject(dg_copy_start, 0) == WAIT_OBJECT_0) {
        dg_copy_state = 1;
        dg_copy_started = GetTickCount();
    }
    if (dg_copy_state != 1)
        return;
    if (WaitForSingleObject(dg_copy_end, 0) == WAIT_OBJECT_0) {
        dg_copy_dump(TRUE);
        dg_copy_state = 2;
        CloseHandle(dg_copy_start);
        CloseHandle(dg_copy_end);
        dg_copy_start = dg_copy_end = NULL;
        return;
    }
    ++dg_copy_presents;
    if (!(dg_copy_presents % 128))
        dg_copy_dump(FALSE);
}
HRESULT dg_surface_load_location(unsigned int source, unsigned int line,
                                 struct wined3d_surface *surface, struct wined3d_context *context,
                                 DWORD location) {
    HRESULT result;
    unsigned int old_source = dg_copy_source, old_line = dg_copy_line;
    if (!dg_copy_depth) {
        dg_copy_root_source = source;
        dg_copy_root_line = line;
    }
    ++dg_copy_depth;
    dg_copy_source = source;
    dg_copy_line = line;
    if (!(surface->locations & location))
        dg_copy_record(1, surface, location);
    result = surface_load_location(surface, context, location);
    --dg_copy_depth;
    dg_copy_source = old_source;
    dg_copy_line = old_line;
    return result;
}
