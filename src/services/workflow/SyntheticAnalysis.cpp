#include "services/workflow/SyntheticAnalysis.h"

#include "core/format/DriveText.h"
#include "core/log/Log.h"
#include "services/scan/Classification.h"

#include <algorithm>
#include <utility>

namespace icd::services {

    namespace {

        // Average used bytes per synthetic file; gives 20 M files for 18 TB of data, the design target.
        constexpr std::uint64_t bytesPerFile = 900ULL * 1024;

    } // namespace

    core::SynthOptions syntheticOptionsFor(const core::DriveInfo& drive) {
        core::SynthOptions options;
        options.seed = drive.root.empty() ? 1 : static_cast<unsigned char>(drive.root.front());
        options.geometry.totalClusters =
            std::max<core::ClusterCount>(4096, drive.totalBytes / options.geometry.bytesPerCluster);
        options.fillPercent = std::clamp(100.0 * drive.usedFraction(), 1.0, 99.0);
        options.files =
            static_cast<std::uint32_t>(std::clamp<std::uint64_t>(drive.usedBytes() / bytesPerFile, 5'000, 20'000'000));
        options.fragmentedShare = std::clamp(drive.fragmentationPercent.value_or(20.0) / 100.0, 0.0, 0.9);
        options.maxFragments = 40;
        return options;
    }

    JobFunction makeSyntheticAnalysis(const core::DriveInfo& drive, core::ScreenAspect aspect,
                                      std::shared_ptr<AnalysisOutput> output) {
        return [options = syntheticOptionsFor(drive), aspect, output = std::move(output),
                name = core::driveName(drive)](JobContext& context) {
            context.progress.report({"Generating synthetic volume", 0, 2, name});
            core::Snapshot snapshot = core::generateSyntheticVolume(options);
            context.token.throwIfCancelled();
            context.progress.report({"Building map", 1, 2, name});
            const auto classes = classifyVolume(snapshot, {}, core::newestModification(snapshot, context.executor),
                                                context.executor, context.token)
                                     .classes;
            auto map = core::MapData::build(std::move(snapshot), aspect, context.executor, context.token, classes);
            core::logging::info("synthetic analysis",
                                {{"drive", name},
                                 {"files", map->snapshot().files.size()},
                                 {"extents", map->snapshot().extents.size()},
                                 {"map_bytes", map->memoryBytes()}});
            output->set(std::move(map));
            context.progress.report({"Building map", 2, 2, name});
        };
    }

} // namespace icd::services
