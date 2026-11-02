#include "services/workflow/FakeResults.h"

#include <algorithm>
#include <format>

namespace icd::services {

    namespace {

        constexpr std::uint64_t KB = 1024;
        constexpr std::uint64_t MB = 1024 * KB;

        // Disk speed assumed for time estimates (read and write each move once).
        constexpr std::uint64_t bytesPerSecond = 120 * MB;

        // Share, average file size and in-zone share of one fake class.
        struct ClassShape {
            const char* name = ""; // Class name.
            double share = 0.0; // Share of used bytes.
            std::uint64_t averageSize = 1; // Average file size.
            double inZone = 0.0; // Percent already in the target zone.
        };

        constexpr std::array<ClassShape, 8> classShapes{{
            {"System and metadata", 0.02, 64 * KB, 90.0},
            {"Programs", 0.06, 400 * KB, 55.0},
            {"Documents", 0.04, 300 * KB, 40.0},
            {"Development", 0.08, 40 * KB, 30.0},
            {"Photos", 0.22, 6 * MB, 62.0},
            {"Video", 0.38, 900 * MB, 48.0},
            {"Archives and images", 0.15, 200 * MB, 70.0},
            {"Other", 0.05, MB, 20.0},
        }};

        // bytes scaled by a factor, rounded down.
        std::uint64_t scaled(std::uint64_t bytes, double factor) {
            return static_cast<std::uint64_t>(static_cast<double>(bytes) * factor);
        }

        // Fragmentation of the drive, or a stable made-up value when it was never analysed.
        double fragmentationOf(const core::DriveInfo& drive) {
            const int letter = drive.root.empty() ? 0 : static_cast<unsigned char>(drive.root.front());
            return drive.fragmentationPercent.value_or(8.0 + (letter % 7) * 3.0);
        }

        // What every level of a fake plan is scaled from.
        struct LevelBase {
            std::uint64_t fastBytes = 0; // Bytes the Fast level moves.
            double fragmentation = 0.0; // Fragmentation percent before the plan.
        };

        // One effort level of the fake plan; effort grows the moved bytes and improves the result.
        PlanLevel makeLevel(EffortLevel level, LevelBase base) {
            const std::uint64_t fastBytes = base.fastBytes;
            const double fragmentation = base.fragmentation;
            constexpr std::array moveFactor{1.0, 2.6, 5.5};
            constexpr std::array fragmentationLeft{0.3, 0.08, 0.01};
            constexpr std::array zoneFit{68.0, 84.0, 97.0};
            const auto index = static_cast<std::size_t>(level);
            PlanLevel result;
            result.level = level;
            result.bytesToMove = scaled(fastBytes, moveFactor.at(index));
            result.filesToMove = std::max<std::uint64_t>(1, result.bytesToMove / (8 * MB));
            result.lowerBoundBytes = scaled(fastBytes, 0.8);
            result.estimate = std::chrono::seconds(2 * result.bytesToMove / bytesPerSecond);
            result.fragmentationAfter = fragmentation * fragmentationLeft.at(index);
            result.zoneFitAfter = zoneFit.at(index);
            return result;
        }

    } // namespace

    const char* effortName(EffortLevel level) {
        constexpr std::array names{"Fast", "Moderate", "Maximum"};
        return names.at(static_cast<std::size_t>(level));
    }

    const std::vector<StrategyPreset>& strategyPresets() {
        static const std::vector<StrategyPreset> presets{
            {"Mixed data drive (default)",
             {{"Metadata and hot files", "System, programs, recent documents", 0, 8},
              {"Working set", "Documents, development, photos", 8, 45},
              {"Large media", "Video, archives", 45, 90},
              {"Cold", "Rarely used files", 90, 100}}},
            {"Development storage",
             {{"Hot sources", "Sources, .git packs", 0, 15},
              {"Build output", "Objects, caches (kept apart)", 15, 40},
              {"Packages", "node_modules, SDKs", 40, 80},
              {"Cold", "Old branches, archives", 80, 100}}},
            {"Archive and disk images",
             {{"Indexes", "Catalogs, small files", 0, 5}, {"Images", "ISO, VHDX, zips (sequential)", 5, 100}}},
            {"Photo storage",
             {{"Sidecars", "XMP, catalogs", 0, 5}, {"Photos by date", "RAW and JPEG, dated folders", 5, 100}}},
            {"Video storage", {{"Projects", "Edit files, proxies", 0, 10}, {"Footage", "Large video files", 10, 100}}},
            {"Defragment files only", {{"Whole volume", "Every class, in place", 0, 100}}},
        };
        return presets;
    }

    AnalysisSummary fakeAnalysis(const core::DriveInfo& drive) {
        const std::uint64_t used = drive.usedBytes();
        AnalysisSummary summary;
        summary.fragmentationPercent = fragmentationOf(drive);
        summary.freeBytes = drive.freeBytes;
        summary.freeRuns = std::max<std::uint64_t>(1, drive.freeBytes / (64 * MB));
        summary.largestFreeRun = drive.freeBytes / 9;
        for (const ClassShape& shape : classShapes) {
            const std::uint64_t bytes = scaled(used, shape.share);
            const std::uint64_t files = std::max<std::uint64_t>(1, bytes / shape.averageSize);
            summary.classes.push_back(
                {.id = shape.name, .name = shape.name, .files = files, .bytes = bytes, .inZonePercent = shape.inZone});
            summary.files += files;
        }
        summary.fragmentedFiles = scaled(summary.files, summary.fragmentationPercent / 100.0);
        summary.fragments = summary.fragmentedFiles * 6;
        return summary;
    }

    PlanSummary fakePlan(const core::DriveInfo& drive, const AnalysisSummary& analysis, std::size_t preset) {
        const double presetFactor = 1.0 + 0.1 * static_cast<double>(preset);
        const std::uint64_t fastBytes =
            scaled(drive.usedBytes(), analysis.fragmentationPercent / 100.0 * 0.35 * presetFactor);
        PlanSummary plan;
        const LevelBase base{.fastBytes = fastBytes, .fragmentation = analysis.fragmentationPercent};
        plan.levels = {makeLevel(EffortLevel::Fast, base), makeLevel(EffortLevel::Moderate, base),
                       makeLevel(EffortLevel::Maximum, base)};
        if (drive.badges.vssPresent) {
            plan.warnings.emplace_back("Shadow copies exist: moved clusters make them grow (VSS).");
        }
        const double freePercent = 100.0 * (1.0 - drive.usedFraction());
        if (freePercent < 10.0) {
            plan.warnings.push_back(
                std::format("Low free space ({:.1f}%): some files may stay where they are.", freePercent));
        }
        plan.warnings.emplace_back("3 files will be skipped: in use or locked.");
        return plan;
    }

    std::vector<std::pair<std::string, std::string>> fakeSkippedFiles(const core::DriveInfo& drive) {
        const std::string root = drive.root;
        return {
            {root + "pagefile.sys", "In use by the system"},
            {root + "Data\\mail\\outlook.pst", "Locked by another process"},
            {root + "Projects\\build\\app.pdb", "Changed after the analysis; skipped for safety"},
        };
    }

} // namespace icd::services
