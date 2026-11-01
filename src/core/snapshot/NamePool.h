#pragma once

#include "core/util/LargeArray.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace icd::core {

    // Saves and loads the private parts of snapshot classes (SnapshotIO.cpp).
    struct SnapshotAccess;

    // Identifies a name in a NamePool.
    using NameId = std::uint32_t;

    // All file names of a snapshot in one UTF-8 buffer, each stored once: a 20 M-file volume repeats names
    // like "index.js" or "desktop.ini" millions of times, and one buffer beats millions of small strings for
    // memory and cache (plan.md 6.2 principle 4).
    class NamePool {
    public:
        // An empty pool on the heap.
        NamePool() = default;

        // An empty pool whose text and offsets live in `storage` (mapped files in the low-memory mode).
        explicit NamePool(const std::shared_ptr<std::pmr::memory_resource>& storage) :
            text_(LargeArrayAllocator<char>(storage)), offsets_({0}, LargeArrayAllocator<std::uint32_t>(storage)) {}

        // Interns name: returns the id of an equal name already stored, or stores it.
        NameId intern(std::string_view name);

        // The text of a name; valid until the next intern().
        [[nodiscard]] std::string_view name(NameId id) const;

        // Number of distinct names.
        [[nodiscard]] std::size_t size() const { return offsets_.size() - 1; }

        // Bytes of name text (without the offset table).
        [[nodiscard]] std::size_t textBytes() const { return text_.size(); }

        // Frees the table intern() uses to find duplicates; the pool stays readable and intern() still
        // works, but no longer deduplicates. Saves memory once building is done.
        void freezeLookup();

    private:
        friend struct SnapshotAccess; // Saves and loads the private parts (SnapshotIO).

        LargeString text_; // All names back to back.
        LargeVector<std::uint32_t> offsets_{0}; // Start of each name in text_, plus one end marker.
        // Ids by the hash of their text (several ids may share a hash). Keyed by hash rather than by
        // string_view so nothing points into text_, which moves when it grows.
        std::unordered_multimap<std::size_t, NameId> lookup_;
    };

} // namespace icd::core
