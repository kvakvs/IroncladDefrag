#pragma once

#include "core/execute/MoveCalls.h"
#include "core/execute/MovePieces.h"
#include "platform/win/MoveFile.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace icd::services {

    // A file opened for moves on a move device.
    class MovableFile {
    public:
        MovableFile() = default;
        MovableFile(const MovableFile&) = delete;
        MovableFile& operator=(const MovableFile&) = delete;
        MovableFile(MovableFile&&) = delete;
        MovableFile& operator=(MovableFile&&) = delete;
        virtual ~MovableFile() = default;

        // The file's allocated runs now, in (stream, VCN) order over its main stream (or directory index) and named
        // streams; false when they cannot be read.
        [[nodiscard]] virtual bool readRuns(std::vector<core::execute::StreamRun>& runs) = 0;

        // One move call (FSCTL_MOVE_FILE on the file system).
        [[nodiscard]] virtual platform::MoveCallResult move(const core::execute::MoveCall& call) = 0;
    };

    // What the executor moves files on (plan.md 10.4): the volume through FSCTL_MOVE_FILE (VolumeMover), or a volume
    // in memory in tests. open() may be called from a helper thread while a move runs on another file.
    class MoveDevice {
    public:
        MoveDevice() = default;
        MoveDevice(const MoveDevice&) = delete;
        MoveDevice& operator=(const MoveDevice&) = delete;
        MoveDevice(MoveDevice&&) = delete;
        MoveDevice& operator=(MoveDevice&&) = delete;
        virtual ~MoveDevice() = default;

        // Opens a file by NTFS file reference (a reused record is another file and fails); null with `why` set
        // ("file-gone", "open-failed") when it cannot be opened.
        [[nodiscard]] virtual std::unique_ptr<MovableFile> open(std::uint64_t fileReference, std::string& why) = 0;

        // The free runs inside a range now (from the bitmap): the stale-bitmap check (plan.md 10.4).
        [[nodiscard]] virtual std::vector<core::ClusterRun> freeRunsIn(core::ClusterRun range) = 0;
    };

} // namespace icd::services
