/* SPDX-License-Identifier: GPL-2.0-or-later
 * Bounded cursor assembly and expansion, independent of VMM/register ABI.
 * A single serialized stream owns all raw bytes. No allocation, runtime,
 * exception, or user pointer; device completion ownership stays with VMM.
 */
#include <stdint.h>
typedef uint32_t DWORD;
typedef uint8_t BYTE;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#include "cursor32.h"
static_assert(sizeof(DG9_CURSOR_LAYOUT) == 32);
namespace {
class cursor_stream final {
    BYTE raw_[DG9_CURSOR_RAW_MAX]{};
    DG9_CURSOR_LAYOUT layout_{};
    DWORD used_{};
    bool staging_{};
    static BYTE byte(DWORD a, DWORD b, DWORD c, DWORD i) noexcept {
        const DWORD word = i < 4 ? a : i < 8 ? b : c;
        return static_cast<BYTE>(word >> ((i % 4) * 8));
    }

  public:
    constexpr cursor_stream() noexcept = default;
    cursor_stream(const cursor_stream &) = delete;
    cursor_stream &operator=(const cursor_stream &) = delete;
    cursor_stream(cursor_stream &&) = delete;
    cursor_stream &operator=(cursor_stream &&) = delete;
    void abort() noexcept {
        staging_ = false;
    }
    void reset() noexcept {
        staging_ = false;
        used_ = 0;
    }
    DWORD used() const noexcept {
        return used_;
    }
    int begin(DWORD bpp, DWORD a, DWORD b, DWORD c, DG9_CURSOR_LAYOUT *layout) noexcept {
        staging_ = false;
        used_ = 0;
        BYTE header[DG9_CURSOR_HEADER_BYTES];
        for (DWORD i = 0; i < sizeof(header); ++i)
            header[i] = byte(a, b, c, i);
        if (!layout || !Dg9CursorLayout(header, bpp, &layout_))
            return 0;
        *layout = layout_;
        staging_ = true;
        return 1;
    }
    int append(DWORD a, DWORD b, DWORD c) noexcept {
        if (!staging_ || used_ >= layout_.Bytes)
            return 0;
        DWORD count = layout_.Bytes - used_;
        if (count > 12)
            count = 12;
        /* Validate the complete final fragment before mutating staging. */
        for (DWORD i = count; i < 12; ++i)
            if (byte(a, b, c, i)) {
                staging_ = false;
                return 0;
            }
        for (DWORD i = 0; i < count; ++i)
            raw_[used_ + i] = byte(a, b, c, i);
        used_ += count;
        return 1;
    }
    int commit(DWORD bpp, DWORD b, DWORD c, DWORD *pixels, DWORD capacity) noexcept {
        if (!staging_ || used_ != layout_.Bytes || b || c)
            return 0;
        staging_ = false;
        const DWORD words = layout_.Width * layout_.Height * 2;
        if (!pixels || capacity < words || (layout_.Bits != 1 && bpp != 32))
            return 0;
        DWORD output = 0;
        for (DWORD y = 0; y < layout_.Height; ++y) {
            const DWORD and_start = y * layout_.AndStride;
            const DWORD xor_start = layout_.Height * layout_.AndStride + y * layout_.XorStride;
            for (DWORD x = 0; x < layout_.Width; ++x) {
                const DWORD mask = 1u << (7 - (x & 7));
                pixels[output++] = (raw_[and_start + x / 8] & mask) ? 0x00ffffffUL : 0;
                if (layout_.Bits == 1)
                    pixels[output++] = (raw_[xor_start + x / 8] & mask) ? 0x00ffffffUL : 0;
                else {
                    const DWORD offset = xor_start + x * 4;
                    pixels[output++] = raw_[offset] | (DWORD(raw_[offset + 1]) << 8) |
                                       (DWORD(raw_[offset + 2]) << 16);
                }
            }
        }
        return 1;
    }
};
constinit cursor_stream stream;
} // namespace
extern "C" int DREAMGPU_CDECL DreamGpuCursorBegin(DWORD bpp, DWORD a, DWORD b, DWORD c,
                                                  DG9_CURSOR_LAYOUT *layout) {
    return stream.begin(bpp, a, b, c, layout);
}
extern "C" int DREAMGPU_CDECL DreamGpuCursorData(DWORD a, DWORD b, DWORD c) {
    return stream.append(a, b, c);
}
extern "C" int DREAMGPU_CDECL DreamGpuCursorCommit(DWORD bpp, DWORD b, DWORD c, DWORD *pixels,
                                                   DWORD capacity) {
    return stream.commit(bpp, b, c, pixels, capacity);
}
extern "C" void DREAMGPU_CDECL DreamGpuCursorAbort(void) {
    stream.abort();
}
extern "C" void DREAMGPU_CDECL DreamGpuCursorReset(void) {
    stream.reset();
}
extern "C" DWORD DREAMGPU_CDECL DreamGpuCursorUsed(void) {
    return stream.used();
}
