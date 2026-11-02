// Lower bound and target fitting (T083): which files must move on hand-made layouts with known minimum bytes, and
// the slack placement against brute force over every slack split on random small volumes.

#include "core/planner/LowerBound.h"
#include "core/synth/SynthRandom.h"

#include "core/PlannerTestUtil.h"

#include <gtest/gtest.h>

namespace icd::core::planner { namespace {

    using strategy::Anchor;
    using strategy::ZoneAction;
    using test::pictureVolume;
    using test::zone;

    // Letters: A photos, B archives, V video, T temp, U unknown.
    const test::PictureFacts facts{
        .classes = {{'A', "photos"}, {'B', "archives"}, {'C', "archives"}, {'V', "video"}, {'T', "temp"}}};

    // Photos then archives, both free to shift, video at the back.
    strategy::ZoneScript shiftable() {
        return test::script({zone("Photos", {"photos"}, ZoneAction::FastFill),
                             zone("Archives", {"archives"}, ZoneAction::FastFill),
                             zone("Video", {"video"}, ZoneAction::PackToBack, Anchor::Back)});
    }

    // Lower bound of a picture under a script and layout inputs.
    ClusterCount boundOf(const test::PictureVolume& v, const strategy::ZoneScript& s, const LayoutInputs& in,
                         GoodEnough rules = {}) {
        InlineExecutor serial;
        const ZoneAssignment a = test::assign(v, s);
        const TargetLayout layout = resolveLayout(v.snapshot, s, a, v.classes, classify::builtinCatalog(), in);
        return lowerBound(v.snapshot, s, a, layout, rules, serial).clusters;
    }

    // The lower bound after fitting the target.
    ClusterCount fittedBound(const test::PictureVolume& v, const strategy::ZoneScript& s, GoodEnough rules = {}) {
        InlineExecutor serial;
        const ZoneAssignment a = test::assign(v, s);
        const LayoutInputs in = fitTarget(v.snapshot, s, a, v.classes, classify::builtinCatalog(), {}, rules, serial);
        return boundOf(v, s, in, rules);
    }

    TEST(LowerBound, FragmentsOkFollowsFragmentOk) {
        const std::vector<ClusterRun> one{{0, 4}, {4, 4}}; // Split record, one fragment.
        const std::vector<ClusterRun> two{{0, 4}, {10, 2}};
        EXPECT_TRUE(fragmentsOk(one, std::nullopt, 4096));
        EXPECT_FALSE(fragmentsOk(two, std::nullopt, 4096));
        EXPECT_TRUE(fragmentsOk(two, 2 * 4096, 4096));
        EXPECT_FALSE(fragmentsOk(two, 3 * 4096, 4096)); // The 2-cluster tail is too small.
        EXPECT_TRUE(fragmentsOk({}, std::nullopt, 4096));
        const auto s = shiftable();
        EXPECT_EQ(fragmentOkFor(s, 0, {}), strategy::Zone::defaultFragmentOk);
        EXPECT_EQ(fragmentOkFor(s, noZone, {}), strategy::Zone::defaultFragmentOk);
        EXPECT_EQ(fragmentOkFor(s, 0, {.wholeFiles = true}), std::nullopt);
    }

    TEST(LowerBound, CountsFilesOutsideTheirZoneAndFragmentedOnes) {
        // Photos zone [0,2), archives [2,5), video [10,12) with no slack shifting.
        //  0 1 2 3 4 5 6 7 8 9 10 11
        //  A A B . B C . T U . V  V
        auto s = shiftable();
        s.zones[0].anchor = Anchor::Front;
        s.zones.insert(s.zones.begin(), zone("Churn", {"temp"}, ZoneAction::KeepInPlace));
        s.zones[2].fragmentOk = 4096; // Archive fragments of one cluster are fine.
        const auto v = pictureVolume("AAB.BC.TU.VV", facts);
        InlineExecutor serial;
        const ZoneAssignment a = test::assign(v, s);
        const TargetLayout layout = resolveLayout(v.snapshot, s, a, v.classes, classify::builtinCatalog(), {});
        const LowerBound lb = lowerBound(v.snapshot, s, a, layout, {}, serial);
        EXPECT_EQ(lb.mustMove[v.files.at('A')], 0); // In place.
        EXPECT_EQ(lb.mustMove[v.files.at('B')], 0); // Two fragments, each good enough, inside [2,5).
        EXPECT_EQ(lb.mustMove[v.files.at('C')], 1); // Archives end at 5 (the churn file is an obstacle at 7).
        EXPECT_EQ(lb.mustMove[v.files.at('T')], 0); // Churn stays.
        EXPECT_EQ(lb.mustMove[v.files.at('U')], 0); // No zone, one fragment: defragmented already.
        EXPECT_EQ(lb.mustMove[v.files.at('V')], 0); // Packed against the end.
        EXPECT_EQ(lb.clusters, 1U);
        EXPECT_EQ(lb.perZone, (std::vector<ClusterCount>{0, 0, 1, 0, 0}));
        // At Maximum B must become one fragment.
        EXPECT_EQ(lowerBound(v.snapshot, s, a, layout, {.wholeFiles = true}, serial).clusters, 3U);
    }

