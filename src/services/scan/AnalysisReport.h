#pragma once

#include "core/model/DriveInfo.h"
#include "services/scan/AnalysisJob.h"

#include <string>

namespace icd::services {

    // The report of `icd-cli analyze --json`: volume facts (sizes, geometry, disk placement, NTFS data, media),
    // the scan (time, USN cursor, unreadable files, corrections) and the summary the GUI's Summary tab shows, from
    // the same AnalysisSummary. With `dumpExtents`, every file that owns clusters with its runs.
    [[nodiscard]] std::string analysisJson(const core::DriveInfo& drive, const AnalysisResult& result,
                                           bool dumpExtents);

    // The same figures as plain text, one per line, for people at a console.
    [[nodiscard]] std::string analysisText(const core::DriveInfo& drive, const AnalysisResult& result);

    // Names of the file flags set in `flags` ("directory", "compressed", ...), in flag order.
    [[nodiscard]] std::vector<std::string> fileFlagNames(std::uint16_t flags);

} // namespace icd::services
