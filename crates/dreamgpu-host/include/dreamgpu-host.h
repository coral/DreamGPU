/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef DREAMGPU_HOST_H
#define DREAMGPU_HOST_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
typedef struct DreamGpuProgress {
    uint32_t command, row, column;
} DreamGpuProgress;
typedef struct DreamGpuWork {
    uint64_t bytes;
    uint32_t chunks, complete;
} DreamGpuWork;
typedef void (*DreamGpuTransfer)(void *opaque, uint32_t op, uint32_t bpp, uint64_t src,
                                 uint64_t dst, uint32_t bytes, uint32_t color);
struct DreamGpuGlApi;
typedef struct DreamGpuTexture {
    uint32_t name, target;
    uint64_t version;
    uint32_t guest_name, refs, deleted, undefined_levels;
    void *last_write;
    uint64_t writer_serial, waiter_serial;
    uint32_t widths[12], heights[12];
    uint64_t levels[12];
} DreamGpuTexture;
typedef struct DreamGpuTextureEntry {
    uint32_t name;
    DreamGpuTexture *texture;
} DreamGpuTextureEntry;
typedef struct DreamGpuTextureNamespace {
    uint32_t refs;
    DreamGpuTextureEntry entries[8192];
} DreamGpuTextureNamespace;
typedef struct DreamGpuTextureMemory {
    const struct DreamGpuGlApi *api;
    uint64_t *bytes;
    uint64_t *image_bytes;
    uint32_t *count;
    void *opaque;
    void *(*allocate)(void *, size_t);
    void (*free)(void *, void *);
    void (*forget_read)(void *, DreamGpuTexture *);
} DreamGpuTextureMemory;
typedef struct DreamGpuAttrib {
    uint32_t mask, color_sum, draw_buffer, read_buffer;
    DreamGpuTexture *texture, *texture_1d;
} DreamGpuAttrib;
typedef struct DreamGpuPixelImage {
    uint8_t *pixels;
    uint32_t active, function, descriptor[4], bitmap[4], total, received, id, last_id;
} DreamGpuPixelImage;
uint32_t dreamgpu_pixel_image_interleave(const DreamGpuTextureMemory *, DreamGpuPixelImage *);
void dreamgpu_pixel_image_release(const DreamGpuTextureMemory *, DreamGpuPixelImage *);
typedef struct DreamGpuCapture {
    uint32_t *selection, *feedback;
    uint32_t select_size, feedback_size, mode, completed_mode, completed_count;
} DreamGpuCapture;
typedef struct DreamGpuListState DreamGpuListState;
typedef struct DreamGpuContextState {
    DreamGpuTextureNamespace *textures;
    DreamGpuTexture *default_texture, *bound_texture;
    DreamGpuTexture *default_texture_1d, *bound_texture_1d;
    uint32_t guest_errors, draw_buffer, read_buffer, attrib_depth;
    DreamGpuAttrib attrib[16];
    DreamGpuPixelImage image;
    DreamGpuCapture capture;
    DreamGpuListState *lists;
    uint32_t list_mode;
} DreamGpuContextState;
uint32_t dreamgpu_context_state_init(const DreamGpuTextureMemory *memory,
                                     DreamGpuContextState *state, DreamGpuTextureNamespace *shared);
void dreamgpu_context_state_release(const DreamGpuTextureMemory *memory,
                                    DreamGpuContextState *state);
uint32_t dreamgpu_context_select_buffers(const struct DreamGpuGlApi *api,
                                         const DreamGpuContextState *state);
typedef uint32_t (*DreamGpuCopyTexture)(void *, uint32_t, const uint8_t *);
uint32_t dreamgpu_context_resource(const DreamGpuTextureMemory *memory, DreamGpuContextState *state,
                                   uint64_t serial, uint32_t function, const uint8_t *args,
                                   uint32_t bytes, DreamGpuCopyTexture copy, void *opaque);
void dreamgpu_texture_unref(const DreamGpuTextureMemory *memory, DreamGpuTexture *texture);
void dreamgpu_texture_namespace_unref(const DreamGpuTextureMemory *memory,
                                      DreamGpuTextureNamespace *ns);
uint32_t dreamgpu_texture_bind(const DreamGpuTextureMemory *memory, DreamGpuTextureNamespace *ns,
                               uint32_t target, uint32_t name, DreamGpuTexture *default_texture,
                               DreamGpuTexture **binding, uint64_t serial, uint32_t *errors);
