#pragma once

#include "core/map/MapData.h"
#include "core/model/DriveInfo.h"
#include "core/synth/SyntheticVolume.h"
#include "services/job/JobRunner.h"

#include <memory>
#include <mutex>

namespace icd::services {

    // Where a synthetic analysis leaves its map. The job writes it on a worker; the UI takes it after the job's
    // success event.
    struct AnalysisOutput {
        // Stores the map (worker side).
        void set(std::shared_ptr<const core::MapData> data) {
            const std::scoped_lock lock(mutex);
            map = std::move(data);
        }

        // Takes the map (UI side); null when the job did not get that far.
        [[nodiscard]] std::shared_ptr<const core::MapData> take() {
            const std::scoped_lock lock(mutex);
            return std::exchange(map, nullptr);
        }

        std::mutex mutex; // Guards map.
        std::shared_ptr<const core::MapData> map; // The result.
    };

    // Synthetic volume options shaped like a drive: same size and fill, files from the used space, and a
    // fragmented share from the drive's figure, seeded by the drive letter so a drive always looks the same.
    [[nodiscard]] core::SynthOptions syntheticOptionsFor(const core::DriveInfo& drive);

    // The Phase 2 stand-in for analysis: generates a synthetic snapshot shaped like the drive, then builds its
    // map data (world for aspect, pyramid, flags) on the job's executor, reporting both stages. Cancellation is
    // honoured between stages and while the map builds. Real scanning replaces it in Phase 3.
    [[nodiscard]] JobFunction makeSyntheticAnalysis(const core::DriveInfo& drive, core::ScreenAspect aspect,
                                                    std::shared_ptr<AnalysisOutput> output);

} // namespace icd::services
