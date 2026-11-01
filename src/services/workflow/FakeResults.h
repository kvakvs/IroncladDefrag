#pragma once

#include "core/model/DriveInfo.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace icd::services {

    // Made-up results for the Phase 1 UI shell, so the Summary, Strategy and Plan tabs can be laid out and
    // reviewed before analysis and planning exist. Values are derived from the drive, so the same drive
    // always shows the same numbers. Real result types replace these in Phases 3-6.

    // Effort levels of the planner (plan.md 9.2).
    enum class EffortLevel : std::uint8_t { Fast, Moderate, Maximum };

    // Display name of an effort level.
    [[nodiscard]] const char* effortName(EffortLevel level);

    // One file class on the Summary tab (plan.md 8.2 classes).
    struct ClassSummary {
        std::string name; // Class name, e.g. "Photos".
        std::uint64_t files = 0; // Files in the class.
        std::uint64_t bytes = 0; // Bytes in the class.
        double inZonePercent = 0.0; // Share of the class already in its target zone.
    };

    // What an analysis reports (Summary tab).
    struct AnalysisSummary {
        double fragmentationPercent = 0.0; // Share of files with more than one fragment.
        std::uint64_t files = 0; // Files on the volume.
        std::uint64_t fragmentedFiles = 0; // Files with more than one fragment.
        std::uint64_t fragments = 0; // Extra fragments of those files.
        std::uint64_t freeBytes = 0; // Free space.
        std::uint64_t freeRuns = 0; // Separate free-space runs.
        std::uint64_t largestFreeRun = 0; // Bytes in the largest free run.
        std::vector<ClassSummary> classes; // Per-class counts.

        // Figures only a real scan has; empty for the fake and synthetic stand-ins, so the tab hides them.
        struct ScanFigures {
            std::uint64_t directories = 0; // Directories.
            double fragmentedBytesPercent = 0.0; // Share of file bytes in fragmented files.
            double largeRunFreePercent = 0.0; // Share of free space in runs of 64 MB or more.
            std::uint64_t unmovableBytes = 0; // Metadata, pinned files and unreadable areas.
            std::uint64_t unreadableFiles = 0; // Files that could not be opened.
            std::uint64_t changedDuringScan = 0; // Extents dropped because their file changed during the scan.
        };
        std::optional<ScanFigures> scan; // Present after a real analysis.
    };

    // One zone of a strategy preset (plan.md 8.1), in disk order.
    struct StrategyZone {
        std::string name; // Zone name, e.g. "Hot files".
        std::string classes; // File classes placed there.
        int startPercent = 0; // Start as a share of the volume.
        int endPercent = 0; // End as a share of the volume.
    };

    // A strategy preset for the Strategy tab picker.
    struct StrategyPreset {
        std::string name; // Shown in the picker.
        std::vector<StrategyZone> zones; // Zone list.
    };

    // Result of one effort level (Plan tab columns, plan.md 9.2.2).
    struct PlanLevel {
        EffortLevel level = EffortLevel::Fast; // Which level.
        std::uint64_t filesToMove = 0; // Files the plan moves.
        std::uint64_t bytesToMove = 0; // Bytes the plan moves.
        std::uint64_t lowerBoundBytes = 0; // Least bytes any plan must move to reach the target.
        std::chrono::seconds estimate{}; // Estimated execution time.
        double fragmentationAfter = 0.0; // Fragmentation percent after execution.
        double zoneFitAfter = 0.0; // Percent of bytes in their target zone after execution.
    };

    // What planning reports (Plan tab).
    struct PlanSummary {
        std::array<PlanLevel, 3> levels; // Fast, Moderate, Maximum.
        std::vector<std::string> warnings; // VSS, low free space, skipped files.
    };

    // The presets, default (Mixed data drive) first.
    [[nodiscard]] const std::vector<StrategyPreset>& strategyPresets();

    // Fake analysis of a drive.
    [[nodiscard]] AnalysisSummary fakeAnalysis(const core::DriveInfo& drive);

    // Fake plan for a drive, its analysis and a preset index.
    [[nodiscard]] PlanSummary fakePlan(const core::DriveInfo& drive, const AnalysisSummary& analysis,
                                       std::size_t preset);

    // Fake files that could not be moved, as (path, reason), for the Log tab after execution.
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> fakeSkippedFiles(const core::DriveInfo& drive);

} // namespace icd::services