void dreamgpu_texture_delete(const DreamGpuTextureMemory *memory, DreamGpuTextureNamespace *ns,
                             const uint8_t *data, uint32_t count, DreamGpuTexture *default_2d,
                             DreamGpuTexture *default_1d, DreamGpuTexture **binding_2d,
                             DreamGpuTexture **binding_1d);
DreamGpuTexture *dreamgpu_texture_lookup(DreamGpuTextureNamespace *ns, uint32_t name);
void dreamgpu_texture_wait(const struct DreamGpuGlApi *api, DreamGpuTexture *texture,
                           uint64_t serial);
void dreamgpu_texture_written(const struct DreamGpuGlApi *api, DreamGpuTexture *texture,
                              uint64_t serial);
typedef uint32_t (*DreamGpuZeroTexture)(void *, DreamGpuTexture *, uint32_t, uint32_t, uint32_t);
uint32_t dreamgpu_texture_upload(const struct DreamGpuGlApi *api, DreamGpuTexture *texture,
                                 uint64_t *total, uint64_t serial, uint32_t function,
                                 const uint32_t *args, const uint8_t *data, uint32_t bytes,
                                 DreamGpuZeroTexture zero, void *opaque, uint32_t *gl_error);
uint32_t dreamgpu_gl_function_words(uint32_t function);
uint32_t dreamgpu_gl_call_validate(uint32_t function, const uint8_t *args);
uint32_t dreamgpu_gl_data_validate(uint32_t function, const uint8_t *args, const uint8_t *data,
                                   uint32_t bytes);
uint32_t dreamgpu_gl_query_validate(uint32_t function, const uint8_t *args);
uint32_t dreamgpu_gl_query_result_bytes(uint32_t function, const uint8_t *args);
uint32_t dreamgpu_gl_query_shape(uint32_t function, const uint8_t *args, uint32_t *type);
const char *dreamgpu_gl_query_string(uint32_t name);
uint32_t dreamgpu_gl_vector_function(uint32_t function);
uint32_t dreamgpu_gl_vector_bytes(uint32_t function, const uint32_t *args);
uint32_t dreamgpu_gl_index_bytes(uint32_t type);
uint32_t dreamgpu_gl_texture_params(uint32_t target, uint32_t pname);
uint32_t dreamgpu_gl_texture_env_params(uint32_t target, uint32_t pname);
uint32_t dreamgpu_gl_buffer_selection(uint32_t mode, uint32_t draw);
typedef uint32_t (*DreamGpuResourceHook)(void *, uint32_t, const uint8_t *);
typedef struct DreamGpuQueryState {
    uint32_t in_begin, has_drawable, width, height;
    uint32_t draw_buffer, read_buffer, binding_1d, binding_2d, attrib_depth;
    DreamGpuTextureNamespace *textures;
    DreamGpuCapture *capture;
    const DreamGpuTextureMemory *memory;
    DreamGpuContextState *context;
} DreamGpuQueryState;
typedef uint32_t (*DreamGpuListExecute)(void *, uint32_t, uint32_t, const uint8_t *,
                                        const uint8_t *, uint32_t);
uint32_t dreamgpu_list_dispatch(const DreamGpuTextureMemory *, DreamGpuContextState *, uint32_t *,
                                DreamGpuListExecute, void *, uint32_t, uint32_t, const uint8_t *,
                                uint32_t, const uint8_t *, uint32_t);
typedef uint32_t (*DreamGpuTextureRead)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint8_t *);
uint32_t dreamgpu_gl_query(const struct DreamGpuGlApi *api, const DreamGpuQueryState *state,
                           uint32_t *errors, uint32_t function, const uint8_t *args,
                           uint8_t *result, uint32_t capacity, DreamGpuTextureRead texture_read,
                           void *opaque, uint32_t *bytes, uint32_t *type);
uint32_t dreamgpu_gl_vector(const struct DreamGpuGlApi *api, uint32_t function,
                            const uint32_t *args, const uint8_t *data, uint32_t bytes);
uint32_t dreamgpu_gl_scalar(const struct DreamGpuGlApi *api, uint32_t *in_begin,
                            DreamGpuResourceHook resource, void *opaque, uint32_t function,
                            const uint8_t *args, size_t bytes);
