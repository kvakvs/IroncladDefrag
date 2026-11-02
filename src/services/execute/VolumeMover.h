#pragma once

#include "platform/win/Handle.h"
#include "services/execute/MoveDevice.h"

#include <string>

namespace icd::services {

    // The move device of a real NTFS volume (plan.md 10.4): files opened by id, streams by the scanners' ordinals,
    // moves through FSCTL_MOVE_FILE, the bitmap read per range. Needs administrator rights.
    class VolumeMover final : public MoveDevice {
    public:
        // Opens the volume ("\\?\Volume{GUID}\" or "T:\"); throws std::runtime_error when it cannot.
        explicit VolumeMover(const std::wstring& volumeName);

        // See MoveDevice: OpenFileById with FILE_READ_ATTRIBUTES (a reused record has another sequence and fails).
        [[nodiscard]] std::unique_ptr<MovableFile> open(std::uint64_t fileReference, std::string& why) override;

        // See MoveDevice.
        [[nodiscard]] std::vector<core::ClusterRun> freeRunsIn(core::ClusterRun range) override;

    private:
        platform::UniqueHandle volume_; // Opened for moves.
    };

} // namespace icd::services
