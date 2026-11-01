#include "services/scan/MftScanner.h"

#include "core/ntfs/AttributeList.h"
#include "core/ntfs/MftLayout.h"
#include "core/ntfs/MftRecord.h"
#include "platform/win/MftReader.h"
#include "platform/win/UsnJournal.h"
#include "services/scan/MftMerge.h"
#include "services/scan/MftRows.h"

#include <algorithm>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>

namespace icd::services {

    namespace {

        namespace ntfs = core::ntfs;
        using ntfs::Attribute;
        using ntfs::AttributeType;

        // How often the extension records of $MFT itself are looked for before giving up (each round can only use
        // the parts of $MFT already mapped).
        constexpr int bootstrapRounds = 8;

        // Where $MFT is and which of its records are in use.
        struct MftMap {
            std::vector<ntfs::DataRun> runs; // $MFT's $DATA in VCN order.
            std::uint64_t recordCount = 0; // Records it holds.
            std::vector<std::byte> bitmap; // $MFT::$BITMAP: bit n set when record n is in use.
        };

        // Counts tasks on the executor and waits for them; keeps the first exception. Waits in its destructor too, so
        // no task outlives the buffers it uses.
        class TaskGroup {
        public:
            explicit TaskGroup(core::Executor& executor) : executor_(executor) {}
            ~TaskGroup() { waitQuietly(); }
            TaskGroup(const TaskGroup&) = delete;
            TaskGroup& operator=(const TaskGroup&) = delete;

            // Runs work on the executor, or right here when it has a single thread (which may be the caller's own:
            // a queued task would then wait behind the reader forever).
            void run(core::Executor::Task work) {
                if (executor_.concurrency() <= 1) {
                    work();
                    return;
                }
                {
                    const std::scoped_lock lock(mutex_);
                    ++pending_;
                }
                executor_.submit([this, work = std::move(work)]() mutable {
                    try {
                        work();
                    }
                    catch (...) {
                        keep(std::current_exception());
                    }
                    finishOne();
                });
            }

            // Waits for every task and rethrows the first exception.
            void wait() {
                waitQuietly();
                const std::scoped_lock lock(mutex_);
                if (error_) {
                    std::rethrow_exception(std::exchange(error_, nullptr));
                }
            }

        private:
            // Waits without throwing.
            void waitQuietly() {
                std::unique_lock lock(mutex_);
                done_.wait(lock, [this] { return pending_ == 0; });
            }

            // Stores the first exception.
            void keep(const std::exception_ptr& error) {
                const std::scoped_lock lock(mutex_);
                error_ = error_ ? error_ : error;
            }

            // Marks a task finished.
            void finishOne() {
                const std::scoped_lock lock(mutex_);
                if (--pending_ == 0) {
                    done_.notify_all();
                }
            }

            core::Executor& executor_; // Where tasks run.
            std::mutex mutex_; // Guards the fields below.
            std::condition_variable done_; // Signalled when pending_ reaches 0.
            std::size_t pending_ = 0; // Tasks not finished.
            std::exception_ptr error_; // First failure.
        };

        // Reads a stream's bytes [0, length) through its runs.
        std::vector<std::byte> readStream(const platform::MftReader& reader, std::span<const ntfs::DataRun> runs,
                                          std::uint32_t bytesPerCluster, std::uint64_t length) {
            const auto pieces = ntfs::mapStreamBytes(runs, bytesPerCluster, {0, length});
            if (pieces.empty() && length != 0) {
                throw std::runtime_error("an $MFT attribute is not mapped completely");
            }
            std::vector<std::byte> bytes(length);
            std::size_t at = 0;
            for (const ntfs::RecordPiece& piece : pieces) {
                reader.read(piece.volumeOffset, std::span(bytes).subspan(at, piece.bytes));
                at += piece.bytes;
            }
            return bytes;
        }

        // The unnamed attributes of a type over all parts, sorted by VCN.
        std::vector<const Attribute*> unnamed(const std::vector<ntfs::ParsedRecord>& parts, AttributeType type) {
            std::vector<const Attribute*> out;
            for (const ntfs::ParsedRecord& part : parts) {
                for (const Attribute& a : part.attributes) {
                    if (a.type == type && a.name.empty()) {
                        out.push_back(&a);
                    }
                }
            }
            std::ranges::sort(out, {}, &Attribute::startVcn);
            return out;
        }

