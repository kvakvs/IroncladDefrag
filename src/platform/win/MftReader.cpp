#include "platform/win/MftReader.h"

#include "platform/win/TextConvert.h"
#include "platform/win/Volumes.h"
#include "platform/win/WinError.h"

#include <windows.h>
#include <winioctl.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <stdexcept>

namespace icd::platform {

    namespace {

        using Clock = std::chrono::steady_clock;

        // Alignment of synchronous reads: a multiple of every sector size (512 and 4096).
        constexpr std::uint64_t readAlignment = 4096;

        // Throws with the Win32 error text of the last call.
        [[noreturn]] void fail(const std::string& what) {
            throw std::runtime_error(what + " failed: " + toUtf8(lastWinError()));
        }

        // Frees VirtualAlloc memory.
        struct VirtualFreer {
            void operator()(std::byte* p) const { VirtualFree(p, 0, MEM_RELEASE); }
        };

        // Page-aligned memory for one synchronous read.
        std::unique_ptr<std::byte, VirtualFreer> allocateAligned(std::size_t bytes) {
            auto* p = static_cast<std::byte*>(VirtualAlloc(nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            if (p == nullptr) {
                throw std::bad_alloc();
            }
            return std::unique_ptr<std::byte, VirtualFreer>(p);
        }

        // Idle and query time of the volume's device counters, in 100 ns ticks.
        struct DeviceTimes {
            std::int64_t idle = 0; // Time the device had nothing to do.
            std::int64_t query = 0; // Timestamp of the query.
        };

        // The counters, or nullopt when the device does not keep them.
        std::optional<DeviceTimes> deviceTimes(HANDLE handle) {
            DISK_PERFORMANCE performance{};
            DWORD bytes = 0;
            if (DeviceIoControl(handle, IOCTL_DISK_PERFORMANCE, nullptr, 0, &performance, sizeof(performance), &bytes,
                                nullptr) == FALSE) {
                return std::nullopt;
            }
            return DeviceTimes{performance.IdleTime.QuadPart, performance.QueryTime.QuadPart};
        }

        // 100 - idle % between two counter readings, or nullopt without both.
        std::optional<double> busyBetween(const std::optional<DeviceTimes>& before,
                                          const std::optional<DeviceTimes>& after) {
            if (!before || !after || after->query <= before->query) {
                return std::nullopt;
            }
            const double idle = static_cast<double>(after->idle - before->idle);
            const double span = static_cast<double>(after->query - before->query);
            return std::clamp(100.0 * (1.0 - idle / span), 0.0, 100.0);
        }

        // One read in flight: its OVERLAPPED, event and buffer.
        struct Slot {
            OVERLAPPED overlapped{}; // Offset and event of the read.
            UniqueHandle event; // Manual-reset event the read signals.
            PooledBuffer buffer; // Where the bytes land.
            std::size_t request = 0; // Which request.
            bool pending = false; // Issued and not yet completed.
        };

        // One stream() call: issues reads in a ring of slots and completes them oldest first.
        class Streamer {
        public:
            Streamer(HANDLE handle, std::span<const ReadRequest> requests, BufferPool& pool, std::size_t inFlight,
                     const MftReader::Sink& sink, const core::CancelToken& token) :
                handle_(handle), requests_(requests), pool_(pool), sink_(sink), token_(token),
                slots_(std::max<std::size_t>(1, inFlight)) {
                for (Slot& slot : slots_) {
                    slot.event = UniqueHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr));
                    if (!slot.event) {
                        fail("CreateEventW");
                    }
                }
            }

            // Waits for reads still in flight after an exception, so their buffers are not freed under the disk.
            ~Streamer() {
                for (Slot& slot : slots_) {
                    if (slot.pending) {
                        CancelIoEx(handle_, &slot.overlapped);
                        DWORD ignored = 0;
                        GetOverlappedResult(handle_, &slot.overlapped, &ignored, TRUE);
                    }
                }
            }

            Streamer(const Streamer&) = delete;
            Streamer& operator=(const Streamer&) = delete;

            // Issues and completes every request.
            ReadStats run() {
                const auto start = Clock::now();
                const auto waitedBefore = pool_.waited();
                const std::size_t ring = slots_.size();
                for (std::size_t i = 0; i < requests_.size(); ++i) {
                    Slot& slot = slots_[i % ring];
                    if (slot.pending) {
                        complete(slot);
                    }
                    token_.throwIfCancelled();
                    issue(slot, i);
                }
                for (std::size_t i = requests_.size(); i < requests_.size() + ring; ++i) {
                    if (Slot& slot = slots_[i % ring]; slot.pending) {
                        complete(slot);
                    }
                }
                stats_.elapsed = Clock::now() - start;
                stats_.starved = pool_.waited() - waitedBefore;
                return stats_;
            }

        private:
            // Starts the read of request i into a fresh buffer.
            void issue(Slot& slot, std::size_t i) {
                const ReadRequest& request = requests_[i];
                slot.buffer = pool_.acquire(token_);
                if (request.bytes > slot.buffer.bytes().size()) {
                    throw std::runtime_error("MFT read larger than the reader's buffers");
                }
                slot.overlapped = OVERLAPPED{};
                slot.overlapped.Offset = static_cast<DWORD>(request.offset & 0xFFFF'FFFFU);
                slot.overlapped.OffsetHigh = static_cast<DWORD>(request.offset >> 32U);
                slot.overlapped.hEvent = slot.event.get();
                slot.request = i;
                if (outstanding_++ == 0) {
                    busySince_ = Clock::now();
                }
                const BOOL ok = ReadFile(handle_, slot.buffer.bytes().data(), request.bytes, nullptr, &slot.overlapped);
                if (ok == FALSE && GetLastError() != ERROR_IO_PENDING) {
                    --outstanding_;
                    fail("reading the MFT");
                }
                slot.pending = true;
            }

            // Waits for a slot's read and hands the buffer to the sink.
            void complete(Slot& slot) {
                DWORD bytes = 0;
                const BOOL ok = GetOverlappedResult(handle_, &slot.overlapped, &bytes, TRUE);
                slot.pending = false;
                if (--outstanding_ == 0) {
                    stats_.busy += Clock::now() - busySince_;
                }
                if (ok == FALSE) {
                    fail("reading the MFT");
                }
                if (bytes != requests_[slot.request].bytes) {
                    throw std::runtime_error("short read of the MFT");
                }
                stats_.bytes += bytes;
                ++stats_.reads;
                sink_(slot.request, std::move(slot.buffer));
            }

            HANDLE handle_; // Overlapped volume handle.
            std::span<const ReadRequest> requests_; // What to read.
            BufferPool& pool_; // Buffers.
            const MftReader::Sink& sink_; // Takes completed reads.
            const core::CancelToken& token_; // Checked between reads.
            std::vector<Slot> slots_; // Ring of in-flight reads.
            std::size_t outstanding_ = 0; // Reads issued and not completed.
            Clock::time_point busySince_; // When outstanding_ last went from 0 to 1.
            ReadStats stats_; // Being filled.
        };

    } // namespace

