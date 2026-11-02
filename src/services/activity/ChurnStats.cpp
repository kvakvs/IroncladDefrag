#include "services/activity/ChurnStats.h"

#include <algorithm>

namespace icd::services {

    namespace {

        // USN_REASON_* bits (winioctl.h) the counts use.
        constexpr std::uint32_t reasonOverwrite = 0x0000'0001U; // USN_REASON_DATA_OVERWRITE.
        constexpr std::uint32_t reasonExtend = 0x0000'0002U; // USN_REASON_DATA_EXTEND.
        constexpr std::uint32_t reasonTruncate = 0x0000'0004U; // USN_REASON_DATA_TRUNCATION.
        constexpr std::uint32_t reasonCreate = 0x0000'0100U; // USN_REASON_FILE_CREATE.
        constexpr std::uint32_t reasonDelete = 0x0000'0200U; // USN_REASON_FILE_DELETE.
        constexpr std::uint32_t reasonRenameNew = 0x0000'2000U; // USN_REASON_RENAME_NEW_NAME.
        constexpr std::uint32_t reasonClose = 0x8000'0000U; // USN_REASON_CLOSE: the session's summary record.

        // Files per chunk of the parallel loops.
        constexpr std::size_t fileGrain = 16384;

        // Fewest deletes that count as weak churn evidence: one deleted file says nothing.
        constexpr std::uint32_t minWeakDeletes = 2;

        // Parent walks stop here; deeper chains only occur in damaged data.
        constexpr int maxDepth = 64;

        // Adds one closed session's creation or data change to counts (deletes are counted when they happen).
        void countSession(std::uint32_t reason, ChurnCounts& c) {
            const bool created = (reason & reasonCreate) != 0;
            const bool deleted = (reason & reasonDelete) != 0;
            const bool data = (reason & (reasonOverwrite | reasonExtend | reasonTruncate)) != 0;
            c.creates += created ? 1 : 0;
            c.overwrites += data && !created && !deleted ? 1 : 0;
        }

        // The counts of a reference, or null.
        const ChurnCounts* countsOf(const std::unordered_map<std::uint64_t, ChurnCounts>& map,
                                    std::uint64_t reference) {
            const auto it = map.find(reference);
            return it == map.end() ? nullptr : &it->second;
        }

        // How a directory's own children churn: not at all, a little (some created files deleted again), or enough to
        // count on their own.
        enum class DirectoryChurn : std::uint8_t { None, Weak, Strong };

        // The churn evidence of one snapshot file (only user directories have any).
        DirectoryChurn evidenceOf(const core::Snapshot& s, core::FileIndex f, const ChurnStats& stats,
                                  const ChurnOptions& options) {
            const std::uint64_t reference = s.files.fileReference(f);
            const bool directory = s.files.has(f, core::FileDirectory) && !s.files.has(f, core::FileMetadata);
            const ChurnCounts* c = reference == 0 || !directory ? nullptr : countsOf(stats.directories, reference);
            if (c == nullptr || c->deletes < minWeakDeletes ||
                std::uint64_t{c->deletes} * 100 < std::uint64_t{c->creates} * options.deleteSharePercent) {
                return DirectoryChurn::None;
            }
            return directoryChurns(*c, options) ? DirectoryChurn::Strong : DirectoryChurn::Weak;
        }

        // Whether most directories beside f (same parent, at least two of them) churn strongly.
        bool siblingsChurn(const core::Snapshot& s, core::FileIndex f, const std::vector<DirectoryChurn>& evidence) {
            const core::FileIndex parent = s.files.parent(f);
            if (parent == core::noFile || parent == f) {
                return false;
            }
            std::size_t directories = 0;
            std::size_t strong = 0;
            for (const core::FileIndex child : s.tree.children(parent)) {
                const bool other = child != f && s.files.has(child, core::FileDirectory);
                directories += other ? 1 : 0;
                strong += other && evidence[child] == DirectoryChurn::Strong ? 1 : 0;
            }
            return directories >= 2 && strong * 2 > directories;
        }

        // Per file of a snapshot: whether it is a churning directory. Strong evidence counts on its own; weak evidence
        // counts when most sibling directories churn strongly (the last, small folder of a build tree).
        std::vector<std::uint8_t> churningDirectories(const core::Snapshot& s, const ChurnStats& stats,
                                                      const ChurnOptions& options, core::Executor& executor,
                                                      const core::CancelToken& token) {
            std::vector<DirectoryChurn> evidence(s.files.size(), DirectoryChurn::None);
            executor.parallelFor(
                {evidence.size(), fileGrain},
                [&](std::size_t begin, std::size_t end) {
                    for (std::size_t f = begin; f < end; ++f) {
                        evidence[f] = evidenceOf(s, static_cast<core::FileIndex>(f), stats, options);
                    }
                },
                token);
            std::vector<std::uint8_t> out(s.files.size(), 0);
            executor.parallelFor(
                {out.size(), fileGrain},
                [&](std::size_t begin, std::size_t end) {
                    for (std::size_t f = begin; f < end; ++f) {
                        const auto file = static_cast<core::FileIndex>(f);
                        out[f] = evidence[f] == DirectoryChurn::Strong ||
                            (evidence[f] == DirectoryChurn::Weak && siblingsChurn(s, file, evidence));
                    }
                },
                token);
            return out;
        }

