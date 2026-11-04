#pragma once

#include "services/job/JobRunner.h"

#include <chrono>
#include <cstdint>
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

    // What Windows recorded for a finished TRIM, so the UI can say what happened: retrim of a fast SSD takes about a
    // second and prints little.
    struct TrimResult {
        std::optional<std::chrono::system_clock::time_point> lastRun; // The optimizer's "Last run" after the TRIM.
        std::optional<std::uint64_t>
            bytesTrimmed; // Free space this TRIM trimmed; empty when Windows did not record it.
    };

    // Output lines of a running TRIM, handed from the job's thread to the UI, which takes them when progress
    // arrives and once more when the job ends. Progress lines are not kept, only their percentage is reported.
    class TrimOutput {
    public:
        // Adds a line (job thread).
        void push(std::string line);

        // Removes and returns the lines collected so far (UI thread).
        [[nodiscard]] std::vector<std::string> take();

        // Keeps what Windows recorded after the TRIM (job thread, once at the end).
        void finish(TrimResult result);

        // What Windows recorded, once the job ended successfully (UI thread).
        [[nodiscard]] std::optional<TrimResult> result();

    private:
        std::mutex mutex_; // Guards lines_ and result_.
        std::vector<std::string> lines_; // Not yet taken.
        std::optional<TrimResult> result_; // Set when the TRIM succeeded.
    };

    // The result of a TRIM from the optimizer's statistics read after it: the trimmed bytes count only when the
    // statistics were written by this run (last run not before `started`, less a second of clock rounding).
    [[nodiscard]] TrimResult trimResult(std::optional<std::chrono::system_clock::time_point> lastRun,
                                        std::uint64_t bytesTrimmed, std::chrono::system_clock::time_point started);

    // TRIM of an SSD (plan.md 10.5): runs Windows' "defrag <volume> /L" and reports its percentage as
    // progress; other non-empty output lines go to `output`, and at the end what Windows recorded for the volume
    // (volumeName: its GUID path). Cancel terminates defrag (TRIM is only a hint to the drive, so stopping it halfway
    // is harmless). Fails when defrag exits with an error.
    [[nodiscard]] JobFunction makeTrimJob(std::string root, std::string volumeName, std::shared_ptr<TrimOutput> output);

} // namespace icd::services
