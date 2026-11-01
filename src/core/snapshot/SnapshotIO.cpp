#include "core/snapshot/SnapshotIO.h"

#include "core/util/Crc32.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <limits>
#include <type_traits>

namespace icd::core {

    namespace {

        // First bytes of every snapshot file.
        constexpr std::array<char, 8> magic{'I', 'C', 'D', 'S', 'N', 'A', 'P', '\x1A'};

        // Codec id of blocks stored without compression.
        constexpr std::uint32_t storedCodec = 0;

        // Largest meta section accepted (volume names are short).
        constexpr std::uint32_t maxMetaBytes = 64U * 1024U;

        // Counts bytes without keeping them: the payload size is written before the payload.
        class CountingSink : public ByteSink {
        public:
            // Adds the size.
            void write(std::span<const std::byte> bytes) override { count += bytes.size(); }

            std::uint64_t count = 0; // Bytes written.
        };

        // Typed writes into a sink.
        class Out {
        public:
            explicit Out(ByteSink& sink) : sink_(sink) {}

            // One trivially copyable value.
            template <typename T>
            void put(const T& value) {
                static_assert(std::is_trivially_copyable_v<T>);
                sink_.write(std::as_bytes(std::span(&value, 1)));
            }

            // A vector as count and raw elements.
            template <typename Vector>
            void putVector(const Vector& values) {
                static_assert(std::is_trivially_copyable_v<typename Vector::value_type>);
                put(std::uint64_t{values.size()});
                sink_.write(std::as_bytes(std::span(values)));
            }

            // A string as length and bytes.
            template <typename String>
            void putString(const String& text) {
                put(std::uint64_t{text.size()});
                sink_.write(std::as_bytes(std::span(text)));
            }

        private:
            ByteSink& sink_; // Destination.
        };

        // Typed reads from a source, refusing counts larger than what is left.
        class In {
        public:
            In(ByteSource& source, std::uint64_t remaining) : source_(source), remaining_(remaining) {}

            // One trivially copyable value.
            template <typename T>
            T get() {
                static_assert(std::is_trivially_copyable_v<T>);
                T value{};
                take(sizeof(T));
                source_.read(std::as_writable_bytes(std::span(&value, 1)));
                return value;
            }

            // A vector written by putVector.
            template <typename Vector>
            void getVector(Vector& out) {
                using T = typename Vector::value_type;
                const std::uint64_t count = get<std::uint64_t>();
                if (count > remaining_ / sizeof(T)) {
                    throw SnapshotFormatError("snapshot file: an array is longer than the file");
                }
                out.resize(count);
                take(count * sizeof(T));
                source_.read(std::as_writable_bytes(std::span(out)));
            }

            // A string written by putString.
            std::string getString() {
                const std::uint64_t count = get<std::uint64_t>();
                if (count > remaining_) {
                    throw SnapshotFormatError("snapshot file: a string is longer than the file");
                }
                std::string out(count, '\0');
                take(count);
                source_.read(std::as_writable_bytes(std::span(out)));
                return out;
            }

        private:
            // Accounts for bytes about to be read.
            void take(std::uint64_t bytes) {
                if (bytes > remaining_) {
                    throw SnapshotFormatError("snapshot file ends early");
                }
                remaining_ -= bytes;
            }

            ByteSource& source_; // Where bytes come from.
            std::uint64_t remaining_; // Bytes the section may still hold.
        };

        // Header of one block.
        struct BlockHeader {
            std::uint32_t rawBytes = 0; // Bytes after decoding; 0 ends the payload.
            std::uint32_t storedBytes = 0; // Bytes in the file.
            std::uint32_t codec = 0; // How they are stored.
            std::uint32_t crc = 0; // CRC-32 of the raw bytes.
        };

        // Encodes one block: compressed when that is smaller, else stored.
        std::pair<BlockHeader, std::vector<std::byte>> encode(std::span<const std::byte> raw, const BlockCodec& codec) {
            BlockHeader header{static_cast<std::uint32_t>(raw.size()), 0, codec.id(), Crc32::of(raw)};
            std::vector<std::byte> stored = codec.compress(raw);
            if (stored.size() >= raw.size()) {
                stored.assign(raw.begin(), raw.end());
                header.codec = storedCodec;
            }
            header.storedBytes = static_cast<std::uint32_t>(stored.size());
            return {header, std::move(stored)};
        }

