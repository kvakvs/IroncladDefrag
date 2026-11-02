#include "core/planner/TargetLayout.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <numeric>

namespace icd::core::planner {

    namespace {

        // Sorts runs and merges overlapping and touching ones; empty runs are dropped.
        std::vector<ClusterRun> mergeRuns(std::vector<ClusterRun> runs) {
            std::erase_if(runs, [](const ClusterRun& r) { return r.length == 0; });
            std::ranges::sort(runs, {}, &ClusterRun::start);
            std::vector<ClusterRun> out;
            for (const ClusterRun& r : runs) {
                if (!out.empty() && r.start <= out.back().end()) {
                    out.back().length = std::max(out.back().end(), r.end()) - out.back().start;
                }
                else {
                    out.push_back(r);
                }
            }
            return out;
        }

        // Clusters a percentage of a count gives, rounded up.
        ClusterCount percentOf(double percent, ClusterCount of) {
            return static_cast<ClusterCount>(std::ceil(percent / 100.0 * static_cast<double>(of)));
        }

        // Placed zones split into forward and back zones, in script order.
        struct ZoneLists {
            std::vector<std::size_t> forward; // Indexes into TargetLayout::zones.
            std::vector<std::size_t> back; // Indexes into TargetLayout::zones.
        };

        // Clusters per catalog class over the files that have them.
        std::vector<ClusterCount> clustersPerClass(const Snapshot& snapshot, std::span<const classify::ClassId> classes,
                                                   std::size_t classCount) {
            std::vector<ClusterCount> out(classCount, 0);
            for (std::size_t f = 0; f < classes.size() && f < snapshot.files.size(); ++f) {
                if (classes[f] < classCount) {
                    out[classes[f]] += snapshot.files.allocatedClusters(static_cast<FileIndex>(f));
                }
            }
            return out;
        }

        // Creates the zone targets with their needs, and lists them.
        ZoneLists makeTargets(const strategy::ZoneScript& script, const ZoneAssignment& assignment,
                              TargetLayout& layout) {
            ZoneLists lists;
            layout.slotOf.assign(script.zones.size(), static_cast<std::size_t>(-1));
            for (std::size_t z = 0; z < script.zones.size(); ++z) {
                const strategy::Zone& zone = script.zones[z];
                if (!zone.placed()) {
                    continue;
                }
                const bool back = zone.anchor == strategy::Anchor::Back;
                layout.slotOf[z] = layout.zones.size();
                (back ? lists.back : lists.forward).push_back(layout.zones.size());
                layout.zones.push_back({.zone = static_cast<ZoneIndex>(z),
                                        .back = back,
                                        .need = z < assignment.zoneClusters.size() ? assignment.zoneClusters[z] : 0});
            }
            return lists;
        }

        // Grants the gaps: as wanted when free space allows, else shrunk in proportion. Returns the slack left.
        ClusterCount grantGaps(TargetLayout& layout, ClusterCount freeAfter) {
            const ClusterCount wanted =
                std::accumulate(layout.zones.begin(), layout.zones.end(), ClusterCount{0},
                                [](ClusterCount sum, const ZoneTarget& t) { return sum + t.gapWanted; });
            if (wanted <= freeAfter) {
                for (ZoneTarget& t : layout.zones) {
                    t.gap = t.gapWanted;
                }
                return freeAfter - wanted;
            }
            const double scale = static_cast<double>(freeAfter) / static_cast<double>(wanted);
            ClusterCount granted = 0;
            for (ZoneTarget& t : layout.zones) {
                t.gap = static_cast<ClusterCount>(std::floor(static_cast<double>(t.gapWanted) * scale));
                granted += t.gap;
            }
            layout.notes.push_back(std::format(
                "Free space is short: zone gaps get {:.0f}% of what the strategy asks for.", 100.0 * scale));
            return freeAfter - granted;
        }

        // Lengthens zones by the room they ask for while slack lasts, in script order.
        void grantRoom(TargetLayout& layout, std::span<const ClusterCount> room) {
            for (ZoneTarget& t : layout.zones) {
                const ClusterCount wanted = t.zone < room.size() ? room[t.zone] : 0;
                const ClusterCount granted = std::min(wanted, layout.slack);
                t.length += granted;
                layout.slack -= granted;
            }
        }

