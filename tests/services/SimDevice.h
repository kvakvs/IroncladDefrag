#pragma once

// A move device over a SimVolume for executor tests (T102, T103): FSCTL_MOVE_FILE semantics (a call moves the
// virtual clusters of one stream to free clusters, atomically, or fails), plus what tests inject: files that are
// locked, a hook before every call (to take a destination like a stale bitmap would, or to pause and cancel at an
// exact point), and a crash after a number of calls (an exception, as if the process died).

#include "SimVolume.h"
#include "services/execute/MoveDevice.h"

#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>

namespace icd::testing {

    // Thrown by SimDevice when the simulated process dies.
    class SimulatedCrash : public std::runtime_error {
    public:
        SimulatedCrash() : std::runtime_error("simulated crash") {}
    };

    // See the file comment.
    class SimDevice final : public services::MoveDevice {
    public:
        // Moves on `volume`, which must outlive the device.
        explicit SimDevice(SimVolume& volume) : volume_(volume) {}

        // See MoveDevice.
        std::unique_ptr<services::MovableFile> open(std::uint64_t reference, std::string& why) override {
            const std::scoped_lock lock(mutex_);
            if (!volume_.has(reference)) {
                why = "file-gone";
                return nullptr;
            }
            return std::make_unique<File>(*this, reference);
        }

        // See MoveDevice.
        std::vector<ClusterRun> freeRunsIn(ClusterRun range) override {
            const std::scoped_lock lock(mutex_);
            std::vector<ClusterRun> out;
            for (const ClusterRun& r : volume_.freeRuns()) {
                const Lcn start = std::max(r.start, range.start);
                const Lcn end = std::min(r.end(), range.end());
                if (start < end) {
                    out.push_back({start, end - start});
                }
            }
            return out;
        }

        // Files whose moves fail as locked.
        std::set<std::uint64_t> locked;

        // Called before every move call (outside the device lock), with the file.
        std::function<void(std::uint64_t reference, const core::execute::MoveCall&)> beforeCall;

        // After this many successful calls the next one throws SimulatedCrash (0: never).
        std::uint64_t crashAfterCalls = 0;

        // Like NTFS, move a call in pieces of this many clusters and keep the pieces done before a refusal (0: whole
        // calls, all or nothing).
        std::uint64_t pieceClusters = 0;

        // Successful calls so far.
        std::uint64_t calls = 0;

    private:
        // A file of the simulated volume.
        class File final : public services::MovableFile {
        public:
            // A handle on one file.
            File(SimDevice& device, std::uint64_t reference) : device_(device), reference_(reference) {}

            // See MovableFile.
            bool readRuns(std::vector<StreamRun>& runs) override {
                const std::scoped_lock lock(device_.mutex_);
                const auto now = device_.volume_.runsOf(reference_);
                runs = now.value_or(std::vector<StreamRun>{});
                return now.has_value();
            }

            // See MovableFile.
            platform::MoveCallResult move(const core::execute::MoveCall& call) override {
                if (device_.beforeCall) {
                    device_.beforeCall(reference_, call);
                }
                const std::scoped_lock lock(device_.mutex_);
                if (device_.crashAfterCalls != 0 && device_.calls >= device_.crashAfterCalls) {
                    throw SimulatedCrash();
                }
                if (device_.locked.contains(reference_)) {
                    return {platform::MoveError::Locked, ERROR_SHARING_VIOLATION};
                }
                if (!device_.volume_.has(reference_)) {
                    return {platform::MoveError::FileGone, ERROR_FILE_NOT_FOUND};
                }
                if (!movePieces(call)) {
                    return {platform::MoveError::AccessDenied, ERROR_ACCESS_DENIED}; // As NTFS reports a taken target.
                }
                ++device_.calls;
                return {};
            }

        private:
            // Moves a call whole, or piece by piece when the device says so; false at the first piece refused (the
            // pieces before it stay moved, as on NTFS).
            bool movePieces(const core::execute::MoveCall& call) {
                const std::uint64_t piece = device_.pieceClusters == 0 ? call.allocated : device_.pieceClusters;
                for (std::uint64_t done = 0; done < call.allocated; done += piece) {
                    const std::uint64_t n = std::min(piece, call.allocated - done);
                    if (!device_.volume_.move(reference_, call.stream, call.vcn + done, call.to + done, n)) {
                        return false;
                    }
                }
                return true;
            }

            SimDevice& device_; // Owner.
            std::uint64_t reference_; // The file.
        };

        SimVolume& volume_; // Moved on.
        std::mutex mutex_; // open() runs on the prefetch helper.
    };

} // namespace icd::testing
