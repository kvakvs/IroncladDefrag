#include "core/synth/SyntheticVolume.h"

#include "core/snapshot/SnapshotBuilder.h"
#include "core/synth/SynthRandom.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <queue>
#include <utility>

namespace icd::core {

    std::vector<SynthClass> SynthOptions::defaultClasses() {
        return {
            {".cpp", 0.35, 12e3, 1.2},  {".docx", 0.15, 200e3, 1.0}, {".jpg", 0.25, 6e6, 0.5},
            {".dll", 0.12, 1.5e6, 1.3}, {".zip", 0.02, 300e6, 1.5},  {".mp4", 0.01, 1.5e9, 0.8},
            {".dat", 0.10, 500e3, 2.0},
        };
    }

    namespace {

        // Files at most this large live inside their MFT record and own no clusters.
        constexpr double residentBytes = 700.0;

        // Distinct name stems per class; real volumes repeat names, and the name pool interns them.
        constexpr std::uint32_t nameStems = 50'000;

        // FILETIME of 2026-01-01; synthetic times lie up to ten years before it.
        constexpr std::int64_t newestTime = 134'116'992'000'000'000;

        // 100 ns ticks in ten years.
        constexpr std::int64_t tenYears = 10LL * 365 * 24 * 3600 * 10'000'000;

        // A file drawn before scaling.
        struct DrawnFile {
            std::uint8_t kind = 0; // Index into the classes.
            double bytes = 0.0; // Size before scaling.
        };

        // A piece of a file waiting to be placed.
        struct Piece {
            FileIndex file = noFile; // Owner.
            ClusterCount length = 0; // Clusters.
            std::uint64_t vcn = 0; // Position in the stream.
            std::uint16_t stream = 0; // Stream ordinal.
            std::uint16_t flags = 0; // ExtentFlag bits.
        };

        // A later piece of a fragmented file, placed once enough other pieces went down.
        struct Deferred {
            std::uint64_t due = 0; // Piece count at which it is placed.
            std::uint64_t order = 0; // Tie-break, so equal dues keep scheduling order.
            Piece piece; // What to place.

            // Earliest due first (std::priority_queue keeps the largest on top).
            bool operator>(const Deferred& other) const {
                return std::pair(due, order) > std::pair(other.due, other.order);
            }
        };

        // Size and file counts the layout works from.
        struct Plan {
            ClusterCount usedClusters = 0; // All pieces together.
            std::uint64_t pieces = 0; // All pieces together.
        };

        // Classes with defaults filled in and a usable share.
        std::vector<SynthClass> classesOf(const SynthOptions& options) {
            std::vector<SynthClass> classes =
                options.classes.empty() ? SynthOptions::defaultClasses() : options.classes;
            std::erase_if(classes, [](const SynthClass& c) { return c.fileShare <= 0.0; });
            if (classes.empty()) {
                throw SnapshotError("synthetic volume needs at least one class with a positive share");
            }
            return classes;
        }

        // Draws class and raw size of every file.
        std::vector<DrawnFile> drawFiles(const SynthOptions& options, const std::vector<SynthClass>& classes,
                                         SynthRandom& random) {
            double totalShare = 0.0;
            for (const SynthClass& c : classes) {
                totalShare += c.fileShare;
            }
            std::vector<DrawnFile> files(options.files);
            for (DrawnFile& file : files) {
                double pick = random.unit() * totalShare;
                std::size_t kind = 0;
                while (kind + 1 < classes.size() && pick >= classes[kind].fileShare) {
                    pick -= classes[kind].fileShare;
                    ++kind;
                }
                const SynthClass& c = classes[kind];
                // Log-normal with the class mean: exp(sigma * z - sigma^2 / 2) has mean 1.
                file.kind = static_cast<std::uint8_t>(kind);
                file.bytes = c.meanBytes * std::exp(c.sizeSpread * random.normal() - c.sizeSpread * c.sizeSpread / 2);
            }
            return files;
        }

        // Clusters of a file of the given size (0 for resident files).
        ClusterCount clustersOf(double bytes, std::uint32_t bytesPerCluster) {
            return bytes <= residentBytes ? 0 : static_cast<ClusterCount>(std::ceil(bytes / bytesPerCluster));
        }

