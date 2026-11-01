#include "services/snapshot/UsnCatchUp.h"

#include "core/ntfs/NtfsFormat.h"
#include "core/snapshot/FreeRunList.h"
#include "services/scan/MftMerge.h"
#include "services/scan/MftScanner.h"
#include "services/scan/ScanRules.h"

#include <algorithm>
#include <format>
#include <unordered_set>

namespace icd::services {

    namespace {

        // Records 0-15 are always read again: their changes ($MFT growing, $Bitmap, $Secure) are not journaled.
        constexpr std::uint64_t alwaysReadRecords = 16;

        // Sends a progress report, if anyone listens.
        void report(core::ProgressSink* progress, const char* stage) {
            if (progress != nullptr) {
                progress->report({.stage = stage, .done = 0, .total = 0, .message = {}});
            }
        }

        // The MFT records named by the journal records from the cursor on, plus their directories.
        struct JournalReading {
            platform::UsnReadStatus status = platform::UsnReadStatus::Complete; // Whether all could be read.
            std::vector<std::uint64_t> records; // Sorted, distinct.
            std::uint64_t journalRecords = 0; // Change records read.
        };

        // Reads the journal and collects the records it names.
        JournalReading readJournal(HANDLE volume, const platform::UsnRange& range, const core::CancelToken& token) {
            JournalReading out;
            out.status = platform::readUsnJournal(
                volume, range,
                [&](std::span<const platform::UsnChange> batch) {
                    out.journalRecords += batch.size();
                    for (const platform::UsnChange& change : batch) {
                        out.records.push_back(core::ntfs::recordOf(change.fileReference));
                        out.records.push_back(core::ntfs::recordOf(change.parentReference));
                    }
                },
                token);
            std::ranges::sort(out.records);
            out.records.erase(std::ranges::unique(out.records).begin(), out.records.end());
            return out;
        }

        // The records to read again: the journaled ones, records 0-15 and every metadata file of the snapshot.
        std::vector<std::uint64_t> recordsToRead(std::vector<std::uint64_t> journaled, const core::Snapshot& saved) {
            for (std::uint64_t r = 0; r < alwaysReadRecords; ++r) {
                journaled.push_back(r);
            }
            for (core::FileIndex f = 0; f < saved.files.size(); ++f) {
                if (saved.files.has(f, core::FileMetadata) && saved.files.fileReference(f) != 0) {
                    journaled.push_back(core::ntfs::recordOf(saved.files.fileReference(f)));
                }
            }
            std::ranges::sort(journaled);
            journaled.erase(std::ranges::unique(journaled).begin(), journaled.end());
            return journaled;
        }

        // Sorted, merged runs.
        std::vector<core::ClusterRun> merged(std::vector<core::ClusterRun> runs, core::ClusterCount total) {
            const core::FreeRunList list(std::move(runs), total);
            return {list.runs().begin(), list.runs().end()};
        }

        // One catch-up after the checks passed.
        class CatchUp {
        public:
            CatchUp(const std::wstring& volumeName, core::Snapshot saved, const core::NtfsLayout& ntfs,
                    core::VolumeGeometry geometry, core::Executor& executor, const core::CancelToken& token) :
                volumeName_(volumeName), saved_(std::move(saved)), ntfs_(ntfs), geometry_(geometry),
                executor_(executor), token_(token) {}

            // Re-reads the records and finishes the snapshot.
            CatchUpResult run(const JournalReading& journal, core::UsnCursor cursor) {
                const std::vector<std::uint64_t> records = recordsToRead(journal.records, saved_);
                const std::unordered_set<std::uint64_t> reread(records.begin(), records.end());
                MftRecordSet fresh = readMftRecords(volumeName_, geometry_, ntfs_, records, executor_, token_);
                mft::DraftSet kept =
                    mft::draftsOfSnapshot(saved_, [&](std::uint64_t record) { return !reread.contains(record); });
                mft::DraftSet drafts = mft::draftFiles(*fresh.rows, fresh.extensions, geometry_, executor_, token_);
                CatchUpResult out;
                out.dirty = dirtyRanges(reread, drafts);
                std::vector<core::ClusterRun> free = freeAfter(reread);
                mft::MergeOutput finished = mft::finishDrafts(mft::combineDrafts(std::move(kept), std::move(drafts)),
                                                              geometry_, std::move(free), executor_, token_);
                out.outcome = CatchUpOutcome::CaughtUp;
                out.snapshot = std::move(finished.snapshot);
                out.snapshot.usn = cursor;
                out.journalRecords = journal.journalRecords;
                out.changedRecords = journal.records.size();
                out.recordsRead = fresh.rows->size();
                return out;
            }

