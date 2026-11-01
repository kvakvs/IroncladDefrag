#pragma once

#include "core/model/DriveInfo.h"

#include <chrono>
#include <string>
#include <vector>

namespace icd::core {

    // Texts for the all-disks panel columns. Kept in core, not in the UI, so the wording is tested and the
    // CLI can print the same list.

    // "D: Data", or "D:" without a label.
    std::string driveName(const DriveInfo& drive);

    // "1.10 TB free of 4.00 TB".
    std::string describeSpace(const DriveInfo& drive);

    // "HDD", "SSD", "Virtual" or "Unknown".
    std::string describeMedia(MediaType media);

    // Media column: the media type with the bus and a removable note, e.g. "HDD · SATA".
    std::string describeStorage(const DriveInfo& drive);

    // "12.4%" after an analysis, "-" before one, "not supported" for non-NTFS volumes, empty for SSDs
    // (they are never analysed).
    std::string describeFragmentation(const DriveInfo& drive);

    // "3 h ago" or "never"; empty for SSDs and unsupported volumes.
    std::string describeLastAnalysed(const DriveInfo& drive, std::chrono::system_clock::time_point now);

    // Badge texts in display order, e.g. {"Admin needed", "VSS", "Last access off", "Snapshot yesterday"}.
    std::vector<std::string> badgeLabels(const DriveInfo& drive, std::chrono::system_clock::time_point now);

} // namespace icd::core