        // Scales sizes so the files fill the requested share of what metadata leaves free.
        void scaleToFill(std::vector<DrawnFile>& files, const SynthOptions& options, ClusterCount reserved) {
            const auto& geometry = options.geometry;
            const double target = std::max(0.0,
                                           options.fillPercent / 100.0 * static_cast<double>(geometry.totalClusters) -
                                               static_cast<double>(reserved));
            double drawn = 0.0;
            for (const DrawnFile& file : files) {
                drawn += static_cast<double>(clustersOf(file.bytes, geometry.bytesPerCluster));
            }
            const double factor = drawn > 0.0 ? target / drawn : 1.0;
            for (DrawnFile& file : files) {
                file.bytes *= factor;
            }
        }

        // Random timestamps: created up to ten years ago, then modified, then accessed.
        FileTimes randomTimes(SynthRandom& random) {
            FileTimes times;
            times.created = newestTime - static_cast<std::int64_t>(random.below(tenYears));
            times.modified = times.created + static_cast<std::int64_t>(random.below(newestTime - times.created + 1));
            times.accessed = times.modified + static_cast<std::int64_t>(random.below(newestTime - times.modified + 1));
            return times;
        }

        // What the layout needs to know up front.
        struct LayoutShape {
            ClusterCount scatteredFree = 0; // Free clusters to spread between pieces (the rest is the tail).
            std::uint64_t pieces = 0; // Pieces to place.
            double gapChance = 0.0; // Chance of a gap before a piece.
            std::uint32_t interleave = 1; // Most pieces between two fragments of a file.
        };

        // Places pieces from LCN 0 upwards with free gaps in between, and keeps the later pieces of fragmented
        // files back until a random number of other pieces went down.
        class Layout {
        public:
            // Lays out pieces on builder, spreading shape.scatteredFree between them.
            Layout(SnapshotBuilder& builder, SynthRandom& random, const LayoutShape& shape) :
                builder_(builder), random_(random), scatteredLeft_(shape.scatteredFree), gapChance_(shape.gapChance),
                interleave_(std::max(1U, shape.interleave)),
                meanGap_(shape.pieces == 0 || shape.gapChance <= 0.0 ? 0.0
                                                                     : static_cast<double>(shape.scatteredFree) /
                                 (static_cast<double>(shape.pieces) * shape.gapChance)) {
                extents_.reserve(shape.pieces);
            }

            // Places a whole file: the first piece now, the others later.
            void placeFile(const Piece& whole, std::uint32_t fragments) {
                placeDue();
                const std::uint32_t count = static_cast<std::uint32_t>(std::min<ClusterCount>(fragments, whole.length));
                ClusterCount left = whole.length;
                std::uint64_t vcn = 0;
                std::uint64_t due = placed_;
                for (std::uint32_t index = 0; index < count; ++index) {
                    const std::uint32_t remaining = count - index;
                    const ClusterCount length = remaining == 1 ? left : pieceLength(left, remaining);
                    Piece piece = whole;
                    piece.length = length;
                    piece.vcn = vcn;
                    if (index == 0) {
                        place(piece);
                    }
                    else {
                        due += 1 + random_.below(interleave_);
                        deferred_.push({due, order_++, piece});
                    }
                    vcn += length;
                    left -= length;
                }
            }

            // Places everything still held back and the free tail, and hands the extents to the builder.
            void finish(ClusterCount totalClusters) {
                while (!deferred_.empty()) {
                    place(deferred_.top().piece);
                    deferred_.pop();
                }
                if (lcn_ < totalClusters) {
                    builder_.addFreeRun({lcn_, totalClusters - lcn_});
                }
                builder_.addExtents(std::move(extents_));
            }

        private:
            // Length of the next of remaining pieces of left clusters: around an even share, at least 1, and
            // leaving at least one cluster per later piece.
            ClusterCount pieceLength(ClusterCount left, std::uint32_t remaining) {
                const ClusterCount even = left / remaining;
                const auto varied = static_cast<ClusterCount>(static_cast<double>(even) * (0.5 + random_.unit()));
                return std::clamp<ClusterCount>(varied, 1, left - (remaining - 1));
            }

            // Places held-back pieces that are due.
            void placeDue() {
                while (!deferred_.empty() && deferred_.top().due <= placed_) {
                    place(deferred_.top().piece);
                    deferred_.pop();
                }
            }