        // Cuts the payload into blocks and writes them in parallel batches.
        class BlockSink : public ByteSink {
        public:
            BlockSink(ByteSink& out, const BlockCodec& codec, Executor& executor, const SnapshotFileOptions& options) :
                out_(out), codec_(codec), executor_(executor), options_(options) {}

            // Buffers bytes, encoding full batches.
            void write(std::span<const std::byte> bytes) override {
                while (!bytes.empty()) {
                    const std::size_t room = batchBytes() - pending_.size();
                    const std::size_t take = std::min(room, bytes.size());
                    pending_.insert(pending_.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(take));
                    bytes = bytes.subspan(take);
                    if (pending_.size() == batchBytes()) {
                        flush();
                    }
                }
            }

            // Writes what is left and the end marker.
            void finish() {
                flush();
                Out(out_).put(BlockHeader{});
            }

        private:
            // Raw bytes of one batch.
            [[nodiscard]] std::size_t batchBytes() const { return options_.blockBytes * options_.batchBlocks; }

            // Encodes the pending bytes block by block in parallel and writes them in order.
            void flush() {
                const ChunkedRange blocks{pending_.size(), options_.blockBytes};
                std::vector<std::pair<BlockHeader, std::vector<std::byte>>> encoded(blocks.chunks());
                executor_.parallelFor({blocks.chunks(), 1}, [&](std::size_t begin, std::size_t end) {
                    for (std::size_t b = begin; b < end; ++b) {
                        const auto raw = std::span(pending_).subspan(blocks.chunkBegin(b),
                                                                     blocks.chunkEnd(b) - blocks.chunkBegin(b));
                        encoded[b] = encode(raw, codec_);
                    }
                });
                Out out(out_);
                for (const auto& [header, stored] : encoded) {
                    out.put(header);
                    out_.write(stored);
                }
                pending_.clear();
            }

            ByteSink& out_; // The file.
            const BlockCodec& codec_; // Compressor.
            Executor& executor_; // Parallel encoding.
            SnapshotFileOptions options_; // Block sizes.
            std::vector<std::byte> pending_; // Raw bytes of the current batch.
        };

        // Reads blocks in batches, decodes them in parallel and serves their bytes.
        class BlockSource : public ByteSource {
        public:
            BlockSource(ByteSource& in, const BlockCodec& codec, Executor& executor,
                        const SnapshotFileOptions& options) :
                in_(in), codec_(codec), executor_(executor), options_(options) {}

            // Copies decoded bytes, decoding the next batch when needed.
            void read(std::span<std::byte> out) override {
                while (!out.empty()) {
                    if (position_ == raw_.size()) {
                        fill();
                    }
                    const std::size_t take = std::min(out.size(), raw_.size() - position_);
                    std::memcpy(out.data(), raw_.data() + position_, take);
                    position_ += take;
                    out = out.subspan(take);
                }
            }

            // After the payload: the end marker must follow.
            void expectEnd() {
                if (position_ != raw_.size() || !readEnd()) {
                    throw SnapshotFormatError("snapshot file: data after the end of the snapshot");
                }
            }

        private:
            // Reads the end marker when it was not part of the last batch.
            bool readEnd() {
                if (ended_) {
                    return true;
                }
                BlockHeader header{};
                in_.read(std::as_writable_bytes(std::span(&header, 1)));
                return header.rawBytes == 0;
            }

            // Reads up to a batch of stored blocks and decodes them.
            void fill() {
                std::vector<std::pair<BlockHeader, std::vector<std::byte>>> stored;
                while (!ended_ && stored.size() < options_.batchBlocks) {
                    BlockHeader header{};
                    in_.read(std::as_writable_bytes(std::span(&header, 1)));
                    if (header.rawBytes == 0) {
                        ended_ = true;
                        break;
                    }
                    if (header.rawBytes > options_.blockBytes * 4 || header.storedBytes > header.rawBytes * 2 + 4096) {
                        throw SnapshotFormatError("snapshot file: a block header is damaged");
                    }
                    std::vector<std::byte> bytes(header.storedBytes);
                    in_.read(bytes);
                    stored.emplace_back(header, std::move(bytes));
                }
                if (stored.empty()) {
                    throw SnapshotFormatError("snapshot file ends early");
                }
                decode(stored);
            }

