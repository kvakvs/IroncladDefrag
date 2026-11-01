#pragma once

#include <cstddef>
#include <memory>
#include <memory_resource>
#include <string>
#include <type_traits>
#include <vector>

namespace icd::core {

    // Allocator of the big snapshot arrays (extent index, name pool): normally the heap, in the last-resort memory mode
    // memory-mapped files (plan.md 6.6). Unlike std::pmr::polymorphic_allocator it moves with its container, so a
    // snapshot built into mapped files stays there when it is moved or move-assigned; copies go to the heap. It shares
    // ownership of its storage, so every container using it, moved-from ones included (debug builds keep a small
    // proxy in those), keeps the storage alive.
    template <typename T>
    class LargeArrayAllocator {
    public:
        using value_type = T; // Allocator requirement.
        using propagate_on_container_move_assignment = std::true_type; // Moving keeps the storage.
        using propagate_on_container_swap = std::true_type; // Swapping too.
        using propagate_on_container_copy_assignment = std::false_type; // Copies keep their own storage.

        // Heap storage.
        LargeArrayAllocator() noexcept = default;

        // Storage from a memory resource; null means the heap.
        explicit LargeArrayAllocator(std::shared_ptr<std::pmr::memory_resource> resource) noexcept :
            resource_(resource ? std::move(resource) : heap()) {}

        // Copies share the storage. There are deliberately no move operations: containers move their allocators and
        // still use the moved-from one, so moving must leave it pointing at the same storage.
        LargeArrayAllocator(const LargeArrayAllocator&) noexcept = default;
        LargeArrayAllocator& operator=(const LargeArrayAllocator&) noexcept = default;
        ~LargeArrayAllocator() = default;

        // The same storage for another element type.
        template <typename U>
        LargeArrayAllocator(const LargeArrayAllocator<U>& other) noexcept : resource_(other.shared()) {} // NOLINT

        // Allocates n elements.
        [[nodiscard]] T* allocate(std::size_t n) {
            return static_cast<T*>(resource_->allocate(n * sizeof(T), alignof(T)));
        }

        // Frees n elements.
        void deallocate(T* p, std::size_t n) noexcept { resource_->deallocate(p, n * sizeof(T), alignof(T)); }

        // A copy of a container goes to the heap: only the snapshot being built is placed in mapped files.
        [[nodiscard]] LargeArrayAllocator select_on_container_copy_construction() const { return {}; }

        // The storage.
        [[nodiscard]] std::pmr::memory_resource* resource() const noexcept { return resource_.get(); }

        // The storage, shared.
        [[nodiscard]] const std::shared_ptr<std::pmr::memory_resource>& shared() const noexcept { return resource_; }

        // Allocators are equal when they share storage.
        template <typename U>
        friend bool operator==(const LargeArrayAllocator& a, const LargeArrayAllocator<U>& b) noexcept {
            return a.resource() == b.resource() || a.resource()->is_equal(*b.resource());
        }

    private:
        // The heap, without reference counting (it lives for the whole program).
        static std::shared_ptr<std::pmr::memory_resource> heap() noexcept {
            return {std::shared_ptr<void>(), std::pmr::new_delete_resource()};
        }

        std::shared_ptr<std::pmr::memory_resource> resource_ = heap(); // Where elements live.
    };

    // A vector of big snapshot data.
    template <typename T>
    using LargeVector = std::vector<T, LargeArrayAllocator<T>>;

    // A string of big snapshot data (the name pool's text).
    using LargeString = std::basic_string<char, std::char_traits<char>, LargeArrayAllocator<char>>;

} // namespace icd::core
