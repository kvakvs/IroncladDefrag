// Zone assignment and the target layout resolver (T081): obstacles at every position, full volumes, empty zones,
// gaps, back zones, reserved runs and slack placement.

#include "core/planner/TargetLayout.h"
#include "core/synth/SynthRandom.h"

#include "core/PlannerTestUtil.h"

#include <gtest/gtest.h>

namespace icd::core::planner { namespace {

    using strategy::Anchor;
    using strategy::ZoneAction;
    using test::pictureVolume;
    using test::zone;

    // Runs as text for compact expectations: "[1,4) [6,7)".
    std::string runsText(const std::vector<ClusterRun>& runs) {
        std::string out;
        for (const ClusterRun& r : runs) {
            out += std::format("{}[{},{})", out.empty() ? "" : " ", r.start, r.end());
        }
        return out;
    }

    // The layout of a picture under a script.
    TargetLayout layoutOf(const test::PictureVolume& v, const strategy::ZoneScript& s, const LayoutInputs& in = {}) {
        return resolveLayout(v.snapshot, s, test::assign(v, s), v.classes, classify::builtinCatalog(), in);
    }

    // Photos at the front, archives after them, video at the back.
    strategy::ZoneScript threeZones() {
        return test::script({zone("Photos", {"photos"}, ZoneAction::FastFill, Anchor::Front),
                             zone("Archives", {"archives"}, ZoneAction::FastFill),
                             zone("Video", {"video"}, ZoneAction::PackToBack, Anchor::Back)});
    }

    // Letters of the three-zone tests.
    const test::PictureFacts threeFacts{.classes = {{'A', "photos"}, {'B', "archives"}, {'V', "video"}, {'T', "temp"}}};

    TEST(AvailableAxis, MatchesBruteForceForRandomObstacles) {
        SynthRandom random(7);
        for (int round = 0; round < 200; ++round) {
            const ClusterCount total = 1 + random.below(60);
            std::vector<ClusterRun> obstacles;
            for (std::uint64_t i = random.below(6); i > 0; --i) {
                const Lcn start = random.below(total);
                obstacles.push_back({start, random.below(total - start + 1)});
            }
            const AvailableAxis axis(obstacles, total);
            std::vector<Lcn> usable;
            for (Lcn lcn = 0; lcn < total; ++lcn) {
                const bool blocked =
                    std::ranges::any_of(obstacles, [&](const ClusterRun& r) { return r.contains(lcn); });
                EXPECT_EQ(axis.indexOf(lcn), usable.size()) << round << " " << lcn;
                if (!blocked) {
                    usable.push_back(lcn);
                }
            }
            ASSERT_EQ(axis.size(), usable.size());
            for (std::size_t i = 0; i < usable.size(); ++i) {
                EXPECT_EQ(axis.lcnAt(i), usable[i]);
                const auto pieces = axis.pieces({i, usable.size() - i});
                ClusterCount covered = 0;
                for (const ClusterRun& p : pieces) {
                    for (Lcn lcn = p.start; lcn < p.end(); ++lcn) {
                        EXPECT_EQ(lcn, usable[i + covered]);
                        ++covered;
                    }
                }
                EXPECT_EQ(covered, usable.size() - i);
            }
        }
    }

    TEST(ZoneAssignment, FirstMatchingZoneWinsAndRolesFollowActions) {
        const auto v = pictureVolume("#AABBTTVVX", threeFacts);
        auto s = threeZones();
        s.zones.insert(s.zones.begin(), zone("Churn", {"temp"}, ZoneAction::KeepInPlace));
        s.zones.push_back(zone("Everything", {}, ZoneAction::DefragmentOnly)); // Takes X (unknown) and nothing else.
        const ZoneAssignment a = test::assign(v, s);
        const auto& f = v.files;
        EXPECT_EQ(a.roles[f.at('#')], FileRole::Fixed);
        EXPECT_EQ(a.roles[0], FileRole::Fixed); // The root directory has no clusters.
        EXPECT_EQ(a.roles[f.at('T')], FileRole::InPlace);
        EXPECT_EQ(a.zoneOf[f.at('A')], 1);
        EXPECT_EQ(a.roles[f.at('A')], FileRole::Placed);
        EXPECT_EQ(a.zoneOf[f.at('V')], 3);
        EXPECT_EQ(a.zoneOf[f.at('X')], 4);
        EXPECT_EQ(a.roles[f.at('X')], FileRole::Defragment);
        EXPECT_EQ(a.movable[f.at('B')], 2U);
        EXPECT_EQ(a.zoneClusters, (std::vector<ClusterCount>{0, 2, 2, 2, 0}));
        s.zones.pop_back();
        EXPECT_EQ(test::assign(v, s).roles[f.at('X')], FileRole::Defragment); // No zone: defragment in place.
        EXPECT_EQ(test::assign(v, s).zoneOf[f.at('X')], noZone);
    }

