#include "services/scan/ApiScanner.h"

#include "core/snapshot/ExtentIndex.h"
#include "core/snapshot/SnapshotBuilder.h"
#include "platform/win/Privileges.h"
#include "platform/win/RetrievalPointers.h"
#include "platform/win/TextConvert.h"
#include "platform/win/UsnJournal.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <unordered_map>

namespace icd::services {

    namespace {

        using platform::mftRecordOf;

        constexpr std::uint64_t rootRecord = 5;

        // NTFS system records 0-11 (12-15 are reserved and unused).
        constexpr std::array<const char*, 12> systemNames{"$MFT",     "$MFTMirr", "$LogFile", "$Volume",
                                                          "$AttrDef", "",         "$Bitmap",  "$Boot",
                                                          "$BadClus", "$Secure",  "$UpCase",  "$Extend"};

        // Root-level files Windows keeps open and in place.
        constexpr std::array<std::string_view, 3> pinnedNames{"pagefile.sys", "hiberfil.sys", "swapfile.sys"};

        // FILE_ATTRIBUTE_* bits that map one to one onto file flags.
        constexpr std::array<std::pair<std::uint32_t, std::uint16_t>, 5> attributeFlags{{
            {FILE_ATTRIBUTE_DIRECTORY, core::FileDirectory},
            {FILE_ATTRIBUTE_COMPRESSED, core::FileCompressed},
            {FILE_ATTRIBUTE_SPARSE_FILE, core::FileSparse},
            {FILE_ATTRIBUTE_ENCRYPTED, core::FileEncrypted},
            {FILE_ATTRIBUTE_REPARSE_POINT, core::FileReparsePoint},
        }};

        // How often progress and cancellation are checked, in files.
        constexpr std::size_t reportEvery = 256;

        // A file to read: from the enumeration, or one of the system records.
        struct Pending {
            std::uint64_t reference = 0; // File reference to open by id.
            std::uint64_t parentReference = 0; // Directory holding it.
            std::string name; // Its name (UTF-8).
            std::uint32_t attributes = 0; // Attributes from the enumeration (used when it cannot be opened).
            bool system = false; // One of the NTFS system records.
        };

        // File flags from attributes alone.
        std::uint16_t flagsFromAttributes(std::uint32_t attributes) {
            std::uint16_t flags = 0;
            for (const auto& [attribute, flag] : attributeFlags) {
                flags |= (attributes & attribute) != 0 ? flag : 0;
            }
            return flags;
        }

        // Extent flags for the runs of a file with these flags.
        std::uint16_t extentFlagsFor(std::uint16_t fileFlags) {
            std::uint16_t flags = 0;
            flags |= (fileFlags & core::FileDirectory) != 0 ? core::ExtentDirectoryIndex : 0;
            flags |= (fileFlags & core::FileCompressed) != 0 ? core::ExtentCompressionUnit : 0;
            flags |= (fileFlags & core::FileUnmovable) != 0 ? core::ExtentUnmovable : 0;
            return flags;
        }

        // One scan: collects the file list, reads every file, then builds the snapshot.
        class ApiScanner {
        public:
            ApiScanner(HANDLE volume, std::wstring root, core::VolumeGeometry geometry, core::CancelToken token,
                       core::ProgressSink* progress) :
                volume_(volume), root_(std::move(root)), geometry_(geometry), token_(std::move(token)),
                progress_(progress) {}

            // Runs the scan.
            ApiScanResult run(std::vector<core::ClusterRun> freeRuns) {
                const auto journal = platform::queryUsnJournal(volume_); // Before the files: changes during the
                collect(); // scan stay after the cursor.
                scanAll();
                ApiScanResult result{build(std::move(freeRuns)), stats_};
                if (journal) {
                    result.snapshot.usn = core::UsnCursor{journal->journalId, journal->nextUsn};
                }
                return result;
            }

        private:
            // The system records first (so the root and $Extend are known), then the enumeration.
            void collect() {
                report("Listing files", 0, 0);
                addSystemRecords();
                platform::enumerateUsnFiles(
                    volume_,
                    [&](std::span<const platform::UsnFileEntry> batch) {
                        for (const auto& e : batch) {
                            if (!byRecord_.contains(mftRecordOf(e.fileReference))) {
                                add({e.fileReference, e.parentReference, e.name, e.attributes, false});
                            }
                        }
                    },
                    token_);
            }

            // Adds records 0-11 under the root; their sequence numbers are found by trying to open them.
            void addSystemRecords() {
                for (std::uint64_t record = 0; record < systemNames.size(); ++record) {
                    const std::uint64_t reference = systemReference(record);
                    add({reference, rootRecord | (rootRecord << 48U), systemNames.at(record), 0, true});
                }
            }

