#pragma once

#include "core/snapshot/Types.h"
#include "platform/win/Handle.h"

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace icd::platform {

    // Why a cluster move failed, in the terms execution acts on (plan.md 10.4): retry elsewhere, skip the file with a
    // reason, or give up on the file because the file system will not move it.
    enum class MoveError : std::uint8_t {
        None, // Moved.
        DestinationInUse, // Some target cluster is in use now (a stale bitmap): pick another destination or skip.
        AccessDenied, // Refused; NTFS also says this when the target is taken, so callers probe the bitmap first.
        Locked, // The file is in use in a way that blocks moves (mapped, locked ranges, paging file).
        Unsupported, // The file system will not move this stream or range (bad alignment, special file).
        FileGone, // The file was deleted or replaced since it was opened.
        Retry, // A transient refusal; the same call may work again.
        Failed, // Anything else.
    };

    // Stable name of a move error ("destination-in-use", ...) for logs and skip reasons.
    [[nodiscard]] std::string_view moveErrorKey(MoveError error);

    // The move error a Win32 error code of FSCTL_MOVE_FILE stands for.
    [[nodiscard]] MoveError classifyMoveError(unsigned long win32Error);

    // One FSCTL_MOVE_FILE request: `clusters` virtual clusters of a stream from `vcn` on to `lcn`.
    struct ClusterMove {
        std::uint64_t vcn = 0; // First virtual cluster of the stream to move.
        core::Lcn lcn = 0; // Where it goes.
        std::uint32_t clusters = 0; // Virtual clusters to move (FSCTL_MOVE_FILE takes 32 bits).
    };

    // What one move call returned.
    struct MoveCallResult {
        MoveError error = MoveError::None; // Mapped error.
        unsigned long win32Error = 0; // The raw code, for the log.
    };

    // Opens a volume for moves (plan.md 10.4): read access, sharing read and write, so other programs keep working.
    // Needs administrator rights; invalid on failure.
    [[nodiscard]] UniqueHandle openVolumeForMoves(const std::wstring& volumeName);

    // The two handles a move needs, named so the volume and the stream cannot be swapped.
    struct MoveHandles {
        HANDLE volume = INVALID_HANDLE_VALUE; // From openVolumeForMoves().
        HANDLE stream = INVALID_HANDLE_VALUE; // Opened with FILE_READ_ATTRIBUTES (openFileById(), openStreamForMove()).
    };

    // Moves one range of one stream with FSCTL_MOVE_FILE; NTFS copies the data and switches the run atomically.
    [[nodiscard]] MoveCallResult moveClusters(const MoveHandles& handles, const ClusterMove& move);

    // A range moved in several calls.
    struct ChunkedMove {
        std::uint64_t vcn = 0; // First virtual cluster.
        core::Lcn lcn = 0; // Where it goes.
        std::uint64_t clusters = 0; // Virtual clusters in all.
        std::uint64_t chunkClusters = 4096; // Largest call (the executor uses 16 MB).
    };

    // How a chunked move ended.
    struct ChunkedMoveResult {
        std::uint64_t clustersMoved = 0; // Virtual clusters moved before stopping.
        MoveCallResult last; // The failing call, or a success.
        bool stopped = false; // keepGoing said no.
    };

    // Moves a range in calls of at most `chunkClusters`, so cancel and pause respond between calls; `keepGoing`
    // (optional) is asked before every call after the first. Stops at the first failure.
    [[nodiscard]] ChunkedMoveResult moveClustersChunked(const MoveHandles& handles, const ChunkedMove& move,
                                                        const std::function<bool()>& keepGoing = {});

    // One stream of one file: the scanners' ordinal (services/scan/ScanRules): 0 is the main data stream (a
    // directory's index), 1..n the named data streams in FileStreamInfo order.
    struct StreamId {
        std::uint64_t fileReference = 0; // NTFS file reference (record and sequence).
        std::uint16_t ordinal = 0; // Stream ordinal.
    };

    // Opens a stream of a file for moves. `file` is the file opened by id; a named stream is opened by its path and
    // must be the same file (file id compared), so a rename in between cannot redirect the move. Invalid when the
    // stream does not exist or cannot be opened.
    [[nodiscard]] UniqueHandle openStreamForMove(HANDLE file, StreamId stream);

    // The NTFS file reference (record and sequence) of an open file, or 0 when it cannot be read.
    [[nodiscard]] std::uint64_t fileReferenceOf(HANDLE file);

    // Path of an open file as "\\?\T:\dir\name", or empty when it cannot be resolved.
    [[nodiscard]] std::wstring finalPathOf(HANDLE file);

    // The free runs inside `range` from the volume bitmap now (one FSCTL_GET_VOLUME_BITMAP query for the range): a
    // stale-bitmap check before a destination is chosen again. Throws std::runtime_error on failure.
    [[nodiscard]] std::vector<core::ClusterRun> freeRunsIn(HANDLE volume, core::ClusterRun range);

    // Lowers the calling thread's I/O priority for moves (plan.md 10.4, optional low priority), or restores it.
    // Returns false when Windows refuses.
    bool setBackgroundIo(bool background);

} // namespace icd::platform
