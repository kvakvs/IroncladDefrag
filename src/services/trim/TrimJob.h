#pragma once

#include "services/job/JobRunner.h"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace icd::services {

    // Cuts streamed console output into lines. defrag.exe ends progress lines with a bare CR (it redraws them
    // in place) and other lines with CRLF, so both CR and LF end a line.
    class LineSplitter {
    public:
        // Adds a chunk and returns the lines it completed (without line ends; empty lines included).
        std::vector<std::string> feed(std::string_view chunk);

        // The unfinished last line, if any (output that ended without a line end).
        std::optional<std::string> finish();

    private:
        std::string pending_; // Characters after the last line end.
    };

    // "Retrim:  45% complete..." -> 45; nullopt for other lines.
    [[nodiscard]] std::optional<int> retrimPercent(std::string_view line);

    // Output lines of a running TRIM, handed from the job's thread to the UI, which takes them when progress
    // arrives and once more when the job ends. Progress lines are not kept, only their percentage is reported.
    class TrimOutput {
    public:
        // Adds a line (job thread).
        void push(std::string line);

        // Removes and returns the lines collected so far (UI thread).
        [[nodiscard]] std::vector<std::string> take();

    private:
        std::mutex mutex_; // Guards lines_.
        std::vector<std::string> lines_; // Not yet taken.
    };

    // TRIM of an SSD (plan.md 10.5): runs Windows' "defrag <volume> /L" and reports its percentage as
    // progress; other non-empty output lines go to `output`. Cancel terminates defrag (TRIM is only a hint to
    // the drive, so stopping it halfway is harmless). Fails when defrag exits with an error.
    [[nodiscard]] JobFunction makeTrimJob(std::string root, std::shared_ptr<TrimOutput> output);

} // namespace icd::services