        // The runs of $DATA mapped so far (segments may still be missing).
        std::vector<ntfs::DataRun> knownRuns(const std::vector<ntfs::ParsedRecord>& parts) {
            std::vector<ntfs::DataRun> runs;
            for (const Attribute* a : unnamed(parts, AttributeType::Data)) {
                runs.insert(runs.end(), a->runs.begin(), a->runs.end());
            }
            return runs;
        }

        // Maps $MFT from record 0, following its attribute list into extension records when $MFT is so fragmented
        // that its run list does not fit one record.
        class Bootstrap {
        public:
            Bootstrap(const platform::MftReader& reader, ntfs::MftGeometry geometry, std::uint64_t mftStartLcn) :
                reader_(reader), geometry_(geometry), startLcn_(mftStartLcn) {}

            // Reads and maps.
            MftMap run() {
                std::vector<std::byte> raw(geometry_.bytesPerRecord);
                reader_.read(startLcn_ * geometry_.bytesPerCluster, raw);
                parse(raw, 0);
                readExtensions();
                return map();
            }

        private:
            // Parses one record of $MFT into parts_.
            void parse(std::vector<std::byte>& raw, std::uint64_t number) {
                ntfs::ParsedRecord record;
                const auto status = ntfs::parseRecord(raw, number, record);
                if (status != ntfs::RecordStatus::Ok) {
                    throw std::runtime_error("$MFT record " + std::to_string(number) + " is damaged (" +
                                             ntfs::statusName(status) + ")");
                }
                parts_.push_back(std::move(record));
            }

            // Records named by record 0's attribute list, other than record 0.
            std::set<std::uint64_t> listedRecords() const {
                const auto lists = unnamed(parts_, AttributeType::AttributeList);
                if (lists.empty()) {
                    return {};
                }
                const Attribute& list = *lists.front();
                const std::vector<std::byte> value = list.nonResident
                    ? readStream(reader_, list.runs, geometry_.bytesPerCluster, list.dataSize)
                    : list.value;
                std::vector<ntfs::AttributeListEntry> entries;
                if (!ntfs::parseAttributeList(value, entries)) {
                    throw std::runtime_error("the attribute list of $MFT is damaged");
                }
                std::set<std::uint64_t> records;
                for (const ntfs::AttributeListEntry& e : entries) {
                    if (ntfs::recordOf(e.recordReference) != ntfs::mftRecord) {
                        records.insert(ntfs::recordOf(e.recordReference));
                    }
                }
                return records;
            }

            // Reads the extension records of $MFT in rounds: each round reads those the runs known so far map.
            void readExtensions() {
                std::set<std::uint64_t> missing = listedRecords();
                for (int round = 0; round < bootstrapRounds && !missing.empty(); ++round) {
                    const auto runs = knownRuns(parts_);
                    std::erase_if(missing, [&](std::uint64_t record) { return readIfMapped(runs, record); });
                }
                if (!missing.empty()) {
                    throw std::runtime_error("cannot map $MFT: extension records outside the mapped part");
                }
            }

            // Reads and parses an extension record if the known runs reach it.
            bool readIfMapped(std::span<const ntfs::DataRun> runs, std::uint64_t record) {
                const std::uint64_t size = geometry_.bytesPerRecord;
                const auto pieces = ntfs::mapStreamBytes(runs, geometry_.bytesPerCluster, {record * size, size});
                if (pieces.empty()) {
                    return false;
                }
                std::vector<std::byte> raw(size);
                std::size_t at = 0;
                for (const ntfs::RecordPiece& piece : pieces) {
                    reader_.read(piece.volumeOffset, std::span(raw).subspan(at, piece.bytes));
                    at += piece.bytes;
                }
                parse(raw, record);
                return true;
            }

            // The joined $DATA runs, the record count and the in-use bitmap.
            MftMap map() const {
                const auto data = unnamed(parts_, AttributeType::Data);
                auto runs = ntfs::joinSegments(data);
                if (!runs || data.empty()) {
                    throw std::runtime_error("the run list of $MFT has gaps");
                }
                MftMap out;
                out.runs = std::move(*runs);
                out.recordCount = data.front()->dataSize / geometry_.bytesPerRecord;
                out.bitmap = bitmap();
                return out;
            }

