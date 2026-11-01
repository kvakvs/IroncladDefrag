#include "services/snapshot/SnapshotStore.h"

#include "platform/win/Compression.h"
#include "platform/win/LogSinks.h"

#include <cctype>
#include <fstream>
#include <stdexcept>

namespace icd::services {

    namespace {

        // A sink into a binary file stream.
        class FileSink : public core::ByteSink {
        public:
            explicit FileSink(std::ofstream& out) : out_(out) {}

            // Appends bytes; throws when the stream fails.
            void write(std::span<const std::byte> bytes) override {
                out_.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                if (!out_) {
                    throw std::runtime_error("writing the snapshot file failed");
                }
            }

        private:
            std::ofstream& out_; // Open file.
        };

        // A source over a binary file stream.
        class FileSource : public core::ByteSource {
        public:
            explicit FileSource(std::ifstream& in) : in_(in) {}

            // Fills out; a short read means the file is truncated.
            void read(std::span<std::byte> out) override {
                in_.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
                if (static_cast<std::size_t>(in_.gcount()) != out.size()) {
                    throw core::SnapshotFormatError("snapshot file ends early");
                }
            }

        private:
            std::ifstream& in_; // Open file.
        };

        // The codec of saved snapshots.
        const platform::XpressCodec& codec() {
            static const platform::XpressCodec instance;
            return instance;
        }

        // The file name for a volume name: the GUID part, or the name with unsafe characters replaced.
        std::string fileStem(const std::string& volumeName) {
            const auto open = volumeName.find("Volume{");
            const auto close = volumeName.find('}', open == std::string::npos ? 0 : open);
            if (open != std::string::npos && close != std::string::npos) {
                return volumeName.substr(open, close - open + 1);
            }
            std::string stem;
            for (const char c : volumeName) {
                stem += std::isalnum(static_cast<unsigned char>(c)) != 0 ? c : '_';
            }
            return stem.empty() ? "volume" : stem;
        }

    } // namespace

    SnapshotStore::SnapshotStore(std::filesystem::path directory) : directory_(std::move(directory)) {}

    std::filesystem::path SnapshotStore::defaultDirectory() {
        return platform::defaultLogDirectory().parent_path() / L"snapshots";
    }

    std::filesystem::path SnapshotStore::pathFor(const std::string& volumeName) const {
        return directory_ / (fileStem(volumeName) + ".icds");
    }

    void SnapshotStore::save(const core::Snapshot& snapshot, const core::SnapshotMeta& meta,
                             core::Executor& executor) const {
        std::filesystem::create_directories(directory_);
        const std::filesystem::path target = pathFor(meta.volumeName);
        std::filesystem::path temporary = target;
        temporary += ".tmp";
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            if (!out) {
                throw std::runtime_error("cannot create " + temporary.string());
            }
            FileSink sink(out);
            core::writeSnapshotFile(snapshot, meta, sink, codec(), executor);
            out.flush();
            if (!out) {
                throw std::runtime_error("writing the snapshot file failed");
            }
        }
        std::filesystem::rename(temporary, target); // Replaces the old file in one step.
    }

    std::optional<core::SnapshotMeta> SnapshotStore::peek(const std::string& volumeName) const {
        std::ifstream in(pathFor(volumeName), std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        try {
            FileSource source(in);
            return core::readSnapshotMeta(source);
        }
        catch (const core::SnapshotFormatError&) {
            return std::nullopt;
        }
    }

    std::optional<core::LoadedSnapshot> SnapshotStore::load(const std::string& volumeName, core::Executor& executor,
                                                            std::string* why) const {
        std::ifstream in(pathFor(volumeName), std::ios::binary);
        if (!in) {
            if (why != nullptr) {
                *why = "no saved snapshot";
            }
            return std::nullopt;
        }
        try {
            FileSource source(in);
            return core::readSnapshotFile(source, codec(), executor);
        }
        catch (const core::SnapshotFormatError& e) {
            if (why != nullptr) {
                *why = e.what();
            }
            return std::nullopt;
        }
    }

    void SnapshotStore::remove(const std::string& volumeName) const {
        std::error_code ignored;
        std::filesystem::remove(pathFor(volumeName), ignored);
    }

} // namespace icd::services