    PooledBuffer::PooledBuffer(PooledBuffer&& other) noexcept :
        pool_(std::exchange(other.pool_, nullptr)), data_(std::exchange(other.data_, nullptr)),
        size_(std::exchange(other.size_, 0)) {}

    PooledBuffer& PooledBuffer::operator=(PooledBuffer&& other) noexcept {
        if (this != &other) {
            release();
            pool_ = std::exchange(other.pool_, nullptr);
            data_ = std::exchange(other.data_, nullptr);
            size_ = std::exchange(other.size_, 0);
        }
        return *this;
    }

    PooledBuffer::~PooledBuffer() { release(); }

    void PooledBuffer::release() {
        if (pool_ != nullptr) {
            pool_->giveBack(data_);
            pool_ = nullptr;
            data_ = nullptr;
            size_ = 0;
        }
    }

    BufferPool::BufferPool(Shape shape) : bytes_(shape.bytes) {
        for (std::size_t i = 0; i < shape.count; ++i) {
            auto* p = static_cast<std::byte*>(VirtualAlloc(nullptr, bytes_, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            if (p == nullptr) {
                for (std::byte* q : all_) {
                    VirtualFree(q, 0, MEM_RELEASE);
                }
                throw std::bad_alloc();
            }
            all_.push_back(p);
        }
        free_ = all_;
    }

    BufferPool::~BufferPool() {
        for (std::byte* p : all_) {
            VirtualFree(p, 0, MEM_RELEASE);
        }
    }

    PooledBuffer BufferPool::acquire(const core::CancelToken& token) {
        std::unique_lock lock(mutex_);
        const auto start = Clock::now();
        while (free_.empty()) {
            token.throwIfCancelled();
            available_.wait_for(lock, std::chrono::milliseconds(50));
        }
        waited_ += Clock::now() - start;
        std::byte* data = free_.back();
        free_.pop_back();
        return {this, data, bytes_};
    }

    std::chrono::nanoseconds BufferPool::waited() const {
        const std::scoped_lock lock(mutex_);
        return waited_;
    }

    void BufferPool::giveBack(std::byte* data) {
        {
            const std::scoped_lock lock(mutex_);
            free_.push_back(data);
        }
        available_.notify_one();
    }

    MftReader::MftReader(const std::wstring& volumeName) :
        handle_(openVolume(volumeName, GENERIC_READ, FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING)),
        name_(volumeName) {
        if (!handle_) {
            fail("opening " + toUtf8(volumeName) + " for raw reads (administrator rights are needed)");
        }
    }

    void MftReader::read(std::uint64_t offset, std::span<std::byte> out) const {
        const std::uint64_t begin = offset / readAlignment * readAlignment;
        const std::uint64_t end = (offset + out.size() + readAlignment - 1) / readAlignment * readAlignment;
        const auto bytes = static_cast<DWORD>(end - begin);
        const auto buffer = allocateAligned(bytes);
        OVERLAPPED overlapped{};
        overlapped.Offset = static_cast<DWORD>(begin & 0xFFFF'FFFFU);
        overlapped.OffsetHigh = static_cast<DWORD>(begin >> 32U);
        const UniqueHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        overlapped.hEvent = event.get();
        DWORD got = 0;
        if (ReadFile(handle_.get(), buffer.get(), bytes, nullptr, &overlapped) == FALSE &&
            GetLastError() != ERROR_IO_PENDING) {
            fail("reading " + toUtf8(name_));
        }
        if (GetOverlappedResult(handle_.get(), &overlapped, &got, TRUE) == FALSE) {
            fail("reading " + toUtf8(name_));
        }
        if (got < offset - begin + out.size()) {
            throw std::runtime_error("short read of " + toUtf8(name_));
        }
        std::memcpy(out.data(), buffer.get() + (offset - begin), out.size());
    }

    ReadStats MftReader::stream(std::span<const ReadRequest> requests, BufferPool& pool, std::size_t inFlight,
                                const Sink& sink, const core::CancelToken& token) const {
        const auto before = deviceTimes(handle_.get());
        Streamer streamer(handle_.get(), requests, pool, inFlight, sink, token);
        ReadStats stats = streamer.run();
        stats.deviceBusyPercent = busyBetween(before, deviceTimes(handle_.get()));
        return stats;
    }

} // namespace icd::platform
