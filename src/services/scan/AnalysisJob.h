#pragma once

#include "core/map/MapData.h"
#include "core/model/DriveInfo.h"
#include "core/snapshot/SnapshotStats.h"
#include "services/job/JobRunner.h"
#include "services/scan/ApiScanner.h"
#include "services/scan/MftScanner.h"
#include "services/workflow/FakeResults.h"

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
    };

    // How an analysis reads the files.
    struct AnalysisOptions {
        ScannerKind scanner = ScannerKind::Api; // Which scanner to use first.
        bool fallbackToApi = true; // When the MFT scan fails (not on cancel), scan again with the API path.
        MftScanOptions mft; // Reader settings of the MFT scanner.
    };

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
    [[nodiscard]] JobFunction makeAnalysisJob(const core::DriveInfo& drive, core::ScreenAspect aspect,
                                              std::shared_ptr<AnalysisOutput> output);

} // namespace icd::services
