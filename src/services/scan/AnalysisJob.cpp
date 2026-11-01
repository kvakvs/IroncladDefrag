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

        // Fresh geometry of the volume: cluster counts change as files are written.
        core::VolumeGeometry geometryOf(const std::wstring& volumeName) {
            const platform::VolumeRecord record = platform::queryVolume(volumeName);
            if (record.layout.bytesPerCluster == 0 || record.layout.totalClusters == 0) {
                throw std::runtime_error("cannot read the volume geometry: " + platform::toUtf8(record.problem));
            }
            return {record.layout.bytesPerCluster, record.layout.totalClusters};
        }

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
            Analysis(const core::DriveInfo& drive, JobContext& context, const PartialScanSink& onSnapshot) :
                name_(core::driveName(drive)), volumeName_(platform::fromUtf8(drive.layout.volumeName)),
                root_(platform::fromUtf8(drive.root)), context_(context), onSnapshot_(onSnapshot) {}

            // Bitmap, files and figures.
            AnalysisResult run() {
                const auto start = std::chrono::steady_clock::now();
                const platform::UniqueHandle volume = openForAnalysis(volumeName_);
                const core::VolumeGeometry geometry = geometryOf(volumeName_);
                std::vector<core::ClusterRun> freeRuns = readBitmap(volume.get(), geometry);
                if (onSnapshot_) {
                    onSnapshot_(bitmapSnapshot(geometry, freeRuns));
                }
                ApiScanResult scan = scanFilesApi(volume.get(), root_, geometry, std::move(freeRuns), context_.token,
                                                  &context_.progress, onSnapshot_);
                context_.progress.report({.stage = "Summarizing", .done = 0, .total = 1, .message = name_});
                AnalysisResult result{std::move(scan.snapshot), scan.stats, {}, {}, 0.0};
                result.stats = core::summarize(result.snapshot, context_.executor);
                result.summary = summaryOf(result.stats, geometry.bytesPerCluster, result.scan);
                result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                log(result);
                return result;
            }

        private:
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
                                     {"files", result.stats.files},
                                     {"directories", result.stats.directories},
                                     {"extents", result.snapshot.extents.size()},
                                     {"fragmented_files", result.stats.fragmentedFiles},
                                     {"free_runs", result.stats.freeRuns},
                                     {"unreadable", result.scan.unreadable},
                                     {"changed_during_scan", result.scan.reconcile.droppedExtents},
                                     {"unattributed_clusters", result.scan.reconcile.unattributed},
                                     {"seconds", result.seconds}});
            }

            std::string name_; // "D: Data", for progress and the log.
            std::wstring volumeName_; // "\\?\Volume{GUID}\".
            std::wstring root_; // Mount path, for named streams.
            JobContext& context_; // Token, progress, executor.
            const PartialScanSink& onSnapshot_; // Early and partial snapshots; may be empty.
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

    AnalysisSummary summaryOf(const core::VolumeStats& stats, std::uint32_t bytesPerCluster, const ApiScanStats& scan) {
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

    AnalysisResult analyzeVolume(const core::DriveInfo& drive, JobContext& context, const PartialScanSink& onSnapshot) {
        return Analysis(drive, context, onSnapshot).run();
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
