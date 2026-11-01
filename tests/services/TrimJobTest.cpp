// TRIM output handling (T044): defrag's progress lines end in a bare CR, other lines in CRLF, and chunks from
// the pipe split lines anywhere.

#include "services/trim/TrimJob.h"

#include <gtest/gtest.h>

namespace {

    using icd::services::LineSplitter;
    using icd::services::retrimPercent;

    TEST(TrimJob, SplitsOnCrAndLfAcrossChunks) {
        LineSplitter splitter;
        EXPECT_TRUE(splitter.feed("Invoking retr").empty());
        EXPECT_EQ(splitter.feed("im...\r\n\tRetrim:  0% complete...\r\tRetrim:  1"),
                  (std::vector<std::string>{"Invoking retrim...", "", "\tRetrim:  0% complete..."}));
        EXPECT_EQ(splitter.feed("00% complete.  \r\n"), (std::vector<std::string>{"\tRetrim:  100% complete.  ", ""}));
        EXPECT_FALSE(splitter.finish().has_value());
        splitter.feed("tail");
        EXPECT_EQ(splitter.finish(), "tail");
        EXPECT_FALSE(splitter.finish().has_value());
    }

    TEST(TrimJob, ReadsRetrimPercent) {
        EXPECT_EQ(retrimPercent("\tRetrim:  0% complete..."), 0);
        EXPECT_EQ(retrimPercent("Retrim:  75% complete..."), 75);
        EXPECT_EQ(retrimPercent("  Retrim:  100% complete.  "), 100);
        EXPECT_FALSE(retrimPercent("Retrim:").has_value());
        EXPECT_FALSE(retrimPercent("\tRetrim:").has_value());
        EXPECT_FALSE(retrimPercent("Retrim: 101% complete").has_value());
        EXPECT_FALSE(retrimPercent("Total space trimmed = 208.62 GB").has_value());
        EXPECT_FALSE(retrimPercent("Retrim: 5 complete").has_value());
    }

    TEST(TrimJob, OutputIsTakenOnce) {
        icd::services::TrimOutput output;
        output.push("a");
        output.push("b");
        EXPECT_EQ(output.take(), (std::vector<std::string>{"a", "b"}));
        EXPECT_TRUE(output.take().empty());
    }

} // namespace
