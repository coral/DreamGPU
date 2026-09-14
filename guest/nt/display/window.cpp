/* SPDX-License-Identifier: GPL-2.0-or-later
 * GDI owns window geometry and visible regions. No asynchronous WNDOBJ access:
 * cache rectangles inside its callback, then consume them under the device
 * lock that GDI holds while calling DrvDrawEscape. The private semaphore also
 * protects binding/token lifetime without entering miniport callbacks twice.
 */
#include "ownership.hpp"
extern "C" {
#include "framebuf.h"
}

/* GDI callbacks can re-enter the driver: releasing this guard before engine
 * calls is explicit, while every ordinary return releases automatically. */
class WindowSemaphore final {
    HSEMAPHORE value_ = nullptr;

  public:
    WindowSemaphore() noexcept = default;
    WindowSemaphore(const WindowSemaphore &) = delete;
    WindowSemaphore &operator=(const WindowSemaphore &) = delete;
    ~WindowSemaphore() noexcept {
        release();
    }
    void acquire(HSEMAPHORE value) noexcept {
        release();
        EngAcquireSemaphore(value);
        value_ = value;
    }
    void release() noexcept {
        if (value_) {
            HSEMAPHORE value = value_;
            value_ = nullptr;
            EngReleaseSemaphore(value);
        }
    }
};
struct EngineAllocationDeleter {
    void operator()(void *value) const noexcept {
        EngFreeMem(value);
    }
};
static LONG DgMaximum(LONG a, LONG b) noexcept {
    return a > b ? a : b;
}
static LONG DgMinimum(LONG a, LONG b) noexcept {
    return a < b ? a : b;
}
extern "C" {

typedef struct {
    WNDOBJ *Object;
    HWND Window;
    ULONG Binding;
    ULONG ProcessIdentity;
    BOOL Valid;
    DG_KERNEL_PRESENT_REQUEST Present;
} DG_TRACKED_WINDOW;

BOOL DgDesktopActive(PPDEV dev) {
    return dev && dev->Kernel.DesktopActive && *dev->Kernel.DesktopActive;
}

BOOL DgCoherent(PPDEV dev, ULONG reason) {
    return !DgDesktopActive(dev) ||
           (dev->Kernel.Cohere && dev->Kernel.Cohere(dev->Kernel.Context, reason));
}

VOID APIENTRY DrvSynchronizeSurface(SURFOBJ *surface, RECTL *rect, FLONG flags) {
    PPDEV dev = surface ? (PPDEV)surface->dhpdev : NULL;
    (void)rect;
    (void)flags;
    /* GDI may touch any part of its mapped bitmap after this void callback.
     * A partial readback is insufficient: there is no post-write callback to
     * publish damage. The kernel/native fault path stops the VM on failure. */
    if (dev && surface->hsurf == dev->hSurfEng)
        DgCoherent(dev, DG_COHERE_SYNC);
}

static VOID APIENTRY WindowChanged(WNDOBJ *object, FLONG flags) {
    WindowSemaphore semaphore;
    PPDEV dev;
    DG_TRACKED_WINDOW *window;
    ULONG i;
    BOOL more;
    struct {
        ULONG Count;
        RECTL Rects[32];
    } enumeration;
    if (!object || !object->pvConsumer || !object->psoOwner)
        return;
    dev = (PPDEV)object->psoOwner->dhpdev;
    if (!dev)
        return;
    semaphore.acquire(dev->WindowLock);
    window = (DG_TRACKED_WINDOW *)object->pvConsumer;
    if (flags & WOC_DELETE) {
        for (i = 0; i < DG_GL_MAX_DRAWABLES; ++i)
            if (dev->Windows[i] == window)
                dev->Windows[i] = NULL;
        WNDOBJ_vSetConsumer(object, NULL);
        EngFreeMem(window);
        return;
    }
    if (!(flags & WOC_RGN_CLIENT)) {
        return;
    }
    window->Valid = FALSE;
    window->Present.Count = 0;
    window->Present.WindowX = object->rclClient.left;
    window->Present.WindowY = object->rclClient.top;
    window->Present.WindowWidth = object->rclClient.right - object->rclClient.left;
    window->Present.WindowHeight = object->rclClient.bottom - object->rclClient.top;
    if (WNDOBJ_cEnumStart(object, CT_RECTANGLES, CD_ANY, DG_WINDOW_MAX_CLIPS) == (ULONG)-1)
        goto done;
    do {
        more = WNDOBJ_bEnum(object, sizeof(enumeration), (ULONG *)&enumeration);
        if (enumeration.Count > 32)
            goto done;
        for (i = 0; i < enumeration.Count; ++i) {
            RECTL r = enumeration.Rects[i];
            DG_WINDOW_RECT *out;
            r.left = DgMaximum(DgMaximum(r.left, object->rclClient.left), 0);
            r.top = DgMaximum(DgMaximum(r.top, object->rclClient.top), 0);
            r.right =
                DgMinimum(DgMinimum(r.right, object->rclClient.right), (LONG)dev->ScreenWidth);
            r.bottom =
                DgMinimum(DgMinimum(r.bottom, object->rclClient.bottom), (LONG)dev->ScreenHeight);
            if (r.left >= r.right || r.top >= r.bottom)
                continue;
            if (window->Present.Count == DG_WINDOW_MAX_CLIPS)
                goto done;
            out = &window->Present.Clips[window->Present.Count++];
            out->Left = r.left;
            out->Top = r.top;
            out->Right = r.right;
            out->Bottom = r.bottom;
        }
    } while (more);
    window->Valid = TRUE;
done:
}

ULONG DgBindWindow(SURFOBJ *surface, ULONG input_bytes, PVOID input, ULONG output_bytes,
                   PVOID output) {
    WindowSemaphore semaphore;
    DG_WINDOW_BIND request;
    DG_WINDOW_REPLY reply;
    PPDEV dev;
    DG_TRACKED_WINDOW *window = NULL;
    ULONG i, process, slot = DG_GL_MAX_DRAWABLES;
    WNDOBJ *object;
    if (!surface || !surface->dhpdev || !input || !output || input_bytes != sizeof(request) ||
        output_bytes < sizeof(reply))
        return 0;
    memcpy(&request, input, sizeof(request));
    memset(&reply, 0, sizeof(reply));
    reply.Version = DG_WINDOW_VERSION;
    reply.Status = DG_ESCAPE_INVALID;
    dev = (PPDEV)surface->dhpdev;
    if (surface->hsurf != dev->hSurfEng || (dev->BitsPerPixel != 32 && dev->BitsPerPixel != 16) ||
        request.Magic != DG_WINDOW_MAGIC || request.Version != DG_WINDOW_VERSION ||
        request.Reserved || !request.Window || !request.Context || !request.Drawable ||
        !dev->Kernel.Owner)
        goto done;
    process = dev->Kernel.Owner(dev->Kernel.Context, request.Client);
    if (!process)
        goto done;
    semaphore.acquire(dev->WindowLock);
    for (i = 0; i < DG_GL_MAX_DRAWABLES; ++i) {
        DG_TRACKED_WINDOW *candidate = (DG_TRACKED_WINDOW *)dev->Windows[i];
        if (!candidate)
            slot = i;
        else if (candidate->Window == (HWND)(ULONG_PTR)request.Window)
            window = candidate;
    }
    /* A context's last close retires its client token, while the GDI WNDOBJ
     * correctly survives until HWND destruction. Reopening that same window
     * must compare process ownership, not require the retired token to live. */
    if (window && window->ProcessIdentity != process) {
        reply.Status = DG_ESCAPE_OWNER;
        semaphore.release();
        goto done;
    }
    if (dev->NextBinding == (ULONG)-1 || (!window && slot == DG_GL_MAX_DRAWABLES)) {
        reply.Status = DG_ESCAPE_RESOURCES;
        semaphore.release();
        goto done;
    }
    if (!window) {
        window = (DG_TRACKED_WINDOW *)EngAllocMem(FL_ZERO_MEMORY, sizeof(*window), ALLOC_TAG);
        if (!window) {
            reply.Status = DG_ESCAPE_RESOURCES;
            semaphore.release();
            goto done;
        }
        window->Window = (HWND)(ULONG_PTR)request.Window;
        window->ProcessIdentity = process;
        /* GDI holds the window/device lock in WNDOBJ_SETUP. Do not keep our
         * semaphore across an engine call that can issue a callback. */
        semaphore.release();
        object = EngCreateWnd(surface, window->Window, WindowChanged, WO_RGN_CLIENT, 1);
        semaphore.acquire(dev->WindowLock);
        if (!object || object == (WNDOBJ *)-1) {
            EngFreeMem(window);
            semaphore.release();
            goto done;
        }
        window->Object = object;
        dev->Windows[slot] = window;
        WNDOBJ_vSetConsumer(object, window);
        /* The initial full-region callback follows this escape. */
    }
    window->Binding = ++dev->NextBinding;
    window->Present.Client = request.Client;
    window->Present.Context = request.Context;
    window->Present.Drawable = request.Drawable;
    window->Present.Width = dev->ScreenWidth;
    window->Present.Height = dev->ScreenHeight;
    window->Present.Stride = dev->ScreenDelta;
    window->Present.Bpp = dev->BitsPerPixel;
    reply.Binding = window->Binding;
    reply.Status = DG_ESCAPE_OK;
    reply.Capabilities = dev->Kernel.WindowCapabilities & DG_WINDOW_CAP_FRONT_ONLY;
    semaphore.release();
done:
    memcpy(output, &reply, sizeof(reply));
    return 1;
}

static BOOL ClipAppend(DG_KERNEL_PRESENT_REQUEST *out, const DG_KERNEL_PRESENT_REQUEST *window,
                       const RECTL *clip) {
    ULONG i;
    for (i = 0; i < window->Count; ++i) {
        DG_WINDOW_RECT r = window->Clips[i];
        if (clip) {
            r.Left = DgMaximum(r.Left, clip->left);
            r.Top = DgMaximum(r.Top, clip->top);
            r.Right = DgMinimum(r.Right, clip->right);
            r.Bottom = DgMinimum(r.Bottom, clip->bottom);
        }
        if (r.Left >= r.Right || r.Top >= r.Bottom)
            continue;
        if (out->Count == DG_WINDOW_MAX_CLIPS)
            return FALSE;
        out->Clips[out->Count++] = r;
    }
    return TRUE;
}

ULONG APIENTRY DrvDrawEscape(SURFOBJ *surface, ULONG escape, CLIPOBJ *clip, RECTL *bounds,
                             ULONG input_bytes, PVOID input) {
    dreamgpu::unique_owner<DG_KERNEL_PRESENT_REQUEST, EngineAllocationDeleter> allocation;
    WindowSemaphore semaphore;
    DG_WINDOW_PRESENT request;
    PPDEV dev;
    DG_TRACKED_WINDOW *window = NULL;
    DG_KERNEL_PRESENT_REQUEST *present;
    ULONG i, result = 0;
    BOOL more;
    struct {
        ULONG Count;
        RECTL Rects[32];
    } enumeration;
    (void)bounds;
    if (escape != DG_DRAW_ESCAPE || !surface || !surface->dhpdev || !input ||
        input_bytes != sizeof(request))
        return 0;
    memcpy(&request, input, sizeof(request));
    if (request.Magic != DG_WINDOW_MAGIC || request.Version != DG_WINDOW_VERSION ||
        !request.Binding || (request.Flags & ~DG_WINDOW_PRESENT_FLAGS_MASK))
        return 0;
    dev = (PPDEV)surface->dhpdev;
    if (surface->hsurf != dev->hSurfEng || !dev->Kernel.Present)
        return 0;
    if ((request.Flags & DG_WINDOW_PRESENT_FRONT_ONLY) &&
        !(dev->Kernel.WindowCapabilities & DG_WINDOW_CAP_FRONT_ONLY))
        return 0;
    present = (DG_KERNEL_PRESENT_REQUEST *)EngAllocMem(0, sizeof(*present), ALLOC_TAG);
    if (!present)
        return 0;
    allocation.reset(present);
    semaphore.acquire(dev->WindowLock);
    for (i = 0; i < DG_GL_MAX_DRAWABLES; ++i) {
        DG_TRACKED_WINDOW *candidate = (DG_TRACKED_WINDOW *)dev->Windows[i];
        if (candidate && candidate->Binding == request.Binding) {
            window = candidate;
            break;
        }
    }
    if (!window) {
        result = DG_WINDOW_PRESENT_REBIND;
        goto done;
    }
    if (!window->Valid) {
        result = DG_WINDOW_PRESENT_NOT_READY;
        goto done;
    }
    /* A transient DC clip that exceeds our bounded packet must not retire the
     * GL context. Nothing has reached the kernel until the final Present call. */
    result = DG_WINDOW_PRESENT_NOT_READY;
    *present = window->Present;
    present->Flags = request.Flags;
    present->Count = 0;
    if (!clip || clip->iDComplexity == DC_TRIVIAL) {
        if (!ClipAppend(present, &window->Present, NULL))
            goto done;
    } else if (clip->iDComplexity == DC_RECT) {
        if (!ClipAppend(present, &window->Present, &clip->rclBounds))
            goto done;
    } else {
        CLIPOBJ_cEnumStart(clip, FALSE, CT_RECTANGLES, CD_ANY, DG_WINDOW_MAX_CLIPS);
        do {
            more = CLIPOBJ_bEnum(clip, sizeof(enumeration), (ULONG *)&enumeration);
            if (enumeration.Count > 32)
                goto done;
            for (i = 0; i < enumeration.Count; ++i)
                if (!ClipAppend(present, &window->Present, &enumeration.Rects[i]))
                    goto done;
        } while (more);
    }
    result = dev->Kernel.Present(dev->Kernel.Context, present) == DG_ESCAPE_OK;
done:
    return result;
}

/* GDI supplies and locks this WNDOBJ. Validate identity against our owned
 * table before dereferencing consumer state; another driver's WNDOBJ is not
 * a DreamGPU binding. The ICD runtime finishes commands before this swap.
 * Our transport queues presentation after the submitted command sequence. */
BOOL APIENTRY DrvSwapBuffers(SURFOBJ *surface, WNDOBJ *object) {
    if (!surface || !surface->dhpdev || !object)
        return FALSE;
    PPDEV dev = (PPDEV)surface->dhpdev;
    if (surface->hsurf != dev->hSurfEng || !dev->WindowLock || !dev->Kernel.Present ||
        !dev->Kernel.Owner)
        return FALSE;
    WindowSemaphore semaphore;
    semaphore.acquire(dev->WindowLock);
    for (ULONG i = 0; i < DG_GL_MAX_DRAWABLES; ++i) {
        auto *window = (DG_TRACKED_WINDOW *)dev->Windows[i];
        if (!window || window->Object != object)
            continue;
        if (!window->Valid || !window->Binding ||
            dev->Kernel.Owner(dev->Kernel.Context, window->Present.Client) !=
                window->ProcessIdentity)
            return FALSE;
        return dev->Kernel.Present(dev->Kernel.Context, &window->Present) == DG_ESCAPE_OK;
    }
    return FALSE;
}

VOID DgDeleteWindows(PPDEV dev) {
    WindowSemaphore semaphore;
    ULONG i;
    if (!dev->WindowLock)
        return;
    for (i = 0; i < DG_GL_MAX_DRAWABLES; ++i) {
        DG_TRACKED_WINDOW *window;
        semaphore.acquire(dev->WindowLock);
        window = (DG_TRACKED_WINDOW *)dev->Windows[i];
        dev->Windows[i] = NULL;
        if (window)
            WNDOBJ_vSetConsumer(window->Object, NULL);
        semaphore.release();
        if (window) {
            EngDeleteWnd(window->Object);
            EngFreeMem(window);
        }
    }
}

} /* extern C */