            // The reference that opens a system record: the sequence number equals the record number, except
            // for $MFT and $MFTMirr, which have 1. Records that never open keep the first guess.
            std::uint64_t systemReference(std::uint64_t record) const {
                for (const std::uint64_t sequence : {record, std::uint64_t{1}}) {
                    const std::uint64_t reference = record | (sequence << 48U);
                    if (platform::openFileById(volume_, reference)) {
                        return reference;
                    }
                }
                return record | (record << 48U);
            }

            // Appends a pending file.
            void add(Pending pending) {
                byRecord_.emplace(mftRecordOf(pending.reference), pending_.size());
                pending_.push_back(std::move(pending));
            }

            // Index of the root directory.
            core::FileIndex rootIndex() const {
                const auto it = byRecord_.find(rootRecord);
                return it == byRecord_.end() ? core::noFile : static_cast<core::FileIndex>(it->second);
            }

            // Parent index of a pending file: the root has none; an unknown parent means the root.
            core::FileIndex parentOf(std::size_t i) const {
                if (mftRecordOf(pending_[i].reference) == rootRecord) {
                    return core::noFile;
                }
                const auto it = byRecord_.find(mftRecordOf(pending_[i].parentReference));
                return it == byRecord_.end() ? rootIndex() : static_cast<core::FileIndex>(it->second);
            }

            // Whether a file is NTFS metadata: a system record other than the root, or anything below one
            // ($Extend\$RmMetadata\...).
            bool isMetadata(std::size_t i) {
                for (std::size_t depth = 0; depth < 64; ++depth) {
                    if (pending_[i].system) {
                        return mftRecordOf(pending_[i].reference) != rootRecord;
                    }
                    const core::FileIndex parent = parentOf(i);
                    if (parent == core::noFile || parent == rootIndex()) {
                        return false;
                    }
                    i = parent;
                }
                return false;
            }

            // Whether the file is one of the root-level files Windows pins.
            bool isPinned(std::size_t i) const {
                const auto name = std::string_view(pending_[i].name);
                return parentOf(i) == rootIndex() &&
                    std::ranges::any_of(pinnedNames, [&](std::string_view pinned) { return pinned == name; });
            }

            // Reads every pending file in MFT record order.
            void scanAll() {
                files_.resize(pending_.size());
                for (std::size_t i = 0; i < pending_.size(); ++i) {
                    if (i % reportEvery == 0) {
                        token_.throwIfCancelled();
                        report("Reading files", i, pending_.size());
                    }
                    scanFile(i);
                }
                report("Reading files", pending_.size(), pending_.size());
            }

            // Metadata and extents of one file.
            void scanFile(std::size_t i) {
                const Pending& p = pending_[i];
                files_[i] = {p.name, parentOf(i), 0, flagsFromAttributes(p.attributes), {}, p.reference};
                const auto handle = platform::openFileById(volume_, p.reference);
                platform::FileFacts facts;
                if (!handle || !platform::readFileFacts(handle.get(), facts)) {
                    markUnreadable(i);
                    return;
                }
                std::uint16_t flags = baseFlags(i, facts);
                const std::uint16_t extentFlags = extentFlagsFor(flags);
                bool clusters = platform::readRetrievalPointers(handle.get(), runs_) && addRuns(i, 0, extentFlags);
                clusters = scanNamedStreams(i, facts, extentFlags) || clusters;
                const bool noData = (flags & (core::FileDirectory | core::FileSparse)) == 0 && !clusters;
                flags |= noData ? core::FileResident : 0;
                files_[i].sizeBytes = facts.size;
                files_[i].flags = flags;
                files_[i].times = {facts.created, facts.modified, facts.accessed};
                stats_.directories += (flags & core::FileDirectory) != 0 ? 1 : 0;
            }

            // A file that cannot be opened stays in the list with its enumeration attributes and no extents; its
            // clusters end up with the placeholder.
            void markUnreadable(std::size_t i) {
                ++stats_.unreadable;
                if (stats_.unreadableFiles.size() < 100) {
                    stats_.unreadableFiles.push_back(static_cast<core::FileIndex>(i));
                }
                files_[i].flags |= isMetadata(i) ? core::FileMetadata | core::FileUnmovable : 0;
            }

