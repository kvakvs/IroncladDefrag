#pragma once

#include "core/map/MapData.h"
#include "core/model/DriveInfo.h"
#include "core/snapshot/SnapshotIO.h"
#include "core/snapshot/SnapshotStats.h"
#include "services/job/JobRunner.h"
#include "services/scan/ApiScanner.h"
#include "services/scan/MftScanner.h"
#include "services/workflow/FakeResults.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace icd::services {

    // Where an analysis leaves its results. The job writes on a worker; the UI takes them on its own thread when
    // progress or the end arrives. Maps come more than once (after the bitmap, while files are read, final); only
    // the newest one waits to be taken.
    struct AnalysisOutput {
        // Stores the newest map (worker side).
        void set(std::shared_ptr<const core::MapData> data) {
            const std::scoped_lock lock(mutex);
            map = std::move(data);
        }

        // Takes the newest map (UI side); null when none arrived since the last take.
        [[nodiscard]] std::shared_ptr<const core::MapData> take() {
            const std::scoped_lock lock(mutex);
            return std::exchange(map, nullptr);
        }

        // Stores the final figures (worker side).
        void setSummary(AnalysisSummary value) {
            const std::scoped_lock lock(mutex);
            summary = std::move(value);
        }

        // Takes the final figures (UI side); empty when the job did not finish.
        [[nodiscard]] std::optional<AnalysisSummary> takeSummary() {
            const std::scoped_lock lock(mutex);
            return std::exchange(summary, std::nullopt);
        }

        // The map shown when the analysis started (UI side, before the job runs). After a catch-up of the same volume
        // the final map only recomputes the ranges that changed (MapData::update()).
        std::shared_ptr<const core::MapData> previous;

        std::mutex mutex; // Guards map and summary.
        std::shared_ptr<const core::MapData> map; // Newest map not yet taken.
        std::optional<AnalysisSummary> summary; // Final figures.
    };

    // A snapshot from the bitmap alone: the used space belongs to one placeholder file. It is the analysis's
    // first map, shown seconds after the start while the files are still being read (plan.md 6.2 principle 7).
    [[nodiscard]] core::Snapshot bitmapSnapshot(core::VolumeGeometry geometry, std::vector<core::ClusterRun> freeRuns);

    // The Summary tab's figures from the snapshot statistics and what the scan saw.
    [[nodiscard]] AnalysisSummary summaryOf(const core::VolumeStats& stats, std::uint32_t bytesPerCluster,
                                            const ScanStats& scan);

    // Everything an analysis produces besides maps.
    struct AnalysisResult {
        core::Snapshot snapshot; // Files, extents and free space.
        ScanStats scan; // What the scan saw (scanner, unreadable files, corrections, MFT read figures).
        core::VolumeStats stats; // Figures of the snapshot.
        AnalysisSummary summary; // The Summary tab's view of them.
        double seconds = 0.0; // Wall time of the analysis.
        double scanSeconds = 0.0; // Wall time of the file scan alone (bitmap and figures excluded).
        std::string fallbackReason; // Why the MFT scan gave way to the API scan; empty when it did not.
        core::ScreenAspect aspect; // The drive's map aspect: the saved snapshot's, else the options' (plan.md 7.1).
        bool caughtUp = false; // The snapshot came from the saved one and the journal, not from a full scan.
        std::string catchUp; // What happened to the saved snapshot ("caught up ...", or why it was rescanned).
        std::vector<core::ClusterRun> dirty; // After a catch-up: the LCN ranges whose map may have changed.
        std::string memoryPlan; // The memory mode of a full scan, in words; empty after a catch-up.
        std::vector<std::uint8_t> classes; // Per file: its class under the built-in catalog (class color mode).
        std::uint64_t churnFiles = 0; // Files the USN journal shows as churn (0 without a journal).
    };

    // How an analysis reads the files.
    struct AnalysisOptions {
        ScannerKind scanner = ScannerKind::Mft; // Which scanner to use first (the API path is the fallback).
        bool fallbackToApi = true; // When the MFT scan fails (not on cancel), scan again with the API path.
        MftScanOptions mft; // Reader settings of the MFT scanner.
        bool useSavedSnapshot = true; // Catch the saved snapshot up from the USN journal instead of scanning.
        bool saveSnapshot = true; // Save the result for the next run (plan.md 6.2 principle 6).
        std::filesystem::path snapshotDirectory; // Where snapshots live; empty: SnapshotStore::defaultDirectory().
        core::ScreenAspect aspect; // Map aspect stored with a first snapshot of the drive.
        std::uint64_t memoryBudgetBytes = 0; // Memory the scan plans against (plan.md 6.6); 0: no limit.
        bool forceMappedStorage = false; // Use the last memory rung whatever the budget (tests, check-caps).
    };

    // The saved snapshot's meta for a drive when it still belongs to that volume (same NTFS serial), else nullopt.
    // Gives the map its saved aspect before an analysis starts, and the "saved snapshot" age.
    [[nodiscard]] std::optional<core::SnapshotMeta> savedSnapshotMeta(const core::DriveInfo& drive,
                                                                      const std::filesystem::path& directory = {});

    // Analyses an NTFS volume on the calling thread (plan.md 10.2, API path): bitmap, then every file, then the
    // figures. `onSnapshot` (optional) receives the bitmap-only snapshot and partial ones while files are read.
    // The GUI job and `icd-cli analyze` both run this. Reads only; fails when the volume cannot be opened for
    // reading (administrator rights) and throws OperationCancelled on cancel.
    [[nodiscard]] AnalysisResult analyzeVolume(const core::DriveInfo& drive, JobContext& context,
                                               const PartialScanSink& onSnapshot = {},
                                               const AnalysisOptions& options = {});

    // Real analysis of an NTFS volume (plan.md 10.2, API path): reads the bitmap (the map shows used and free
    // space at once), then every file, refreshing the map from partial snapshots every few seconds, then builds
    // the final map and the summary into `output`. Reads only. Fails when the volume cannot be opened for reading
    // (administrator rights) and stops promptly on cancel.
    // `memoryBudget` is the job's cap (plan.md 6.6); 0 means no limit.
    [[nodiscard]] JobFunction makeAnalysisJob(const core::DriveInfo& drive, core::ScreenAspect aspect,
                                              std::shared_ptr<AnalysisOutput> output, std::uint64_t memoryBudget = 0);

} // namespace icd::services
