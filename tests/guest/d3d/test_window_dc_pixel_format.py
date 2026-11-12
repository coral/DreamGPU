#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the prepared WineD3D pixel-format checks on a window DC (0063).

WineD3D asked wglGetPixelFormat for its window DC on every first-level
acquisition (context_set_pixel_format from context_set_gl_context, or the
check in context_enter when the context was still current). DreamGPU's
frontend answers that through WindowFromDC, an NT system call, and the
native-context owner is released after every D3D operation, so every
operation paid for it. Patch 0063 keeps hdc_has_format, as for a private DC,
once the context's format is verified or set on its own GetDC DC, unless the
window has a class DC or WGL_WINE_pixel_format_passthrough can change (and
restore_pf restore) the format.

The prepared acquire/release, activation, window update, pixel-format and
restore functions, and context_create's initialisation of the DC fields, run
here under ASan/UBSan against a model of windows, DCs (cache, class and
private) and WGL's set-once pixel format. Every successful wglMakeCurrent must
find the context's format on the window the DC belongs to; steady state must
not ask for it at all. Mutations of the patch must fail the test.
"""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('patches', ROOT / 'tests/guest/support/patches.py')
patches = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patches)
MANIFEST = ROOT / 'support/guest/d3d/patches/base/manifest.json'

PREAMBLE = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define USE_WIN32_OPENGL
#define TRUE 1
#define FALSE 0
#define TRACE(...) ((void)0)
#define WARN(...) ((void)0)
#define FIXME(...) ((void)0)
#define ERR(...) ((void)0)
#define WARN_ON(...) 0
#define GL_EXTCALL(call) call
#define WGL_WINE_PIXEL_FORMAT_PASSTHROUGH 0
#define GCL_STYLE (-26)
#define CS_OWNDC 0x0020
#define CS_CLASSDC 0x0040
typedef int BOOL;
typedef unsigned DWORD, UINT;
typedef void *HGLRC;

/* Windows own the pixel format (WGL sets it once); DCs belong to a window. */
struct window { int format; DWORD class_style; struct dc *class_dc; };
struct dc { struct window *window; unsigned held; };
typedef struct window *HWND;
typedef struct dc *HDC;

struct wined3d_context;
struct wined3d_gl_info { BOOL supported[1]; struct { struct { int (*p_wglGetPixelFormat)(HDC); } wgl; } gl_ops; };
struct texture { struct wined3d_swapchain *swapchain; void *surface; };
struct wined3d_surface { struct texture *container; };
struct wined3d_swapchain { struct wined3d_device *device; unsigned num_contexts; struct wined3d_context **context;
    struct texture **back_buffers, *front_buffer; HWND win_handle; HDC backup_dc; };
struct wined3d_device { struct wined3d_swapchain **swapchains; unsigned context_count, contexts_lost;
    struct wined3d_context **contexts; };
struct wined3d_context {
    const struct wined3d_gl_info *gl_info; struct wined3d_swapchain *swapchain; struct wined3d_surface *current_rt;
    unsigned level, tid, current, valid, destroyed, needs_set, hdc_is_private, hdc_has_format;
    int pixel_format, restore_pf; HWND restore_pf_win, win_handle; HDC hdc, restore_dc; HGLRC glCtx, restore_ctx;
    struct wined3d_context *restore_wined3d;
};

static struct wined3d_gl_info gl;
static unsigned format_queries, window_lookups, class_lookups, sets, passthrough_sets, binds, misbinds, fail_bind;
static struct wined3d_context *tls;
static unsigned wined3d_context_tls_idx;
static HGLRC current_gl;
static HDC current_dc;
static struct dc dcs[32];
static unsigned dc_count;

static DWORD GetCurrentThreadId(void) { return 1; }
static DWORD GetLastError(void) { return 6; }
static struct wined3d_context *context_get_current(void) { return tls; }
static BOOL TlsSetValue(unsigned idx, void *value) { (void)idx; tls = value; return TRUE; }
static HGLRC wglGetCurrentContext(void) { return current_gl; }
static HDC wglGetCurrentDC(void) { return current_dc; }
static HDC GetDC(HWND window)
{
    struct dc *dc;
    if (!window) return NULL;
    /* A class DC is shared by the windows of its class: GetDC moves it. */
    if (window->class_dc) { window->class_dc->window = window; ++window->class_dc->held; return window->class_dc; }
    assert(dc_count < sizeof(dcs) / sizeof(*dcs));
    dc = &dcs[dc_count++];
    dc->window = window;
    dc->held = 1;
    return dc;
}
static int ReleaseDC(HWND window, HDC dc) { assert(dc->window == window && dc->held); --dc->held; return 1; }
static void wined3d_release_dc(HWND window, HDC dc) { ReleaseDC(window, dc); }
static HWND WindowFromDC(HDC dc) { ++window_lookups; return dc->window; }
static BOOL IsWindow(HWND window) { return window != NULL; }
static DWORD GetClassLongA(HWND window, int index) { assert(index == GCL_STYLE); ++class_lookups; return window->class_style; }
/* The DreamGPU frontend: WindowFromDC, then the window's format. */
static int get_format(HDC dc) { ++format_queries; return WindowFromDC(dc)->format; }
static BOOL SetPixelFormat(HDC dc, int format, const void *descriptor)
{
    (void)descriptor;
    ++sets;
    if (dc->window->format) return FALSE; /* WGL: set once */
    dc->window->format = format;
    return TRUE;
}
static BOOL wglSetPixelFormatWINE(HDC dc, int format)
{
    assert(gl.supported[WGL_WINE_PIXEL_FORMAT_PASSTHROUGH]);
    ++passthrough_sets;
    dc->window->format = format;
    return TRUE;
}
static BOOL wglMakeCurrent(HDC dc, HGLRC context)
{
    current_gl = NULL;
    current_dc = NULL;
    if (!context) return TRUE;
    if (fail_bind) { --fail_bind; return FALSE; }
    /* Rendering through the DC requires the context's format on its window. */
    if (dc->window->format != 1) ++misbinds;
    ++binds;
    current_gl = context;
    current_dc = dc;
    return TRUE;
}
static HDC swapchain_get_backup_dc(struct wined3d_swapchain *swapchain) { return swapchain->backup_dc; }
static void context_destroy_gl_resources(struct wined3d_context *context) { (void)context; assert(0); }
static void context_setup_target(struct wined3d_context *context, struct wined3d_surface *target)
{
    assert(current_gl == context->glCtx);
    context->current_rt = target;
}
static struct wined3d_context *swapchain_create_context(struct wined3d_swapchain *swapchain) { (void)swapchain; assert(0); return NULL; }
static void *wined3d_texture_get_sub_resource(struct texture *texture, unsigned i) { assert(!i); return texture->surface; }
static struct wined3d_surface *surface_from_resource(void *resource) { return resource; }
static BOOL context_set_current(struct wined3d_context *ctx);
static void context_release(struct wined3d_context *context);
'''

CREATE = r'''
/* context_create: enter, set the format on the new DC, initialise the DC
 * fields with the prepared statements, activate; the caller releases. */
static struct wined3d_context *create(struct wined3d_swapchain *swapchain, struct wined3d_surface *target,
        HDC hdc, BOOL hdc_is_private, int pixel_format)
{
    struct wined3d_context *ret = calloc(1, sizeof(*ret));
    HGLRC ctx = ret;
    assert(ret);
    ret->gl_info = &gl;
    context_enter(ret);
    assert(context_set_pixel_format(ret, hdc, hdc_is_private, pixel_format));
    ret->swapchain = swapchain;
    ret->current_rt = target;
    ret->valid = 1;
@INIT@
    assert(context_set_current(ret));
    swapchain->context[0] = ret;
    swapchain->num_contexts = 1;
    swapchain->device->contexts[0] = ret;
    swapchain->device->context_count = 1;
    context_release(ret);
    assert(!ret->level && !current_gl && !tls);
    return ret;
}
'''

TEST = r'''
struct world {
    struct wined3d_device device; struct wined3d_swapchain swapchain; struct wined3d_context *contexts[1];
    struct wined3d_context *swapchain_contexts[1]; struct wined3d_swapchain *swapchains[1];
    struct texture front; struct wined3d_surface surface;
};

static void setup(struct world *w, HWND window)
{
    memset(w, 0, sizeof(*w));
    w->swapchains[0] = &w->swapchain;
    w->device.swapchains = w->swapchains;
    w->device.contexts = w->contexts;
    w->swapchain.device = &w->device;
    w->swapchain.context = w->swapchain_contexts;
    w->swapchain.front_buffer = &w->front;
    w->swapchain.win_handle = window;
    w->front.swapchain = &w->swapchain;
    w->front.surface = &w->surface;
    w->surface.container = &w->front;
}

/* One D3D operation: first-level acquire, a nested one, release both. */
static void operation(struct world *w, struct wined3d_context *c)
{
    assert(context_acquire(&w->device, &w->surface) == c && c->valid && current_gl == c->glCtx && c->level == 1);
    assert(context_acquire(&w->device, &w->surface) == c && c->level == 2);
    context_release(c);
    assert(current_gl == c->glCtx);
    context_release(c);
    /* DreamGPU releases the native context owner after every operation. */
    assert(!c->level && !current_gl && !tls && !c->current);
}

static void destroy(struct world *w, struct wined3d_context *c)
{
    assert(!c->level);
    if (!c->hdc_is_private) wined3d_release_dc(c->win_handle, c->hdc);
    free(c);
    w->swapchain.num_contexts = 0;
}

int main(void)
{
    struct world w;
    struct window w1 = {0}, w2 = {0}, app = {0}, classed1 = {0}, classed2 = {0}, backup = {0}, own = {0};
    struct dc class_dc = {0};
    struct wined3d_context *c;
    HDC backup_dc = GetDC(&backup);
    unsigned before, lookups, classes, i;
    gl.gl_ops.wgl.p_wglGetPixelFormat = get_format;

    /* Far Cry's path: a GetDC DC of the device window, format set by the
     * context. Creation sets it, the first activation verifies it once. */
    setup(&w, &w1);
    c = create(&w.swapchain, &w.surface, GetDC(&w1), FALSE, 1);
    assert(w1.format == 1 && sets == 1 && format_queries == 2 && class_lookups == 1);
    assert(c->hdc_has_format && !misbinds);
    printf("creation: %u format queries\n", format_queries);
    before = format_queries; lookups = window_lookups; classes = class_lookups; sets = 0;
    for (i = 0; i < 100; ++i)
        operation(&w, c);
    printf("steady state: %u format queries in 100 operations\n", format_queries - before);
    assert(format_queries == before && window_lookups == lookups && class_lookups == classes && !sets);
    assert(binds >= 100 && !misbinds);
    /* Still current at a first-level acquisition (another device's context
     * was restored over it, or WGL was changed directly): context_enter's
     * check is skipped too. */
    tls = c; c->current = 1; current_gl = c->glCtx; current_dc = c->hdc;
    assert(context_acquire(&w.device, &w.surface) == c && !c->needs_set);
    context_release(c);
    assert(format_queries == before && !misbinds && !current_gl);

    /* The swapchain moves to another window (Present's override window, or
     * SetDestWindow): its DC's format is verified and set again. */
    w.swapchain.win_handle = &w2;
    before = format_queries;
    operation(&w, c);
    assert(c->win_handle == &w2 && c->hdc->window == &w2 && w2.format == 1 && sets == 1);
    assert(format_queries == before + 1 && c->hdc_has_format && !misbinds);
    for (i = 0; i < 10; ++i)
        operation(&w, c);
    assert(format_queries == before + 1);
    w.swapchain.win_handle = &w1;
    operation(&w, c);
    assert(c->hdc->window == &w1 && format_queries == before + 2 && sets == 1 && !misbinds);
    operation(&w, c);
    assert(format_queries == before + 2);

    /* The DC fails to bind (its window is gone; GDI gives the handle to a
     * new window without a format): the next binding verifies it again. */
    fail_bind = 1;
    w.swapchain.backup_dc = backup_dc;
    assert(context_acquire(&w.device, &w.surface) == c && c->valid && current_dc == backup_dc);
    context_release(c);
    assert(!c->hdc_has_format);
    w1.format = 0;
    operation(&w, c);
    assert(w1.format == 1 && current_gl == NULL && !misbinds && c->hdc_has_format);
    destroy(&w, c);

    /* A new context (a device reset) on a window that already has the
     * format: verified at creation and activation, then kept. */
    format_queries = sets = 0;
    c = create(&w.swapchain, &w.surface, GetDC(&w1), FALSE, 1);
    assert(format_queries == 2 && !sets && c->hdc_has_format);
    operation(&w, c);
    assert(format_queries == 2 && !misbinds);
    /* Finding another format than the context's never keeps it. */
    c->hdc_has_format = 0;
    w1.format = 2;
    assert(context_set_pixel_format(c, c->hdc, FALSE, 2) && !c->hdc_has_format);
    w1.format = 1;
    assert(context_set_pixel_format(c, c->hdc, FALSE, 1) && c->hdc_has_format);
    destroy(&w, c);

    /* The window already has another format and WGL cannot change it: Wine
     * keeps rendering with it and checks it on every acquisition as before. */
    app.format = 2;
    setup(&w, &app);
    c = create(&w.swapchain, &w.surface, GetDC(&app), FALSE, 1);
    assert(!c->hdc_has_format && app.format == 2);
    before = format_queries; misbinds = 0;
    for (i = 0; i < 5; ++i)
        operation(&w, c);
    assert(format_queries == before + 5 && !c->hdc_has_format);
    misbinds = 0;
    destroy(&w, c);

    /* With WGL_WINE_pixel_format_passthrough Wine replaces the application's
     * format while it renders and restores it (restore_pf) at release: the
     * format is checked and set again on every acquisition. */
    gl.supported[WGL_WINE_PIXEL_FORMAT_PASSTHROUGH] = 1;
    app.format = 2;
    c = create(&w.swapchain, &w.surface, GetDC(&app), FALSE, 1);
    assert(app.format == 2 && !c->hdc_has_format && c->needs_set && !c->restore_pf);
    before = format_queries; passthrough_sets = 0;
    for (i = 0; i < 5; ++i)
    {
        operation(&w, c);
        assert(app.format == 2 && c->needs_set && !c->restore_pf);
    }
    assert(format_queries == before + 5 && passthrough_sets == 10 && !misbinds && !c->hdc_has_format);
    /* Another device or the application may change it at any time. */
    app.format = 0;
    destroy(&w, c);
    setup(&w, &w2);
    c = create(&w.swapchain, &w.surface, GetDC(&w2), FALSE, 1);
    operation(&w, c);
    w2.format = 3;
    operation(&w, c);
    assert(!misbinds && !c->hdc_has_format);
    destroy(&w, c);
    gl.supported[WGL_WINE_PIXEL_FORMAT_PASSTHROUGH] = 0;

    /* A class DC moves to whichever window of its class last took it. */
    classed1.class_style = classed2.class_style = CS_CLASSDC;
    classed1.class_dc = classed2.class_dc = &class_dc;
    setup(&w, &classed1);
    c = create(&w.swapchain, &w.surface, GetDC(&classed1), FALSE, 1);
    assert(classed1.format == 1 && !c->hdc_has_format);
    operation(&w, c);
    ReleaseDC(&classed2, GetDC(&classed2));
    assert(c->hdc->window == &classed2);
    before = format_queries;
    operation(&w, c);
    assert(classed2.format == 1 && !misbinds && format_queries == before + 1);
    class_dc.window = &classed1;
    destroy(&w, c);

    /* An own DC (CS_OWNDC) stays with its window: kept. */
    own.class_style = CS_OWNDC;
    setup(&w, &own);
    c = create(&w.swapchain, &w.surface, GetDC(&own), FALSE, 1);
    before = format_queries;
    operation(&w, c);
    assert(c->hdc_has_format && format_queries == before && !misbinds);
    destroy(&w, c);

    /* A private (backup window) DC keeps its creation-time behaviour: the
     * format is never queried after creation, nor its class. */
    setup(&w, &backup);
    w.swapchain.backup_dc = backup_dc;
    format_queries = 0; classes = class_lookups;
    c = create(&w.swapchain, &w.surface, backup_dc, TRUE, 1);
    assert(format_queries == 1 && c->hdc_has_format && class_lookups == classes);
    operation(&w, c);
    assert(format_queries == 1 && !misbinds);
    destroy(&w, c);

    puts("PASS prepared WineD3D window-DC pixel format: verified once and kept, no query per operation; "
         "rechecked after a window change, a failed binding, another format, passthrough restore_pf and class DCs");
    return 0;
}
'''

MUTATIONS = {
    'private-only-skip': [(
        '    if (dc == context->hdc && context->hdc_has_format)\n        return TRUE;\n',
        '    if (dc == context->hdc && context->hdc_is_private && context->hdc_has_format)\n        return TRUE;\n')],
    'private-only-keep': [(
        '    if (dc == context->hdc && (context->hdc_is_private\n',
        '    if (dc == context->hdc && context->hdc_is_private && (context->hdc_is_private\n')],
    'enter-checks-window-dcs': [(
        'else if (!context->needs_set && !context->hdc_has_format\n',
        'else if (!context->needs_set && !(context->hdc_is_private && context->hdc_has_format)\n')],
    'any-format-kept': [(
        '            || (format == context->pixel_format\n            && ',
        '            || (')],
    'passthrough-kept': [(
        '            && !gl_info->supported[WGL_WINE_PIXEL_FORMAT_PASSTHROUGH]\n', '')],
    'class-dc-kept': [(
        '\n            && !(GetClassLongA(context->win_handle, GCL_STYLE) & CS_CLASSDC))))\n', ')))\n')],
    'window-change-kept': [(
        '    /* DreamGPU: the format is verified on the new window\'s DC. */\n    context->hdc_has_format = FALSE;\n', '')],
    'created-with-format': [(
        '    ret->hdc_has_format = hdc_is_private;\n', '    ret->hdc_has_format = TRUE;\n')],
    'bind-failure-kept': [(
        '        if (!ctx->hdc_is_private)\n            ctx->hdc_has_format = FALSE;\n', '')],
}


def function(source, prefix):
    start = source.index(prefix)
    return source[start:source.index('\n}', start) + 2]


def between(source, first, last):
    start = source.index(first)
    return source[start:source.index(last, start) + len(last)]


def build(work, name, text, expect_pass):
    path = work / (name + '.c')
    path.write_text(text)
    executable = work / name
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-Wno-unused-parameter', '-Wno-unused-variable', '-Wno-sign-compare', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-g', str(path), '-o', str(executable)],
                   check=True)
    result = subprocess.run([str(executable)], capture_output=not expect_pass, text=True)
    if expect_pass:
        result.check_returncode()
    else:
        assert result.returncode, (name, 'not rejected')
        lines = (result.stdout + result.stderr).strip().splitlines()
        return next((line for line in lines if 'Assertion' in line), lines[-1] if lines else '(abort)')


def program(context, swapchain):
    # The production (non-WIN32_NATIVE) helper is the first definition.
    create = function(context, 'struct wined3d_context *context_create(')
    init = between(create, '    ret->glCtx = ctx;\n', '    ret->needs_set = 1;\n')
    code = [function(context, prefix) for prefix in (
        'static BOOL context_restore_pixel_format(', 'static BOOL context_set_pixel_format(',
        'static BOOL context_set_gl_context(', 'static void context_restore_gl_context(',
        'static void context_update_window(', 'BOOL context_set_current(', 'void context_release(',
        'static void context_enter(')]
    code.append(function(swapchain, 'struct wined3d_context *swapchain_get_context('))
    code.append(function(context, 'struct wined3d_context *context_acquire('))
    return PREAMBLE + '\n'.join(code) + CREATE.replace('@INIT@', init) + TEST


with tempfile.TemporaryDirectory(prefix='dreamgpu-window-dc-format-') as directory:
    work = Path(directory)
    for name in json.loads(MANIFEST.read_text())['files']:
        destination = work / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, destination)
    patches.apply(work, MANIFEST)
    # The production build is USE_WIN32_OPENGL without WIN32_NATIVE.
    assert '-DUSE_WIN32_OPENGL' in (ROOT / 'vendor/wine9x/Makefile').read_text()
    context = (work / 'wined3d/context.c').read_text()
    swapchain = (work / 'wined3d/swapchain.c').read_text()

    # context_create sets the format on the new DC before initialising the
    # context's DC fields, then activates it (the order CREATE follows).
    create = function(context, 'struct wined3d_context *context_create(')
    set_call = create.index('    if (!context_set_pixel_format(ret, hdc, hdc_is_private, pixel_format))')
    init = create.index('    ret->glCtx = ctx;\n')
    assert set_call < init < create.index('    if (!context_set_current(ret))') and create.count('ret->hdc_has_format') == 1
    # The only wglGetPixelFormat callers are the ones run here (and the
    # legacy WIN32_NATIVE helper, not built).
    assert context.count('gl_ops.wgl.p_wglGetPixelFormat(') == 3
    # context_release still releases the native owner after every operation.
    assert 'Failed to release the native context owner.' in function(context, 'void context_release(')

    build(work, 'fixed', program(context, swapchain), True)
    for name, replacements in MUTATIONS.items():
        mutated = context
        for old, new in replacements:
            assert mutated.count(old) == 1, name
            mutated = mutated.replace(old, new)
        print(name, 'rejected:', build(work, name, program(mutated, swapchain), False))
    print(f'PASS all {len(MUTATIONS)} mutations of the patch are rejected')