        private:
            // Clusters of the saved files whose records are read again.
            std::vector<core::ClusterRun> oldRuns(const std::unordered_set<std::uint64_t>& reread) const {
                std::vector<core::ClusterRun> runs;
                for (const core::ExtentRecord& r : saved_.extents.records()) {
                    const std::uint64_t reference = saved_.files.fileReference(r.file());
                    if (reference != 0 && reread.contains(core::ntfs::recordOf(reference))) {
                        runs.push_back(r.run());
                    }
                }
                return runs;
            }

            // The saved free space plus what the re-read files held before; finishing takes out what they hold now.
            std::vector<core::ClusterRun> freeAfter(const std::unordered_set<std::uint64_t>& reread) const {
                std::vector<core::ClusterRun> free(saved_.freeSpace.runs().begin(), saved_.freeSpace.runs().end());
                const auto old = oldRuns(reread);
                free.insert(free.end(), old.begin(), old.end());
                return merged(std::move(free), geometry_.totalClusters);
            }

            // Old and new clusters of the re-read files: where the map may have changed.
            std::vector<core::ClusterRun> dirtyRanges(const std::unordered_set<std::uint64_t>& reread,
                                                      const mft::DraftSet& drafts) const {
                std::vector<core::ClusterRun> runs = oldRuns(reread);
                for (const core::ExtentEntry& e : drafts.extents) {
                    runs.push_back(e.run);
                }
                return merged(std::move(runs), geometry_.totalClusters);
            }

            const std::wstring& volumeName_; // For raw reads.
            core::Snapshot saved_; // The snapshot being caught up.
            core::NtfsLayout ntfs_; // MFT location and record size.
            core::VolumeGeometry geometry_; // Cluster size and count.
            core::Executor& executor_; // Parallel work.
            const core::CancelToken& token_; // Checked by every step.
        };

    } // namespace

    std::optional<std::string> catchUpBlocker(const core::Snapshot& saved,
                                              const std::optional<platform::UsnJournalState>& journal,
                                              core::VolumeGeometry current) {
        if (!saved.usn) {
            return "the snapshot has no journal position";
        }
        if (!journal) {
            return "the volume has no change journal";
        }
        if (journal->journalId != saved.usn->journalId) {
            return "the change journal was recreated";
        }
        if (saved.usn->nextUsn < journal->lowestValidUsn || saved.usn->nextUsn < journal->firstUsn ||
            saved.usn->nextUsn > journal->nextUsn) {
            return "the change journal no longer reaches back to the snapshot";
        }
        if (current.totalClusters != saved.geometry.totalClusters ||
            current.bytesPerCluster != saved.geometry.bytesPerCluster) {
            return std::format("the volume changed size ({} -> {} clusters)", saved.geometry.totalClusters,
                               current.totalClusters);
        }
        return std::nullopt;
    }

    CatchUpResult catchUpSnapshot(HANDLE volume, const std::wstring& volumeName, core::Snapshot saved,
                                  const core::NtfsLayout& ntfs, core::VolumeGeometry geometry, core::Executor& executor,
                                  const core::CancelToken& token, core::ProgressSink* progress) {
        report(progress, "Reading change journal");
        const auto journal = platform::queryUsnJournal(volume);
        CatchUpResult out;
        auto blocker = catchUpBlocker(saved, journal, geometry);
        if (blocker || !journal || !saved.usn) { // The blocker covers both; the checks keep the reads below safe.
            out.reason = blocker.value_or("no journal position");
            return out;
        }
        const core::UsnCursor cursor{journal->journalId, journal->nextUsn};
        const std::int64_t savedUsn = saved.usn->nextUsn;
        if (savedUsn == cursor.nextUsn) {
            out.outcome = CatchUpOutcome::UpToDate;
            out.snapshot = std::move(saved);
            return out;
        }
        const JournalReading reading = readJournal(volume, {cursor.journalId, savedUsn, cursor.nextUsn}, token);
        if (reading.status != platform::UsnReadStatus::Complete) {
            out.reason = reading.status == platform::UsnReadStatus::RecordsLost
                ? "the change journal no longer reaches back to the snapshot"
                : "the change journal was recreated";
            return out;
        }
        report(progress, "Reading changed files");
        return CatchUp(volumeName, std::move(saved), ntfs, geometry, executor, token).run(reading, cursor);
    }

} // namespace icd::services
