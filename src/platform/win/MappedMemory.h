#pragma once

#include <cstddef>
#include <filesystem>
#include <memory_resource>
#include <mutex>
#include <unordered_map>

namespace icd::platform {

    // Memory backed by temporary files (plan.md 6.6, last rung of the memory ladder): each large allocation is a file
    // in `directory`, mapped into the address space, so Windows pages it to that file instead of holding it in RAM or
    // the page file, and the commit charge stays low. Files are created FILE_FLAG_DELETE_ON_CLOSE and
    // FILE_ATTRIBUTE_TEMPORARY, so nothing stays behind, not even after a crash. Allocations below `minBytes` go to
    // the heap. Thread-safe.
    class MappedFileResource : public std::pmr::memory_resource {
    public:
        // Files in directory (created when needed); allocations of at least minBytes are mapped.
        explicit MappedFileResource(std::filesystem::path directory, std::size_t minBytes = std::size_t{1} << 20U);
        ~MappedFileResource() override;
        MappedFileResource(const MappedFileResource&) = delete;
        MappedFileResource& operator=(const MappedFileResource&) = delete;

        // Bytes currently held in mapped files.
        [[nodiscard]] std::size_t mappedBytes() const;

        // Mapped allocations made so far.
        [[nodiscard]] std::size_t mappedAllocations() const;

    private:
        // One mapped allocation's handles.
        struct Mapping {
            void* file = nullptr; // HANDLE of the backing file.
            void* section = nullptr; // HANDLE of the file mapping.
            std::size_t bytes = 0; // Size of the view.
        };

        // Maps a new file of at least bytes; throws std::bad_alloc when that fails.
        void* do_allocate(std::size_t bytes, std::size_t alignment) override;

        // Unmaps and deletes the file, or frees heap memory.
        void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override;

        // Only the same object can free what it allocated.
        [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
            return this == &other;
        }

        // Releases one mapping's view and handles.
        static void release(void* view, const Mapping& mapping) noexcept;

        std::filesystem::path directory_; // Where the files go.
        std::size_t minBytes_; // Smaller allocations use the heap.
        mutable std::mutex mutex_; // Guards the fields below.
        std::unordered_map<void*, Mapping> mappings_; // View -> handles.
        std::size_t mappedBytes_ = 0; // Sum of the views.
        std::size_t allocations_ = 0; // Mapped allocations made.
        std::size_t nextName_ = 0; // Counter for unique file names.
    };

} // namespace icd::platform