            // Puts a free gap (maybe empty) and then the piece at the current position.
            void place(const Piece& piece) {
                ClusterCount gap = 0;
                if (random_.unit() < gapChance_) {
                    gap = std::min(scatteredLeft_, static_cast<ClusterCount>(random_.exponential(meanGap_)));
                }
                if (gap > 0) {
                    builder_.addFreeRun({lcn_, gap});
                    lcn_ += gap;
                    scatteredLeft_ -= gap;
                }
                ExtentEntry entry;
                entry.file = piece.file;
                entry.run = {lcn_, piece.length};
                entry.vcn = piece.vcn;
                entry.stream = piece.stream;
                entry.flags = piece.flags;
                extents_.push_back(entry);
                lcn_ += piece.length;
                ++placed_;
            }

            SnapshotBuilder& builder_; // Receives extents and free runs.
            SynthRandom& random_; // Shared random source.
            ClusterCount scatteredLeft_ = 0; // Free clusters still to spread between pieces.
            double gapChance_ = 0.0; // Chance of a gap before a piece.
            std::uint32_t interleave_ = 1; // Most pieces between two fragments of a file.
            double meanGap_ = 0.0; // Average gap before a piece.
            Lcn lcn_ = 0; // Next free position.
            std::uint64_t placed_ = 0; // Pieces placed so far.
            std::uint64_t order_ = 0; // Scheduling counter for ties.
            std::vector<ExtentEntry> extents_; // Placed pieces.
            std::priority_queue<Deferred, std::vector<Deferred>, std::greater<>> deferred_; // Held-back pieces.
        };

        // Share of free space spread between pieces for a pattern (the rest is the tail).
        double scatteredShare(FreePattern pattern) {
            switch (pattern) {
            case FreePattern::Scattered:
                return 1.0;
            case FreePattern::Tail:
                return 0.02;
            case FreePattern::Mixed:
                break;
            }
            return 0.5;
        }

        // The generator's state while it adds files and lays them out.
        class Generator {
        public:
            // Prepares a generator for options.
            explicit Generator(const SynthOptions& options) :
                options_(options), random_(options.seed), builder_(options.geometry), classes_(classesOf(options)) {}

            // Produces the snapshot.
            Snapshot run() {
                const FileIndex root = addEntry("", {.parent = noFile, .flags = FileDirectory, .bytes = 0});
                const ClusterCount mftClusters = std::max<ClusterCount>(
                    1, (std::uint64_t{options_.files} * 1024) / options_.geometry.bytesPerCluster);
                const FileIndex mft = addEntry("$MFT",
                                               {.parent = root,
                                                .flags = FileMetadata | FileUnmovable,
                                                .bytes = mftClusters * options_.geometry.bytesPerCluster});
                addDirectories(root);
                std::vector<DrawnFile> drawn = drawFiles(options_, classes_, random_);
                scaleToFill(drawn, options_, mftClusters + 4 * directories_.size());
                addFiles(drawn);
                queue_.insert(queue_.begin(), {mft, mftClusters, 1, ExtentUnmovable, 0});
                layOut();
                addProjectMarker(root);
                return std::move(builder_).build();
            }

        private:
            // A file to lay out.
            struct Job {
                FileIndex file = noFile; // Owner.
                ClusterCount clusters = 0; // Clusters.
                std::uint32_t fragments = 1; // Pieces.
                std::uint16_t flags = 0; // ExtentFlag bits.
                std::uint16_t stream = 0; // Stream ordinal.
            };

            // Facts of a new file entry, named at the call site.
            struct EntryFacts {
                FileIndex parent = noFile; // Containing directory.
                std::uint16_t flags = 0; // FileFlag bits.
                std::uint64_t bytes = 0; // Logical size.
            };

            // Adds a file entry with random times and the next file reference.
            FileIndex addEntry(std::string_view name, const EntryFacts& facts) {
                FileEntry entry;
                entry.name = name;
                entry.parent = facts.parent;
                entry.flags = facts.flags;
                entry.sizeBytes = facts.bytes;
                entry.times = randomTimes(random_);
                entry.fileReference = next_++;
                return builder_.addFile(entry);
            }