    TEST(ZoneAssignment, SelectorConditions) {
        test::PictureFacts facts{.classes = {{'A', "photos"}, {'B', "photos"}, {'C', "photos"}, {'D', "photos"}},
                                 .modified = {{'A', test::now - 400 * test::day}},
                                 .names = {{'A', "a.jpg"}, {'B', "b.png"}, {'C', "c.jpg"}, {'D', "d.jpg"}},
                                 .flags = {{'D', FileSparse}}};
        const auto v = pictureVolume("AABBBBCD", facts);
        strategy::Zone old = zone("Old", {"photos"}, ZoneAction::FastFill);
        old.select.minAgeDays = 365;
        strategy::Zone png = zone("Png", {}, ZoneAction::FastFill);
        png.select.extensions = {"png"};
        strategy::Zone sparse = zone("Sparse", {}, ZoneAction::FastFill);
        sparse.select.flagsAll = FileSparse;
        strategy::Zone big = zone("Small", {}, ZoneAction::FastFill);
        big.select.maxSize = 4096 + 1;
        strategy::Zone rest = zone("Rest", {}, ZoneAction::FastFill);
        rest.select.paths = {"**/*.jpg"};
        const auto a = test::assign(v, test::script({old, png, sparse, big, rest}));
        EXPECT_EQ(a.zoneOf[v.files.at('A')], 0); // Old.
        EXPECT_EQ(a.zoneOf[v.files.at('B')], 1); // By extension.
        EXPECT_EQ(a.zoneOf[v.files.at('D')], 2); // By flag, before the size rule.
        EXPECT_EQ(a.zoneOf[v.files.at('C')], 3); // One cluster: small.
        EXPECT_EQ(a.zoneOf[0], 3); // The root (size 0) is small, but without clusters it stays Fixed.
        EXPECT_EQ(a.roles[0], FileRole::Fixed);
    }

    TEST(TargetLayout, ZonesFlowAroundObstaclesAndBackZonesEndAtTheEnd) {
        // 0 1 2 3 4 5 6 7 8 9 10 11
        // # A A . B B . V V . .  A
        const auto v = pictureVolume("#AA.BB.VV..A", threeFacts);
        const TargetLayout layout = layoutOf(v, threeZones());
        ASSERT_EQ(layout.zones.size(), 3U);
        EXPECT_EQ(runsText(layout.zones[0].pieces), "[1,4)");
        EXPECT_EQ(runsText(layout.zones[1].pieces), "[4,6)");
        EXPECT_EQ(runsText(layout.zones[2].pieces), "[10,12)");
        EXPECT_EQ(layout.slack, 4U);
        EXPECT_EQ(runsText(layout.slackPieces), "[6,10)");
        EXPECT_TRUE(layout.notes.empty());
        EXPECT_TRUE(layout.zones[0].holds({1, 3}));
        EXPECT_FALSE(layout.zones[0].holds({3, 2}));
        EXPECT_EQ(layout.target(1)->zone, 1);
    }

    TEST(TargetLayout, ObstaclesAtEveryPosition) {
        // Unmovable clusters at the start, inside zones, between zones and at the very end; a churn file in place.
        const auto v = pictureVolume("#A#AT.B..V#", threeFacts);
        auto s = threeZones();
        s.zones.insert(s.zones.begin(), zone("Churn", {"temp"}, ZoneAction::KeepInPlace));
        const TargetLayout layout = layoutOf(v, s);
        EXPECT_EQ(runsText(layout.axis.obstacles()), "[0,1) [2,3) [4,5) [10,11)");
        EXPECT_EQ(runsText(layout.zones[0].pieces), "[1,2) [3,4)"); // Photos split by the obstacle at 2.
        EXPECT_EQ(runsText(layout.zones[1].pieces), "[5,6)"); // After the churn file.
        EXPECT_EQ(runsText(layout.zones[2].pieces), "[9,10)"); // Ends before the obstacle at the end.
        EXPECT_EQ(runsText(layout.slackPieces), "[6,9)");
        EXPECT_FALSE(layout.target(0)); // Keep-in-place zones have no place.
    }

