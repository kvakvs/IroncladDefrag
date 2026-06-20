#pragma once

#include <Windows.h>

namespace icd::win {

// Owns a Win32 HANDLE so platform code cannot leak opened files or volumes.
class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE value) : handle(value) {}
    ~UniqueHandle();

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept;
    UniqueHandle& operator=(UniqueHandle&& other) noexcept;

    HANDLE Get() const { return handle; }
    bool IsValid() const { return handle != nullptr && handle != INVALID_HANDLE_VALUE; }
    HANDLE Release();
    void Reset(HANDLE value = INVALID_HANDLE_VALUE);

private:
    HANDLE handle = INVALID_HANDLE_VALUE;
};

} // namespace icd::win