        // Whether a file churns by its own events.
        bool fileChurns(const core::Snapshot& s, core::FileIndex f, const ChurnStats& stats,
                        const ChurnOptions& options) {
            const std::uint64_t reference = s.files.fileReference(f);
            const ChurnCounts* c = reference == 0 ? nullptr : countsOf(stats.files, reference);
            return c != nullptr && c->total() >= options.fileEvents;
        }

        // Whether a file is a churning directory or lies below one.
        bool belowChurn(const core::Snapshot& s, core::FileIndex f, const std::vector<std::uint8_t>& churning) {
            core::FileIndex at = f;
            for (int depth = 0; depth < maxDepth && at != core::noFile; ++depth) {
                if (churning[at] != 0) {
                    return true;
                }
                const core::FileIndex parent = s.files.parent(at);
                at = parent == at ? core::noFile : parent;
            }
            return false;
        }

    } // namespace

    void ChurnStats::add(std::span<const platform::UsnChange> changes) {
        records += changes.size();
        for (const platform::UsnChange& c : changes) {
            if ((c.reason & (reasonDelete | reasonRenameNew)) == 0) {
                homes_[c.fileReference] = c.parentReference;
            }
            if ((c.reason & reasonDelete) != 0 && deleted_.insert(c.fileReference).second) {
                ++files[c.fileReference].deletes;
                ++directories[directoryOf(c)].deletes;
            }
            if ((c.reason & reasonClose) == 0) {
                continue;
            }
            ++sessions;
            countSession(c.reason, files[c.fileReference]);
            countSession(c.reason, directories[directoryOf(c)]);
        }
    }

    std::uint64_t ChurnStats::directoryOf(const platform::UsnChange& c) const {
        if ((c.reason & reasonDelete) == 0) {
            return c.parentReference;
        }
        const auto home = homes_.find(c.fileReference);
        return home == homes_.end() ? c.parentReference : home->second;
    }

    ChurnStats collectChurn(HANDLE volume, const core::CancelToken& token) {
        ChurnStats stats;
        for (int attempt = 0; attempt < 2; ++attempt) { // Once more if the journal wrapped while being read.
            const auto journal = platform::queryUsnJournal(volume);
            if (!journal) {
                return {};
            }
            stats = {};
            stats.firstUsn = std::max(journal->firstUsn, journal->lowestValidUsn);
            stats.nextUsn = journal->nextUsn;
            const auto status = platform::readUsnJournal(
                volume, {journal->journalId, stats.firstUsn, stats.nextUsn},
                [&](std::span<const platform::UsnChange> batch) { stats.add(batch); }, token);
            if (status == platform::UsnReadStatus::Complete) {
                break;
            }
        }
        return stats;
    }

    bool directoryChurns(const ChurnCounts& children, const ChurnOptions& options) {
        const std::uint64_t share = std::uint64_t{children.creates} * options.deleteSharePercent;
        return children.deletes >= options.directoryDeletes && std::uint64_t{children.deletes} * 100 >= share;
    }

    std::vector<std::uint8_t> churnFlags(const core::Snapshot& snapshot, const ChurnStats& stats,
                                         const ChurnOptions& options, core::Executor& executor,
                                         const core::CancelToken& token) {
        const std::vector<std::uint8_t> churning = churningDirectories(snapshot, stats, options, executor, token);
        std::vector<std::uint8_t> out(snapshot.files.size(), 0);
        executor.parallelFor(
            {out.size(), fileGrain},
            [&](std::size_t begin, std::size_t end) {
                for (std::size_t f = begin; f < end; ++f) {
                    const auto file = static_cast<core::FileIndex>(f);
                    out[f] = fileChurns(snapshot, file, stats, options) || belowChurn(snapshot, file, churning) ? 1 : 0;
                }
            },
            token);
        return out;
    }

    std::vector<std::string> churnDirectories(const core::Snapshot& snapshot, const ChurnStats& stats,
                                              const ChurnOptions& options) {
        core::InlineExecutor inline_;
        const std::vector<std::uint8_t> churning = churningDirectories(snapshot, stats, options, inline_, {});
        std::vector<std::string> out;
        for (core::FileIndex f = 0; f < churning.size(); ++f) {
            if (churning[f] != 0) {
                out.push_back(snapshot.path(f));
            }
        }
        std::ranges::sort(out);
        return out;
    }

} // namespace icd::services
