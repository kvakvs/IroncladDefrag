#include "platform/win/MoveFile.h"

#include "core/snapshot/BitmapToRuns.h"
#include "platform/win/RetrievalPointers.h"
#include "platform/win/TextConvert.h"
#include "platform/win/WinError.h"

#include <winioctl.h>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace icd::platform {

    namespace {

        // One Win32 code and the move error it means.
        struct ErrorMapping {
            DWORD code; // Win32 error.
            MoveError error; // What execution makes of it.
        };

        // The codes FSCTL_MOVE_FILE is known to return. STATUS_ALREADY_COMMITTED (target clusters in use) arrives
        // as ERROR_ACCESS_DENIED, which is why the caller probes the bitmap on AccessDenied.
        constexpr std::array errorMappings{
            ErrorMapping{ERROR_ACCESS_DENIED, MoveError::AccessDenied},
            ErrorMapping{ERROR_SHARING_VIOLATION, MoveError::Locked},
            ErrorMapping{ERROR_LOCK_VIOLATION, MoveError::Locked},
            ErrorMapping{ERROR_USER_MAPPED_FILE, MoveError::Locked},
            ErrorMapping{ERROR_INVALID_PARAMETER, MoveError::Unsupported},
            ErrorMapping{ERROR_NOT_SUPPORTED, MoveError::Unsupported},
            ErrorMapping{ERROR_INVALID_FUNCTION, MoveError::Unsupported},
            ErrorMapping{ERROR_INVALID_DATA, MoveError::Unsupported},
            ErrorMapping{ERROR_FILE_NOT_FOUND, MoveError::FileGone},
            ErrorMapping{ERROR_INVALID_HANDLE, MoveError::FileGone},
            ErrorMapping{ERROR_FILE_INVALID, MoveError::FileGone},
            ErrorMapping{ERROR_DELETE_PENDING, MoveError::FileGone},
            ErrorMapping{ERROR_HANDLE_EOF, MoveError::FileGone}, // The range is past the end: the file shrank.
            ErrorMapping{ERROR_RETRY, MoveError::Retry},
            ErrorMapping{ERROR_NOT_READY, MoveError::Retry},
        };

        // Bitmap bytes asked for per query of freeRunsIn() (32 M clusters).
        constexpr std::size_t bitmapPieceBytes = std::size_t{4} << 20U;

        // Bytes before the bitmap in VOLUME_BITMAP_BUFFER (StartingLcn, BitmapSize).
        constexpr std::size_t bitmapHeaderBytes = offsetof(VOLUME_BITMAP_BUFFER, Buffer);

        // One bitmap query from `start`; the chunk it returns (empty past the end of the volume).
        core::BitmapChunk queryBitmap(HANDLE volume, core::Lcn start, std::vector<std::byte>& buffer) {
            STARTING_LCN_INPUT_BUFFER in{};
            in.StartingLcn.QuadPart = static_cast<LONGLONG>(start);
            DWORD bytes = 0;
            if (DeviceIoControl(volume, FSCTL_GET_VOLUME_BITMAP, &in, sizeof(in), buffer.data(),
                                static_cast<DWORD>(buffer.size()), &bytes, nullptr) == FALSE &&
                GetLastError() != ERROR_MORE_DATA) {
                throw std::runtime_error("FSCTL_GET_VOLUME_BITMAP failed: " + toUtf8(lastWinError()));
            }
            const auto* header = reinterpret_cast<const VOLUME_BITMAP_BUFFER*>(buffer.data());
            const std::size_t bitmapBytes = bytes > bitmapHeaderBytes ? bytes - bitmapHeaderBytes : 0;
            const auto remaining = static_cast<core::ClusterCount>(header->BitmapSize.QuadPart);
            return {static_cast<core::Lcn>(header->StartingLcn.QuadPart),
                    std::min<core::ClusterCount>(remaining, bitmapBytes * 8),
                    std::span(reinterpret_cast<const std::uint8_t*>(buffer.data() + bitmapHeaderBytes), bitmapBytes)};
        }

        // Appends the runs of `chunk` clipped to `range`.
        void appendClipped(const core::BitmapChunk& chunk, core::ClusterRun range, std::vector<core::ClusterRun>& out) {
            std::vector<core::ClusterRun> runs;
            for (const core::ClusterRun& r : core::freeRunsOf(chunk)) {
                const core::Lcn start = std::max(r.start, range.start);
                const core::Lcn end = std::min(r.end(), range.end());
                if (start < end) {
                    runs.push_back({start, end - start});
                }
            }
            core::stitchRuns(out, runs);
        }

    } // namespace

    std::string_view moveErrorKey(MoveError error) {
        constexpr std::array<std::string_view, 8> keys{"none",        "destination-in-use", "access-denied", "locked",
                                                       "unsupported", "file-gone",          "retry",         "failed"};
        return keys.at(static_cast<std::size_t>(error));
    }

    MoveError classifyMoveError(unsigned long win32Error) {
        if (win32Error == ERROR_SUCCESS) {
            return MoveError::None;
        }
        const auto found = std::ranges::find(errorMappings, win32Error, &ErrorMapping::code);
        return found != errorMappings.end() ? found->error : MoveError::Failed;
    }

    UniqueHandle openVolumeForMoves(const std::wstring& volumeName) {
        std::wstring device = volumeName;
        if (device.size() == 3 && device[1] == L':') {
            device = L"\\\\.\\" + device.substr(0, 2);
        }
        while (!device.empty() && device.back() == L'\\') {
            device.pop_back(); // A trailing backslash would open the root directory, not the volume.
        }
        return UniqueHandle(CreateFileW(device.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                        OPEN_EXISTING, 0, nullptr));
    }

    MoveCallResult moveClusters(const MoveHandles& handles, const ClusterMove& move) {
        MOVE_FILE_DATA in{};
        in.FileHandle = handles.stream;
        in.StartingVcn.QuadPart = static_cast<LONGLONG>(move.vcn);
        in.StartingLcn.QuadPart = static_cast<LONGLONG>(move.lcn);
        in.ClusterCount = move.clusters;
        DWORD bytes = 0;
        if (DeviceIoControl(handles.volume, FSCTL_MOVE_FILE, &in, sizeof(in), nullptr, 0, &bytes, nullptr) != FALSE) {
            return {};
        }
        const DWORD code = GetLastError();
        return {classifyMoveError(code), code};
    }

    ChunkedMoveResult moveClustersChunked(const MoveHandles& handles, const ChunkedMove& move,
                                          const std::function<bool()>& keepGoing) {
        ChunkedMoveResult out;
        const std::uint64_t chunk = std::clamp<std::uint64_t>(move.chunkClusters, 1, 0x8000'0000ULL);
        while (out.clustersMoved < move.clusters) {
            if (out.clustersMoved != 0 && keepGoing && !keepGoing()) {
                out.stopped = true;
                return out;
            }
            const std::uint64_t n = std::min(chunk, move.clusters - out.clustersMoved);
            out.last = moveClusters(
                handles, {move.vcn + out.clustersMoved, move.lcn + out.clustersMoved, static_cast<std::uint32_t>(n)});
            if (out.last.error != MoveError::None) {
                return out;
            }
            out.clustersMoved += n;
        }
        return out;
    }

    std::uint64_t fileReferenceOf(HANDLE file) {
        BY_HANDLE_FILE_INFORMATION info{};
        if (GetFileInformationByHandle(file, &info) == FALSE) {
            return 0;
        }
        return (std::uint64_t{info.nFileIndexHigh} << 32U) | info.nFileIndexLow;
    }

    std::wstring finalPathOf(HANDLE file) {
        std::wstring path(MAX_PATH, L'\0');
        for (int attempt = 0; attempt < 2; ++attempt) {
            const DWORD n = GetFinalPathNameByHandleW(file, path.data(), static_cast<DWORD>(path.size()),
                                                      FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
            if (n == 0) {
                return {};
            }
            if (n < path.size()) {
                path.resize(n);
                return path;
            }
            path.resize(std::size_t{n} + 1); // n is the size needed, terminator included.
        }
        return {};
    }

    UniqueHandle openStreamForMove(HANDLE file, StreamId stream) {
        if (stream.ordinal == 0) {
            HANDLE copy = nullptr;
            if (DuplicateHandle(GetCurrentProcess(), file, GetCurrentProcess(), &copy, 0, FALSE,
                                DUPLICATE_SAME_ACCESS) == FALSE) {
                return {};
            }
            return UniqueHandle(copy);
        }
        FileFacts facts;
        const std::wstring path = finalPathOf(file);
        if (path.empty() || !readFileFacts(file, facts) || stream.ordinal > facts.namedStreams.size()) {
            return {};
        }
        UniqueHandle named = openForQuery(path + L":" + facts.namedStreams[stream.ordinal - 1U]);
        if (!named || fileReferenceOf(named.get()) != stream.fileReference) {
            return {}; // Renamed or replaced between the two opens: not the file the plan means.
        }
        return named;
    }

    std::vector<core::ClusterRun> freeRunsIn(HANDLE volume, core::ClusterRun range) {
        std::vector<core::ClusterRun> out;
        std::vector<std::byte> buffer(bitmapHeaderBytes + bitmapPieceBytes);
        for (core::Lcn at = range.start; at < range.end();) {
            const core::BitmapChunk chunk = queryBitmap(volume, at, buffer);
            if (chunk.clusters == 0) {
                break; // Past the end of the volume.
            }
            appendClipped(chunk, range, out);
            at = chunk.startLcn + chunk.clusters;
        }
        return out;
    }

    bool setBackgroundIo(bool background) {
        return SetThreadPriority(GetCurrentThread(),
                                 background ? THREAD_MODE_BACKGROUND_BEGIN : THREAD_MODE_BACKGROUND_END) != FALSE;
    }

} // namespace icd::platform