    TEST(LowerBound, DefragmentOnlyFilesMoveOnlyWhenFragmented) {
        const auto v = pictureVolume("U.UX..", facts);
        const auto s = test::script({zone("Rest", {}, ZoneAction::DefragmentOnly)});
        EXPECT_EQ(boundOf(v, s, {}, {.wholeFiles = true}), 2U); // U in two pieces; X fine.
        EXPECT_EQ(boundOf(v, s, {}), 2U); // One-cluster fragments are under 64 MB.
    }

    TEST(LowerBound, SlackMovesZonesTowardsTheFiles) {
        //  0 1 2 3 4 5 6 7 8 9 10
        //  . . . . A A . B B . .   Photos stay at slack 4, archives at slack 5.
        const auto v = pictureVolume("....AA.BB..", facts);
        EXPECT_EQ(boundOf(v, shiftable(), {}), 4U);
        EXPECT_EQ(fittedBound(v, shiftable()), 0U);
        InlineExecutor serial;
        const auto in = fitTarget(v.snapshot, shiftable(), test::assign(v, shiftable()), v.classes,
                                  classify::builtinCatalog(), {}, {}, serial);
        EXPECT_EQ(in.slackBefore, (std::vector<ClusterCount>{4, 5}));
    }

    TEST(LowerBound, SlackCannotReorderZones) {
        // Archives sit before photos: at most one of them can stay; the heavier wins.
        const auto v = pictureVolume("...BBAAA....", facts);
        EXPECT_EQ(fittedBound(v, shiftable()), 2U); // Photos (3 clusters) stay at slack 5, archives move.
        const auto w = pictureVolume("...BBBAA....", facts);
        EXPECT_EQ(fittedBound(w, shiftable()), 2U); // Archives (3) stay at slack 1, photos move.
        const auto x = pictureVolume("BB.......AAA", facts);
        EXPECT_EQ(fittedBound(x, shiftable()), 5U); // Neither can stay: photos cannot reach the end.
    }

    TEST(LowerBound, FrontZonesNeverShift) {
        auto s = shiftable();
        s.zones[0].anchor = Anchor::Front;
        const auto v = pictureVolume("..AA..BB..", facts);
        EXPECT_EQ(fittedBound(v, s), 2U); // Photos must come to the front; archives can stay.
    }

    // The least lower bound over every non-decreasing slack split, by brute force.
    ClusterCount bruteForceBound(const test::PictureVolume& v, const strategy::ZoneScript& s, ClusterCount slack,
                                 std::size_t zones, GoodEnough rules) {
        ClusterCount best = std::numeric_limits<ClusterCount>::max();
        std::vector<ClusterCount> split(zones, 0);
        while (true) {
            best = std::min(best, boundOf(v, s, {.slackBefore = split}, rules));
            std::size_t z = zones;
            while (z > 0 && split[z - 1] == slack) {
                --z;
            }
            if (z == 0) {
                return best;
            }
            ++split[z - 1];
            for (std::size_t k = z; k < zones; ++k) {
                split[k] = split[z - 1];
            }
        }
    }

    TEST(LowerBound, FittingEqualsBruteForceOnRandomVolumes) {
        SynthRandom random(5);
        const std::string letters = "AABBCV..UT#";
        int shifted = 0; // Rounds where fitting beat the plain layout.
        for (int round = 0; round < 300; ++round) {
            std::string picture;
            const std::size_t length = 8 + random.below(8);
            for (std::size_t i = 0; i < length; ++i) {
                picture += letters[random.below(letters.size())];
            }
            auto s = shiftable();
            s.zones.insert(s.zones.begin() + 1, zone("Temp", {"temp"}, ZoneAction::FastFill));
            const GoodEnough rules{.wholeFiles = random.below(2) == 0};
            const auto v = pictureVolume(picture, facts);
            const ZoneAssignment a = test::assign(v, s);
            const TargetLayout plain = resolveLayout(v.snapshot, s, a, v.classes, classify::builtinCatalog(), {});
            const ClusterCount expect = bruteForceBound(v, s, plain.slack, 3, rules);
            EXPECT_EQ(fittedBound(v, s, rules), expect) << picture;
            shifted += expect < boundOf(v, s, {}, rules) ? 1 : 0;
        }
        EXPECT_GT(shifted, 30); // The random volumes exercise the slack choice.
    }

}} // namespace icd::core::planner
