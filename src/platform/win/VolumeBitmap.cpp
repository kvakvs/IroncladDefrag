#include "platform/win/VolumeBitmap.h"

#include "platform/win/TextConvert.h"
#include "platform/win/WinError.h"

#include <winioctl.h>

#include <stdexcept>
#include <vector>

namespace icd::platform {

    namespace {

        // Bytes before the bitmap in VOLUME_BITMAP_BUFFER (StartingLcn, BitmapSize).
        constexpr std::size_t headerBytes = offsetof(VOLUME_BITMAP_BUFFER, Buffer);

        // Asks for the bitmap from `start`; returns false when this was the last piece.
        bool readPiece(HANDLE volume, core::Lcn start, std::vector<std::byte>& buffer, DWORD& bytes) {
            STARTING_LCN_INPUT_BUFFER in{};
            in.StartingLcn.QuadPart = static_cast<LONGLONG>(start);
            bytes = 0;
            if (DeviceIoControl(volume, FSCTL_GET_VOLUME_BITMAP, &in, sizeof(in), buffer.data(),
                                static_cast<DWORD>(buffer.size()), &bytes, nullptr) != FALSE) {
                return false;
            }
            if (GetLastError() == ERROR_MORE_DATA) {
                return true;
            }
            throw std::runtime_error("FSCTL_GET_VOLUME_BITMAP failed: " + toUtf8(lastWinError()));
        }

    } // namespace

    void readVolumeBitmap(HANDLE volume, std::size_t chunkBytes,
                          const std::function<void(const core::BitmapChunk&)>& onChunk,
                          const core::CancelToken& token) {
        std::vector<std::byte> buffer(headerBytes + std::max<std::size_t>(chunkBytes, 8));
        core::Lcn start = 0;
        for (bool more = true; more;) {
            token.throwIfCancelled();
            DWORD bytes = 0;
            more = readPiece(volume, start, buffer, bytes);
            const auto* header = reinterpret_cast<const VOLUME_BITMAP_BUFFER*>(buffer.data());
            const auto first = static_cast<core::Lcn>(header->StartingLcn.QuadPart);
            const auto remaining = static_cast<core::ClusterCount>(header->BitmapSize.QuadPart);
            const std::size_t bitmapBytes = bytes > headerBytes ? bytes - headerBytes : 0;
            const core::ClusterCount clusters = std::min<core::ClusterCount>(remaining, bitmapBytes * 8);
            if (clusters == 0) {
                break;
            }
            onChunk({first, clusters,
                     std::span(reinterpret_cast<const std::uint8_t*>(buffer.data() + headerBytes), bitmapBytes)});
            start = first + clusters;
        }
    }

    core::FreeRunList readFreeRuns(HANDLE volume, core::ClusterCount totalClusters, core::Executor& executor,
                                   const core::CancelToken& token, const BitmapProgress& progress) {
        constexpr std::size_t chunkBytes = 32U << 20U;
        std::vector<core::ClusterRun> runs;
        readVolumeBitmap(
            volume, chunkBytes,
            [&](const core::BitmapChunk& chunk) {
                core::stitchRuns(runs, core::freeRunsParallel(executor, chunk, 1U << 20U, token));
                if (progress) {
                    progress(chunk.startLcn + chunk.clusters, totalClusters);
                }
            },
            token);
        return {std::move(runs), totalClusters};
    }

} // namespace icd::platform
