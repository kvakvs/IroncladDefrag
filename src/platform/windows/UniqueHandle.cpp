#include "UniqueHandle.h"

namespace icd::win {

UniqueHandle::~UniqueHandle()
{
    Reset();
}

UniqueHandle::UniqueHandle(UniqueHandle&& other) noexcept
    : handle(other.Release())
{
}

UniqueHandle& UniqueHandle::operator=(UniqueHandle&& other) noexcept
{
    if (this != &other) {
        Reset(other.Release());
    }

    return *this;
}

HANDLE UniqueHandle::Release()
{
    HANDLE released = handle;
    handle = INVALID_HANDLE_VALUE;
    return released;
}

void UniqueHandle::Reset(HANDLE value)
{
    if (IsValid()) {
        CloseHandle(handle);
    }

    handle = value;
}

} // namespace icd::win