    TEST(TargetLayout, FullVolumeAndEmptyZones) {
        auto s = threeZones();
        s.zones[0].gapAfter.bytes = 2 * 4096;
        s.zones.insert(s.zones.begin() + 1, zone("Nothing", {"catalogs"}, ZoneAction::FastFill));
        const auto full = pictureVolume("BAVBAV", threeFacts);
        const TargetLayout layout = layoutOf(full, s);
        EXPECT_EQ(layout.slack, 0U);
        EXPECT_EQ(layout.zones[0].gap, 0U); // No room for the gap.
        EXPECT_EQ(layout.zones[0].gapWanted, 2U);
        EXPECT_EQ(layout.notes.size(), 1U);
        EXPECT_EQ(runsText(layout.zones[0].pieces), "[0,2)");
        EXPECT_EQ(layout.zones[1].length, 0U); // The empty zone takes no space.
        EXPECT_FALSE(layout.zones[1].firstLcn());
        EXPECT_EQ(runsText(layout.zones[2].pieces), "[2,4)");
        EXPECT_EQ(runsText(layout.zones[3].pieces), "[4,6)");
        const auto empty = pictureVolume("......", threeFacts);
        const TargetLayout none = layoutOf(empty, s);
        EXPECT_EQ(none.slack, 4U); // Six free clusters, two of them the photos gap.
        EXPECT_EQ(runsText(none.zones[0].gapPieces), "[0,2)");
    }

    TEST(TargetLayout, GapsShrinkInProportionWhenShort) {
        auto s = threeZones();
        s.zones[0].gapAfter.bytes = 4 * 4096;
        s.zones[1].gapAfter.bytes = 2 * 4096;
        s.zones[2].gapAfter.bytes = 4096; // The last zone's gap is dropped.
        const auto v = pictureVolume("AABB...VV", threeFacts);
        const TargetLayout layout = layoutOf(v, s);
        EXPECT_EQ(layout.zones[0].gap + layout.zones[1].gap + layout.slack, 3U);
        EXPECT_EQ(layout.zones[0].gap, 2U); // 4 x 3/6.
        EXPECT_EQ(layout.zones[1].gap, 1U);
        EXPECT_EQ(layout.zones[2].gapWanted, 0U);
        EXPECT_EQ(runsText(layout.zones[1].pieces), "[4,6)");
        EXPECT_EQ(runsText(layout.zones[1].gapPieces), "[6,7)");
    }

    TEST(TargetLayout, GapTermsTakeTheLargest) {
        const auto& catalog = classify::builtinCatalog();
        std::vector<ClusterCount> perClass(catalog.classes.size(), 0);
        perClass[*catalog.find("build")] = 30;
        perClass[*catalog.find("temp")] = 20;
        const VolumeGeometry g{.bytesPerCluster = 4096, .totalClusters = 1000};
        strategy::GapSpec churn{.volumePercent = 5, .classMultiple = 2, .classes = {"build", "temp"}};
        EXPECT_EQ(gapClusters(churn, g, 500, catalog, perClass), 100U); // 2 x 50 beats 5% of 1000.
        churn.volumePercent = 20;
        EXPECT_EQ(gapClusters(churn, g, 500, catalog, perClass), 200U);
        EXPECT_EQ(gapClusters({.freePercent = 1}, g, 450, catalog, perClass), 5U); // Rounded up.
        EXPECT_EQ(gapClusters({.bytes = 4097}, g, 0, catalog, perClass), 2U);
        EXPECT_EQ(gapClusters({}, g, 500, catalog, perClass), 0U);
    }

    TEST(TargetLayout, ReservedRunsAreKeptFreeUnlessTheFilesNeedThem) {
        const auto v = pictureVolume("AA...BBVV", threeFacts);
        const TargetLayout kept = layoutOf(v, threeZones(), {.reserved = {{2, 2}}});
        EXPECT_EQ(runsText(kept.zones[1].pieces), "[4,6)"); // Archives skip the reserved [2,4).
        EXPECT_TRUE(kept.notes.empty());
        const TargetLayout used = layoutOf(v, threeZones(), {.reserved = {{0, 7}}});
        EXPECT_EQ(used.notes.size(), 1U);
        EXPECT_EQ(runsText(used.zones[0].pieces), "[0,2)");
    }

    TEST(TargetLayout, SlackBeforeShiftsForwardZones) {
        auto s = threeZones();
        s.zones[0].anchor = Anchor::AfterPrevious; // So the first zone may move too.
        const auto v = pictureVolume("..AA..BB..VV", threeFacts);
        const TargetLayout layout = layoutOf(v, s, {.slackBefore = {2, 4}});
        EXPECT_EQ(runsText(layout.zones[0].pieces), "[2,4)");
        EXPECT_EQ(runsText(layout.zones[1].pieces), "[6,8)");
        EXPECT_EQ(runsText(layout.slackPieces), "[0,2) [4,6) [8,10)");
        const TargetLayout front = layoutOf(v, threeZones(), {.slackBefore = {2, 1}});
        EXPECT_EQ(runsText(front.zones[0].pieces), "[0,2)"); // A front zone ignores slack; later ones never go back.
        EXPECT_EQ(runsText(front.zones[1].pieces), "[3,5)");
        const TargetLayout capped = layoutOf(v, s, {.slackBefore = {99, 99}});
        EXPECT_EQ(runsText(capped.zones[1].pieces), "[8,10)");
    }

}} // namespace icd::core::planner
