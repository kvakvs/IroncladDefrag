#pragma once

#include <windows.h>

#include <utility>

namespace icd::platform {

    // Owns a Win32 kernel handle (files, volumes, devices) and closes it once; move-only. Every system
    // call that opens something hands the result to one of these, so no path can leak a volume handle.
    class UniqueHandle {
    public:
        UniqueHandle() = default;
        // Takes ownership; INVALID_HANDLE_VALUE and nullptr both mean "none".
        explicit UniqueHandle(HANDLE handle) : handle_(handle == nullptr ? INVALID_HANDLE_VALUE : handle) {}
        UniqueHandle(const UniqueHandle&) = delete;
        UniqueHandle& operator=(const UniqueHandle&) = delete;
        UniqueHandle(UniqueHandle&& other) noexcept : handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE)) {}
        UniqueHandle& operator=(UniqueHandle&& other) noexcept {
            if (this != &other) {
                reset();
                handle_ = std::exchange(other.handle_, INVALID_HANDLE_VALUE);
            }
            return *this;
        }
        ~UniqueHandle() { reset(); }

        // The handle for API calls (still owned).
        [[nodiscard]] HANDLE get() const { return handle_; }
        // Whether a handle is held.
        [[nodiscard]] bool valid() const { return handle_ != INVALID_HANDLE_VALUE; }
        explicit operator bool() const { return valid(); }

        // Closes the handle now.
        void reset() {
            if (valid()) {
                CloseHandle(handle_);
                handle_ = INVALID_HANDLE_VALUE;
            }
        }

    private:
        HANDLE handle_ = INVALID_HANDLE_VALUE; // Owned handle or INVALID_HANDLE_VALUE.
    };

} // namespace icd::platform
