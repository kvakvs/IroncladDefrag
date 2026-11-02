#include "services/execute/VolumeMover.h"

#include "platform/win/RetrievalPointers.h"
#include "platform/win/TextConvert.h"
#include "platform/win/WinError.h"

#include <map>
#include <stdexcept>

namespace icd::services {

    namespace {

        using core::execute::StreamRun;

        // Appends a stream's allocated runs (holes left out).
        void appendRuns(std::uint16_t stream, const std::vector<platform::RetrievedRun>& runs,
                        std::vector<StreamRun>& out) {
            for (const platform::RetrievedRun& r : runs) {
                if (r.lcn >= 0 && r.length != 0) {
                    out.push_back({stream, r.vcn, static_cast<core::Lcn>(r.lcn), r.length});
                }
            }
        }

        // A file opened by id, with its streams opened on first use.
        class VolumeFile final : public MovableFile {
        public:
            // Takes the file handle.
            VolumeFile(HANDLE volume, platform::UniqueHandle file, std::uint64_t reference) :
                volume_(volume), file_(std::move(file)), reference_(reference) {}

            // Main stream (or directory index), then every named stream in ordinal order.
            bool readRuns(std::vector<StreamRun>& runs) override {
                runs.clear();
                platform::FileFacts facts;
                if (!platform::readFileFacts(file_.get(), facts)) {
                    return false;
                }
                const auto named = static_cast<std::uint16_t>(facts.namedStreams.size());
                for (std::uint16_t ordinal = 0; ordinal <= named; ++ordinal) {
                    const HANDLE stream = streamHandle(ordinal);
                    if (stream == INVALID_HANDLE_VALUE || !platform::readRetrievalPointers(stream, retrieved_)) {
                        return false;
                    }
                    appendRuns(ordinal, retrieved_, runs);
                }
                return true;
            }

            // One FSCTL_MOVE_FILE call on the stream.
            platform::MoveCallResult move(const core::execute::MoveCall& call) override {
                const HANDLE stream = streamHandle(call.stream);
                if (stream == INVALID_HANDLE_VALUE) {
                    return {platform::MoveError::FileGone, ERROR_FILE_NOT_FOUND};
                }
                return platform::moveClusters({volume_, stream},
                                              {call.vcn, call.to, static_cast<std::uint32_t>(call.span)});
            }

        private:
            // The handle of a stream, opened once; INVALID_HANDLE_VALUE when it cannot be opened.
            HANDLE streamHandle(std::uint16_t ordinal) {
                if (ordinal == 0) {
                    return file_.get();
                }
                auto found = streams_.find(ordinal);
                if (found == streams_.end()) {
                    found = streams_.emplace(ordinal, platform::openStreamForMove(file_.get(), {reference_, ordinal}))
                                .first;
                }
                return found->second.valid() ? found->second.get() : INVALID_HANDLE_VALUE;
            }

            HANDLE volume_; // Not owned.
            platform::UniqueHandle file_; // The file (main stream).
            std::uint64_t reference_; // Its NTFS file reference.
            std::map<std::uint16_t, platform::UniqueHandle> streams_; // Named streams opened so far.
            std::vector<platform::RetrievedRun> retrieved_; // Reused query buffer.
        };

    } // namespace

    VolumeMover::VolumeMover(const std::wstring& volumeName) : volume_(platform::openVolumeForMoves(volumeName)) {
        if (!volume_) {
            throw std::runtime_error("cannot open the volume for moves: " + platform::toUtf8(platform::lastWinError()));
        }
    }

    std::unique_ptr<MovableFile> VolumeMover::open(std::uint64_t fileReference, std::string& why) {
        platform::UniqueHandle file = platform::openFileById(volume_.get(), fileReference);
        if (!file) {
            const DWORD error = GetLastError();
            const bool gone =
                error == ERROR_FILE_NOT_FOUND || error == ERROR_INVALID_PARAMETER || error == ERROR_PATH_NOT_FOUND;
            why = gone ? "file-gone" : "open-failed";
            return nullptr;
        }
        return std::make_unique<VolumeFile>(volume_.get(), std::move(file), fileReference);
    }

    std::vector<core::ClusterRun> VolumeMover::freeRunsIn(core::ClusterRun range) {
        return platform::freeRunsIn(volume_.get(), range);
    }

} // namespace icd::services