            // $MFT::$BITMAP, resident or not.
            std::vector<std::byte> bitmap() const {
                const auto segments = unnamed(parts_, AttributeType::Bitmap);
                if (segments.empty()) {
                    return {}; // Every record counts as used.
                }
                if (!segments.front()->nonResident) {
                    return segments.front()->value;
                }
                const auto runs = ntfs::joinSegments(segments);
                if (!runs) {
                    throw std::runtime_error("the run list of $MFT::$BITMAP has gaps");
                }
                return readStream(reader_, *runs, geometry_.bytesPerCluster, segments.front()->dataSize);
            }

            const platform::MftReader& reader_; // Raw reads.
            ntfs::MftGeometry geometry_; // Cluster and record size.
            std::uint64_t startLcn_; // Where record 0 is.
            std::vector<ntfs::ParsedRecord> parts_; // Record 0 and its extension records.
        };

        // One scan: map, plan, stream, parse, merge.
        class MftScan {
        public:
            MftScan(const std::wstring& volumeName, core::VolumeGeometry geometry, const core::NtfsLayout& layout,
                    core::Executor& executor, const core::CancelToken& token, core::ProgressSink* progress,
                    const MftScanOptions& options) :
                reader_(volumeName), geometry_(geometry), layout_(layout), executor_(executor), token_(token),
                progress_(progress), options_(options),
                mftGeometry_{geometry.bytesPerCluster, layout.bytesPerFileRecord} {}

            // Reads and merges.
            MftScanResult run(std::vector<core::ClusterRun> freeRuns) {
                if (mftGeometry_.bytesPerRecord == 0 || mftGeometry_.bytesPerRecord % ntfs::fixupStride != 0) {
                    throw std::runtime_error("unexpected MFT record size");
                }
                report("Reading MFT", 0, 0);
                map_ = Bootstrap(reader_, mftGeometry_, layout_.mftStartLcn).run();
                plan_ =
                    ntfs::planMftReads(map_.runs, map_.recordCount, mftGeometry_, map_.bitmap,
                                       {.maxChunkBytes = options_.bufferBytes, .minSkipBytes = options_.minSkipBytes});
                readAll();
                report("Building snapshot", 0, 0);
                const mft::RowIndex rows(std::move(chunks_));
                mft::MergeOutput merged = mft::mergeRows(rows, {}, geometry_, std::move(freeRuns), executor_, token_);
                merged.stats.mft = figures(rows);
                return {std::move(merged.snapshot), std::move(merged.stats)};
            }

        private:
            // Streams every chunk to parser tasks, then reads the split records.
            void readAll() {
                std::vector<platform::ReadRequest> requests;
                requests.reserve(plan_.chunks.size());
                for (const ntfs::MftChunk& chunk : plan_.chunks) {
                    requests.push_back({chunk.volumeOffset, chunk.bytes});
                }
                const std::size_t workers = std::max<std::size_t>(1, executor_.concurrency());
                // Buffers: the reads in flight plus a few parsing; more parsers than that would only hold memory.
                platform::BufferPool pool({.count = options_.inFlight + std::min<std::size_t>(workers, 8) + 1,
                                           .bytes = options_.bufferBytes});
                chunks_.assign(plan_.chunks.size() + plan_.split.size(), nullptr);
                {
                    TaskGroup tasks(executor_);
                    std::uint64_t done = 0;
                    read_ = reader_.stream(
                        requests, pool, options_.inFlight,
                        [&](std::size_t i, platform::PooledBuffer buffer) {
                            done += plan_.chunks[i].bytes;
                            report("Reading MFT", done, plan_.bytes);
                            tasks.run([this, i, buffer = std::move(buffer)]() mutable { parse(i, buffer); });
                        },
                        token_);
                    tasks.wait();
                }
                readSplit();
            }

