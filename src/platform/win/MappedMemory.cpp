#include "platform/win/MappedMemory.h"

#include <windows.h>

#include <new>
#include <string>

namespace icd::platform {

    namespace {

        // Views are mapped in whole allocation-granularity units.
        constexpr std::size_t granularity = std::size_t{64} * 1024;

        // A new temporary file of `bytes`, deleted when its last handle closes; INVALID_HANDLE_VALUE on failure.
        HANDLE createBacking(const std::filesystem::path& path, std::size_t bytes) {
            HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                      FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
            if (file == INVALID_HANDLE_VALUE) {
                return file;
            }
            LARGE_INTEGER size{};
            size.QuadPart = static_cast<LONGLONG>(bytes);
            if (SetFilePointerEx(file, size, nullptr, FILE_BEGIN) == FALSE || SetEndOfFile(file) == FALSE) {
                CloseHandle(file);
                return INVALID_HANDLE_VALUE;
            }
            return file;
        }

    } // namespace

    MappedFileResource::MappedFileResource(std::filesystem::path directory, std::size_t minBytes) :
        directory_(std::move(directory)), minBytes_(minBytes) {}

    MappedFileResource::~MappedFileResource() {
        for (const auto& [view, mapping] : mappings_) {
            release(view, mapping); // Containers outliving the resource would be a bug; never leak files.
        }
    }

    std::size_t MappedFileResource::mappedBytes() const {
        const std::scoped_lock lock(mutex_);
        return mappedBytes_;
    }

    std::size_t MappedFileResource::mappedAllocations() const {
        const std::scoped_lock lock(mutex_);
        return allocations_;
    }

    void* MappedFileResource::do_allocate(std::size_t bytes, std::size_t alignment) {
        if (bytes < minBytes_) {
            return std::pmr::new_delete_resource()->allocate(bytes, alignment);
        }
        const std::size_t size = (bytes + granularity - 1) / granularity * granularity;
        std::filesystem::path path;
        {
            const std::scoped_lock lock(mutex_);
            std::filesystem::create_directories(directory_);
            path = directory_ /
                ("icd-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(nextName_++) + ".map");
        }
        HANDLE file = createBacking(path, size);
        HANDLE section = file == INVALID_HANDLE_VALUE
            ? nullptr
            : CreateFileMappingW(file, nullptr, PAGE_READWRITE, static_cast<DWORD>(std::uint64_t{size} >> 32U),
                                 static_cast<DWORD>(size & 0xFFFF'FFFFU), nullptr);
        void* view = section == nullptr ? nullptr : MapViewOfFile(section, FILE_MAP_ALL_ACCESS, 0, 0, size);
        const Mapping mapping{file, section, size};
        if (view == nullptr) {
            release(nullptr, mapping);
            throw std::bad_alloc();
        }
        const std::scoped_lock lock(mutex_);
        mappings_.emplace(view, mapping);
        mappedBytes_ += size;
        ++allocations_;
        return view;
    }

    void MappedFileResource::do_deallocate(void* p, std::size_t bytes, std::size_t alignment) {
        Mapping mapping;
        {
            const std::scoped_lock lock(mutex_);
            const auto it = mappings_.find(p);
            if (it == mappings_.end()) {
                std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
                return;
            }
            mapping = it->second;
            mappedBytes_ -= mapping.bytes;
            mappings_.erase(it);
        }
        release(p, mapping);
    }

    void MappedFileResource::release(void* view, const Mapping& mapping) noexcept {
        if (view != nullptr) {
            UnmapViewOfFile(view);
        }
        if (mapping.section != nullptr) {
            CloseHandle(mapping.section);
        }
        if (mapping.file != nullptr && mapping.file != INVALID_HANDLE_VALUE) {
            CloseHandle(mapping.file); // The file is deleted with its last handle.
        }
    }

} // namespace icd::platform