        // Lays the forward zones from the front, each after its slack share, and records the slack pieces.
        void placeForward(TargetLayout& layout, const ZoneLists& lists, const LayoutInputs& inputs,
                          const strategy::ZoneScript& script) {
            ClusterCount cursor = 0; // Next usable cluster after the previous zone's gap (before slack).
            ClusterCount slackUsed = 0;
            for (std::size_t i = 0; i < lists.forward.size(); ++i) {
                ZoneTarget& t = layout.zones[lists.forward[i]];
                const bool front = script.zones[t.zone].anchor == strategy::Anchor::Front;
                const ClusterCount wantedSlack = i < inputs.slackBefore.size() ? inputs.slackBefore[i] : 0;
                const ClusterCount before = front ? 0 : std::clamp(wantedSlack, slackUsed, layout.slack);
                const auto slackHere = layout.axis.pieces({cursor + slackUsed, before - slackUsed});
                layout.slackPieces.insert(layout.slackPieces.end(), slackHere.begin(), slackHere.end());
                slackUsed = before;
                t.start = cursor + before;
                t.pieces = layout.axis.pieces({t.start, t.length});
                t.gapPieces = layout.axis.pieces({t.start + t.length, t.gap});
                cursor += t.length + t.gap;
            }
            const auto tail = layout.axis.pieces({cursor + slackUsed, layout.slack - slackUsed});
            layout.slackPieces.insert(layout.slackPieces.end(), tail.begin(), tail.end());
        }

        // Lays the back zones so the last one ends at the end of the volume.
        void placeBack(TargetLayout& layout, const ZoneLists& lists) {
            ClusterCount end = layout.axis.size();
            for (std::size_t i = lists.back.size(); i-- > 0;) {
                ZoneTarget& t = layout.zones[lists.back[i]];
                if (i + 1 < lists.back.size()) {
                    end -= t.gap;
                    t.gapPieces = layout.axis.pieces({end, t.gap});
                }
                else {
                    t.gap = 0; // Nothing follows the last zone; its gap would be outside the volume.
                }
                end -= t.length;
                t.start = end;
                t.pieces = layout.axis.pieces({t.start, t.length});
            }
        }

        // Gives every zone its length; when usable space is short (never with consistent input) later zones lose.
        ClusterCount assignLengths(TargetLayout& layout) {
            ClusterCount left = layout.axis.size();
            for (ZoneTarget& t : layout.zones) {
                t.length = std::min(t.need, left);
                left -= t.length;
            }
            return left;
        }

    } // namespace

    AvailableAxis::AvailableAxis(std::vector<ClusterRun> obstacles, ClusterCount totalClusters) :
        total_(totalClusters) {
        for (ClusterRun& r : obstacles) {
            r = {std::min(r.start, totalClusters), std::min(r.end(), totalClusters) - std::min(r.start, totalClusters)};
        }
        obstacles_ = mergeRuns(std::move(obstacles));
        before_.reserve(obstacles_.size());
        for (const ClusterRun& r : obstacles_) {
            before_.push_back(obstacleClusters_);
            obstacleClusters_ += r.length;
        }
    }

    ClusterCount AvailableAxis::indexOf(Lcn lcn) const {
        lcn = std::min(lcn, total_);
        const auto it = std::ranges::upper_bound(obstacles_, lcn, {}, &ClusterRun::start);
        if (it == obstacles_.begin()) {
            return lcn;
        }
        const auto i = static_cast<std::size_t>(it - obstacles_.begin()) - 1;
        const ClusterRun& r = obstacles_[i];
        return r.contains(lcn) ? r.start - before_[i] : lcn - before_[i] - r.length;
    }

    Lcn AvailableAxis::lcnAt(ClusterCount index) const {
        // The first obstacle with more usable clusters before it than index decides the offset.
        std::size_t lo = 0;
        std::size_t hi = obstacles_.size();
        while (lo < hi) {
            const std::size_t mid = (lo + hi) / 2;
            if (obstacles_[mid].start - before_[mid] > index) {
                hi = mid;
            }
            else {
                lo = mid + 1;
            }
        }
        return index + (lo < before_.size() ? before_[lo] : obstacleClusters_);
    }