            // Parses one chunk's buffer into its rows and frees the buffer.
            void parse(std::size_t i, platform::PooledBuffer& buffer) {
                const ntfs::MftChunk& chunk = plan_.chunks[i];
                auto rows = std::make_shared<mft::ChunkRows>();
                const std::span<std::byte> records = buffer.bytes().subspan(chunk.recordOffset);
                mft::parseChunk(records, {chunk.firstRecord, chunk.recordCount, mftGeometry_.bytesPerRecord},
                                map_.bitmap, *rows);
                buffer.release();
                chunks_[i] = std::move(rows);
            }

            // Records cut by $MFT run boundaries, read piece by piece.
            void readSplit() {
                for (std::size_t k = 0; k < plan_.split.size(); ++k) {
                    std::vector<std::byte> raw(mftGeometry_.bytesPerRecord);
                    std::size_t at = 0;
                    for (const ntfs::RecordPiece& piece : plan_.split[k].pieces) {
                        reader_.read(piece.volumeOffset, std::span(raw).subspan(at, piece.bytes));
                        at += piece.bytes;
                    }
                    auto rows = std::make_shared<mft::ChunkRows>();
                    mft::parseChunk(raw, {plan_.split[k].record, 1, mftGeometry_.bytesPerRecord}, map_.bitmap, *rows);
                    chunks_[plan_.chunks.size() + k] = std::move(rows);
                }
            }

            // What the read cost and found.
            MftReadFigures figures(const mft::RowIndex& rows) const {
                MftReadFigures f;
                f.records = map_.recordCount;
                f.skippedRecords = plan_.skippedRecords;
                f.recordsInUse = rows.size();
                for (std::size_t i = 0; i < rows.size(); ++i) {
                    f.extensionRecords += rows.row(i).baseReference != 0 ? 1 : 0;
                    f.attributeListFiles += (rows.row(i).facts & mft::RowAttributeList) != 0 ? 1 : 0;
                }
                for (const auto& chunk : rows.chunks()) {
                    f.badRecords += chunk->badRecords;
                }
                f.bytesRead = read_.bytes;
                f.reads = read_.reads;
                f.readSeconds = std::chrono::duration<double>(read_.elapsed).count();
                f.readerBusyPercent = read_.busyPercent();
                f.deviceBusyPercent = read_.deviceBusyPercent.value_or(-1.0);
                f.starvedSeconds = std::chrono::duration<double>(read_.starved).count();
                f.workers = static_cast<std::uint32_t>(executor_.concurrency());
                f.bufferBytes = options_.bufferBytes;
                f.inFlight = options_.inFlight;
                return f;
            }

            // Sends a progress report, if anyone listens.
            void report(const char* stage, std::uint64_t done, std::uint64_t total) const {
                if (progress_ != nullptr) {
                    progress_->report({.stage = stage, .done = done, .total = total, .message = {}});
                }
            }

            platform::MftReader reader_; // Raw volume reads.
            core::VolumeGeometry geometry_; // Cluster size and count.
            core::NtfsLayout layout_; // MFT location and record size.
            core::Executor& executor_; // Parser tasks and merge loops.
            const core::CancelToken& token_; // Checked between reads.
            core::ProgressSink* progress_; // May be null.
            MftScanOptions options_; // Reader settings.
            ntfs::MftGeometry mftGeometry_; // Cluster and record size for the planner.
            MftMap map_; // Where $MFT is.
            ntfs::MftReadPlan plan_; // What to read.
            std::vector<std::shared_ptr<const mft::ChunkRows>> chunks_; // Parser output per chunk and split record.
            platform::ReadStats read_; // Cost of the stream.
        };

    } // namespace

    MftScanResult scanFilesMft(HANDLE volume, const std::wstring& volumeName, core::VolumeGeometry geometry,
                               const core::NtfsLayout& ntfs, std::vector<core::ClusterRun> freeRuns,
                               core::Executor& executor, const core::CancelToken& token, core::ProgressSink* progress,
                               const MftScanOptions& options) {
        const auto journal = platform::queryUsnJournal(volume); // Before the read: later changes stay after it.
        MftScan scan(volumeName, geometry, ntfs, executor, token, progress, options);
        MftScanResult result = scan.run(std::move(freeRuns));
        if (journal) {
            result.snapshot.usn = core::UsnCursor{journal->journalId, journal->nextUsn};
        }
        return result;
    }

} // namespace icd::services
