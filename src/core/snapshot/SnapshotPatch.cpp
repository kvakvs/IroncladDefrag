#include "core/snapshot/SnapshotPatch.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <numeric>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace icd::core {

    namespace {

        // Extent records per chunk of the parallel record pass.
        constexpr std::size_t recordGrain = std::size_t{1} << 16U;

        // Files per chunk of the parallel file passes.
        constexpr std::size_t fileGrain = std::size_t{1} << 15U;

        // "No position yet" in the position maps.
        constexpr std::uint32_t noPosition = std::numeric_limits<std::uint32_t>::max();

        // A record the patch adds (a new extent or a placeholder run), with what the per-file order needs.
        struct NewRecord {
            ExtentRecord record; // As stored.
            std::uint64_t vcn = 0; // Position in its stream.
        };

        // The builder's order of pending extents: by LCN, then owner, so the result does not depend on input order.
        bool lcnOrder(const ExtentEntry& a, const ExtentEntry& b) {
            return std::tie(a.run.start, a.file, a.stream, a.vcn, a.run.length) <
                std::tie(b.run.start, b.file, b.stream, b.vcn, b.run.length);
        }

        // A run as records of at most maxExtentLength clusters, as the builder splits it.
        void appendRecords(const ExtentEntry& e, std::vector<NewRecord>& out) {
            for (ClusterCount done = 0; done < e.run.length; done += maxExtentLength) {
                const ClusterCount piece = std::min(maxExtentLength, e.run.length - done);
                out.push_back(
                    {ExtentRecord(
                         {.run = {e.run.start + done, piece}, .file = e.file, .stream = e.stream, .flags = e.flags}),
                     e.vcn + done});
            }
        }

        // Sorted runs with touching and overlapping ones joined.
        std::vector<ClusterRun> joinSorted(std::span<const ClusterRun> runs) {
            std::vector<ClusterRun> out;
            out.reserve(runs.size());
            for (const ClusterRun& run : runs) {
                if (!out.empty() && run.start <= out.back().end()) {
                    out.back().length = std::max(out.back().end(), run.end()) - out.back().start;
                }
                else if (run.length != 0) {
                    out.push_back(run);
                }
            }
            return out;
        }

        // Two sorted run lists as one, touching and overlapping runs joined.
        std::vector<ClusterRun> mergeJoined(std::span<const ClusterRun> a, std::span<const ClusterRun> b) {
            std::vector<ClusterRun> out;
            out.reserve(a.size() + b.size());
            const auto add = [&](ClusterRun run) {
                if (!out.empty() && run.start <= out.back().end()) {
                    out.back().length = std::max(out.back().end(), run.end()) - out.back().start;
                }
                else if (run.length != 0) {
                    out.push_back(run);
                }
            };
            std::size_t i = 0;
            std::size_t k = 0;
            while (i < a.size() || k < b.size()) {
                const bool fromA = k == b.size() || (i < a.size() && a[i].start <= b[k].start);
                add(fromA ? a[i++] : b[k++]);
            }
            return out;
        }

        // What is left of one run once the records from `first` on (sorted, disjoint) are taken out.
        void cutRun(ClusterRun run, std::span<const NewRecord> taken, std::size_t first, std::vector<ClusterRun>& out) {
            Lcn cursor = run.start;
            for (std::size_t k = first; k < taken.size() && taken[k].record.lcn() < run.end(); ++k) {
                if (taken[k].record.lcn() > cursor) {
                    out.push_back({cursor, taken[k].record.lcn() - cursor});
                }
                cursor = std::max(cursor, taken[k].record.end());
            }
            if (cursor < run.end()) {
                out.push_back({cursor, run.end() - cursor});
            }
        }

        // Sorted, disjoint runs minus the clusters of sorted, disjoint records.
        std::vector<ClusterRun> subtract(std::span<const ClusterRun> runs, std::span<const NewRecord> taken) {
            std::vector<ClusterRun> out;
            out.reserve(runs.size() + taken.size());
            std::size_t first = 0;
            for (const ClusterRun& run : runs) {
                while (first < taken.size() && taken[first].record.end() <= run.start) {
                    ++first;
                }
                cutRun(run, taken, first, out);
            }
            return out;
        }

        // One chunk of the record pass: its base records, the inserted records that fall among them, and where its
        // output starts.
        struct ChunkMerge {
            std::size_t base = 0; // First base record.
            std::size_t baseEnd = 0; // One past the last base record.
            std::size_t inserted = 0; // First inserted record.
            std::size_t insertedEnd = 0; // One past the last inserted record.
            std::size_t out = 0; // First output position.
        };

        // LCN of a new record, for searches.
        Lcn lcnOfNew(const NewRecord& r) { return r.record.lcn(); }

    } // namespace

    // Applies one patch; a friend of the snapshot classes.
    class SnapshotPatcher {
    public:
        SnapshotPatcher(Snapshot& base, SnapshotPatch& patch, Executor& executor, const CancelToken& token) :
            base_(base), patch_(patch), executor_(executor), token_(token) {}

        // All steps; nullopt before anything of the base is touched.
        std::optional<Snapshot> run() {
            if (!mapBaseFiles() || !prepareNew() || !newFitsReleased() || !placeUnattributed()) {
                return std::nullopt;
            }
            std::vector<ClusterRun> free = freeAfter();
            std::ranges::merge(new_, placed_, std::back_inserter(inserted_), {}, lcnOfNew, lcnOfNew);
            Snapshot out;
            out.geometry = base_.geometry;
            out.usn = base_.usn;
            out.extents = ExtentIndex(base_.extents.records_.get_allocator().shared());
            buildRecords(out.extents);
            buildFiles(out);
            buildByFile(out);
            out.tree = DirectoryTree(out.files);
            out.freeSpace.runs_ = std::move(free);
            for (const ClusterRun& run : out.freeSpace.runs_) {
                out.freeSpace.freeClusters_ += run.length;
            }
            return out;
        }

    private:
        // Base file -> new file for continued files; false when the columns disagree, a base file is continued twice
        // or a continued file's flags changed. Written in parallel, then checked: of two files claiming one base file
        // only one can win, so the other fails the check.
        bool mapBaseFiles() {
            const std::size_t n = patch_.baseOf.size();
            if (patch_.parents.size() != n || patch_.flags.size() != n || !validUnattributed() ||
                std::ranges::count(patch_.baseOf, noFile) != static_cast<std::ptrdiff_t>(patch_.newFiles.size())) {
                return false;
            }
            newOf_.assign(base_.files.size(), noFile);
            const auto bad = [&](FileIndex b) {
                return b != noFile && (b >= newOf_.size() || b == patch_.baseUnattributed);
            };
            if (std::ranges::any_of(patch_.baseOf, bad)) {
                return false;
            }
            forFiles([&](std::size_t f) {
                if (patch_.baseOf[f] != noFile) {
                    newOf_[patch_.baseOf[f]] = static_cast<FileIndex>(f);
                }
            });
            return parallelAll([&](std::size_t f) { return continues(f); });
        }

        // The new placeholder, if any, is a new file without a base.
        [[nodiscard]] bool validUnattributed() const {
            const FileIndex u = patch_.unattributed;
            return u == noFile || (u < patch_.baseOf.size() && patch_.baseOf[u] == noFile);
        }

        // Whether new file f (if it continues a base file) is the one mapped to it and keeps its flags.
        [[nodiscard]] bool continues(std::size_t f) const {
            const FileIndex b = patch_.baseOf[f];
            return b == noFile || (newOf_[b] == f && patch_.flags[f] == base_.files.flags(b));
        }

        // Runs body(f) for every new file on the executor.
        template <typename Body>
        void forFiles(Body body) {
            executor_.parallelFor(
                {patch_.baseOf.size(), fileGrain},
                [&](std::size_t begin, std::size_t end) {
                    for (std::size_t f = begin; f < end; ++f) {
                        body(f);
                    }
                },
                token_);
        }

        // Whether test(f) holds for every new file, tested in parallel.
        template <typename Test>
        bool parallelAll(Test test) {
            // An int, not a bool: the reduction keeps one partial per chunk, and std::vector<bool> packs bits.
            const int all = parallelReduce(
                executor_, {patch_.baseOf.size(), fileGrain}, 1,
                [&](std::size_t begin, std::size_t end) {
                    for (std::size_t f = begin; f < end; ++f) {
                        if (!test(f)) {
                            return 0;
                        }
                    }
                    return 1;
                },
                [](int a, int b) { return a & b; }, token_);
            return all != 0;
        }

        // Sorts the new extents into records; false when they overlap or do not belong to new files.
        bool prepareNew() {
            auto& extents = patch_.extents;
            std::erase_if(extents, [](const ExtentEntry& e) { return e.run.length == 0; });
            std::ranges::sort(extents, lcnOrder);
            Lcn previousEnd = 0;
            for (const ExtentEntry& e : extents) {
                if (!validNew(e) || e.run.start < previousEnd) {
                    return false;
                }
                previousEnd = e.run.end();
                appendRecords(e, new_);
            }
            return true;
        }

        // A new extent belongs to a new file (not the placeholder) and lies on the volume.
        [[nodiscard]] bool validNew(const ExtentEntry& e) const {
            return e.file < patch_.baseOf.size() && patch_.baseOf[e.file] == noFile && e.file != patch_.unattributed &&
                e.run.end() <= base_.geometry.totalClusters && e.run.end() >= e.run.start;
        }

        // No new record overlaps a continued file's extent (they may land on released or placeholder clusters).
        [[nodiscard]] bool newFitsReleased() const {
            for (const NewRecord& r : new_) {
                for (const ExtentRecord& b : base_.extents.overlapping(r.record.run())) {
                    if (newOf_[b.file()] != noFile) {
                        return false;
                    }
                }
            }
            return true;
        }

        // Runs of a base file in LCN order, joined.
        [[nodiscard]] std::vector<ClusterRun> baseRuns(FileIndex b) const {
            std::vector<ClusterRun> runs;
            for (const std::uint32_t position : base_.extents.extentsOf(b)) {
                runs.push_back(base_.extents.records_[position].run());
            }
            std::ranges::sort(runs, {}, &ClusterRun::start);
            return joinSorted(runs);
        }

        // What is left of the base placeholder's clusters becomes the new placeholder's records.
        bool placeUnattributed() {
            if (patch_.baseUnattributed == noFile || patch_.baseUnattributed >= base_.files.size()) {
                return true;
            }
            const std::vector<ClusterRun> left = subtract(baseRuns(patch_.baseUnattributed), new_);
            if (!left.empty() && patch_.unattributed == noFile) {
                return false;
            }
            std::uint64_t vcn = 0;
            for (const ClusterRun& run : left) {
                appendRecords(
                    {.run = run, .vcn = vcn, .file = patch_.unattributed, .stream = 0, .flags = ExtentUnmovable},
                    placed_);
                vcn += run.length;
            }
            return true;
        }

        // The base's free runs plus the clusters of base files not continued, minus the new records.
        [[nodiscard]] std::vector<ClusterRun> freeAfter() const {
            std::vector<ClusterRun> released;
            for (FileIndex b = 0; b < base_.files.size(); ++b) {
                if (newOf_[b] == noFile && b != patch_.baseUnattributed) {
                    for (const std::uint32_t position : base_.extents.extentsOf(b)) {
                        released.push_back(base_.extents.records_[position].run());
                    }
                }
            }
            std::ranges::sort(released, {}, &ClusterRun::start);
            return subtract(mergeJoined(base_.freeSpace.runs_, released), new_);
        }

        // Where each chunk of base records starts taking inserted records (by LCN), plus an end marker.
        [[nodiscard]] std::vector<std::size_t> insertedStarts(ChunkedRange range, std::size_t chunks) const {
            const auto& records = base_.extents.records_;
            std::vector<std::size_t> starts(chunks + 1, inserted_.size());
            starts[0] = 0;
            for (std::size_t c = 1; c < chunks; ++c) {
                const Lcn lcn = records[range.chunkBegin(c)].lcn();
                starts[c] = static_cast<std::size_t>(std::ranges::lower_bound(inserted_, lcn, {}, lcnOfNew) -
                                                     inserted_.begin());
            }
            return starts;
        }

        // Kept base records in [begin, end).
        [[nodiscard]] std::size_t keptIn(std::size_t begin, std::size_t end) const {
            const auto& records = base_.extents.records_;
            std::size_t kept = 0;
            for (std::size_t i = begin; i < end; ++i) {
                kept += newOf_[records[i].file()] != noFile ? 1 : 0;
            }
            return kept;
        }

        // The kept base records (new file indexes) merged with the inserted ones in LCN order, chunk by chunk.
        void buildRecords(ExtentIndex& index) {
            const auto& records = base_.extents.records_;
            const ChunkedRange range{records.size(), recordGrain};
            const std::size_t chunks = std::max<std::size_t>(1, range.chunks());
            const std::vector<std::size_t> starts = insertedStarts(range, chunks);
            std::vector<std::size_t> offsets(chunks + 1, 0);
            executor_.parallelFor(
                {chunks, 1},
                [&](std::size_t begin, std::size_t end) {
                    for (std::size_t c = begin; c < end; ++c) {
                        offsets[c + 1] = keptIn(range.chunkBegin(c), range.chunkEnd(c)) + starts[c + 1] - starts[c];
                    }
                },
                token_);
            std::partial_sum(offsets.begin(), offsets.end(), offsets.begin());
            index.records_.resize(offsets.back());
            newPosition_.assign(records.size(), noPosition);
            insertedPosition_.assign(inserted_.size(), noPosition);
            executor_.parallelFor(
                {chunks, 1},
                [&](std::size_t begin, std::size_t end) {
                    for (std::size_t c = begin; c < end; ++c) {
                        mergeChunk({.base = range.chunkBegin(c),
                                    .baseEnd = range.chunkEnd(c),
                                    .inserted = starts[c],
                                    .insertedEnd = starts[c + 1],
                                    .out = offsets[c]},
                                   index.records_);
                    }
                },
                token_);
        }

        // Merges a chunk's base records with its inserted ones into the output from `at.out` on.
        void mergeChunk(ChunkMerge at, LargeVector<ExtentRecord>& records) {
            const auto& from = base_.extents.records_;
            auto [b, bEnd, i, iEnd, out] = at;
            while (b < bEnd || i < iEnd) {
                if (b < bEnd && newOf_[from[b].file()] == noFile) {
                    ++b;
                    continue;
                }
                if (i == iEnd || (b < bEnd && from[b].lcn() < inserted_[i].record.lcn())) {
                    records[out] = remapped(from[b]);
                    newPosition_[b++] = static_cast<std::uint32_t>(out++);
                }
                else {
                    records[out] = inserted_[i].record;
                    insertedPosition_[i++] = static_cast<std::uint32_t>(out++);
                }
            }
        }

        // A kept base record with its new file index.
        [[nodiscard]] ExtentRecord remapped(const ExtentRecord& r) const {
            return ExtentRecord({.run = r.run(), .file = newOf_[r.file()], .stream = r.stream(), .flags = r.flags()});
        }

        // The file columns: values from the entries, kept names and clusters from the base, new names interned into
        // the base's pool (which the result takes over).
        void buildFiles(Snapshot& out) {
            FileTable& t = out.files;
            const std::size_t n = patch_.baseOf.size();
            t.names_.resize(n);
            t.parents_.resize(n);
            t.sizes_.resize(n);
            t.clusters_.assign(n, 0);
            t.flags_.resize(n);
            t.times_.resize(n);
            t.references_.resize(n);
            executor_.parallelFor(
                {n, fileGrain},
                [&](std::size_t begin, std::size_t end) {
                    for (std::size_t f = begin; f < end; ++f) {
                        copyFile(f, t);
                    }
                },
                token_);
            // Copies of the new names first: they may point into the base's pool, which moves as it grows.
            std::vector<std::string> names;
            names.reserve(patch_.newFiles.size());
            for (const FileEntry& e : patch_.newFiles) {
                names.emplace_back(e.name);
            }
            out.names = std::move(base_.names);
            std::size_t k = 0;
            for (std::size_t f = 0; f < n; ++f) {
                if (patch_.baseOf[f] == noFile) {
                    copyNewFile(f, patch_.newFiles[k], names[k], out);
                    ++k;
                }
            }
            out.names.freezeLookup();
        }

        // One file's parent and flags, and a continued file's other columns from the base.
        void copyFile(std::size_t f, FileTable& t) const {
            const FileIndex b = patch_.baseOf[f];
            t.parents_[f] = patch_.parents[f];
            t.flags_[f] = patch_.flags[f];
            if (b == noFile) {
                return;
            }
            const FileTable& from = base_.files;
            t.names_[f] = from.names_[b];
            t.sizes_[f] = from.sizes_[b];
            t.times_[f] = from.times_[b];
            t.references_[f] = from.references_[b];
            t.clusters_[f] = from.clusters_[b];
        }

        // A new file's columns from its entry; its name joins the pool.
        static void copyNewFile(std::size_t f, const FileEntry& e, std::string_view name, Snapshot& out) {
            FileTable& t = out.files;
            t.names_[f] = out.names.intern(name);
            t.sizes_[f] = e.sizeBytes;
            t.times_[f] = e.times;
            t.references_[f] = e.fileReference;
        }

        // Per-file positions: continued files keep their base order (positions mapped), inserted records are ordered
        // by stream and VCN; new files' clusters summed.
        void buildByFile(Snapshot& out) {
            ExtentIndex& index = out.extents;
            const ExtentIndex& from = base_.extents;
            const std::size_t n = patch_.baseOf.size();
            index.fileStart_.assign(n + 1, 0);
            for (std::size_t f = 0; f < n; ++f) {
                const FileIndex b = patch_.baseOf[f];
                index.fileStart_[f + 1] = b != noFile ? from.fileStart_[b + 1] - from.fileStart_[b] : 0;
            }
            for (const NewRecord& r : inserted_) {
                ++index.fileStart_[r.record.file() + 1];
                out.files.clusters_[r.record.file()] += r.record.length();
            }
            std::partial_sum(index.fileStart_.begin(), index.fileStart_.end(), index.fileStart_.begin());
            index.byFile_.resize(index.records_.size());
            executor_.parallelFor(
                {n, fileGrain},
                [&](std::size_t begin, std::size_t end) {
                    for (std::size_t f = begin; f < end; ++f) {
                        mapPositions(static_cast<FileIndex>(f), index);
                    }
                },
                token_);
            placeInserted(index);
        }

        // A continued file's positions, in its base order.
        void mapPositions(FileIndex f, ExtentIndex& index) const {
            const FileIndex b = patch_.baseOf[f];
            if (b == noFile) {
                return;
            }
            const ExtentIndex& from = base_.extents;
            std::uint32_t to = index.fileStart_[f];
            for (std::uint32_t k = from.fileStart_[b]; k < from.fileStart_[b + 1]; ++k) {
                index.byFile_[to++] = newPosition_[from.byFile_[k]];
            }
        }

        // The inserted records' positions, per file by stream and VCN (the builder's per-file order).
        void placeInserted(ExtentIndex& index) const {
            std::vector<std::uint32_t> order(inserted_.size());
            std::iota(order.begin(), order.end(), 0U);
            std::ranges::sort(order, {}, [&](std::uint32_t i) {
                const ExtentRecord& r = inserted_[i].record;
                return std::tuple(r.file(), r.stream(), inserted_[i].vcn);
            });
            FileIndex file = noFile;
            std::uint32_t to = 0;
            for (const std::uint32_t i : order) {
                if (inserted_[i].record.file() != file) {
                    file = inserted_[i].record.file();
                    to = index.fileStart_[file];
                }
                index.byFile_[to++] = insertedPosition_[i];
            }
        }

        Snapshot& base_; // The snapshot being patched; spent at the end.
        SnapshotPatch& patch_; // The changes; its extents get sorted.
        Executor& executor_; // Parallel passes.
        const CancelToken& token_; // Checked by the passes.
        std::vector<FileIndex> newOf_; // Base file -> new file, noFile when not continued.
        std::vector<NewRecord> new_; // The new extents as records, by LCN.
        std::vector<NewRecord> placed_; // The new placeholder's records, by LCN.
        std::vector<NewRecord> inserted_; // Both, by LCN.
        std::vector<std::uint32_t> newPosition_; // Base record position -> new position (kept records only).
        std::vector<std::uint32_t> insertedPosition_; // Inserted record -> new position.
    };

    std::optional<Snapshot> patchSnapshot(Snapshot& base, SnapshotPatch patch, Executor& executor,
                                          const CancelToken& token) {
        return SnapshotPatcher(base, patch, executor, token).run();
    }

} // namespace icd::core
