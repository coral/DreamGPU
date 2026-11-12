/* SPDX-License-Identifier: GPL-2.0-or-later
 * Rectangle locks of render targets through the public D3D8/9 API (Wine
 * 0056). Included by probe.cpp after tween-probe.inc. WineD3D downloaded the
 * whole surface for any lock of a target whose current copy is its texture;
 * Unreal Tournament 2003 locks one backbuffer pixel read-only after every
 * present. 0056 reads only the locked rectangle and, after a writable lock,
 * uploads only that rectangle back. Every pixel is cleared exactly (1x1
 * Clear rectangles in a 20x20 region of distinct colours over a background),
 * so each value is known. Per target:
 * - a whole-surface read-only lock (Wine's full download) checks the
 *   surface and records the pitch; the target is then repainted;
 * - read-only 1x1 and 16x16 locks and the last pixel are exact and report
 *   the same pitch;
 * - eight 1x1 read-only locks, each after a Clear of that pixel, see the new
 *   colour every time;
 * - a writable 8x8 lock returns the current pixels; a pattern written into
 *   it is, after the unlock and a Clear elsewhere (which renders into the
 *   target's texture), exactly what a whole-surface lock then shows, with
 *   every other pixel unchanged.
 * Only pixels inside each locked rectangle are read: Direct3D defines nothing
 * else, so whether a lock transferred more than its rectangle is not checked
 * here (UT2003's own transport counters show that). Targets: the X8R8G8B8
 * backbuffer, a lockable 400x300 X8R8G8B8 render target (beyond the 320x240
 * window) and, with framebuffer objects (as rtt-probe.inc decides) and when
 * claimed, a 400x300 R5G6B5 one. With exact R5G6B5 storage (probe.cpp's
 * rgb565_storage, DG_CAP2_GL_RGB565) Wine keeps R5G6B5 targets in GL_RGB565:
 * their 20x20 region then holds every 6-bit green and every 5-bit red and
 * blue. Without it (older hosts and frontends) Wine stores them as GL_RGB5,
 * green keeps 5 bits, and the colours use only the 32 six-bit greens
 * round(v * 63 / 31) that survive 5-, 6- and 8-bit storage and both readback
 * conversions. Every FAIL line names the check; the stage line before it
 * names the target.
 * Whole-surface reads of 1024x768 targets (a WineD3D back buffer of that size
 * is what Warcraft III locks after every present): each row and every 64th
 * column carry their own colour, so a pixel landing in the wrong row, page or
 * transfer chunk is caught. Each target is painted, locked whole read-only,
 * repainted and locked whole again, every pixel exact. With bulk reads
 * (DG_CAP2_GL_BULK_READ) the frontend reads such a surface in one QUERY after
 * a one-query shape, otherwise in 48 64 KiB chunks; both must be exact.
 */
#if DG_D3D_VERSION == 8
typedef D3DVIEWPORT8 LockViewport;
#define LOCK_TARGET(device, width, height, format, out)                                            \
    DEVICE(CreateRenderTarget, device, width, height, format, D3DMULTISAMPLE_NONE, TRUE, out)
#else
typedef D3DVIEWPORT9 LockViewport;
#define LOCK_TARGET(device, width, height, format, out)                                            \
    DEVICE(CreateRenderTarget, device, width, height, format, D3DMULTISAMPLE_NONE, 0, TRUE, out,   \
           NULL)
#endif

