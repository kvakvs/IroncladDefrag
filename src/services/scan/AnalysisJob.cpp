#include "services/scan/AnalysisJob.h"

#include "core/format/DriveText.h"
#include "core/log/Log.h"
#include "core/snapshot/ScanReconcile.h"
#include "core/snapshot/SnapshotBuilder.h"
#include "platform/win/TextConvert.h"
#include "platform/win/VolumeBitmap.h"
#include "platform/win/Volumes.h"
#include "platform/win/WinError.h"

#include <chrono>
#include <stdexcept>

namespace icd::services {

    namespace {

        // Name of the placeholder that owns used space before the files are read.
        constexpr const char* unreadName = "(files not read yet)";

        // Fresh layout of the volume: cluster counts and the MFT size change as files are written.
        core::VolumeLayout layoutOf(const std::wstring& volumeName) {
            const platform::VolumeRecord record = platform::queryVolume(volumeName);
            if (record.layout.bytesPerCluster == 0 || record.layout.totalClusters == 0) {
                throw std::runtime_error("cannot read the volume geometry: " + platform::toUtf8(record.problem));
            }
            return record.layout;
        }

        // A scanner's snapshot and statistics.
        struct ScanOutcome {
            core::Snapshot snapshot; // Files, extents and free space.
            ScanStats stats; // What the scan saw.
        };

        // Opens the volume for reading or explains why not.
        platform::UniqueHandle openForAnalysis(const std::wstring& volumeName) {
            platform::UniqueHandle volume = platform::openVolume(volumeName, GENERIC_READ);
            if (!volume) {
                throw std::runtime_error("cannot open the volume for reading (administrator rights are needed): " +
                                         platform::toUtf8(platform::lastWinError()));
            }
            return volume;
        }

        // One analysis run: the steps share the volume, the executor and the snapshot sink.
        class Analysis {
        public:
            Analysis(const core::DriveInfo& drive, JobContext& context, const PartialScanSink& onSnapshot,
                     const AnalysisOptions& options) :
                name_(core::driveName(drive)), volumeName_(platform::fromUtf8(drive.layout.volumeName)),
                root_(platform::fromUtf8(drive.root)), context_(context), onSnapshot_(onSnapshot), options_(options) {}

            // Bitmap, files and figures.
            AnalysisResult run() {
                const auto start = std::chrono::steady_clock::now();
                const platform::UniqueHandle volume = openForAnalysis(volumeName_);
                const core::VolumeLayout layout = layoutOf(volumeName_);
                const core::VolumeGeometry geometry{layout.bytesPerCluster, layout.totalClusters};
                std::vector<core::ClusterRun> freeRuns = readBitmap(volume.get(), geometry);
                if (onSnapshot_) {
                    onSnapshot_(bitmapSnapshot(geometry, freeRuns));
                }
                const auto scanStart = std::chrono::steady_clock::now();
                ScanOutcome scan = scanFiles(volume.get(), layout, geometry, std::move(freeRuns));
                const auto scanEnd = std::chrono::steady_clock::now();
                context_.progress.report({.stage = "Summarizing", .done = 0, .total = 1, .message = name_});
                AnalysisResult result{std::move(scan.snapshot), std::move(scan.stats), {}, {}, 0.0, 0.0, fallback_};
                result.stats = core::summarize(result.snapshot, context_.executor);
                result.summary = summaryOf(result.stats, geometry.bytesPerCluster, result.scan);
                result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                result.scanSeconds = std::chrono::duration<double>(scanEnd - scanStart).count();
                log(result);
                return result;
            }

        private:
            // The chosen scanner; a failed MFT scan falls back to the API path when allowed (never on cancel).
            ScanOutcome scanFiles(HANDLE volume, const core::VolumeLayout& layout, core::VolumeGeometry geometry,
                                  std::vector<core::ClusterRun> freeRuns) {
                if (options_.scanner == ScannerKind::Mft && layout.ntfs) {
                    try {
                        MftScanResult mft =
                            scanFilesMft(volume, volumeName_, geometry, *layout.ntfs, freeRuns, context_.executor,
                                         context_.token, &context_.progress, options_.mft);
                        return {std::move(mft.snapshot), std::move(mft.stats)};
                    }
                    catch (const core::OperationCancelled&) {
                        throw;
                    }
                    catch (const std::exception& e) {
                        if (!options_.fallbackToApi) {
                            throw;
                        }
                        fallback_ = e.what();
                        core::logging::warning("analysis", {{"drive", name_}, {"mft_scan_failed", fallback_}});
                    }
                }
                ApiScanResult api = scanFilesApi(volume, root_, geometry, std::move(freeRuns), context_.token,
                                                 &context_.progress, onSnapshot_);
                return {std::move(api.snapshot), std::move(api.stats)};
            }