            // Decodes and checks the blocks in parallel, then joins them.
            void decode(const std::vector<std::pair<BlockHeader, std::vector<std::byte>>>& stored) {
                std::vector<std::vector<std::byte>> raw(stored.size());
                executor_.parallelFor({stored.size(), 1}, [&](std::size_t begin, std::size_t end) {
                    for (std::size_t b = begin; b < end; ++b) {
                        raw[b] = decodeOne(stored[b].first, stored[b].second);
                    }
                });
                raw_.clear();
                for (const auto& block : raw) {
                    raw_.insert(raw_.end(), block.begin(), block.end());
                }
                position_ = 0;
            }

            // One block, checked against its checksum.
            [[nodiscard]] std::vector<std::byte> decodeOne(const BlockHeader& header,
                                                           const std::vector<std::byte>& stored) const {
                std::vector<std::byte> raw;
                if (header.codec == storedCodec) {
                    raw = stored;
                }
                else if (header.codec == codec_.id()) {
                    raw = codec_.decompress(stored, header.rawBytes);
                }
                else {
                    throw SnapshotFormatError("snapshot file: unknown block compression");
                }
                if (raw.size() != header.rawBytes || Crc32::of(raw) != header.crc) {
                    throw SnapshotFormatError("snapshot file: a block is damaged (checksum mismatch)");
                }
                return raw;
            }

            ByteSource& in_; // The file.
            const BlockCodec& codec_; // Decompressor.
            Executor& executor_; // Parallel decoding.
            SnapshotFileOptions options_; // Block sizes.
            std::vector<std::byte> raw_; // Decoded bytes of the current batch.
            std::size_t position_ = 0; // Next byte in raw_.
            bool ended_ = false; // The end marker was read.
        };

    } // namespace

    // Reads and writes the private parts of the snapshot classes; a friend of each.
    struct SnapshotAccess {
        // The whole payload.
        static void write(const Snapshot& s, Out& out) {
            out.put(s.geometry.bytesPerCluster);
            out.put(s.geometry.totalClusters);
            out.put(static_cast<std::uint8_t>(s.usn ? 1 : 0));
            out.put(s.usn ? s.usn->journalId : 0);
            out.put(s.usn ? s.usn->nextUsn : 0);
            out.putVector(s.names.offsets_);
            out.putString(s.names.text_);
            writeFiles(s.files, out);
            out.putVector(s.extents.records_);
            out.putVector(s.extents.fileStart_);
            out.putVector(s.extents.byFile_);
            out.putVector(s.freeSpace.runs_);
        }

        // The file columns.
        static void writeFiles(const FileTable& f, Out& out) {
            out.putVector(f.names_);
            out.putVector(f.parents_);
            out.putVector(f.sizes_);
            out.putVector(f.clusters_);
            out.putVector(f.flags_);
            out.putVector(f.times_);
            out.putVector(f.references_);
        }

        // The whole payload, checked.
        static Snapshot read(In& in) {
            Snapshot s;
            s.geometry.bytesPerCluster = in.get<std::uint32_t>();
            s.geometry.totalClusters = in.get<ClusterCount>();
            const bool hasUsn = in.get<std::uint8_t>() != 0;
            const UsnCursor usn{in.get<std::uint64_t>(), in.get<std::int64_t>()};
            s.usn = hasUsn ? std::optional(usn) : std::nullopt;
            in.getVector(s.names.offsets_);
            const std::string text = in.getString();
            s.names.text_.assign(text.begin(), text.end());
            readFiles(s.files, in);
            in.getVector(s.extents.records_);
            in.getVector(s.extents.fileStart_);
            in.getVector(s.extents.byFile_);
            in.getVector(s.freeSpace.runs_);
            check(s);
            return s;
        }