uint32_t dreamgpu_gl_arrays(const struct DreamGpuGlApi *api, uint32_t function,
                            const uint32_t *args, const uint8_t *data, uint32_t bytes,
                            uint32_t *gl_error);
typedef struct DreamGpuNativeDrawable {
    uint32_t width, height, color, front, depth;
    void *last_write;
} DreamGpuNativeDrawable;
typedef struct DreamGpuReadCache {
    DreamGpuTexture *texture;
    uint64_t version;
    uint32_t level, bytes;
    uint8_t *pixels;
} DreamGpuReadCache;
uint32_t dreamgpu_drawable_init(const struct DreamGpuGlApi *, DreamGpuNativeDrawable *, uint32_t,
                                uint32_t);
void dreamgpu_drawable_flush(const struct DreamGpuGlApi *, DreamGpuNativeDrawable *);
void dreamgpu_drawable_release(const struct DreamGpuGlApi *, DreamGpuNativeDrawable *);
uint32_t dreamgpu_drawable_bind(const struct DreamGpuGlApi *, DreamGpuNativeDrawable *, uint32_t *,
                                uint32_t *, const DreamGpuContextState *);
void dreamgpu_drawable_exchange(const struct DreamGpuGlApi *, DreamGpuNativeDrawable *, uint32_t,
                                const DreamGpuContextState *);
uint32_t dreamgpu_texture_copy(const struct DreamGpuGlApi *, DreamGpuTexture *, uint64_t *,
                               uint32_t *, uint64_t, uint32_t, uint32_t, const uint8_t *);
uint32_t dreamgpu_texture_zero(const DreamGpuTextureMemory *, const DreamGpuTexture *, uint32_t,
                               uint32_t, uint32_t);
void dreamgpu_read_cache_release(const DreamGpuTextureMemory *, DreamGpuReadCache *);
void dreamgpu_read_cache_forget(const DreamGpuTextureMemory *, DreamGpuReadCache *,
                                const DreamGpuTexture *);
uint32_t dreamgpu_texture_read(const DreamGpuTextureMemory *, DreamGpuReadCache *,
                               DreamGpuTexture *, uint32_t *, uint64_t, uint32_t, uint32_t,
                               uint32_t, uint32_t, uint8_t *);
typedef uint32_t (*DreamGpuExportStep)(void *);
typedef int64_t (*DreamGpuClock)(void *);
typedef void (*DreamGpuSleep)(void *, uint64_t);
uint32_t dreamgpu_export(const struct DreamGpuGlApi *, DreamGpuNativeDrawable *,
                         const DreamGpuContextState *, uint32_t, uint32_t, uint32_t,
                         DreamGpuExportStep, DreamGpuExportStep, void *);
uint32_t dreamgpu_export_wait(const struct DreamGpuGlApi *, void *, DreamGpuClock, DreamGpuSleep,
                              void *);
uint32_t dreamgpu_gl_data(const DreamGpuTextureMemory *, DreamGpuContextState *, uint32_t, uint64_t,
                          uint32_t, const uint8_t *, const uint8_t *, uint32_t);
typedef struct DreamGpuNativeImage {
    uint32_t width, height, stride, offset;
    uint64_t modifier;
    void *surface, *native_image, *bo, *context, *fence;
    int32_t fd, fence_fd;
} DreamGpuNativeImage;
typedef uint32_t (*DreamGpuImageCreate)(void *, DreamGpuNativeImage *);
typedef void (*DreamGpuImageDestroy)(void *, DreamGpuNativeImage *);
DreamGpuNativeImage *dreamgpu_image_new(const DreamGpuTextureMemory *, uint32_t, uint32_t,
                                        DreamGpuImageCreate, DreamGpuImageDestroy, void *,
                                        uint32_t *);
void dreamgpu_image_free(const DreamGpuTextureMemory *, DreamGpuNativeImage *, DreamGpuImageDestroy,
                         void *);
