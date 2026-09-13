/* SPDX-License-Identifier: GPL-2.0-or-later
 * Freestanding ownership: no allocator, exceptions, RTTI or C++ runtime.
 * The owner only releases a resource; borrowing never transfers ownership.
 */
#ifndef DREAMGPU_GUEST_OWNERSHIP_HPP
#define DREAMGPU_GUEST_OWNERSHIP_HPP
namespace dreamgpu {
template <class T, class Deleter> class unique_owner final {
    T *value_;
    [[no_unique_address]] Deleter deleter_;

  public:
    explicit unique_owner(T *value = nullptr, Deleter deleter = {}) noexcept
        : value_(value), deleter_(deleter) {}
    ~unique_owner() noexcept {
        reset();
    }
    unique_owner(const unique_owner &) = delete;
    unique_owner &operator=(const unique_owner &) = delete;
    unique_owner(unique_owner &&other) noexcept
        : value_(other.release()), deleter_(other.deleter_) {}
    unique_owner &operator=(unique_owner &&other) noexcept {
        if (this != &other) {
            reset();
            deleter_ = other.deleter_;
            value_ = other.release();
        }
        return *this;
    }
    [[nodiscard]] T *get() const noexcept {
        return value_;
    }
    [[nodiscard]] explicit operator bool() const noexcept {
        return value_ != nullptr;
    }
    [[nodiscard]] T *operator->() const noexcept {
        return value_;
    }
    [[nodiscard]] T *release() noexcept {
        T *value = value_;
        value_ = nullptr;
        return value;
    }
    void reset(T *value = nullptr) noexcept {
        if (value == value_)
            return;
        T *old = value_;
        value_ = value;
        if (old)
            deleter_(old);
    }
};
} // namespace dreamgpu
#endif
