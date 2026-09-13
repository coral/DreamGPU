/* SPDX-License-Identifier: GPL-2.0-or-later
 * Freestanding Win32 ownership. API calls borrow get(); ownership cannot copy.
 * Reset preserves the triggering Win32 error, including early-return cleanup.
 */
#ifndef DREAMGPU_TOOLS_UNREAL_HANDLES_HPP
#define DREAMGPU_TOOLS_UNREAL_HANDLES_HPP
template <auto Close> class ScopedHandle final {
    HANDLE value_{};

  public:
    explicit ScopedHandle(HANDLE value = HANDLE{}) noexcept : value_(value) {}
    ~ScopedHandle() noexcept {
        reset();
    }
    ScopedHandle(const ScopedHandle &) = delete;
    ScopedHandle &operator=(const ScopedHandle &) = delete;
    ScopedHandle(ScopedHandle &&other) noexcept : value_(other.release()) {}
    ScopedHandle &operator=(ScopedHandle &&other) noexcept {
        if (this != &other)
            reset(other.release());
        return *this;
    }
    [[nodiscard]] HANDLE get() const noexcept {
        return value_;
    }
    [[nodiscard]] explicit operator bool() const noexcept {
        return value_ != HANDLE{} && value_ != INVALID_HANDLE_VALUE;
    }
    [[nodiscard]] HANDLE release() noexcept {
        HANDLE result = value_;
        value_ = HANDLE{};
        return result;
    }
    void reset(HANDLE replacement = HANDLE{}) noexcept {
        if (replacement == value_)
            return;
        HANDLE old = value_;
        value_ = replacement;
        if (old != HANDLE{} && old != INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            Close(old);
            SetLastError(error);
        }
    }
};
using OwnedHandle = ScopedHandle<CloseHandle>;
using FindHandle = ScopedHandle<FindClose>;
#endif
