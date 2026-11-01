#pragma once

#include "core/job/CancelToken.h"
#include "platform/win/Handle.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace icd::platform {

    class BufferPool;

    // A buffer borrowed from a BufferPool; it goes back to the pool when destroyed, from whichever thread. Move-only,
    // so a buffer is owned by exactly one place: the reader, the queue, or the worker parsing it.
    class PooledBuffer {
    public:
        PooledBuffer() = default;
        PooledBuffer(const PooledBuffer&) = delete;
        PooledBuffer& operator=(const PooledBuffer&) = delete;
        PooledBuffer(PooledBuffer&& other) noexcept;
        PooledBuffer& operator=(PooledBuffer&& other) noexcept;
        ~PooledBuffer();

        // The whole buffer (page-aligned, so sector-aligned for unbuffered reads).
        [[nodiscard]] std::span<std::byte> bytes() const { return {data_, size_}; }

        // Returns the buffer to its pool now.
        void release();

    private:
        friend class BufferPool; // The only creator.

        // Wraps one of the pool's buffers.
        PooledBuffer(BufferPool* pool, std::byte* data, std::size_t size) : pool_(pool), data_(data), size_(size) {}

        BufferPool* pool_ = nullptr; // Owner; null when empty.
        std::byte* data_ = nullptr; // Buffer memory.
        std::size_t size_ = 0; // Bytes.
    };

    // A fixed set of equal, page-aligned buffers (VirtualAlloc). acquire() blocks while all are out, which is what
    // bounds the reader-to-worker queue (plan.md 6.2 principle 1): the reader stops issuing reads while the parser
    // workers are behind. Must outlive every PooledBuffer taken from it.
    class BufferPool {
    public:
        // How many buffers of what size.
        struct Shape {
            std::size_t count = 0; // Buffers.
            std::size_t bytes = 0; // Bytes each.
        };

        // The buffers; throws std::bad_alloc when the memory cannot be committed.
        explicit BufferPool(Shape shape);
        ~BufferPool();
        BufferPool(const BufferPool&) = delete;
        BufferPool& operator=(const BufferPool&) = delete;

        // Waits for a free buffer; throws OperationCancelled when token is cancelled while waiting.
        [[nodiscard]] PooledBuffer acquire(const core::CancelToken& token);

        // Bytes per buffer.
        [[nodiscard]] std::size_t bufferBytes() const { return bytes_; }

        // Total time acquire() spent waiting: when large, the workers (CPU) and not the disk limit the scan.
        [[nodiscard]] std::chrono::nanoseconds waited() const;

    private:
        friend class PooledBuffer; // Gives buffers back.

        // Puts a buffer back and wakes a waiter.
        void giveBack(std::byte* data);

        std::size_t bytes_; // Bytes per buffer.
        std::vector<std::byte*> all_; // Every buffer, for freeing.
        std::vector<std::byte*> free_; // Buffers not handed out.
        mutable std::mutex mutex_; // Guards free_ and waited_.
        std::condition_variable available_; // Signalled when a buffer comes back.
        std::chrono::nanoseconds waited_{0}; // Time spent in acquire() waiting.
    };

    // One read of a stream of reads.
    struct ReadRequest {
        std::uint64_t offset = 0; // Byte offset on the volume (sector-aligned).
        std::uint32_t bytes = 0; // Bytes (whole sectors, at most the pool's buffer size).
    };

    // What a stream of reads cost.
    struct ReadStats {
        std::uint64_t bytes = 0; // Bytes read.
        std::uint64_t reads = 0; // Requests completed.
        std::chrono::nanoseconds elapsed{0}; // Wall time of the stream.
        std::chrono::nanoseconds busy{0}; // Time with at least one read outstanding: how busy the disk was kept.
        std::chrono::nanoseconds starved{0}; // Time waiting for a free buffer (workers behind).
        // Busy share of the device from its own counters (IOCTL_DISK_PERFORMANCE, 100 - idle %), which also sees
        // reads that finished while the reader waited for a buffer; empty when the counters are unavailable.
        std::optional<double> deviceBusyPercent;

        // Share of the wall time with a read outstanding as the reader saw it, 0-100 (an upper bound: a read that
        // completes while the reader waits for a buffer counts until the reader looks).
        [[nodiscard]] double busyPercent() const {
            return elapsed.count() == 0
                ? 0.0
                : 100.0 * static_cast<double>(busy.count()) / static_cast<double>(elapsed.count());
        }
    };

    // Raw reads of a volume device for the MFT scanner (plan.md 6.3): one handle opened for overlapped, unbuffered
    // reads, so large sequential requests go straight to the disk with several in flight. Reads only; the volume
    // stays mounted and in use.
    class MftReader {
    public:
        // Opens "\\?\Volume{GUID}\" or "T:\"; throws std::runtime_error without administrator rights.
        explicit MftReader(const std::wstring& volumeName);

        // Reads any byte range synchronously (aligned to 4 KB internally): record 0, $MFT::$BITMAP, attribute
        // lists, single records. Throws std::runtime_error on failure or a short read.
        void read(std::uint64_t offset, std::span<std::byte> out) const;

        // Called on the reader thread with each completed read, in request order; the buffer holds the bytes.
        using Sink = std::function<void(std::size_t request, PooledBuffer buffer)>;

        // Issues the requests in order with up to `inFlight` outstanding, each into a buffer of `pool`, and hands each
        // completed one to `sink`. Returns when every read has completed and been handed over. Throws
        // OperationCancelled between reads when token is cancelled, and std::runtime_error when a read fails; in both
        // cases only after the outstanding reads have finished, so no buffer is freed under the disk.
        ReadStats stream(std::span<const ReadRequest> requests, BufferPool& pool, std::size_t inFlight,
                         const Sink& sink, const core::CancelToken& token) const;

    private:
        UniqueHandle handle_; // Overlapped, unbuffered volume handle.
        std::wstring name_; // For error messages.
    };

} // namespace icd::platform
