#include "services/scan/MftMerge.h"

#include "core/ntfs/NtfsFormat.h"
#include "core/snapshot/ExtentIndex.h"
#include "core/snapshot/ScanReconcile.h"
#include "core/snapshot/SnapshotBuilder.h"
#include "core/snapshot/SnapshotPatch.h"
#include "services/scan/ScanRules.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace icd::services::mft {

    namespace {

        using core::FileIndex;
        using core::noFile;
        using core::ntfs::AttributeType;

        // Files per chunk of the parallel loops.
        constexpr std::size_t fileGrain = 4096;

        // Parent walks stop here; deeper chains only occur in damaged data.
        constexpr int maxDepth = 64;

        // Names a catch-up may leave in the pool beyond twice the file count (deleted files' names stay in a patched
        // pool); past that the full finish builds a fresh pool.
        constexpr std::size_t namePoolSlack = 4096;

        // Where a draft's records are and how many real names it has, while drafting.
        struct DraftSource {
            std::size_t baseRow = 0; // Position of the base record's row.
            std::uint16_t links = 0; // Names that are not DOS aliases.
        };

        // The name a file is listed under: the first that is not a DOS alias (else the first), and its link count.
        struct NamePick {
            std::string_view name; // Chosen name.
            std::uint64_t parentReference = 0; // Its directory.
            std::uint16_t links = 0; // Names that are not DOS aliases.
            bool chosen = false; // A non-alias name was taken.
            bool any = false; // Some name was taken.

            // Considers the next name in record order.
            void offer(const ChunkRows& chunk, const NameRow& row) {
                const bool dos = row.nameSpace == core::ntfs::NameSpace::Dos;
                links += dos ? 0 : 1;
                if ((dos || chosen) && any) {
                    return;
                }
                name = chunk.textAt(row.textOffset, row.textBytes);
                parentReference = row.parentReference;
                chosen = chosen || !dos;
                any = true;
            }
        };

        // How one segment is stored in the snapshot.
        struct SegmentClass {
            std::uint16_t ordinal = 0; // Stream ordinal.
            bool extra = false; // Not $DATA and not a directory index: unmovable, MFT path only.
        };

        // Stream ordinals of one file, handed out in the order segments appear.
        class StreamNumbers {
        public:
            // The class of a segment of a file (directory or not).
            SegmentClass classify(AttributeType type, std::string_view name, bool directory) {
                if (type == AttributeType::Data) {
                    return {name.empty() ? std::uint16_t{0} : number(named_, name, 1), false};
                }
                if (type == AttributeType::IndexAllocation && directory && name == "$I30") {
                    return {0, false};
                }
                const auto key = static_cast<std::uint32_t>(type);
                const auto it = std::ranges::find(extras_, std::pair(key, name));
                const auto index = static_cast<std::uint16_t>(it - extras_.begin());
                if (it == extras_.end()) {
                    extras_.emplace_back(key, name);
                }
                return {static_cast<std::uint16_t>(extraStreamBase + index), true};
            }

            // Whether a named $DATA stream was seen.
            [[nodiscard]] bool hasNamed() const { return !named_.empty(); }

        private:
            // Index of name in list (added when new) plus first.
            static std::uint16_t number(std::vector<std::string_view>& list, std::string_view name,
                                        std::uint16_t first) {
                const auto it = std::ranges::find(list, name);
                const auto index = static_cast<std::uint16_t>(it - list.begin());
                if (it == list.end()) {
                    list.push_back(name);
                }
                return static_cast<std::uint16_t>(first + index);
            }

            std::vector<std::string_view> named_; // Named $DATA streams in order.
            std::vector<std::pair<std::uint32_t, std::string_view>> extras_; // Other attributes by type and name.
        };

        // Runs body(i) for i in [0, count) on the executor.
        template <typename Body>
        void forEach(core::Executor& executor, const core::CancelToken& token, std::size_t count, Body body) {
            executor.parallelFor(
                {count, fileGrain},
                [&](std::size_t begin, std::size_t end) {
                    for (std::size_t i = begin; i < end; ++i) {
                        body(static_cast<FileIndex>(i));
                    }
                },
                token);
        }

        // Rows to drafts.
        class Drafter {
        public:
            Drafter(const RowIndex& rows, const ExtensionMap& extensions, core::VolumeGeometry geometry,
                    core::Executor& executor, const core::CancelToken& token) :
                rows_(rows), extensions_(extensions), geometry_(geometry), executor_(executor), token_(token) {}

            // All steps.
            DraftSet run() {
                selectFiles();
                forEach(executor_, token_, drafts_.size(), [this](FileIndex f) { describe(f); });
                segments_.assign(rows_.segmentCount(), {});
                forEach(executor_, token_, drafts_.size(), [this](FileIndex f) { classifyStreams(f); });
                DraftSet out;
                out.extents = collectExtents();
                out.files = std::move(drafts_);
                return out;
            }

        private:
            // Calls fn with each row of a file: the base row, then its extension rows.
            template <typename Fn>
            void forEachPart(FileIndex f, Fn fn) const {
                const std::size_t base = sources_[f].baseRow;
                fn(base);
                if (const auto it = extensions_.find(base); it != extensions_.end()) {
                    for (const std::size_t row : it->second) {
                        fn(row);
                    }
                }
            }

            // Whether a base row becomes a file: it has a name (here or in an extension), or is a system record.
            [[nodiscard]] bool qualifies(std::size_t i) const {
                if (rows_.row(i).record() < systemRecordCount || rows_.row(i).nameCount != 0) {
                    return true;
                }
                const auto it = extensions_.find(i);
                return it != extensions_.end() &&
                    std::ranges::any_of(it->second, [&](std::size_t r) { return rows_.row(r).nameCount != 0; });
            }

            // Numbers the files in record order and maps every accepted row to its file.
            void selectFiles() {
                fileOfRow_.assign(rows_.size(), noFile);
                for (std::size_t i = 0; i < rows_.size(); ++i) {
                    if (rows_.row(i).baseReference == 0 && qualifies(i)) {
                        fileOfRow_[i] = static_cast<FileIndex>(sources_.size());
                        sources_.push_back({.baseRow = i});
                    }
                }
                if (sources_.size() >= noFile) {
                    throw std::runtime_error("too many files for 32-bit file indexes");
                }
                for (const auto& [base, parts] : extensions_) {
                    for (const std::size_t row : parts) {
                        fileOfRow_[row] = fileOfRow_[base];
                    }
                }
                drafts_.resize(sources_.size());
                token_.throwIfCancelled();
            }

            // Name, size, times and attribute flags of a file.
            void describe(FileIndex f) {
                const RecordRow& base = rows_.row(sources_[f].baseRow);
                FileDraft& d = drafts_[f];
                d.reference = base.reference;
                d.times = base.times;
                d.flags = fileFlagsFromAttributes(base.dosAttributes);
                d.flags |= (base.recordFlags & core::ntfs::recordDirectory) != 0 ? core::FileDirectory : 0;
                chooseName(f);
                d.flags |= sources_[f].links > 1 ? core::FileHardLinked : 0;
                d.name = base.record() == rootRecordNumber ? std::string_view() : d.name;
                d.sizeBytes = (d.flags & core::FileDirectory) != 0 ? indexSize(f) : mainSize(f);
            }

            // Logical size of the unnamed $DATA, from whichever part holds its first segment or resident value.
            [[nodiscard]] std::uint64_t mainSize(FileIndex f) const {
                std::uint64_t size = 0;
                forEachPart(f, [&](std::size_t r) {
                    const RecordRow& row = rows_.row(r);
                    size = (row.facts & RowMainData) != 0 ? row.mainSize : size;
                });
                return size;
            }

            // A directory's size as Windows reports it (end of file): the size of its $I30 index allocation, 0 for a
            // small directory whose index fits the record.
            [[nodiscard]] std::uint64_t indexSize(FileIndex f) const {
                std::uint64_t size = 0;
                forEachPart(f, [&](std::size_t r) {
                    const ChunkRows& chunk = rows_.chunk(r);
                    const RecordRow& row = rows_.row(r);
                    for (std::uint32_t k = row.firstSegment; k < row.firstSegment + row.segmentCount; ++k) {
                        const SegmentRow& seg = chunk.segments[k];
                        const bool index = seg.type == AttributeType::IndexAllocation && seg.startVcn == 0 &&
                            chunk.textAt(seg.nameOffset, seg.nameBytes) == "$I30";
                        size = index ? seg.dataSize : size;
                    }
                });
                return size;
            }

            // The first name that is not a DOS alias (else the first name), and the hard link count.
            void chooseName(FileIndex f) {
                NamePick pick;
                forEachPart(f, [&](std::size_t r) {
                    const ChunkRows& chunk = rows_.chunk(r);
                    const RecordRow& row = rows_.row(r);
                    for (std::uint32_t k = row.firstName; k < row.firstName + row.nameCount; ++k) {
                        pick.offer(chunk, chunk.names[k]);
                    }
                });
                drafts_[f].name = pick.name;
                drafts_[f].parentReference = pick.parentReference;
                sources_[f].links = pick.links;
            }

            // Stream ordinals of a file's segments; then whether it has clusters and named streams.
            void classifyStreams(FileIndex f) {
                StreamNumbers numbers;
                FileDraft& d = drafts_[f];
                const bool directory = (d.flags & core::FileDirectory) != 0;
                forEachPart(f, [&](std::size_t r) {
                    const ChunkRows& chunk = rows_.chunk(r);
                    const RecordRow& row = rows_.row(r);
                    const std::size_t base = rows_.segmentBase(r);
                    d.mainSeen = d.mainSeen || (row.facts & RowMainData) != 0;
                    for (std::uint16_t s = 0; s < row.segmentCount; ++s) {
                        const SegmentRow& seg = chunk.segments[row.firstSegment + s];
                        const SegmentClass cls =
                            numbers.classify(seg.type, chunk.textAt(seg.nameOffset, seg.nameBytes), directory);
                        segments_[base + s] = cls;
                        d.clusters = d.clusters || (!cls.extra && seg.extentCount != 0);
                    }
                });
                d.flags |= numbers.hasNamed() ? core::FileAlternateStreams : 0;
            }

            // Every chunk's extents with file positions and stream ordinals; rows that belong to no file and runs past
            // the volume end are dropped.
            std::vector<core::ExtentEntry> collectExtents() {
                const auto& chunks = rows_.chunks();
                std::vector<std::size_t> extentBase(chunks.size() + 1, 0);
                for (std::size_t c = 0; c < chunks.size(); ++c) {
                    extentBase[c + 1] = extentBase[c] + chunks[c]->extents.size();
                }
                std::vector<core::ExtentEntry> out(extentBase.back());
                executor_.parallelFor(
                    {chunks.size(), 1},
                    [&](std::size_t begin, std::size_t end) {
                        for (std::size_t c = begin; c < end; ++c) {
                            placeChunk(c, std::span(out).subspan(extentBase[c], chunks[c]->extents.size()));
                        }
                    },
                    token_);
                std::erase_if(out, [](const core::ExtentEntry& e) { return e.file == noFile; });
                return out;
            }

            // Rewrites one chunk's extents into out (same positions).
            void placeChunk(std::size_t c, std::span<core::ExtentEntry> out) const {
                const ChunkRows& chunk = *rows_.chunks()[c];
                const std::size_t rowBase = rows_.chunkRowBase(c);
                for (std::size_t r = 0; r < chunk.records.size(); ++r) {
                    const RecordRow& row = chunk.records[r];
                    const FileIndex f = fileOfRow_[rowBase + r];
                    const std::size_t segmentBase = rows_.segmentBase(rowBase + r);
                    for (std::uint32_t k = row.firstExtent; k < row.firstExtent + row.extentCount; ++k) {
                        out[k] = placed(chunk.extents[k], f, segments_[segmentBase + chunk.extents[k].stream]);
                    }
                }
            }

            // One extent as drafted: owner, ordinal and the extra-attribute flag, or a dropped marker.
            [[nodiscard]] core::ExtentEntry placed(core::ExtentEntry e, FileIndex f, SegmentClass cls) const {
                if (f == noFile || e.run.end() > geometry_.totalClusters || e.run.end() < e.run.start) {
                    e.file = noFile;
                    return e;
                }
                e.file = f;
                e.stream = cls.ordinal;
                e.flags = cls.extra ? core::ExtentUnmovable : 0;
                return e;
            }

            const RowIndex& rows_; // Every row.
            const ExtensionMap& extensions_; // Accepted extension rows per base row.
            core::VolumeGeometry geometry_; // Cluster size and count.
            core::Executor& executor_; // Parallel loops.
            const core::CancelToken& token_; // Checked by the loops.
            std::vector<FileIndex> fileOfRow_; // Row -> file.
            std::vector<DraftSource> sources_; // Per file, while drafting.
            std::vector<FileDraft> drafts_; // Per file.
            std::vector<SegmentClass> segments_; // Per global segment.
        };

        // What parent resolution looks files up in.
        struct ParentLookup {
            const std::vector<FileIndex>& fileOfRecord; // Record number -> file.
            FileIndex root = noFile; // The root directory.
        };

        // The parent of file f whose chosen name lies in the directory `reference`: by reference with matching sequence
        // number; the root has none, and an unknown or stale parent means the root (as on the API path). Shared by the
        // full finish and the catch-up patch so both resolve parents alike.
        template <typename ReferenceOf>
        FileIndex resolveParent(FileIndex f, const ParentLookup& lookup, ReferenceOf referenceOf,
                                std::uint64_t reference) {
            if (f == lookup.root) {
                return noFile;
            }
            const std::uint64_t record = core::ntfs::recordOf(reference);
            const FileIndex parent = record < lookup.fileOfRecord.size() ? lookup.fileOfRecord[record] : noFile;
            const bool current = parent != noFile && referenceOf(parent) == reference;
            return current && parent != f ? parent : lookup.root;
        }

        // Metadata (system records other than the root, and everything below them) and pinned root files: the flags
        // that depend on where a file sits. Shared by the full finish and the catch-up patch.
        template <typename ParentOf, typename ReferenceOf>
        std::uint16_t placementFlags(FileIndex f, std::string_view name, FileIndex root, ParentOf parentOf,
                                     ReferenceOf referenceOf) {
            std::uint16_t flags = 0;
            FileIndex at = f;
            for (int depth = 0; depth < maxDepth && at != noFile && at != root; ++depth) {
                if (core::ntfs::recordOf(referenceOf(at)) < systemRecordCount) {
                    flags = core::FileMetadata | core::FileUnmovable;
                    break;
                }
                at = parentOf(at);
            }
            const bool pinned = parentOf(f) == root && f != root && isPinnedRootName(name);
            return static_cast<std::uint16_t>(flags | (pinned ? core::FileUnmovable : 0));
        }

        // The `$Unattributed` placeholder under the root.
        core::FileEntry placeholderEntry(FileIndex root) {
            return {unattributedName, root, 0, core::FileMetadata | core::FileUnmovable, {}, 0};
        }

        // Drafts to a snapshot.
        class Finisher {
        public:
            Finisher(DraftSet drafts, core::VolumeGeometry geometry, core::Executor& executor,
                     const core::CancelToken& token, std::shared_ptr<std::pmr::memory_resource> storage) :
                drafts_(std::move(drafts)), geometry_(geometry), executor_(executor), token_(token),
                storage_(std::move(storage)) {}

            // All steps.
            MergeOutput run(std::vector<core::ClusterRun> freeRuns) {
                indexRecords();
                entries_.resize(drafts_.files.size());
                forEach(executor_, token_, entries_.size(), [this](FileIndex f) { describe(f); });
                forEach(executor_, token_, entries_.size(), [this](FileIndex f) { markSystem(f); });
                forEach(executor_, token_, drafts_.extents.size(), [this](std::size_t i) { flagExtent(i); });
                return build(std::move(freeRuns));
            }

        private:
            // Record number -> file, and the root.
            void indexRecords() {
                const auto& files = drafts_.files;
                const std::uint64_t records = files.empty() ? 0 : core::ntfs::recordOf(files.back().reference) + 1;
                fileOfRecord_.assign(records, noFile);
                for (std::size_t f = 0; f < files.size(); ++f) {
                    const std::uint64_t record = core::ntfs::recordOf(files[f].reference);
                    if (record >= records || fileOfRecord_[record] != noFile ||
                        (f > 0 && record <= core::ntfs::recordOf(files[f - 1].reference))) {
                        throw std::runtime_error("file drafts are not in record order");
                    }
                    fileOfRecord_[record] = static_cast<FileIndex>(f);
                }
                rootFile_ = rootRecordNumber < records ? fileOfRecord_[rootRecordNumber] : noFile;
            }

            // The entry of a draft with its parent.
            void describe(FileIndex f) {
                const FileDraft& d = drafts_.files[f];
                entries_[f] = {d.name, parentOf(f), d.sizeBytes, d.flags, d.times, d.reference};
            }

            // The parent's index: by reference with matching sequence number; the root has none, and an unknown or
            // stale parent means the root (as on the API path).
            [[nodiscard]] FileIndex parentOf(FileIndex f) const {
                return resolveParent(
                    f, {fileOfRecord_, rootFile_}, [this](FileIndex p) { return drafts_.files[p].reference; },
                    drafts_.files[f].parentReference);
            }

            // Metadata (system records other than the root, and everything below them), pinned root files and
            // residency (which needs the main stream to have been seen).
            void markSystem(FileIndex f) {
                core::FileEntry& e = entries_[f];
                e.flags |= placementFlags(
                    f, e.name, rootFile_, [this](FileIndex at) { return entries_[at].parent; },
                    [this](FileIndex at) { return entries_[at].fileReference; });
                const FileDraft& d = drafts_.files[f];
                e.flags |= d.mainSeen && isResident(e.flags, d.clusters) ? core::FileResident : 0;
            }

            // An extent's flags from its file's final flags.
            void flagExtent(std::size_t i) {
                core::ExtentEntry& e = drafts_.extents[i];
                e.flags = static_cast<std::uint16_t>(e.flags | extentFlagsFor(entries_[e.file].flags));
            }

            // Reconciles with the free runs and builds the snapshot; the placeholder comes last.
            MergeOutput build(std::vector<core::ClusterRun> freeRuns) {
                MergeOutput out;
                const auto placeholder = static_cast<FileIndex>(entries_.size());
                out.stats.scanner = ScannerKind::Mft;
                out.stats.reconcile =
                    core::reconcileScan(drafts_.extents, freeRuns, geometry_.totalClusters, placeholder);
                core::SnapshotBuilder builder(geometry_, storage_);
                for (const core::FileEntry& e : entries_) {
                    builder.addFile(e);
                    out.stats.directories += (e.flags & core::FileDirectory) != 0 ? 1 : 0;
                    out.stats.streams += (e.flags & core::FileAlternateStreams) != 0 ? 1 : 0;
                }
                builder.addFile(placeholderEntry(rootFile_));
                builder.addExtents(std::move(drafts_.extents));
                for (const core::ClusterRun& run : freeRuns) {
                    builder.addFreeRun(run);
                }
                out.snapshot = std::move(builder).build();
                out.stats.files = entries_.size() + 1;
                return out;
            }

            DraftSet drafts_; // Input; extents get their final flags.
            core::VolumeGeometry geometry_; // Cluster size and count.
            core::Executor& executor_; // Parallel loops.
            const core::CancelToken& token_; // Checked by the loops.
            std::vector<FileIndex> fileOfRecord_; // Record number -> file.
            std::vector<core::FileEntry> entries_; // Per file, as the builder takes them.
            FileIndex rootFile_ = noFile; // The root directory.
            std::shared_ptr<std::pmr::memory_resource> storage_; // For the snapshot's large arrays; may be null.
        };

    } // namespace

    namespace {

        // Chunks by their first row, not the first record read: two reads of one stretch with other records in use
        // share the first record.
        void sortChunks(std::vector<std::shared_ptr<const ChunkRows>>& chunks) {
            std::ranges::stable_sort(chunks, {}, [](const auto& c) {
                return c->records.empty() ? c->firstRecord : c->records.front().record();
            });
        }

    } // namespace

    RowIndex::RowIndex(std::vector<std::shared_ptr<const ChunkRows>> chunks) : chunks_(std::move(chunks)) {
        sortChunks(chunks_);
        segmentBase_.push_back(0);
        for (std::size_t c = 0; c < chunks_.size(); ++c) {
            rowBase_.push_back(refs_.size());
            for (std::size_t r = 0; r < chunks_[c]->records.size(); ++r) {
                refs_.push_back({static_cast<std::uint32_t>(c), static_cast<std::uint32_t>(r)});
            }
            segmentBase_.push_back(segmentBase_.back() + chunks_[c]->segments.size());
            extentCount_ += chunks_[c]->extents.size();
        }
        rowOfRecord_.assign(refs_.empty() ? 0 : row(refs_.size() - 1).record() + 1, 0);
        for (std::size_t i = 0; i < refs_.size(); ++i) {
            const std::uint64_t record = row(i).record();
            if (record >= rowOfRecord_.size() || rowOfRecord_[record] != 0 ||
                (i > 0 && record <= row(i - 1).record())) {
                throw std::runtime_error("MFT chunks overlap or are out of order");
            }
            rowOfRecord_[record] = static_cast<std::uint32_t>(i + 1);
        }
    }

    std::size_t RowIndex::find(std::uint64_t record) const {
        if (record >= rowOfRecord_.size() || rowOfRecord_[record] == 0) {
            return none;
        }
        return rowOfRecord_[record] - 1;
    }

    namespace {

        // Whether a snapshot's last file is the `$Unattributed` placeholder the scanners add.
        bool hasPlaceholder(const core::Snapshot& s) {
            const auto last = static_cast<FileIndex>(s.files.size() - 1);
            return s.files.size() != 0 && s.files.fileReference(last) == 0 &&
                s.names.name(s.files.name(last)) == unattributedName;
        }

        // The draft of one snapshot file.
        FileDraft draftOf(const core::Snapshot& s, FileIndex f) {
            const auto& files = s.files;
            FileDraft d;
            d.reference = files.fileReference(f);
            d.parentReference = files.parent(f) == noFile ? d.reference : files.fileReference(files.parent(f));
            d.name = s.names.name(files.name(f));
            d.sizeBytes = files.sizeBytes(f);
            d.times = files.times(f);
            d.flags = static_cast<std::uint16_t>(files.flags(f) &
                                                 ~(core::FileMetadata | core::FileUnmovable | core::FileResident));
            for (const std::uint32_t position : s.extents.extentsOf(f)) {
                d.clusters = d.clusters || s.extents.records()[position].stream() < extraStreamBase;
            }
            // finishDrafts() calls a file resident only when its main stream was seen: say so exactly when that gives
            // back the saved flag.
            d.mainSeen = files.has(f, core::FileResident) || !isResident(d.flags, d.clusters);
            return d;
        }

        // A snapshot file's extents (file = f), VCNs counted per stream in the saved order.
        void appendExtents(const core::Snapshot& s, FileIndex f, std::vector<core::ExtentEntry>& out) {
            std::vector<std::pair<std::uint16_t, std::uint64_t>> vcns; // Next VCN per stream.
            for (const std::uint32_t position : s.extents.extentsOf(f)) {
                const core::ExtentRecord& r = s.extents.records()[position];
                auto it = std::ranges::find(vcns, r.stream(), &std::pair<std::uint16_t, std::uint64_t>::first);
                if (it == vcns.end()) {
                    it = vcns.insert(vcns.end(), {r.stream(), 0});
                }
                const auto flags =
                    r.stream() >= extraStreamBase ? std::uint16_t{core::ExtentUnmovable} : std::uint16_t{0};
                out.push_back({.run = r.run(), .vcn = it->second, .file = f, .stream = r.stream(), .flags = flags});
                it->second += r.length();
            }
        }

        // Extents of a draft set renumbered by `to` (noFile drops them) and appended.
        void appendRemapped(const std::vector<core::ExtentEntry>& extents, const std::vector<FileIndex>& to,
                            std::vector<core::ExtentEntry>& out) {
            for (core::ExtentEntry e : extents) {
                e.file = to[e.file];
                if (e.file != noFile) {
                    out.push_back(e);
                }
            }
        }

        // Record number of a draft.
        std::uint64_t recordOfDraft(const FileDraft& d) { return core::ntfs::recordOf(d.reference); }

        // The finish of a catch-up as a patch of the saved snapshot. The saved files that were not re-read are only
        // given their new parent and flags (computed by the same rules as the full finish); everything else of theirs
        // stays in the snapshot. The fresh drafts are finished in full.
        class CatchUpFinisher {
        public:
            CatchUpFinisher(const core::Snapshot& saved, const DraftSet& fresh, std::span<const std::uint64_t> reread,
                            core::Executor& executor, const core::CancelToken& token) :
                saved_(saved), fresh_(fresh), reread_(reread), executor_(executor), token_(token),
                freshTo_(fresh.files.size(), noFile) {}

            // All steps; nullopt when the patch cannot give the full finish's result.
            std::optional<core::Snapshot> run(core::Snapshot& saved) {
                if (!walk()) {
                    return std::nullopt;
                }
                indexRecords();
                const std::size_t n = baseOf_.size();
                parents_.resize(n + 1);
                flags_.resize(n + 1);
                forEach(executor_, token_, n, [this](FileIndex f) { parents_[f] = parentOf(f); });
                forEach(executor_, token_, n, [this](FileIndex f) { flags_[f] = flagsOf(f); });
                return patch(saved);
            }

        private:
            // Merges the saved files (minus the re-read records) with the fresh drafts by record number; false when
            // either is out of record order.
            bool walk() {
                const std::size_t savedFiles = saved_.files.size() - (hasPlaceholder(saved_) ? 1 : 0);
                baseOf_.reserve(savedFiles + fresh_.files.size() + 1);
                for (FileIndex f = 0; f < savedFiles; ++f) {
                    if (!offer(f)) {
                        return false;
                    }
                }
                return takeFresh(std::numeric_limits<std::uint64_t>::max());
            }

            // Saved file f: fresh drafts before it go first; a re-read record is left to its fresh draft.
            bool offer(FileIndex f) {
                const std::uint64_t record = core::ntfs::recordOf(saved_.files.fileReference(f));
                if (!takeFresh(record)) {
                    return false;
                }
                while (r_ < reread_.size() && reread_[r_] < record) {
                    ++r_;
                }
                if (r_ < reread_.size() && reread_[r_] == record) {
                    return true;
                }
                baseOf_.push_back(f);
                return inOrder(record);
            }

            // Appends the fresh drafts with a record below `record`.
            bool takeFresh(std::uint64_t record) {
                for (; n_ < fresh_.files.size() && recordOfDraft(fresh_.files[n_]) < record; ++n_) {
                    if (!inOrder(recordOfDraft(fresh_.files[n_]))) {
                        return false;
                    }
                    freshTo_[n_] = static_cast<FileIndex>(baseOf_.size());
                    baseOf_.push_back(noFile);
                }
                return true;
            }

            // Whether records arrive strictly ascending (both lists must be in record order).
            bool inOrder(std::uint64_t record) {
                const bool ascending = record > lastRecord_ || first_;
                lastRecord_ = std::max(lastRecord_, record);
                first_ = false;
                return ascending;
            }

            // A file's reference.
            [[nodiscard]] std::uint64_t referenceOf(FileIndex f) const { return references_[f]; }

            // References per file, record number -> file, and the root.
            void indexRecords() {
                const std::size_t n = baseOf_.size();
                references_.resize(n);
                forEach(executor_, token_, n, [this](FileIndex f) {
                    references_[f] = baseOf_[f] != noFile ? saved_.files.fileReference(baseOf_[f]) : 0;
                });
                for (std::size_t k = 0; k < freshTo_.size(); ++k) {
                    references_[freshTo_[k]] = fresh_.files[k].reference;
                    freshOf_.emplace(freshTo_[k], k);
                }
                fileOfRecord_.assign(n == 0 ? 0 : core::ntfs::recordOf(references_.back()) + 1, noFile);
                forEach(executor_, token_, n,
                        [this](FileIndex f) { fileOfRecord_[core::ntfs::recordOf(references_[f])] = f; });
                rootFile_ = rootRecordNumber < fileOfRecord_.size() ? fileOfRecord_[rootRecordNumber] : noFile;
            }

            // The draft a fresh file came from.
            [[nodiscard]] const FileDraft& draftAt(FileIndex f) const { return fresh_.files[freshOf_.at(f)]; }

            // The directory reference a file's draft would carry: a kept file's saved parent (itself for a saved
            // root), so it resolves exactly as draftsOfSnapshot() + finishDrafts() would.
            [[nodiscard]] std::uint64_t parentReferenceOf(FileIndex f) const {
                const FileIndex b = baseOf_[f];
                if (b == noFile) {
                    return draftAt(f).parentReference;
                }
                const FileIndex p = saved_.files.parent(b);
                return saved_.files.fileReference(p == noFile ? b : p);
            }

            // A file's new parent.
            [[nodiscard]] FileIndex parentOf(FileIndex f) const {
                return resolveParent(
                    f, {fileOfRecord_, rootFile_}, [this](FileIndex p) { return referenceOf(p); },
                    parentReferenceOf(f));
            }

            // A file's final flags: its own (residency as saved for kept files) plus where it sits.
            [[nodiscard]] std::uint16_t flagsOf(FileIndex f) const {
                const FileIndex b = baseOf_[f];
                const std::string_view name = b != noFile ? saved_.names.name(saved_.files.name(b)) : draftAt(f).name;
                const std::uint16_t placed = placementFlags(
                    f, name, rootFile_, [this](FileIndex at) { return parents_[at]; },
                    [this](FileIndex at) { return referenceOf(at); });
                if (b != noFile) {
                    constexpr std::uint16_t derived = core::FileMetadata | core::FileUnmovable;
                    return static_cast<std::uint16_t>((saved_.files.flags(b) & ~derived) | placed);
                }
                const FileDraft& d = draftAt(f);
                const auto flags = static_cast<std::uint16_t>(d.flags | placed);
                return static_cast<std::uint16_t>(
                    flags | (d.mainSeen && isResident(flags, d.clusters) ? core::FileResident : 0));
            }

            // The fresh files' entries and extents, the placeholder, and the patch.
            std::optional<core::Snapshot> patch(core::Snapshot& saved) {
                core::SnapshotPatch patch;
                for (std::size_t k = 0; k < freshTo_.size(); ++k) {
                    const FileDraft& d = fresh_.files[k];
                    patch.newFiles.push_back({d.name, noFile, d.sizeBytes, 0, d.times, d.reference});
                }
                for (core::ExtentEntry e : fresh_.extents) {
                    e.file = freshTo_[e.file];
                    e.flags = static_cast<std::uint16_t>(e.flags | extentFlagsFor(flags_[e.file]));
                    patch.extents.push_back(e);
                }
                const auto placeholder = static_cast<FileIndex>(baseOf_.size());
                const core::FileEntry entry = placeholderEntry(rootFile_);
                patch.newFiles.push_back(entry);
                baseOf_.push_back(noFile);
                parents_[placeholder] = entry.parent;
                flags_[placeholder] = entry.flags;
                patch.baseOf = baseOf_;
                patch.parents = parents_;
                patch.flags = flags_;
                patch.baseUnattributed =
                    hasPlaceholder(saved_) ? static_cast<FileIndex>(saved_.files.size() - 1) : noFile;
                patch.unattributed = placeholder;
                return core::patchSnapshot(saved, std::move(patch), executor_, token_);
            }

            const core::Snapshot& saved_; // The saved snapshot (read only until the patch).
            const DraftSet& fresh_; // Drafts of the re-read records.
            std::span<const std::uint64_t> reread_; // Re-read records, sorted.
            core::Executor& executor_; // Parallel loops.
            const core::CancelToken& token_; // Checked by the loops.
            std::vector<FileIndex> freshTo_; // Fresh draft -> file.
            std::unordered_map<FileIndex, std::size_t> freshOf_; // File -> fresh draft.
            std::vector<FileIndex> baseOf_; // Per file: the saved file it continues, or noFile.
            std::vector<std::uint64_t> references_; // Per file.
            std::vector<FileIndex> fileOfRecord_; // Record number -> file.
            std::vector<FileIndex> parents_; // Per file, final.
            std::vector<std::uint16_t> flags_; // Per file, final.
            FileIndex rootFile_ = noFile; // The root directory.
            std::size_t n_ = 0; // Next fresh draft during the walk.
            std::size_t r_ = 0; // Next re-read record during the walk.
            std::uint64_t lastRecord_ = 0; // Last record taken during the walk.
            bool first_ = true; // Nothing taken yet.
        };

    } // namespace

    DraftSet draftsOfSnapshot(const core::Snapshot& snapshot, const std::function<bool(std::uint64_t record)>& keep) {
        DraftSet out;
        const std::size_t files = snapshot.files.size() - (hasPlaceholder(snapshot) ? 1 : 0);
        for (FileIndex f = 0; f < files; ++f) {
            if (!keep(core::ntfs::recordOf(snapshot.files.fileReference(f)))) {
                continue;
            }
            const auto to = static_cast<FileIndex>(out.files.size());
            out.files.push_back(draftOf(snapshot, f));
            const std::size_t first = out.extents.size();
            appendExtents(snapshot, f, out.extents);
            for (std::size_t k = first; k < out.extents.size(); ++k) {
                out.extents[k].file = to;
            }
        }
        return out;
    }

    DraftSet combineDrafts(DraftSet kept, DraftSet fresh) {
        DraftSet out;
        std::vector<FileIndex> keptTo(kept.files.size(), noFile);
        std::vector<FileIndex> freshTo(fresh.files.size(), noFile);
        const auto take = [&](const FileDraft& d, FileIndex& to) {
            to = static_cast<FileIndex>(out.files.size());
            out.files.push_back(d);
        };
        std::size_t k = 0;
        for (std::size_t n = 0; n < fresh.files.size(); ++n) {
            const std::uint64_t record = recordOfDraft(fresh.files[n]);
            for (; k < kept.files.size() && recordOfDraft(kept.files[k]) < record; ++k) {
                take(kept.files[k], keptTo[k]);
            }
            k += k < kept.files.size() && recordOfDraft(kept.files[k]) == record ? 1 : 0; // Replaced by the fresh one.
            take(fresh.files[n], freshTo[n]);
        }
        for (; k < kept.files.size(); ++k) {
            take(kept.files[k], keptTo[k]);
        }
        appendRemapped(kept.extents, keptTo, out.extents);
        appendRemapped(fresh.extents, freshTo, out.extents);
        return out;
    }

    std::optional<core::Snapshot> finishCatchUp(core::Snapshot& saved, const DraftSet& fresh,
                                                std::span<const std::uint64_t> rereadRecords, core::Executor& executor,
                                                const core::CancelToken& token) {
        if (saved.files.size() == 0 || saved.names.size() > 2 * saved.files.size() + namePoolSlack ||
            !std::ranges::is_sorted(rereadRecords)) {
            return std::nullopt;
        }
        return CatchUpFinisher(saved, fresh, rereadRecords, executor, token).run(saved);
    }

    DraftSet draftFiles(const RowIndex& rows, const ExtensionMap& extensions, core::VolumeGeometry geometry,
                        core::Executor& executor, const core::CancelToken& token) {
        return Drafter(rows, extensions, geometry, executor, token).run();
    }

    MergeOutput finishDrafts(DraftSet drafts, core::VolumeGeometry geometry, std::vector<core::ClusterRun> freeRuns,
                             core::Executor& executor, const core::CancelToken& token,
                             std::shared_ptr<std::pmr::memory_resource> storage) {
        return Finisher(std::move(drafts), geometry, executor, token, std::move(storage)).run(std::move(freeRuns));
    }

    MergeOutput mergeRows(const RowIndex& rows, const ExtensionMap& extensions, core::VolumeGeometry geometry,
                          std::vector<core::ClusterRun> freeRuns, core::Executor& executor,
                          const core::CancelToken& token, std::shared_ptr<std::pmr::memory_resource> storage) {
        return finishDrafts(draftFiles(rows, extensions, geometry, executor, token), geometry, std::move(freeRuns),
                            executor, token, std::move(storage));
    }

} // namespace icd::services::mft
