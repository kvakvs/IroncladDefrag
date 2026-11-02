#include "core/classify/AccessTimeHeuristic.h"

#include <algorithm>
#include <format>

namespace icd::core::classify {

    namespace {

        // FILETIME ticks per hour: NTFS writes access times at most once an hour, so smaller gaps prove nothing.
        constexpr std::int64_t hour = 36'000'000'000;

        // How recent "recently" is.
        constexpr std::int64_t thirtyDays = std::int64_t{30} * 24 * hour;

        // Files per chunk of the parallel passes.
        constexpr std::size_t fileGrain = 16384;

        // Fewest files that let the evidence decide.
        constexpr std::uint64_t minFiles = 100;

        // Shares (in thousandths) of files read after writing that decide without a setting.
        constexpr std::uint64_t reliablePerMille = 50;
        constexpr std::uint64_t unreliablePerMille = 5;

        // Whether a file says something about access times: a regular file of the user's.
        bool counts(const FileTable& files, FileIndex f) {
            constexpr std::uint16_t skip = FileDirectory | FileMetadata;
            return (files.flags(f) & skip) == 0 && files.fileReference(f) != 0;
        }

        // Adds one counted file's times to the evidence.
        void addFile(const FileTimes& t, std::int64_t newest, AccessTimeEvidence& e) {
            const bool read = t.accessed > std::max(t.created, t.modified) + hour;
            ++e.files;
            e.readAfterWrite += read ? 1 : 0;
            e.readRecently += read && t.accessed >= newest - thirtyDays ? 1 : 0;
        }

        // Adds two evidence counts.
        AccessTimeEvidence plus(AccessTimeEvidence a, const AccessTimeEvidence& b) {
            a.files += b.files;
            a.readAfterWrite += b.readAfterWrite;
            a.readRecently += b.readRecently;
            return a;
        }

        // The newest timestamp of any kind among the counted files (the snapshot's "now").
        std::int64_t newestTime(const Snapshot& s, Executor& executor, const CancelToken& token) {
            return parallelReduce(
                executor, {s.files.size(), fileGrain}, std::int64_t{0},
                [&](std::size_t begin, std::size_t end) {
                    std::int64_t newest = 0;
                    for (std::size_t f = begin; f < end; ++f) {
                        const FileTimes& t = s.files.times(static_cast<FileIndex>(f));
                        newest = std::max({newest, t.created, t.modified, t.accessed});
                    }
                    return newest;
                },
                [](std::int64_t a, std::int64_t b) { return std::max(a, b); }, token);
        }

        // The share as a percentage with one decimal.
        std::string percent(std::uint64_t part, std::uint64_t whole) {
            return std::format("{:.1f}%",
                               whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole));
        }

        // The verdict from the evidence alone.
        AccessTimeAssessment fromEvidence(const AccessTimeEvidence& e, std::uint64_t perMille) {
            const std::string share = percent(e.readAfterWrite, e.files);
            if (e.files < minFiles) {
                return {AccessTimeVerdict::Unknown, std::format("too few files to tell ({})", e.files), e};
            }
            if (perMille >= reliablePerMille) {
                return {AccessTimeVerdict::Reliable,
                        std::format("{} of files were read after their last change", share), e};
            }
            if (perMille < unreliablePerMille) {
                return {AccessTimeVerdict::Unreliable,
                        std::format("only {} of files were read after their last change", share), e};
            }
            return {AccessTimeVerdict::Unknown, std::format("{} of files were read after their last change", share), e};
        }

    } // namespace

    std::string_view verdictName(AccessTimeVerdict verdict) {
        switch (verdict) {
        case AccessTimeVerdict::Reliable:
            return "Reliable";
        case AccessTimeVerdict::Unreliable:
            return "Unreliable";
        default:
            return "Unknown";
        }
    }

    AccessTimeEvidence gatherAccessEvidence(const Snapshot& snapshot, Executor& executor, const CancelToken& token) {
        const std::int64_t newest = newestTime(snapshot, executor, token);
        const FileTable& files = snapshot.files;
        return parallelReduce(
            executor, {files.size(), fileGrain}, AccessTimeEvidence{},
            [&](std::size_t begin, std::size_t end) {
                AccessTimeEvidence e;
                for (std::size_t i = begin; i < end; ++i) {
                    const auto f = static_cast<FileIndex>(i);
                    if (counts(files, f)) {
                        addFile(files.times(f), newest, e);
                    }
                }
                return e;
            },
            plus, token);
    }

    AccessTimeAssessment assessAccessTimes(std::optional<bool> updatesEnabled, const AccessTimeEvidence& evidence) {
        const std::uint64_t perMille = evidence.files == 0 ? 0 : evidence.readAfterWrite * 1000 / evidence.files;
        if (updatesEnabled == false) {
            return {AccessTimeVerdict::Unreliable, "NTFS last-access updates are turned off", evidence};
        }
        if (updatesEnabled == true && evidence.readAfterWrite > 0) {
            return {AccessTimeVerdict::Reliable,
                    std::format("NTFS updates them; {} of files were read after their last change",
                                percent(evidence.readAfterWrite, evidence.files)),
                    evidence};
        }
        if (updatesEnabled == true) {
            return {AccessTimeVerdict::Unknown, "NTFS updates them, but no file shows a read after its last change yet",
                    evidence};
        }
        return fromEvidence(evidence, perMille);
    }

} // namespace icd::core::classify