        // The file columns.
        static void readFiles(FileTable& f, In& in) {
            in.getVector(f.names_);
            in.getVector(f.parents_);
            in.getVector(f.sizes_);
            in.getVector(f.clusters_);
            in.getVector(f.flags_);
            in.getVector(f.times_);
            in.getVector(f.references_);
        }

        // Every consistency rule the builder guarantees; rebuilds the derived parts.
        static void check(Snapshot& s) {
            checkNames(s.names);
            checkFiles(s.files, s.names.size());
            checkExtents(s);
            checkFree(s);
            try {
                s.tree = DirectoryTree(s.files);
            }
            catch (const SnapshotError& e) {
                throw SnapshotFormatError(std::string("snapshot file: ") + e.what());
            }
        }

        // Offsets start at 0, never decrease and end at the text size.
        static void checkNames(const NamePool& names) {
            const auto& o = names.offsets_;
            if (o.empty() || o.front() != 0 || o.back() != names.text_.size() || !std::ranges::is_sorted(o)) {
                throw SnapshotFormatError("snapshot file: the name table is damaged");
            }
        }

        // Equal column sizes, valid name ids and parents.
        static void checkFiles(const FileTable& f, std::size_t names) {
            const std::size_t n = f.names_.size();
            const bool sizes = f.parents_.size() == n && f.sizes_.size() == n && f.clusters_.size() == n &&
                f.flags_.size() == n && f.times_.size() == n && f.references_.size() == n;
            const bool ids = std::ranges::all_of(f.names_, [&](NameId id) { return id < names; });
            const bool parents = std::ranges::all_of(f.parents_, [&](FileIndex p) { return p == noFile || p < n; });
            if (!sizes || !ids || !parents) {
                throw SnapshotFormatError("snapshot file: the file table is damaged");
            }
        }

        // Records sorted, disjoint, inside the volume and owned by real files; the per-file index consistent.
        static void checkExtents(const Snapshot& s) {
            const auto& records = s.extents.records_;
            Lcn previousEnd = 0;
            std::vector<ClusterCount> clusters(s.files.size(), 0);
            for (const ExtentRecord& r : records) {
                if (r.length() == 0 || r.lcn() < previousEnd || r.end() > s.geometry.totalClusters ||
                    r.file() >= s.files.size()) {
                    throw SnapshotFormatError("snapshot file: the extent index is damaged");
                }
                previousEnd = r.end();
                clusters[r.file()] += r.length();
            }
            if (clusters != s.files.clusters_) {
                throw SnapshotFormatError("snapshot file: cluster counts do not match the extents");
            }
            checkByFile(s.extents, s.files.size());
        }

        // fileStart_ brackets each file's positions, which point at its own records.
        static void checkByFile(const ExtentIndex& x, std::size_t files) {
            const auto& start = x.fileStart_;
            if (start.size() != files + 1 || start.front() != 0 || start.back() != x.records_.size() ||
                x.byFile_.size() != x.records_.size() || !std::ranges::is_sorted(start)) {
                throw SnapshotFormatError("snapshot file: the per-file extent index is damaged");
            }
            for (std::size_t f = 0; f < files; ++f) {
                for (std::uint32_t k = start[f]; k < start[f + 1]; ++k) {
                    if (x.byFile_[k] >= x.records_.size() || x.records_[x.byFile_[k]].file() != f) {
                        throw SnapshotFormatError("snapshot file: the per-file extent index is damaged");
                    }
                }
            }
        }

        // Free runs sorted, disjoint, inside the volume; their total recomputed.
        static void checkFree(Snapshot& s) {
            Lcn previousEnd = 0;
            ClusterCount total = 0;
            for (const ClusterRun& run : s.freeSpace.runs_) {
                if (run.length == 0 || run.start < previousEnd || run.end() > s.geometry.totalClusters) {
                    throw SnapshotFormatError("snapshot file: the free space list is damaged");
                }
                previousEnd = run.end();
                total += run.length;
            }
            s.freeSpace.freeClusters_ = total;
        }
    };

    void MemorySource::read(std::span<std::byte> out) {
        if (out.size() > data_.size() - position_) {
            throw SnapshotFormatError("snapshot file ends early");
        }
        std::memcpy(out.data(), data_.data() + position_, out.size());
        position_ += out.size();
    }

