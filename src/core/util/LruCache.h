#pragma once

#include <cstddef>
#include <functional>
#include <list>
#include <unordered_map>
#include <utility>

namespace icd::core {

    // Least-recently-used cache with a cost limit: each entry has a cost (e.g. bytes), and inserting beyond the
    // limit evicts the oldest entries. Used for map tiles (plan.md 7.3), where the limit is a memory cap.
    // Single-threaded.
    template <typename Key, typename Value, typename Hash = std::hash<Key>>
    class LruCache {
    public:
        // A cache that holds entries up to a total cost of capacity.
        explicit LruCache(std::size_t capacity) : capacity_(capacity) {}

        // The value for key, marked as most recently used; null when absent.
        Value* find(const Key& key) {
            const auto found = index_.find(key);
            if (found == index_.end()) {
                return nullptr;
            }
            order_.splice(order_.begin(), order_, found->second);
            return &found->second->value;
        }

        // Whether key is cached, without changing the order.
        [[nodiscard]] bool contains(const Key& key) const { return index_.contains(key); }

        // Adds or replaces an entry and evicts the least recently used ones until the cost fits again (the new
        // entry itself always stays).
        void insert(const Key& key, Value value, std::size_t cost) {
            erase(key);
            order_.push_front(Entry{key, std::move(value), cost});
            index_.emplace(key, order_.begin());
            cost_ += cost;
            while (cost_ > capacity_ && order_.size() > 1) {
                erase(order_.back().key);
            }
        }

        // Removes an entry if present.
        void erase(const Key& key) {
            const auto found = index_.find(key);
            if (found == index_.end()) {
                return;
            }
            cost_ -= found->second->cost;
            order_.erase(found->second);
            index_.erase(found);
        }

        // Removes everything.
        void clear() {
            order_.clear();
            index_.clear();
            cost_ = 0;
        }

        // Entries held.
        [[nodiscard]] std::size_t size() const { return index_.size(); }

        // Total cost held.
        [[nodiscard]] std::size_t cost() const { return cost_; }

        // The cost limit.
        [[nodiscard]] std::size_t capacity() const { return capacity_; }

    private:
        // One cached value with its key and cost.
        struct Entry {
            Key key; // Lookup key.
            Value value; // Cached value.
            std::size_t cost = 0; // Its share of the limit.
        };

        std::size_t capacity_ = 0; // Cost limit.
        std::size_t cost_ = 0; // Cost held.
        std::list<Entry> order_; // Most recently used first.
        std::unordered_map<Key, typename std::list<Entry>::iterator, Hash> index_; // Key to entry.
    };

} // namespace icd::core
