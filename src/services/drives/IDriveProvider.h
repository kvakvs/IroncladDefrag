#pragma once

#include "core/model/DriveInfo.h"

#include <string>
#include <vector>

namespace icd::services {

    // Source of the all-disks list. The UI and the CLI depend on this interface only, so Phase 1 runs on a
    // fake provider and Phase 3 swaps in real volume enumeration without touching them.
    class IDriveProvider {
    public:
        virtual ~IDriveProvider() = default;

        // Current volumes in display order (by drive letter).
        [[nodiscard]] virtual std::vector<core::DriveInfo> listDrives() = 0;

        // Takes the drive out of Windows' scheduled optimization (scheduled false) or puts it back (true), which the
        // user confirmed first (plan.md 10.4: the schedule may undo zone layouts). Returns an error text, empty on
        // success; the next listDrives() shows the new badge.
        [[nodiscard]] virtual std::string setScheduledOptimization(const core::DriveInfo& drive, bool scheduled) = 0;
    };

} // namespace icd::services