            // The bitmap as free runs, with progress in clusters.
            std::vector<core::ClusterRun> readBitmap(HANDLE volume, core::VolumeGeometry geometry) {
                const auto progress = [this](core::ClusterCount done, core::ClusterCount total) {
                    context_.progress.report(
                        {.stage = "Reading volume bitmap", .done = done, .total = total, .message = name_});
                };
                progress(0, geometry.totalClusters);
                const core::FreeRunList free =
                    platform::readFreeRuns(volume, geometry.totalClusters, context_.executor, context_.token, progress);
                return {free.runs().begin(), free.runs().end()};
            }

            // One log line with the main figures.
            void log(const AnalysisResult& result) const {
                core::logging::info("analysis",
                                    {{"drive", name_},
                                     {"scanner", scannerName(result.scan.scanner)},
                                     {"files", result.stats.files},
                                     {"directories", result.stats.directories},
                                     {"extents", result.snapshot.extents.size()},
                                     {"fragmented_files", result.stats.fragmentedFiles},
                                     {"free_runs", result.stats.freeRuns},
                                     {"unreadable", result.scan.unreadable},
                                     {"changed_during_scan", result.scan.reconcile.droppedExtents},
                                     {"unattributed_clusters", result.scan.reconcile.unattributed},
                                     {"scan_seconds", result.scanSeconds},
                                     {"seconds", result.seconds}});
            }

            std::string name_; // "D: Data", for progress and the log.
            std::wstring volumeName_; // "\\?\Volume{GUID}\".
            std::wstring root_; // Mount path, for named streams.
            JobContext& context_; // Token, progress, executor.
            const PartialScanSink& onSnapshot_; // Early and partial snapshots; may be empty.
            AnalysisOptions options_; // Scanner choice.
            std::string fallback_; // Why the MFT scan gave way, if it did.
        };

    } // namespace

    core::Snapshot bitmapSnapshot(core::VolumeGeometry geometry, std::vector<core::ClusterRun> freeRuns) {
        std::vector<core::ExtentEntry> extents;
        constexpr core::FileIndex placeholder = 1;
        core::reconcileScan(extents, freeRuns, geometry.totalClusters, placeholder);
        core::SnapshotBuilder builder(geometry);
        builder.addFile({"", core::noFile, 0, core::FileDirectory, {}, 0});
        builder.addFile({unreadName, 0, 0, core::FileUnmovable, {}, 0});
        builder.addExtents(std::move(extents));
        for (const core::ClusterRun& run : freeRuns) {
            builder.addFreeRun(run);
        }
        return std::move(builder).build();
    }

    AnalysisSummary summaryOf(const core::VolumeStats& stats, std::uint32_t bytesPerCluster, const ScanStats& scan) {
        AnalysisSummary summary;
        summary.fragmentationPercent = stats.fragmentedFilePercent();
        summary.files = stats.files;
        summary.fragmentedFiles = stats.fragmentedFiles;
        summary.fragments = stats.extraFragments;
        summary.freeBytes = stats.freeClusters * bytesPerCluster;
        summary.freeRuns = stats.freeRuns;
        summary.largestFreeRun = stats.largestFreeRun * bytesPerCluster;
        summary.scan = AnalysisSummary::ScanFigures{
            .directories = stats.directories,
            .fragmentedBytesPercent = stats.fragmentedClusterPercent(),
            .largeRunFreePercent = stats.largeRunFreePercent(),
            .unmovableBytes = stats.unmovableClusters * bytesPerCluster,
            .unreadableFiles = scan.unreadable,
            .changedDuringScan = scan.reconcile.droppedExtents,
        };
        return summary;
    }

    AnalysisResult analyzeVolume(const core::DriveInfo& drive, JobContext& context, const PartialScanSink& onSnapshot,
                                 const AnalysisOptions& options) {
        return Analysis(drive, context, onSnapshot, options).run();
    }

    JobFunction makeAnalysisJob(const core::DriveInfo& drive, core::ScreenAspect aspect,
                                std::shared_ptr<AnalysisOutput> output) {
        return [drive, aspect, output = std::move(output)](JobContext& context) {
            const auto publish = [&](core::Snapshot snapshot) {
                output->set(core::MapData::build(std::move(snapshot), aspect, context.executor, context.token));
            };
            AnalysisResult result = analyzeVolume(drive, context, publish);
            context.progress.report({.stage = "Building map", .done = 0, .total = 1, .message = {}});
            output->setSummary(std::move(result.summary));
            publish(std::move(result.snapshot));
            context.progress.report({.stage = "Building map", .done = 1, .total = 1, .message = {}});
        };
    }

} // namespace icd::services
