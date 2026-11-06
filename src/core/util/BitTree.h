#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace icd::core {

    // A set of indexes [0, size) as bits with a summary level per 64 words, so the nearest member on either side of an
    // index is found in a few word scans: a ready set of millions of moves costs bits, not tree nodes (T133).
    class BitTree {
    public:
        // Returned by next() and prev() when there is no such member.
        static constexpr std::size_t npos = static_cast<std::size_t>(-1);

        // An empty set over [0, size).
        explicit BitTree(std::size_t size);

        // Adds i (i < size).
        void insert(std::size_t i);

        // Removes i (i < size).
        void erase(std::size_t i);

        // Whether i is a member (i < size).
        [[nodiscard]] bool contains(std::size_t i) const;

        // The smallest member >= i, or npos.
        [[nodiscard]] std::size_t next(std::size_t i) const;

        // The largest member <= i, or npos.
        [[nodiscard]] std::size_t prev(std::size_t i) const;

        // Whether the set has no member.
        [[nodiscard]] bool empty() const;

    private:
        // Level 0 holds the members; bit j of level k+1 says whether word j of level k has any bit set.
        std::vector<std::vector<std::uint64_t>> levels_;
    };

} // namespace icd::core