            // Flags from the facts and the file's place in the tree (everything but residency).
            std::uint16_t baseFlags(std::size_t i, const platform::FileFacts& facts) {
                std::uint16_t flags = flagsFromAttributes(facts.attributes);
                flags |= facts.links > 1 ? core::FileHardLinked : 0;
                flags |= facts.namedStreams.empty() ? 0 : core::FileAlternateStreams;
                flags |= isMetadata(i) ? core::FileMetadata | core::FileUnmovable : 0;
                flags |= isPinned(i) ? core::FileUnmovable : 0;
                return flags;
            }

            // Extents of the named data streams (opened by path); true when any has clusters.
            bool scanNamedStreams(std::size_t i, const platform::FileFacts& facts, std::uint16_t extentFlags) {
                bool clusters = false;
                for (std::size_t k = 0; k < facts.namedStreams.size(); ++k) {
                    const auto stream = platform::openForQuery(pathOf(i) + L":" + facts.namedStreams[k]);
                    if (stream && platform::readRetrievalPointers(stream.get(), runs_)) {
                        clusters = addRuns(i, static_cast<std::uint16_t>(k + 1), extentFlags) || clusters;
                        ++stats_.streams;
                    }
                }
                return clusters;
            }

            // Stores the allocated runs in runs_ (holes skipped) as extents; true when there were any.
            bool addRuns(std::size_t i, std::uint16_t stream, std::uint16_t flags) {
                bool any = false;
                for (const platform::RetrievedRun& run : runs_) {
                    if (run.lcn >= 0 && run.length > 0) {
                        extents_.push_back({{static_cast<core::Lcn>(run.lcn), run.length},
                                            run.vcn,
                                            static_cast<core::FileIndex>(i),
                                            stream,
                                            flags});
                        any = true;
                    }
                }
                return any;
            }

            // Full path of a pending file ("\\?\T:\dir\name"), for opening named streams.
            std::wstring pathOf(std::size_t i) const {
                std::vector<std::size_t> chain;
                for (std::size_t at = i; parentOf(at) != core::noFile && chain.size() < 4096; at = parentOf(at)) {
                    chain.push_back(at);
                }
                std::wstring path = L"\\\\?\\" + root_;
                for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                    path += platform::fromUtf8(pending_[*it].name);
                    path += it + 1 == chain.rend() ? L"" : L"\\";
                }
                return path;
            }

            // Reconciles the reads with the bitmap and builds the snapshot; the placeholder file comes last.
            core::Snapshot build(std::vector<core::ClusterRun> freeRuns) {
                report("Building snapshot", 0, 0);
                const auto placeholder = static_cast<core::FileIndex>(files_.size());
                stats_.reconcile = core::reconcileScan(extents_, freeRuns, geometry_.totalClusters, placeholder);
                core::SnapshotBuilder builder(geometry_);
                for (const core::FileEntry& file : files_) {
                    builder.addFile(file);
                }
                builder.addFile({unattributedName, rootIndex(), 0, core::FileMetadata | core::FileUnmovable, {}, 0});
                builder.addExtents(std::move(extents_));
                for (const core::ClusterRun& run : freeRuns) {
                    builder.addFreeRun(run);
                }
                stats_.files = files_.size() + 1;
                return std::move(builder).build();
            }

            // Sends a progress report, if anyone listens.
            void report(const char* stage, std::uint64_t done, std::uint64_t total) const {
                if (progress_ != nullptr) {
                    progress_->report({.stage = stage, .done = done, .total = total, .message = {}});
                }
            }

            HANDLE volume_; // Volume opened for reading.
            std::wstring root_; // Mount path, for named streams.
            core::VolumeGeometry geometry_; // Cluster size and count.
            core::CancelToken token_; // Checked between files.
            core::ProgressSink* progress_; // May be null.
            std::vector<Pending> pending_; // Files in index order.
            std::unordered_map<std::uint64_t, std::size_t> byRecord_; // MFT record number -> index.
            std::vector<core::FileEntry> files_; // Per file; names point into pending_.
            std::vector<core::ExtentEntry> extents_; // Every allocated run read.
            std::vector<platform::RetrievedRun> runs_; // Reused retrieval pointer buffer.
            ApiScanStats stats_; // Counters.
        };

    } // namespace

    ApiScanResult scanFilesApi(HANDLE volume, const std::wstring& root, core::VolumeGeometry geometry,
                               std::vector<core::ClusterRun> freeRuns, const core::CancelToken& token,
                               core::ProgressSink* progress) {
        // Administrators hold the backup privilege but have it switched off; with it on, files only SYSTEM may
        // open (System Volume Information) can still be queried.
        platform::enablePrivilege(SE_BACKUP_NAME);
        ApiScanner scanner(volume, root, geometry, token, progress);
        return scanner.run(std::move(freeRuns));
    }

} // namespace icd::services