typedef void (*DreamGpuBatchReject)(void *, const uint8_t *, uint32_t, uint32_t);
uint32_t dreamgpu_batch_validate(const uint8_t *, size_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                 uint32_t *, DreamGpuBatchReject, void *);
typedef struct DreamGpuContext {
    uint32_t client, id, drawable;
    void *native;
} DreamGpuContext;
typedef struct DreamGpuDrawable {
    uint32_t client, id, width, height;
    uint64_t epoch, generation, published_epoch, published_generation;
    void *native;
} DreamGpuDrawable;
typedef struct DreamGpuExportSlot {
    void *image;
    uint32_t state, sending, release_pending;
    uint64_t generation;
    uint8_t packet[128];
} DreamGpuExportSlot;
typedef struct DreamGpuImageMetadata {
    uint32_t stride, offset;
    uint64_t modifier;
    uint32_t ready_fence;
    uint8_t uuid[16];
} DreamGpuImageMetadata;
uint32_t dreamgpu_slot_claim(DreamGpuExportSlot *, uint32_t);
void dreamgpu_slot_release(DreamGpuExportSlot *, uint64_t, uint64_t);
uint32_t dreamgpu_slot_publish(DreamGpuExportSlot *, uint32_t, uint32_t);
uint32_t dreamgpu_slot_sent(DreamGpuExportSlot *, DreamGpuDrawable *, uint32_t, uint32_t, uint32_t);
void dreamgpu_slot_abort(DreamGpuExportSlot *);
uint32_t dreamgpu_slot_prepare(DreamGpuExportSlot *, DreamGpuDrawable *, uint32_t, uint32_t,
                               const DreamGpuImageMetadata *);
void dreamgpu_slot_pending(DreamGpuExportSlot *);
typedef struct DreamGpuDesktopState {
    uint64_t epoch, sequence;
    uint32_t width, height, active, coherent, exclusive;
} DreamGpuDesktopState;
typedef struct DreamGpuDesktopOps {
    void *opaque;
    uint32_t (*capture)(void *, const uint8_t *, uint32_t);
    uint32_t (*readback)(void *, const uint8_t *, uint8_t *);
    uint32_t (*queue)(void *, const uint8_t *);
    uint32_t (*retained)(void *, const uint8_t *);
    void (*failed)(void *);
} DreamGpuDesktopOps;
uint32_t dreamgpu_desktop_execute(DreamGpuDesktopState *, const DreamGpuDesktopOps *,
                                  const uint8_t *, uint32_t, uint32_t);
uint32_t dreamgpu_desktop_retained(const DreamGpuExportSlot *, const uint8_t *);
typedef struct DreamGpuResources {
    DreamGpuContext contexts[32];
    DreamGpuDrawable drawables[32];
    uint64_t next_epoch;
} DreamGpuResources;
typedef struct DreamGpuPlatform {
    void *opaque;
    void *(*context_new)(void *, void *);
    void *(*drawable_new)(void *, uint32_t, uint32_t);
    void (*context_free)(void *, uint32_t);
    void (*drawable_free)(void *, uint32_t);
    void (*close_begin)(void *);
    uint32_t (*in_begin)(void *, uint32_t);
    uint32_t (*make_current)(void *, uint32_t, uint32_t);
    uint32_t (*call)(void *, uint32_t, uint32_t, const uint8_t *);
    uint32_t (*data)(void *, uint32_t, uint32_t, const uint8_t *, const uint8_t *, uint32_t);
    uint32_t (*words)(void *, uint32_t);
    uint32_t (*query)(void *, uint32_t, uint32_t, const uint8_t *);
    uint32_t (*present)(void *, uint32_t, uint32_t, uint32_t);
    uint32_t (*desktop)(void *, const uint8_t *);
} DreamGpuPlatform;
uint32_t dreamgpu_gl_execute(DreamGpuResources *resources, const DreamGpuPlatform *platform,
                             const uint8_t *record, size_t bytes, uint32_t width, uint32_t height);
void dreamgpu_gl_close_client(DreamGpuResources *resources, const DreamGpuPlatform *platform,
                              uint32_t client);
uint32_t dreamgpu_cursor_validate(const uint8_t *data, uint32_t pixels, uint32_t format);
uint32_t dreamgpu_desktop_validate(const uint8_t *record, uint32_t width, uint32_t height,
                                   uint32_t vram_bytes, uint64_t *work);
uint32_t dreamgpu_desktop_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                               uint32_t limit_w, uint32_t limit_h);
uint32_t dreamgpu_2d_validate(const uint8_t *commands, uint32_t count, uint64_t vram_bytes,
                              uint64_t *work);
uint32_t dreamgpu_2d_execute(const uint8_t *commands, uint32_t count, uint64_t vram_bytes,
                             DreamGpuProgress *progress, uint32_t budget, DreamGpuTransfer transfer,
                             void *opaque, DreamGpuWork *result);

