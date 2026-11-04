#pragma once

#include "services/drives/IDriveProvider.h"

#include <chrono>
#include <set>
#include <string>

namespace icd::services {

    // Made-up drives for the UI shell (plan.md Phase 1): an SSD system drive, three NTFS HDDs in different
    // states (never analysed, analysed with a snapshot, nearly full), one exFAT drive the app cannot
    // optimize and a 30 TB drive at the design target, so every row style and the map at full scale can be
    // seen and reviewed.
    class FakeDriveProvider : public IDriveProvider {
    public:
        // Ages ("3 h ago") are relative to now, so tests can pin it.
        explicit FakeDriveProvider(std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

        // The fixed fake list, with the schedule changes made so far.
        [[nodiscard]] std::vector<core::DriveInfo> listDrives() override;

        // Remembers the change for the next listing (drives whose schedule badge is unknown are refused).
        [[nodiscard]] std::string setScheduledOptimization(const core::DriveInfo& drive, bool scheduled) override;

    private:
        std::chrono::system_clock::time_point now_; // Reference time for analysis and snapshot ages.
        std::set<std::string> unscheduled_; // Roots taken out of the simulated Windows schedule.
    };

} // namespace icd::services
