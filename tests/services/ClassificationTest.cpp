// Classification of a volume for the Summary tab and the class map (T074): per-class files and bytes add up to the
// snapshot, in catalog order, the same for any executor, and appear in the JSON report.

#include "core/classify/Catalog.h"
#include "core/synth/SyntheticVolume.h"
#include "services/exec/ThreadPool.h"
#include "services/scan/AnalysisReport.h"
#include "services/scan/Classification.h"

#include <gtest/gtest.h>

#include <numeric>

namespace icd::services { namespace {

    // A synthetic volume with the default class mix (sources, photos, archives, videos, ...).
    core::Snapshot volume() {
        core::SynthOptions o;
        o.files = 5000;
        o.geometry = {4096, 1ULL << 22U};
        return core::generateSyntheticVolume(o);
    }

    TEST(Classification, ClassRowsAddUpToTheVolume) {
        const core::Snapshot s = volume();
        core::InlineExecutor serial;
        const Classification c = classifyVolume(s, {}, 0, serial);
        ASSERT_EQ(c.classes.size(), s.files.size());
        std::uint64_t files = 0;
        std::uint64_t bytes = 0;
        for (const ClassSummary& row : c.summary) {
            files += row.files;
            bytes += row.bytes;
            EXPECT_GT(row.files, 0U) << row.id;
            EXPECT_FALSE(row.inZonePercent.has_value()); // No strategy yet.
        }
        std::uint64_t clusters = 0;
        for (core::FileIndex f = 0; f < s.files.size(); ++f) {
            clusters += s.files.allocatedClusters(f);
        }
        EXPECT_EQ(files, s.files.size());
        EXPECT_EQ(bytes, clusters * s.geometry.bytesPerCluster);
        // Catalog order, and the synthetic mix reaches several classes.
        const auto& catalog = core::classify::builtinCatalog();
        for (std::size_t i = 1; i < c.summary.size(); ++i) {
            EXPECT_LT(*catalog.find(c.summary[i - 1].id), *catalog.find(c.summary[i].id));
        }
        EXPECT_GE(c.summary.size(), 5U);
        ThreadPool pool(4);
        const Classification p = classifyVolume(s, {}, 0, pool);
        EXPECT_EQ(p.classes, c.classes);
        ASSERT_EQ(p.summary.size(), c.summary.size());
        for (std::size_t i = 0; i < p.summary.size(); ++i) {
            EXPECT_EQ(p.summary[i].files, c.summary[i].files);
            EXPECT_EQ(p.summary[i].bytes, c.summary[i].bytes);
        }
    }

    TEST(Classification, ChurnTurnsFilesIntoTemp) {
        const core::Snapshot s = volume();
        core::InlineExecutor serial;
        const std::vector<std::uint8_t> churn(s.files.size(), 1);
        const Classification c = classifyVolume(s, churn, 0, serial);
        const auto temp = *core::classify::builtinCatalog().find("temp");
        const auto directories = *core::classify::builtinCatalog().find("directories");
        for (core::FileIndex f = 0; f < s.files.size(); ++f) {
            const bool structural = s.files.has(f, core::FileDirectory) || s.files.has(f, core::FileMetadata);
            if (!structural) {
                ASSERT_EQ(c.classes[f], temp) << s.path(f); // The churn rule comes before every content class.
            }
            else if (s.files.has(f, core::FileDirectory) && !s.files.has(f, core::FileMetadata)) {
                ASSERT_EQ(c.classes[f], directories);
            }
        }
    }

}} // namespace icd::services