    namespace {

        // The meta section as bytes.
        std::vector<std::byte> metaBytes(const SnapshotMeta& meta) {
            MemorySink sink;
            Out out(sink);
            out.putString(meta.volumeName);
            out.put(meta.volumeSerial);
            out.put(meta.scanTime);
            out.put(meta.aspect.width);
            out.put(meta.aspect.height);
            out.put(meta.scanner);
            out.put(meta.files);
            out.put(meta.extents);
            return sink.bytes;
        }

        // The meta section back.
        SnapshotMeta parseMeta(std::span<const std::byte> bytes) {
            MemorySource source(bytes);
            In in(source, bytes.size());
            SnapshotMeta meta;
            meta.volumeName = in.getString();
            meta.volumeSerial = in.get<std::uint64_t>();
            meta.scanTime = in.get<std::int64_t>();
            meta.aspect.width = in.get<double>();
            meta.aspect.height = in.get<double>();
            meta.scanner = in.get<std::uint8_t>();
            meta.files = in.get<std::uint64_t>();
            meta.extents = in.get<std::uint64_t>();
            return meta;
        }

        // Checksum of the meta section and the payload size, so damage to either is caught.
        std::uint32_t headerCrc(std::span<const std::byte> meta, std::uint64_t payloadBytes) {
            Crc32 crc;
            crc.update(meta);
            crc.update(std::as_bytes(std::span(&payloadBytes, 1)));
            return crc.value();
        }

        // Header and meta; returns the meta and the payload size.
        std::pair<SnapshotMeta, std::uint64_t> readHeader(ByteSource& in) {
            std::array<char, 8> head{};
            in.read(std::as_writable_bytes(std::span(head)));
            if (head != magic) {
                throw SnapshotFormatError("not a snapshot file");
            }
            In fixed(in, 4 + 4);
            const auto version = fixed.get<std::uint32_t>();
            if (version != snapshotFormatVersion) {
                throw SnapshotFormatError(
                    std::format("snapshot file version {} (expected {})", version, snapshotFormatVersion));
            }
            const auto size = fixed.get<std::uint32_t>();
            if (size > maxMetaBytes) {
                throw SnapshotFormatError("snapshot file: the header is damaged");
            }
            std::vector<std::byte> bytes(size);
            in.read(bytes);
            In tail(in, 8 + 4);
            const auto payload = tail.get<std::uint64_t>();
            if (tail.get<std::uint32_t>() != headerCrc(bytes, payload)) {
                throw SnapshotFormatError("snapshot file: the header is damaged (checksum mismatch)");
            }
            return {parseMeta(bytes), payload};
        }

    } // namespace

    void writeSnapshotFile(const Snapshot& snapshot, const SnapshotMeta& meta, ByteSink& out, const BlockCodec& codec,
                           Executor& executor, const SnapshotFileOptions& options) {
        CountingSink counter;
        Out counting(counter);
        SnapshotAccess::write(snapshot, counting);
        const auto metaData = metaBytes(meta);
        out.write(std::as_bytes(std::span(magic)));
        Out header(out);
        header.put(snapshotFormatVersion);
        header.put(static_cast<std::uint32_t>(metaData.size()));
        out.write(metaData);
        header.put(counter.count);
        header.put(headerCrc(metaData, counter.count));
        BlockSink blocks(out, codec, executor, options);
        Out payload(blocks);
        SnapshotAccess::write(snapshot, payload);
        blocks.finish();
    }

    SnapshotMeta readSnapshotMeta(ByteSource& in) { return readHeader(in).first; }

    LoadedSnapshot readSnapshotFile(ByteSource& in, const BlockCodec& codec, Executor& executor,
                                    const SnapshotFileOptions& options) {
        auto [meta, payloadBytes] = readHeader(in);
        BlockSource blocks(in, codec, executor, options);
        In payload(blocks, payloadBytes);
        LoadedSnapshot loaded{std::move(meta), SnapshotAccess::read(payload)};
        blocks.expectEnd();
        return loaded;
    }

} // namespace icd::core