/* Device snapshot callbacks operate on private host allocations, never guest references. */
typedef struct DreamGpuDeviceMemory {
    void *opaque;
    uint8_t *(*allocate)(void *, uint32_t);
    void (*free)(void *, uint8_t *);
    uint32_t (*read)(void *, uint64_t, uint8_t *, uint32_t);
    uint32_t (*ram)(void *, uint64_t, uint32_t, uint32_t);
} DreamGpuDeviceMemory;
typedef struct DreamGpuCursorRequest {
    uint64_t address;
    uint32_t operation, flags, bytes, width, height, hot_x, hot_y, format;
} DreamGpuCursorRequest;
typedef struct DreamGpuDeviceRestore {
    DreamGpuCursorRequest cursor;
    uint32_t cursor_status, gl_status, status, count, command, row, column;
    uint64_t vram;
} DreamGpuDeviceRestore;
typedef struct DreamGpuDeviceGlRequest {
    uint64_t address, result_address;
    uint32_t bytes, generation, current_generation, bpp;
    uint32_t busy_2d, result_capacity;
} DreamGpuDeviceGlRequest;
typedef struct DreamGpuDeviceGlResult {
    uint32_t sensitive, records, result_capacity;
} DreamGpuDeviceGlResult;
typedef struct DreamGpuDeviceGlCallbacks {
    DreamGpuDeviceMemory memory;
    uint32_t (*validate)(void *, const uint8_t *, uint32_t, uint32_t *);
    void (*diagnostics_begin)(void *, uint32_t);
    void (*diagnostic_record)(void *, const uint8_t *);
    uint32_t (*enqueue)(void *, uint8_t *, uint32_t, uint32_t);
} DreamGpuDeviceGlCallbacks;
uint32_t dreamgpu_device_2d_capture(const DreamGpuDeviceMemory *, uint64_t, uint32_t, uint8_t *,
                                    uint64_t, uint64_t *);
uint32_t dreamgpu_device_cursor(const DreamGpuDeviceMemory *, const DreamGpuCursorRequest *,
                                uint8_t *);
uint32_t dreamgpu_device_restore(const DreamGpuDeviceRestore *, const uint8_t *, const uint8_t *,
                                 uint64_t *);
uint32_t dreamgpu_device_gl_submit(const DreamGpuDeviceGlCallbacks *,
                                   const DreamGpuDeviceGlRequest *, DreamGpuDeviceGlResult *);
/* Worker-owned bounded output queue, CPU mappings, and reply descriptors.
 * All queue/lease/reply transitions require the engine mutex. */
typedef struct DreamGpuOutput {
    DreamGpuExportSlot *slot;
    uint8_t packet[128];
} DreamGpuOutput;
typedef struct DreamGpuOutputQueue {
    uint32_t head, len, pending, desktop;
    DreamGpuOutput items[160];
} DreamGpuOutputQueue;
typedef struct DreamGpuCpuSlot {
    uint8_t *pixels;
    size_t bytes;
    int32_t fd;
    uint32_t width, height, stride, published;
    uint64_t epoch, sequence;
} DreamGpuCpuSlot;
typedef struct DreamGpuCpuMemory {
    void *opaque;
    uint8_t *(*allocate)(void *, size_t, int32_t *);
    void (*free)(void *, uint8_t *, size_t, int32_t);
} DreamGpuCpuMemory;
typedef struct DreamGpuReply {
    uint32_t pending, ready;
    uint64_t epoch, sequence, token;
    uint8_t packet[128];
    int32_t fd;
} DreamGpuReply;
typedef struct DreamGpuReplyResult {
    int32_t fd;
    uint32_t error;
    uint8_t packet[128];
} DreamGpuReplyResult;
uint32_t dreamgpu_output_push(DreamGpuOutputQueue *, DreamGpuExportSlot *, const uint8_t *);
uint32_t dreamgpu_output_pop(DreamGpuOutputQueue *, DreamGpuOutput *);
void dreamgpu_output_done(DreamGpuOutputQueue *, uint32_t);
uint32_t dreamgpu_cpu_claim(DreamGpuCpuSlot *, uint64_t, uint64_t);
void dreamgpu_cpu_release(DreamGpuCpuSlot *, uint64_t, uint64_t);
uint32_t dreamgpu_cpu_storage(DreamGpuCpuSlot *, const DreamGpuCpuMemory *, uint32_t, uint32_t);
void dreamgpu_cpu_storage_free(DreamGpuCpuSlot *, const DreamGpuCpuMemory *);
void dreamgpu_cpu_packet(const DreamGpuCpuSlot *, uint32_t, uint32_t, const uint8_t *, uint64_t,
                         uint64_t, uint8_t *);
