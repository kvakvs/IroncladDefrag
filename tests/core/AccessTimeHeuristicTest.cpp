// Last-access evidence and verdict (T072): which files count as read after their last change, and how the system
// setting and the evidence combine.

#include "core/classify/AccessTimeHeuristic.h"
#include "core/snapshot/SnapshotBuilder.h"
#include "services/exec/ThreadPool.h"

#include <gtest/gtest.h>

namespace icd::core::classify { namespace {

    // FILETIME ticks per hour.
    constexpr std::int64_t hour = 36'000'000'000;

    // A base time for the hand-made files.
    constexpr std::int64_t t0 = 100'000 * hour;

    // A snapshot whose regular files have the given times, plus a directory and a metadata file that never count.
    Snapshot withTimes(const std::vector<FileTimes>& times) {
        SnapshotBuilder b({4096, 100});
        const FileIndex root = b.addFile({"", noFile, 0, FileDirectory, {t0, t0, t0 + 1000 * hour}, 5});
        b.addFile({"$LogFile", root, 0, FileMetadata, {t0, t0, t0 + 1000 * hour}, 2});
        std::uint64_t reference = 100;
        for (const FileTimes& t : times) {
            b.addFile({"f", root, 1, 0, t, reference++});
        }
        return std::move(b).build();
    }

    TEST(AccessTimeHeuristic, CountsReadsAfterTheLastChange) {
        const Snapshot s = withTimes({
            {t0, t0, t0}, // Never read after writing.
            {t0, t0, t0 + hour / 2}, // Within NTFS's hour of slack: proves nothing.
            {t0, t0, t0 + 2 * hour}, // Read two hours after writing.
            {t0 + 10 * hour, t0, t0 + 10 * hour}, // Copied: creation (and access) at copy time, modified earlier.
            {t0, t0 + 50 * 24 * hour, t0 + 50 * 24 * hour + 2 * hour}, // Read recently.
        });
        InlineExecutor inline_;
        const AccessTimeEvidence e = gatherAccessEvidence(s, inline_);
        EXPECT_EQ(e.files, 5U); // The directory and the metadata file do not count.
        EXPECT_EQ(e.readAfterWrite, 2U);
        EXPECT_EQ(e.readRecently, 1U); // Only the last one is within 30 days of the newest time.
        services::ThreadPool pool(3);
        const AccessTimeEvidence p = gatherAccessEvidence(s, pool);
        EXPECT_EQ(p.readAfterWrite, e.readAfterWrite);
        EXPECT_EQ(p.readRecently, e.readRecently);
    }

    TEST(AccessTimeHeuristic, SettingAndEvidenceCombine) {
        const AccessTimeEvidence many{.files = 1000, .readAfterWrite = 200, .readRecently = 50};
        const AccessTimeEvidence none{.files = 1000, .readAfterWrite = 0, .readRecently = 0};
        const AccessTimeEvidence few{.files = 1000, .readAfterWrite = 3, .readRecently = 0};
        const AccessTimeEvidence middle{.files = 1000, .readAfterWrite = 20, .readRecently = 0};
        const AccessTimeEvidence tiny{.files = 50, .readAfterWrite = 50, .readRecently = 50};
        EXPECT_EQ(assessAccessTimes(false, many).verdict, AccessTimeVerdict::Unreliable); // Off wins.
        EXPECT_EQ(assessAccessTimes(true, many).verdict, AccessTimeVerdict::Reliable);
        EXPECT_EQ(assessAccessTimes(true, few).verdict, AccessTimeVerdict::Reliable);
        EXPECT_EQ(assessAccessTimes(true, none).verdict, AccessTimeVerdict::Unknown);
        EXPECT_EQ(assessAccessTimes(std::nullopt, many).verdict, AccessTimeVerdict::Reliable);
        EXPECT_EQ(assessAccessTimes(std::nullopt, few).verdict, AccessTimeVerdict::Unreliable);
        EXPECT_EQ(assessAccessTimes(std::nullopt, middle).verdict, AccessTimeVerdict::Unknown);
        EXPECT_EQ(assessAccessTimes(std::nullopt, tiny).verdict, AccessTimeVerdict::Unknown); // Too few files.
        EXPECT_NE(assessAccessTimes(true, many).reason.find("20.0%"), std::string::npos);
        EXPECT_EQ(verdictName(AccessTimeVerdict::Unreliable), "Unreliable");
    }

}} // namespace icd::core::classify
