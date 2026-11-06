#include "core/util/BitTree.h"

#include <algorithm>
#include <bit>

namespace icd::core {

    namespace {

        // Bits per word.
        constexpr std::size_t wordBits = 64;

        // The bit of an index inside its word.
        std::uint64_t bitOf(std::size_t i) { return std::uint64_t{1} << (i % wordBits); }

        // The bits of a word at and above position `from`.
        std::uint64_t fromBit(std::size_t from) { return ~std::uint64_t{0} << from; }

        // The bits of a word at and below position `to`.
        std::uint64_t toBit(std::size_t to) { return ~std::uint64_t{0} >> (wordBits - 1 - to); }

    } // namespace

    BitTree::BitTree(std::size_t size) {
        std::size_t count = size;
        do {
            count = (count + wordBits - 1) / wordBits;
            levels_.emplace_back(std::max<std::size_t>(count, 1), 0);
        }
        while (count > 1);
    }

    void BitTree::insert(std::size_t i) {
        for (auto& words : levels_) {
            words[i / wordBits] |= bitOf(i);
            i /= wordBits;
        }
    }

    void BitTree::erase(std::size_t i) {
        for (auto& words : levels_) {
            std::uint64_t& word = words[i / wordBits];
            word &= ~bitOf(i);
            if (word != 0) {
                return; // The levels above still see a member in this word.
            }
            i /= wordBits;
        }
    }

    bool BitTree::contains(std::size_t i) const { return (levels_.front()[i / wordBits] & bitOf(i)) != 0; }

    bool BitTree::empty() const { return levels_.back().front() == 0; }

    std::size_t BitTree::next(std::size_t i) const {
        std::size_t level = 0;
        for (; level < levels_.size(); ++level) {
            const std::size_t w = i / wordBits;
            if (w >= levels_[level].size()) {
                return npos;
            }
            const std::uint64_t bits = levels_[level][w] & fromBit(i % wordBits);
            if (bits != 0) {
                i = w * wordBits + static_cast<std::size_t>(std::countr_zero(bits));
                break;
            }
            i = w + 1; // The next word of this level is the next bit of the level above.
        }
        if (level == levels_.size()) {
            return npos;
        }
        while (level-- > 0) {
            i = i * wordBits + static_cast<std::size_t>(std::countr_zero(levels_[level][i]));
        }
        return i;
    }

    std::size_t BitTree::prev(std::size_t i) const {
        std::size_t level = 0;
        for (; level < levels_.size(); ++level) {
            const std::size_t w = i / wordBits;
            const std::uint64_t bits = levels_[level][w] & toBit(i % wordBits);
            if (bits != 0) {
                i = w * wordBits + wordBits - 1 - static_cast<std::size_t>(std::countl_zero(bits));
                break;
            }
            if (w == 0) {
                return npos;
            }
            i = w - 1; // The previous word of this level is the previous bit of the level above.
        }
        if (level == levels_.size()) {
            return npos;
        }
        while (level-- > 0) {
            i = i * wordBits + wordBits - 1 - static_cast<std::size_t>(std::countl_zero(levels_[level][i]));
        }
        return i;
    }

} // namespace icd::core
