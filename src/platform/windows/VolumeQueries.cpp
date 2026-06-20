#include "../../precompiled.h"
#include "VolumeQueries.h"

#include "DriveEnumerator.h"
#include "UniqueHandle.h"

#include <winioctl.h>

#include <cstddef>
#include <vector>

namespace icd::win {

namespace {
constexpr std::uint64_t InvalidLcn = static_cast<std::uint64_t>(-1);

// todo: use std::min<data type>()
std::uint64_t MinU64(std::uint64_t lhs, std::uint64_t rhs)
{
    return lhs < rhs ? lhs : rhs;
}
} // namespace

// Reads file retrieval pointers without modifying the file or volume.
FileExtentsResult QueryFileExtents(const std::filesystem::path& path)
{
    FileExtentsResult result;
    UniqueHandle file(CreateFileW(path.c_str(),
                                  FILE_READ_ATTRIBUTES,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr,
                                  OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS,
                                  nullptr));
    if (!file.IsValid()) {
        return result;
    }

    STARTING_VCN_INPUT_BUFFER input{};
    std::vector<unsigned char> output(64 * 1024);

    for (;;) {
        DWORD bytesReturned = 0;
        const BOOL ok = DeviceIoControl(file.Get(),
                                        FSCTL_GET_RETRIEVAL_POINTERS,
                                        &input,
                                        sizeof(input),
                                        output.data(),
                                        static_cast<DWORD>(output.size()),
                                        &bytesReturned,
                                        nullptr);

        const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
        if (!ok && error != ERROR_MORE_DATA) {
            return result;
        }

        if (bytesReturned < offsetof(RETRIEVAL_POINTERS_BUFFER, Extents)) {
            return result;
        }

        const auto* pointers = reinterpret_cast<const RETRIEVAL_POINTERS_BUFFER*>(output.data());
        std::uint64_t currentVcn = static_cast<std::uint64_t>(pointers->StartingVcn.QuadPart);

        for (DWORD index = 0; index < pointers->ExtentCount; ++index) {
            const std::uint64_t nextVcn = static_cast<std::uint64_t>(pointers->Extents[index].NextVcn.QuadPart);
            const std::uint64_t lcn = static_cast<std::uint64_t>(pointers->Extents[index].Lcn.QuadPart);
            const std::uint64_t clusterCount = nextVcn > currentVcn ? nextVcn - currentVcn : 0;

            if (lcn != InvalidLcn && clusterCount > 0) {
                result.fragments.push_back({index64_t(lcn), count64_t(clusterCount)});
            }

            currentVcn = nextVcn;
        }

        result.available = true;

        if (ok) {
            break;
        }

        if (pointers->ExtentCount == 0) {
            break;
        }

        input.StartingVcn.QuadPart = pointers->Extents[pointers->ExtentCount - 1].NextVcn.QuadPart;
    }

    return result;
}

// Reads the volume bitmap in chunks so large drives can be analysed without a huge buffer.
FreeSpaceQueryResult QueryVolumeFreeSpace(const std::wstring& rootPath)
{
    FreeSpaceQueryResult result;
    UniqueHandle volume = OpenVolumeReadOnly(rootPath);
    if (!volume.IsValid()) {
        return result;
    }

    STARTING_LCN_INPUT_BUFFER input{};
    std::vector<unsigned char> output(1024 * 1024);
    std::vector<FreeSpaceMap::FreeBlock> freeBlocks;
    bool inFreeBlock = false;
    std::uint64_t freeBlockStart = 0;
    std::uint64_t freeBlockLength = 0;
    std::uint64_t largestFreeBlock = 0;

    for (;;) {
        DWORD bytesReturned = 0;
        const BOOL ok = DeviceIoControl(volume.Get(),
                                        FSCTL_GET_VOLUME_BITMAP,
                                        &input,
                                        sizeof(input),
                                        output.data(),
                                        static_cast<DWORD>(output.size()),
                                        &bytesReturned,
                                        nullptr);

        const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
        if (!ok && error != ERROR_MORE_DATA) {
            return result;
        }

        if (bytesReturned <= offsetof(VOLUME_BITMAP_BUFFER, Buffer)) {
            return result;
        }

        const auto* bitmap = reinterpret_cast<const VOLUME_BITMAP_BUFFER*>(output.data());
        const std::uint64_t startingLcn = static_cast<std::uint64_t>(bitmap->StartingLcn.QuadPart);
        const std::uint64_t totalBitsFromStart = static_cast<std::uint64_t>(bitmap->BitmapSize.QuadPart);
        const std::uint64_t bytesOfBits = bytesReturned - offsetof(VOLUME_BITMAP_BUFFER, Buffer);
        const std::uint64_t bitsToProcess = MinU64(totalBitsFromStart, bytesOfBits * 8);

        if (bitsToProcess == 0) {
            return result;
        }

        for (std::uint64_t bit = 0; bit < bitsToProcess; ++bit) {
            const unsigned char byte = bitmap->Buffer[bit / 8];
            const bool allocated = (byte & (1u << (bit % 8))) != 0;
            const std::uint64_t lcn = startingLcn + bit;

            if (!allocated) {
                if (!inFreeBlock) {
                    inFreeBlock = true;
                    freeBlockStart = lcn;
                    freeBlockLength = 0;
                }
                ++freeBlockLength;
                continue;
            }

            if (inFreeBlock) {
                freeBlocks.push_back({index64_t(freeBlockStart), count64_t(freeBlockLength)});
                if (freeBlockLength > largestFreeBlock) {
                    largestFreeBlock = freeBlockLength;
                }
                inFreeBlock = false;
                freeBlockLength = 0;
            }
        }

        result.available = true;

        if (ok) {
            break;
        }

        input.StartingLcn.QuadPart = static_cast<LONGLONG>(startingLcn + bitsToProcess);
    }

    if (inFreeBlock) {
        freeBlocks.push_back({index64_t(freeBlockStart), count64_t(freeBlockLength)});
        if (freeBlockLength > largestFreeBlock) {
            largestFreeBlock = freeBlockLength;
        }
    }

    result.freeSpace = FreeSpaceMap(std::move(freeBlocks));
    result.largestFreeBlockSectors = count64_t(largestFreeBlock);
    return result;
}

} // namespace icd::win
