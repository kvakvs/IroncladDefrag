#pragma once

#include "services/drives/IDriveProvider.h"

#include <chrono>

namespace icd::services {

    // Made-up drives for the UI shell (plan.md Phase 1): an SSD system drive, three NTFS HDDs in different
    // states (never analysed, analysed with a snapshot, nearly full) and one exFAT drive the app cannot
    // optimize, so every row style can be seen and reviewed.
    class FakeDriveProvider : public IDriveProvider {
    public:
        // Ages ("3 h ago") are relative to now, so tests can pin it.
        explicit FakeDriveProvider(std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

        // The fixed fake list.
        [[nodiscard]] std::vector<core::DriveInfo> listDrives() override;

    private:
        std::chrono::system_clock::time_point now_; // Reference time for analysis and snapshot ages.
    };

} // namespace icd::services