            // Adds an empty resident ".git" folder to the root, last and without random draws so layouts stay the
            // same: the volume is one software project, so its sources classify as sources (plan.md 8.3).
            void addProjectMarker(FileIndex root) {
                FileEntry entry;
                entry.name = ".git";
                entry.parent = root;
                entry.flags = FileDirectory | FileResident;
                entry.fileReference = next_++;
                builder_.addFile(entry);
            }

            // Adds the directory tree; each directory gets a small index allocation.
            void addDirectories(FileIndex root) {
                const std::uint32_t count =
                    std::max<std::uint32_t>(1, options_.files / std::max(1U, options_.filesPerDirectory));
                directories_.push_back(root);
                for (std::uint32_t index = 0; index < count; ++index) {
                    const FileIndex parent = directories_[random_.below(directories_.size())];
                    const FileIndex directory = addEntry(std::format("dir{}", index % nameStems),
                                                         {.parent = parent, .flags = FileDirectory, .bytes = 0});
                    directories_.push_back(directory);
                    queue_.push_back({directory, 1 + random_.below(4), 1, ExtentDirectoryIndex, 1});
                }
            }

            // Adds the regular files and queues the ones that own clusters.
            void addFiles(const std::vector<DrawnFile>& drawn) {
                const std::uint32_t bytesPerCluster = options_.geometry.bytesPerCluster;
                for (std::size_t index = 0; index < drawn.size(); ++index) {
                    const DrawnFile& file = drawn[index];
                    const ClusterCount clusters = clustersOf(file.bytes, bytesPerCluster);
                    const std::string name = std::format("f{}{}", index % nameStems, classes_[file.kind].extension);
                    const FileIndex parent = directories_[1 + random_.below(directories_.size() - 1)];
                    const FileIndex entry =
                        addEntry(name,
                                 {.parent = parent,
                                  .flags = static_cast<std::uint16_t>(clusters == 0 ? FileResident : 0),
                                  .bytes = static_cast<std::uint64_t>(file.bytes)});
                    if (clusters > 0) {
                        queue_.push_back({entry, clusters, fragmentsFor(), 0, 0});
                    }
                }
            }

            // Pieces for the next file: one, or 2..maxFragments for the fragmented share.
            std::uint32_t fragmentsFor() {
                if (options_.maxFragments < 2 || random_.unit() >= options_.fragmentedShare) {
                    return 1;
                }
                return 2 + static_cast<std::uint32_t>(random_.below(options_.maxFragments - 1));
            }

            // Shuffles the files (the $MFT stays first) and places them.
            void layOut() {
                Plan plan;
                for (const Job& job : queue_) {
                    plan.usedClusters += job.clusters;
                    plan.pieces += std::min<ClusterCount>(job.fragments, job.clusters);
                }
                const ClusterCount total = options_.geometry.totalClusters;
                if (plan.usedClusters > total) {
                    throw SnapshotError(std::format("synthetic files need {} clusters but the volume has {}",
                                                    plan.usedClusters, total));
                }
                for (std::size_t index = queue_.size() - 1; index > 1; --index) {
                    std::swap(queue_[index], queue_[1 + random_.below(index)]);
                }
                const auto scattered = static_cast<ClusterCount>(static_cast<double>(total - plan.usedClusters) *
                                                                 scatteredShare(options_.freePattern));
                Layout layout(builder_, random_,
                              {.scatteredFree = scattered,
                               .pieces = plan.pieces,
                               .gapChance = options_.gapChance,
                               .interleave = options_.interleave});
                for (const Job& job : queue_) {
                    layout.placeFile({job.file, job.clusters, 0, job.stream, job.flags}, job.fragments);
                }
                layout.finish(total);
            }

            const SynthOptions& options_; // What to build.
            SynthRandom random_; // The only random source, so the seed decides everything.
            SnapshotBuilder builder_; // Collects the snapshot.
            std::vector<SynthClass> classes_; // File kinds in use.
            std::vector<FileIndex> directories_; // Root first, then every directory.
            std::vector<Job> queue_; // Files to lay out.
            std::uint64_t next_ = 1; // Next file reference number.
        };

    } // namespace

    Snapshot generateSyntheticVolume(const SynthOptions& options) { return Generator(options).run(); }

} // namespace icd::core