    std::vector<ClusterRun> AvailableAxis::pieces(ClusterRun numbers) const {
        ClusterCount count = numbers.length;
        std::vector<ClusterRun> out;
        if (count == 0) {
            return out;
        }
        Lcn lcn = lcnAt(numbers.start);
        auto it = std::ranges::upper_bound(obstacles_, lcn, {}, &ClusterRun::start);
        while (count > 0) {
            const Lcn limit = it == obstacles_.end() ? total_ : it->start;
            const ClusterCount take = std::min(count, limit - lcn);
            out.push_back({lcn, take});
            count -= take;
            if (count > 0) {
                lcn = it->end();
                ++it;
            }
        }
        return out;
    }

    std::optional<Lcn> ZoneTarget::firstLcn() const {
        return pieces.empty() ? std::nullopt : std::optional(pieces.front().start);
    }

    Lcn ZoneTarget::endLcn() const { return pieces.empty() ? 0 : pieces.back().end(); }

    bool ZoneTarget::holds(ClusterRun run) const {
        const auto it = std::ranges::upper_bound(pieces, run.start, {}, &ClusterRun::start);
        return it != pieces.begin() && std::prev(it)->contains(run.start) && run.end() <= std::prev(it)->end();
    }

    const ZoneTarget* TargetLayout::target(ZoneIndex zone) const {
        if (zone >= slotOf.size() || slotOf[zone] >= zones.size()) {
            return nullptr;
        }
        return &zones[slotOf[zone]];
    }

    std::vector<ClusterRun> layoutObstacles(const Snapshot& snapshot, const ZoneAssignment& assignment,
                                            std::span<const ClusterRun> reserved) {
        std::vector<ClusterRun> runs(reserved.begin(), reserved.end());
        for (const ExtentRecord& r : snapshot.extents.records()) {
            const bool placed = r.file() < assignment.roles.size() && assignment.roles[r.file()] == FileRole::Placed;
            if ((r.flags() & ExtentUnmovable) != 0 || !placed) {
                runs.push_back(r.run());
            }
        }
        return mergeRuns(std::move(runs));
    }

    ClusterCount gapClusters(const strategy::GapSpec& gap, const VolumeGeometry& geometry, ClusterCount freeClusters,
                             const classify::Catalog& catalog, std::span<const ClusterCount> classClusters) {
        ClusterCount classSum = 0;
        for (const std::string& id : gap.classes) {
            const auto found = catalog.find(id);
            classSum += found && *found < classClusters.size() ? classClusters[*found] : 0;
        }
        const ClusterCount cluster = std::max<ClusterCount>(1, geometry.bytesPerCluster);
        return std::max({percentOf(gap.freePercent, freeClusters), percentOf(gap.volumePercent, geometry.totalClusters),
                         (gap.bytes + cluster - 1) / cluster,
                         static_cast<ClusterCount>(std::ceil(gap.classMultiple * static_cast<double>(classSum)))});
    }

    TargetLayout resolveLayout(const Snapshot& snapshot, const strategy::ZoneScript& script,
                               const ZoneAssignment& assignment, std::span<const classify::ClassId> classes,
                               const classify::Catalog& catalog, const LayoutInputs& inputs) {
        TargetLayout layout;
        const ClusterCount total = snapshot.geometry.totalClusters;
        layout.axis = AvailableAxis(layoutObstacles(snapshot, assignment, inputs.reserved), total);
        const ZoneLists lists = makeTargets(script, assignment, layout);
        const ClusterCount need = std::accumulate(layout.zones.begin(), layout.zones.end(), ClusterCount{0},
                                                  [](ClusterCount sum, const ZoneTarget& t) { return sum + t.need; });
        if (need > layout.axis.size() && !inputs.reserved.empty()) {
            layout.axis = AvailableAxis(layoutObstacles(snapshot, assignment, {}), total);
            layout.notes.emplace_back("The files only fit with the MFT zone used; it holds files after the plan.");
            layout.reservedUsed = true;
        }
        const ClusterCount freeAfter = assignLengths(layout);
        const auto perClass = clustersPerClass(snapshot, classes, catalog.classes.size());
        for (ZoneTarget& t : layout.zones) {
            t.gapWanted = gapClusters(script.zones[t.zone].gapAfter, snapshot.geometry, freeAfter, catalog, perClass);
        }
        if (!lists.back.empty()) {
            layout.zones[lists.back.back()].gapWanted = 0; // The last zone ends at the end of the volume.
        }
        layout.slack = grantGaps(layout, freeAfter);
        grantRoom(layout, inputs.room);
        placeForward(layout, lists, inputs, script);
        placeBack(layout, lists);
        return layout;
    }

} // namespace icd::core::planner