uint32_t dreamgpu_reply_receive(DreamGpuReply *, const uint8_t *, int32_t);
void dreamgpu_reply_begin(DreamGpuReply *, uint64_t, uint64_t, uint8_t *);
void dreamgpu_reply_finish(DreamGpuReply *, uint32_t, uint32_t, uint32_t, DreamGpuReplyResult *);
uint32_t dreamgpu_reply_layout(const DreamGpuReplyResult *, uint32_t, uint32_t, uint32_t, int64_t,
                               uint64_t *, uint32_t *);
/* Immutable command snapshots and bounded completion ownership. */
typedef struct DreamGpuSubmissionMemory {
    void *opaque;
    void *(*allocate)(void *, size_t);
    void (*free)(void *, void *);
} DreamGpuSubmissionMemory;
typedef struct DreamGpuFrame {
    uint32_t slot, client, drawable;
    uint64_t epoch, generation;
} DreamGpuFrame;
typedef struct DreamGpuCompletion {
    uint32_t sequence, generation, error;
    bool resources_live, reset;
    DreamGpuFrame present;
    uint32_t result_bytes, result_type;
    uint8_t result[512];
    uint8_t *bulk_result;
} DreamGpuCompletion;
typedef struct DreamGpuBatch {
    uint8_t *data;
    size_t bytes;
    uint32_t sequence, generation, records;
    uint64_t trace_queued_us;
    uint32_t primary_width, primary_height;
    uint32_t result_bytes, result_type;
    uint8_t result[512];
    uint8_t *bulk_result;
} DreamGpuBatch;
typedef struct DreamGpuSubmission {
    DreamGpuBatch *pending;
    uint32_t reset, reset_generation;
    uint64_t reset_cpu_epoch, reset_cpu_generation;
    uint32_t head, len;
    DreamGpuCompletion done[16];
} DreamGpuSubmission;
typedef struct DreamGpuSubmissionRequest {
    uint8_t *data;
    size_t bytes;
    uint32_t sequence, generation, records;
    uint64_t trace_queued_us;
    uint32_t primary_width, primary_height;
} DreamGpuSubmissionRequest;
typedef struct DreamGpuSubmissionRun {
    void *opaque;
    uint32_t (*cancelled)(void *);
    void (*report)(void *, const uint8_t *, uint32_t);
} DreamGpuSubmissionRun;
uint32_t dreamgpu_submission_submit(DreamGpuSubmission *, const DreamGpuSubmissionMemory *,
                                    const DreamGpuSubmissionRequest *, uint32_t);
DreamGpuBatch *dreamgpu_submission_take(DreamGpuSubmission *);
void dreamgpu_submission_reset(DreamGpuSubmission *, const DreamGpuSubmissionMemory *, uint32_t,
                               uint64_t, uint64_t);
typedef struct DreamGpuResetTicket {
    uint32_t generation;
    uint64_t epoch, frame;
} DreamGpuResetTicket;
uint32_t dreamgpu_submission_reset_snapshot(const DreamGpuSubmission *, DreamGpuResetTicket *);
uint32_t dreamgpu_submission_reset_done(DreamGpuSubmission *, const DreamGpuSubmissionMemory *,
                                        const DreamGpuResetTicket *, DreamGpuDesktopState *,
                                        DreamGpuFrame *);
uint8_t *dreamgpu_submission_query(DreamGpuBatch *, const DreamGpuSubmissionMemory *, uint32_t);
uint32_t dreamgpu_submission_run(DreamGpuBatch *, DreamGpuResources *, const DreamGpuPlatform *,
                                 const DreamGpuSubmissionRun *, uint32_t);
void dreamgpu_submission_complete(DreamGpuSubmission *, const DreamGpuSubmissionMemory *,
                                  DreamGpuBatch *, uint32_t, const DreamGpuResources *,
                                  const DreamGpuDesktopState *, const DreamGpuFrame *);
uint32_t dreamgpu_submission_poll(DreamGpuSubmission *, DreamGpuCompletion *);
void dreamgpu_submission_free(DreamGpuSubmission *, const DreamGpuSubmissionMemory *);
#endif
