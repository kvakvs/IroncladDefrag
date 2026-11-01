#include "core/snapshot/BitmapToRuns.h"

#include <algorithm>
#include <bit>
#include <cstring>

namespace icd::core {

    namespace {

        constexpr std::uint64_t allUsed = ~std::uint64_t{0};

        // 64 clusters starting at word w; bytes past the span and bits past the valid clusters read as used.
        std::uint64_t loadWord(const BitmapChunk& chunk, std::size_t w) {
            std::uint64_t value = allUsed;
            const std::size_t offset = w * 8;
            if (offset < chunk.bits.size()) {
                std::memcpy(&value, chunk.bits.data() + offset, std::min<std::size_t>(8, chunk.bits.size() - offset));
            }
            const std::uint64_t first = std::uint64_t{w} * 64;
            if (first + 64 > chunk.clusters) {
                const std::uint64_t valid = chunk.clusters > first ? chunk.clusters - first : 0;
                value |= valid >= 64 ? 0 : allUsed << valid;
            }
            return value;
        }

        // Turns words of bits into runs; keeps the run that is open across word boundaries.
        class RunScanner {
        public:
            explicit RunScanner(Lcn base) : base_(base) {}

            // Handles the 64 clusters starting at offset `first` within the chunk.
            void word(std::uint64_t first, std::uint64_t value) {
                if (value == 0) {
                    open(first);
                }
                else if (value == allUsed) {
                    close(first);
                }
                else {
                    mixed(first, value);
                }
            }

            // Closes a run still open at the chunk end and returns the runs.
            std::vector<ClusterRun> finish(std::uint64_t end) {
                close(end);
                return std::move(runs_);
            }

        private:
            // A word with both free and used clusters: walk its bit changes.
            void mixed(std::uint64_t first, std::uint64_t value) {
                unsigned pos = 0;
                while (pos < 64) {
                    // Looking for the next used bit when a run is open, else for the next free one.
                    const std::uint64_t pattern = (open_ ? value : ~value) >> pos;
                    if (pattern == 0) {
                        return; // The rest of the word continues the current state.
                    }
                    pos += static_cast<unsigned>(std::countr_zero(pattern));
                    if (open_) {
                        close(first + pos);
                    }
                    else {
                        open(first + pos);
                    }
                }
            }

            // Starts a run at offset `at` unless one is open.
            void open(std::uint64_t at) {
                if (!open_) {
                    open_ = true;
                    start_ = at;
                }
            }

            // Ends the open run, if any, at offset `at`.
            void close(std::uint64_t at) {
                if (open_) {
                    open_ = false;
                    runs_.push_back({base_ + start_, at - start_});
                }
            }

            Lcn base_; // LCN of offset 0.
            bool open_ = false; // Whether a free run is open.
            std::uint64_t start_ = 0; // Offset where the open run started.
            std::vector<ClusterRun> runs_; // Finished runs.
        };

    } // namespace

    std::vector<ClusterRun> freeRunsOf(const BitmapChunk& chunk) {
        RunScanner scanner(chunk.startLcn);
        const std::size_t words = static_cast<std::size_t>((chunk.clusters + 63) / 64);
        for (std::size_t w = 0; w < words; ++w) {
            scanner.word(std::uint64_t{w} * 64, loadWord(chunk, w));
        }
        return scanner.finish(chunk.clusters);
    }

    void stitchRuns(std::vector<ClusterRun>& runs, std::span<const ClusterRun> next) {
        if (next.empty()) {
            return;
        }
        auto from = next.begin();
        if (!runs.empty() && runs.back().end() == from->start) {
            runs.back().length += from->length;
            ++from;
        }
        runs.insert(runs.end(), from, next.end());
    }

    std::vector<ClusterRun> freeRunsParallel(Executor& executor, const BitmapChunk& chunk, std::size_t grainBytes,
                                             const CancelToken& token) {
        const std::size_t bytes = static_cast<std::size_t>((chunk.clusters + 7) / 8);
        const std::size_t grain = std::max<std::size_t>(8, (grainBytes + 7) / 8 * 8);
        const ChunkedRange range{bytes, grain};
        std::vector<std::vector<ClusterRun>> parts(range.chunks());
        executor.parallelFor(
            range,
            [&](std::size_t begin, std::size_t end) {
                const std::uint64_t first = std::uint64_t{begin} * 8;
                const BitmapChunk piece{chunk.startLcn + first,
                                        std::min<std::uint64_t>(chunk.clusters - first, std::uint64_t{end - begin} * 8),
                                        chunk.bits.subspan(begin, end - begin)};
                parts[begin / grain] = freeRunsOf(piece);
            },
            token);
        std::vector<ClusterRun> runs;
        for (const auto& part : parts) {
            stitchRuns(runs, part);
        }
        return runs;
    }

} // namespace icd::core
