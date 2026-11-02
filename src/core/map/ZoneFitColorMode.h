#pragma once

#include "core/map/PlanColorMode.h"
#include "core/planner/TargetLayout.h"
#include "core/planner/ZoneAssignment.h"
#include "core/snapshot/Snapshot.h"

#include <memory>

namespace icd::core {

    // Colors of the zone-fit mode: the zones' target ranges, and data that sits outside its zone.
    inline constexpr Rgba zoneTargetColor = rgb(120, 190, 120);
    inline constexpr Rgba misplacedColor = rgb(215, 60, 60);

    // The zone-fit overlay (plan.md 7.4: in its target zone or misplaced): every zone's target runs, and over them the
    // runs of placed files that lie outside their own zone.
    [[nodiscard]] std::shared_ptr<const MapOverlay> zoneFitOverlay(const Snapshot& snapshot,
                                                                   const planner::ZoneAssignment& assignment,
                                                                   const planner::TargetLayout& layout);

} // namespace icd::core