namespace partial_lock_probe {

static constexpr int Left = 40, Top = 30, Size = 20;
static constexpr int Wide = 400, High = 300;
/* The 1x1 lock, the 16x16 lock and the writable 8x8 lock. */
static constexpr RECT Pixel = {45, 33, 46, 34};
static constexpr RECT Square = {42, 32, 58, 48};
static constexpr RECT Written = {44, 36, 52, 44};

struct Target {
    Surface *surface;
    int width, height;
    BOOL wide; /* X8R8G8B8, else R5G6B5 */
    const char *name;
};

/* Colour components exactly representable in the target: 8 bits, or 5-bit
 * red and blue and a 6-bit green (5-bit-safe without exact R5G6B5 storage,
 * see above), as their bit-replicated 8-bit expansions. */
static DWORD Argb(DWORD r, DWORD g, DWORD b, BOOL wide) {
    if (!wide) {
        r &= 31, g = rgb565_storage ? g & 63 : ((g & 31) * 126 + 31) / 62, b &= 31;
        r = r << 3 | r >> 2, g = g << 2 | g >> 4, b = b << 3 | b >> 2;
    }
    return 0xff000000u | (r & 255) << 16 | (g & 255) << 8 | (b & 255);
}
/* The locked representation of a colour from Argb. */
static DWORD Stored(DWORD argb, BOOL wide) {
    if (wide)
        return argb & 0x00ffffff;
    return (argb >> 19 & 31) << 11 | (argb >> 10 & 63) << 5 | (argb >> 3 & 31);
}
static DWORD Background(int seed, BOOL wide) {
    return Argb(16 + seed * 9, 32 + seed * 5, 48 + seed * 3, wide);
}
/* Over the 20x20 region of either seed: all 64 low six green bits and all 32
 * low five red and blue bits. */
static DWORD Color(int x, int y, int seed, BOOL wide) {
    return Argb(x * 11 + y * 7 + seed * 37 + 1, y * 13 + x * 3 + seed * 5 + 2,
                x * 7 + y * 3 + seed + 3, wide);
}
static BOOL Inside(const RECT &r, int x, int y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}
static DWORD Expected(int x, int y, int seed, BOOL wide) {
    const RECT region = {Left, Top, Left + Size, Top + Size};
    return Stored(Inside(region, x, y) ? Color(x, y, seed, wide) : Background(seed, wide), wide);
}
/* The pattern written through the writable lock (Stored form). */
static DWORD Pattern(int x, int y, BOOL wide) {
    return Stored(Argb(255 - x * 5, 128 + y * 7, x * y + 17, wide), wide);
}
static DWORD Read(const D3DLOCKED_RECT &locked, int dx, int dy, BOOL wide) {
    const BYTE *row = (const BYTE *)locked.pBits + dy * locked.Pitch;
    return wide ? *(const DWORD *)(row + dx * 4) & 0x00ffffff : *(const WORD *)(row + dx * 2);
}
static void Write(const D3DLOCKED_RECT &locked, int dx, int dy, BOOL wide, DWORD value) {
    BYTE *row = (BYTE *)locked.pBits + dy * locked.Pitch;
    if (wide)
        *(DWORD *)(row + dx * 4) = 0xff000000u | value;
    else
        *(WORD *)(row + dx * 2) = (WORD)value;
}
static BOOL Mismatch(const char *stage, int x, int y, DWORD actual, DWORD expected) {
    Check(FALSE, stage, actual);
    Number("pixel x", x);
    Number("pixel y", y);
    Number("pixel expected", expected);
    return FALSE;
}

/* Clear the whole target to the background, then each region pixel. */
static BOOL Paint(Device *dev, const Target &t, int seed) {
    if (!HR(DEVICE(Clear, dev, 0, NULL, D3DCLEAR_TARGET, Background(seed, t.wide), 1, 0),
            "FAIL rectangle lock background Clear"))
        return FALSE;
    for (int y = Top; y < Top + Size; ++y)
        for (int x = Left; x < Left + Size; ++x) {
            const D3DRECT rect = {x, y, x + 1, y + 1};
            if (!HR(DEVICE(Clear, dev, 1, &rect, D3DCLEAR_TARGET, Color(x, y, seed, t.wide), 1, 0),
                    "FAIL rectangle lock pixel Clear"))
                return FALSE;
        }
    return TRUE;
}
static BOOL Lock(const Target &t, const RECT *rect, DWORD flags, D3DLOCKED_RECT *locked) {
    locked->pBits = NULL;
    locked->Pitch = 0;
    return HR(SURFACE(LockRect, t.surface, locked, rect, flags), "FAIL rectangle lock LockRect") &&
           Check(locked->pBits != NULL, "FAIL rectangle lock null pointer", 0);
}
static BOOL Unlock(const Target &t) {
    return HR(SURFACE(UnlockRect, t.surface), "FAIL rectangle lock UnlockRect");
}
/* Every pixel of rect, from a lock of exactly rect. */
static BOOL Compare(const char *stage, const Target &t, const D3DLOCKED_RECT &locked,
                    const RECT &rect, int seed, const RECT *written) {
    for (int y = rect.top; y < rect.bottom; ++y)
        for (int x = rect.left; x < rect.right; ++x) {
            const DWORD expected = written && Inside(*written, x, y) ? Pattern(x, y, t.wide)
                                                                     : Expected(x, y, seed, t.wide);
            const DWORD actual = Read(locked, x - rect.left, y - rect.top, t.wide);
            if (actual != expected)
                return Mismatch(stage, x, y, actual, expected);
        }
    return TRUE;
}
/* A read-only lock of rect compared with the target's current pixels. */
static BOOL ReadBox(const char *stage, const Target &t, const RECT &rect, int seed, INT pitch) {
    D3DLOCKED_RECT locked;
    if (!Lock(t, &rect, D3DLOCK_READONLY, &locked))
        return FALSE;
    BOOL ok = Check(locked.Pitch == pitch,
                    "FAIL rectangle lock: read-only pitch differs from the whole lock",
                    (DWORD)locked.Pitch) &&
              Compare(stage, t, locked, rect, seed, NULL);
    return Unlock(t) && ok;
}

static BOOL Case(Device *dev, const Target &t) {
    const RECT whole = {0, 0, t.width, t.height};
    const RECT last = {t.width - 1, t.height - 1, t.width, t.height};
    D3DLOCKED_RECT locked;
    INT pitch;
    Log(t.name);
    if (!rtt_probe::Target(dev, t.surface, NULL, t.width, t.height))
        return FALSE;
    /* Baseline: a whole-surface lock (Wine's full download) of the first paint. */
    if (!Paint(dev, t, 1) || !Lock(t, NULL, D3DLOCK_READONLY, &locked))
        return FALSE;
    pitch = locked.Pitch;
    Number("whole-lock pitch", (DWORD)pitch);
    BOOL ok = Check(pitch >= t.width * (t.wide ? 4 : 2),
                    "FAIL rectangle lock baseline: whole-lock pitch too small", pitch) &&
              Compare("FAIL rectangle lock baseline: whole-surface read-only lock pixel", t, locked,
                      whole, 1, NULL);
    if (!Unlock(t) || !ok)
        return FALSE;
    /* Repainted: system memory is stale, the texture current. */
    if (!Paint(dev, t, 2) ||
        !ReadBox("FAIL rectangle lock: read-only 1x1 pixel", t, Pixel, 2, pitch) ||
        !ReadBox("FAIL rectangle lock: read-only 16x16 pixel", t, Square, 2, pitch) ||
        !ReadBox("FAIL rectangle lock: read-only last pixel", t, last, 2, pitch))
        return FALSE;
    /* Maps interleaved with draws see each new value. */
    for (int i = 0; i < 8; ++i) {
        const D3DRECT rect = {Pixel.left, Pixel.top, Pixel.right, Pixel.bottom};
        const DWORD color = Argb(i * 31 + 5, 200 - i * 17, i * 9 + 40, t.wide);
        if (!HR(DEVICE(Clear, dev, 1, &rect, D3DCLEAR_TARGET, color, 1, 0),
                "FAIL rectangle lock: Clear before a repeated 1x1 lock") ||
            !Lock(t, &Pixel, D3DLOCK_READONLY, &locked))
            return FALSE;
        const DWORD actual = Read(locked, 0, 0, t.wide);
        ok = actual == Stored(color, t.wide) ||
             Mismatch("FAIL rectangle lock: read-only 1x1 pixel after its Clear", Pixel.left,
                      Pixel.top, actual, Stored(color, t.wide));
        if (!Unlock(t) || !ok)
            return FALSE;
    }
    {
        const D3DRECT rect = {Pixel.left, Pixel.top, Pixel.right, Pixel.bottom};
        if (!HR(DEVICE(Clear, dev, 1, &rect, D3DCLEAR_TARGET,
                       Color(Pixel.left, Pixel.top, 2, t.wide), 1, 0),
                "FAIL rectangle lock: restoring Clear"))
            return FALSE;
    }
    /* Writable box: current pixels in, a pattern out, nothing else changed. */
    if (!Lock(t, &Written, 0, &locked))
        return FALSE;
    ok = Check(locked.Pitch == pitch,
               "FAIL rectangle lock: writable 8x8 pitch differs from the whole lock",
               (DWORD)locked.Pitch) &&
         Compare("FAIL rectangle lock: writable 8x8 box pixel before writing", t, locked, Written,
                 2, NULL);
    if (ok)
        for (int y = Written.top; y < Written.bottom; ++y)
            for (int x = Written.left; x < Written.right; ++x)
                Write(locked, x - Written.left, y - Written.top, t.wide, Pattern(x, y, t.wide));
    if (!Unlock(t) || !ok)
        return FALSE;
    /* Rendering elsewhere uses the target's texture: the written box must be in it. */
    {
        const D3DRECT rect = {last.left, last.top, last.right, last.bottom};
        if (!HR(DEVICE(Clear, dev, 1, &rect, D3DCLEAR_TARGET, Background(2, t.wide), 1, 0),
                "FAIL rectangle lock: Clear after the writable 8x8 lock"))
            return FALSE;
    }
    if (!Lock(t, NULL, D3DLOCK_READONLY, &locked))
        return FALSE;
    ok = Compare("FAIL rectangle lock: whole-surface lock after the writable 8x8 box", t, locked,
                 whole, 2, &Written);
    return Unlock(t) && ok;
}

static constexpr int LargeWide = 1024, LargeHigh = 768;
/* Rows, then every 64th column from 17, each its own colour. */
static DWORD LargeColor(int x, int y, int seed, BOOL wide) {
    if (x % 64 == 17)
        return Argb(x / 4 + seed * 3, 200 - seed * 20, x / 64 * 17 + seed, wide);
    return Argb(y & 255, (y >> 8) * 64 + seed * 16 + 5, (y * 7 + seed * 29) & 255, wide);
}
static BOOL LargePaint(Device *dev, const Target &t, int seed) {
    for (int y = 0; y < t.height; ++y) {
        const D3DRECT rect = {0, y, t.width, y + 1};
        if (!HR(DEVICE(Clear, dev, 1, &rect, D3DCLEAR_TARGET, LargeColor(0, y, seed, t.wide), 1, 0),
                "FAIL whole-surface lock row Clear"))
            return FALSE;
    }
    for (int x = 17; x < t.width; x += 64) {
        const D3DRECT rect = {x, 0, x + 1, t.height};
        if (!HR(DEVICE(Clear, dev, 1, &rect, D3DCLEAR_TARGET, LargeColor(x, 0, seed, t.wide), 1, 0),
                "FAIL whole-surface lock column Clear"))
            return FALSE;
    }
    return TRUE;
}
/* Two whole-surface read-only locks of a large target, each after a paint. */
static BOOL Large(Device *dev, const Target &t) {
    Log(t.name);
    if (!rtt_probe::Target(dev, t.surface, NULL, t.width, t.height))
        return FALSE;
    for (int seed = 1; seed <= 2; ++seed) {
        D3DLOCKED_RECT locked;
        if (!LargePaint(dev, t, seed) || !Lock(t, NULL, D3DLOCK_READONLY, &locked))
            return FALSE;
        BOOL ok = Check(locked.Pitch >= t.width * (t.wide ? 4 : 2),
                        "FAIL whole-surface lock pitch too small", (DWORD)locked.Pitch);
        for (int y = 0; ok && y < t.height; ++y)
            for (int x = 0; ok && x < t.width; ++x) {
                const DWORD expected = Stored(LargeColor(x, y, seed, t.wide), t.wide);
                const DWORD actual = Read(locked, x, y, t.wide);
                if (actual != expected)
                    ok = Mismatch(seed == 1 ? "FAIL whole-surface lock pixel"
                                            : "FAIL whole-surface lock pixel after repainting",
                                  x, y, actual, expected);
            }
        if (!Unlock(t) || !ok)
            return FALSE;
    }
    return TRUE;
}

static void Run(D3D *d3d, Device *dev, Surface *backbuffer) {
    Surface *wide = NULL, *narrow = NULL;
    Log("STAGE rectangle locks of render targets");
    D3DDISPLAYMODE mode = {};
    if (!HR(D3D_CALL(GetAdapterDisplayMode, d3d, D3DADAPTER_DEFAULT, &mode),
            "FAIL rectangle lock adapter display mode"))
        return;
    /* As rtt_probe::Run: only framebuffer objects claim a target in the other depth. */
    const D3DFORMAT other = mode.Format == D3DFMT_R5G6B5 ? D3DFMT_X8R8G8B8 : D3DFMT_R5G6B5;
    const BOOL framebuffer_objects =
        SUCCEEDED(D3D_CALL(CheckDeviceFormat, d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, mode.Format,
                           D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, other));
    const BOOL rgb565 =
        framebuffer_objects &&
        SUCCEEDED(D3D_CALL(CheckDeviceFormat, d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, mode.Format,
                           D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, D3DFMT_R5G6B5));
    Number("framebuffer objects (other-depth render target claimed)", framebuffer_objects);
    if (!HR(DEVICE(SetRenderState, dev, D3DRS_COLORWRITEENABLE, 0xf),
            "FAIL rectangle lock colour writes"))
        return;
#if DG_D3D_VERSION == 9
    if (!HR(DEVICE(SetRenderState, dev, D3DRS_SCISSORTESTENABLE, FALSE),
            "FAIL rectangle lock scissor"))
        return;
#endif
    BOOL ok = Case(dev, {backbuffer, rtt_probe::ScreenWidth, rtt_probe::ScreenHeight, TRUE,
                         "STAGE rectangle lock: X8R8G8B8 backbuffer 320x240"});
    if (ok && framebuffer_objects) {
        ok = HR(LOCK_TARGET(dev, Wide, High, D3DFMT_X8R8G8B8, &wide),
                "FAIL rectangle lock X8R8G8B8 render target creation") &&
             Case(dev, {wide, Wide, High, TRUE,
                        "STAGE rectangle lock: lockable X8R8G8B8 render target 400x300"});
        if (ok && rgb565)
            ok = HR(LOCK_TARGET(dev, Wide, High, D3DFMT_R5G6B5, &narrow),
                    "FAIL rectangle lock R5G6B5 render target creation") &&
                 Case(dev, {narrow, Wide, High, FALSE,
                            rgb565_storage
                                ? "STAGE rectangle lock: lockable R5G6B5 render target 400x300, "
                                  "all 64 greens"
                                : "STAGE rectangle lock: lockable R5G6B5 render target 400x300, "
                                  "5-bit-safe greens (no exact R5G6B5 storage)"});
        else if (ok)
            Log("SKIP rectangle lock R5G6B5 render target: not claimed");
    } else if (ok) {
        Log("SKIP rectangle lock render targets: offscreen targets render "
            "through the backbuffer (no framebuffer objects)");
    }
    rtt_probe::Target(dev, backbuffer, NULL, rtt_probe::ScreenWidth, rtt_probe::ScreenHeight);
    if (narrow)
        SURFACE(Release, narrow);
    if (wide)
        SURFACE(Release, wide);
    if (ok)
        Log("PASS rectangle locks: read-only 1x1/16x16/last-pixel and writable 8x8 locks of "
            "render targets exact, same pitch as a whole lock, fresh after each Clear, written box "
            "kept by rendering");
    if (!ok)
        return;
    /* Whole-surface reads of 1024x768 targets (bulk reads when negotiated). */
    Log("STAGE whole-surface locks of 1024x768 render targets");
    if (!framebuffer_objects) {
        Log("SKIP whole-surface locks: offscreen targets render through the backbuffer (no "
            "framebuffer objects)");
        return;
    }
    Surface *large = NULL, *large16 = NULL;
    ok = HR(LOCK_TARGET(dev, LargeWide, LargeHigh, D3DFMT_X8R8G8B8, &large),
            "FAIL whole-surface lock X8R8G8B8 render target creation") &&
         Large(dev, {large, LargeWide, LargeHigh, TRUE,
                     "STAGE whole-surface lock: lockable X8R8G8B8 render target 1024x768"});
    if (ok && rgb565)
        ok = HR(LOCK_TARGET(dev, LargeWide, LargeHigh, D3DFMT_R5G6B5, &large16),
                "FAIL whole-surface lock R5G6B5 render target creation") &&
             Large(dev, {large16, LargeWide, LargeHigh, FALSE,
                         "STAGE whole-surface lock: lockable R5G6B5 render target 1024x768"});
    rtt_probe::Target(dev, backbuffer, NULL, rtt_probe::ScreenWidth, rtt_probe::ScreenHeight);
    if (large16)
        SURFACE(Release, large16);
    if (large)
        SURFACE(Release, large);
    if (ok)
        Log("PASS whole-surface locks: 1024x768 render targets exact in every row and column, "
            "twice");
}

} // namespace partial_lock_probe
