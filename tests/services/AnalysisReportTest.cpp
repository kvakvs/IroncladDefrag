// Analysis report of icd-cli analyze (T049): the summary in the JSON is the GUI's AnalysisSummary, runs are joined
// where records continue on disk, and flag names follow the file flags.

#include "core/snapshot/SnapshotBuilder.h"
#include "services/scan/AnalysisReport.h"

#include <gtest/gtest.h>

namespace {

    using namespace icd;

    // A small analysed volume: a root, one file split into a continuing run and a second fragment.
    services::AnalysisResult smallResult() {
        core::SnapshotBuilder builder({4096, 1000});
        const core::FileIndex root = builder.addFile({"", core::noFile, 0, core::FileDirectory, {}, 0});
        const core::FileIndex file = builder.addFile({"a \"b\".bin", root, 12288, core::FileCompressed, {}, 0});
        builder.addExtent({{10, 2}, 0, file, 0, 0});
        builder.addExtent({{12, 1}, 2, file, 0, 0}); // Continues the first run on disk.
        builder.addExtent({{50, 4}, 3, file, 0, 0});
        builder.addFreeRun({100, 900});
        services::AnalysisResult result{std::move(builder).build(), {}, {}, {}, 1.5};
        result.summary.fragmentationPercent = 100.0;
        result.summary.files = 1;
        result.summary.fragmentedFiles = 1;
        result.summary.freeBytes = 900 * 4096;
        result.summary.scan = services::AnalysisSummary::ScanFigures{.directories = 1};
        result.snapshot.usn = core::UsnCursor{0xABC, 77};
        return result;
    }

    TEST(AnalysisReport, JsonCarriesTheGuiSummary) {
        core::DriveInfo drive;
        drive.root = "T:\\";
        drive.label = "ICD-TEST";
        drive.fileSystem = "NTFS";
        const std::string json = services::analysisJson(drive, smallResult(), false);
        EXPECT_NE(json.find("\"root\": \"T:\\\\\""), std::string::npos);
        EXPECT_NE(json.find("\"fragmentationPercent\": 100"), std::string::npos);
        EXPECT_NE(json.find("\"freeBytes\": 3686400"), std::string::npos);
        EXPECT_NE(json.find("\"directories\": 1"), std::string::npos);
        EXPECT_NE(json.find("\"journalId\": \"0x0000000000000abc\""), std::string::npos);
        EXPECT_NE(json.find("\"nextUsn\": 77"), std::string::npos);
        EXPECT_EQ(json.find("\"runs\""), std::string::npos); // No extents without --dump-extents.
    }

    TEST(AnalysisReport, DumpJoinsContinuingRuns) {
        const std::string json = services::analysisJson({}, smallResult(), true);
        EXPECT_NE(json.find("\"path\": \"\\\\a \\\"b\\\".bin\""), std::string::npos);
        EXPECT_NE(json.find("\"compressed\""), std::string::npos);
        // [10, 3, 0] (two records joined) and [50, 4, 0], compared without the indentation.
        std::string compact = json;
        std::erase_if(compact, [](char c) { return c == ' ' || c == '\n'; });
        EXPECT_NE(compact.find("\"runs\":[[10,3,0],[50,4,0]]"), std::string::npos);
    }

    TEST(AnalysisReport, FlagNames) {
        EXPECT_EQ(services::fileFlagNames(core::FileDirectory | core::FileUnmovable),
                  (std::vector<std::string>{"directory", "unmovable"}));
        EXPECT_TRUE(services::fileFlagNames(0).empty());
    }

} // namespace
