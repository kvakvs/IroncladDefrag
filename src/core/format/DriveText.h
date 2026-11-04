#pragma once

#include "core/model/DriveInfo.h"

#include <chrono>
#include <string>
#include <string_view>
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

    // Short name of a move block for the badge: "RAID", "Storage Spaces", "dynamic disk", "removable", "BitLocker".
    std::string_view blockName(MoveBlock block);

    // Why a move block stops execution, as a sentence fragment ("it sits behind a RAID controller, ...").
    std::string_view blockReason(MoveBlock block);

    // The explanation shown where Execute would be: "Moves are blocked on this drive: <reasons>. ..."; empty when
    // moves are allowed (or the drive is not optimizable at all).
    std::string movesBlockedText(const DriveInfo& drive);

    // Badge texts in display order, e.g. {"No moves (RAID)", "Admin needed", "VSS", "Last access off",
    // "Snapshot yesterday"}.
    std::vector<std::string> badgeLabels(const DriveInfo& drive, std::chrono::system_clock::time_point now);

} // namespace icd::core
