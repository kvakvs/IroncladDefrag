#pragma once

#include "core/model/DriveInfo.h"

#include <vector>

namespace icd::services {

    // Source of the all-disks list. The UI and the CLI depend on this interface only, so Phase 1 runs on a
    // fake provider and Phase 3 swaps in real volume enumeration without touching them.
    class IDriveProvider {
    public:
        virtual ~IDriveProvider() = default;

        // Current volumes in display order (by drive letter).
        [[nodiscard]] virtual std::vector<core::DriveInfo> listDrives() = 0;
    };

} // namespace icd::services
