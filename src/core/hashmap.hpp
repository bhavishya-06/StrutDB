#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace strutdb::core {

// Concurrent open-addressed-by-shard map. Each shard has its own
// std::shared_mutex so readers proceed in parallel across shards while a
// writer exclusive-locks only the affected shard.
//
// V is copied under the lock (cheap when V is std::shared_ptr<T>). Callers
// must not retain references/pointers into the map after the lock drops;
// use the returned value copy or the with_* callbacks.
template <typename Key, typename Value, typename Hash = std::hash<Key>>
class ShardedHashMap {
public:
    static constexpr std::size_t kDefaultShardCount = 64;

    explicit ShardedHashMap(std::size_t shard_count = kDefaultShardCount)
        : shard_mask_(0) {
        const std::size_t n = next_power_of_two(shard_count == 0 ? kDefaultShardCount : shard_count);
        shard_mask_ = n - 1;
        shards_.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            shards_.push_back(std::make_unique<Shard>());
        }
    }

    ShardedHashMap(const ShardedHashMap&)            = delete;
    ShardedHashMap& operator=(const ShardedHashMap&) = delete;
    ShardedHashMap(ShardedHashMap&&)                 = default;
    ShardedHashMap& operator=(ShardedHashMap&&)      = default;
    ~ShardedHashMap()                                = default;

    [[nodiscard]] std::optional<Value> find(const Key& key) const {
        const Shard& shard = shard_for(key);
        std::shared_lock<std::shared_mutex> lock(shard.mutex);
        const auto it = shard.map.find(key);
        if (it == shard.map.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    // Inserts only if absent. Returns true when a new entry was written.
    bool insert(Key key, Value value) {
        Shard& shard = shard_for(key);
        std::unique_lock<std::shared_mutex> lock(shard.mutex);
        const auto [it, inserted] = shard.map.emplace(std::move(key), std::move(value));
        (void)it;
        return inserted;
    }

    // Inserts or overwrites. Returns true if the key was newly inserted.
    bool upsert(Key key, Value value) {
        Shard& shard = shard_for(key);
        std::unique_lock<std::shared_mutex> lock(shard.mutex);
        auto [it, inserted] = shard.map.insert_or_assign(std::move(key), std::move(value));
        (void)it;
        return inserted;
    }

    bool erase(const Key& key) {
        Shard& shard = shard_for(key);
        std::unique_lock<std::shared_mutex> lock(shard.mutex);
        return shard.map.erase(key) > 0;
    }

    [[nodiscard]] bool contains(const Key& key) const {
        const Shard& shard = shard_for(key);
        std::shared_lock<std::shared_mutex> lock(shard.mutex);
        return shard.map.contains(key);
    }

    // Invoke fn(const Value&) while holding a shared lock on the key's shard.
    template <typename Fn>
    bool with_shared(const Key& key, Fn&& fn) const {
        const Shard& shard = shard_for(key);
        std::shared_lock<std::shared_mutex> lock(shard.mutex);
        const auto it = shard.map.find(key);
        if (it == shard.map.end()) {
            return false;
        }
        std::forward<Fn>(fn)(it->second);
        return true;
    }

    // Invoke fn(Value&) while holding an exclusive lock on the key's shard.
    template <typename Fn>
    bool with_exclusive(const Key& key, Fn&& fn) {
        Shard& shard = shard_for(key);
        std::unique_lock<std::shared_mutex> lock(shard.mutex);
        const auto it = shard.map.find(key);
        if (it == shard.map.end()) {
            return false;
        }
        std::forward<Fn>(fn)(it->second);
        return true;
    }

    [[nodiscard]] std::size_t shard_count() const noexcept { return shards_.size(); }

private:
    struct Shard {
        mutable std::shared_mutex          mutex;
        std::unordered_map<Key, Value, Hash> map;
    };

    [[nodiscard]] static std::size_t next_power_of_two(std::size_t n) noexcept {
        std::size_t p = 1;
        while (p < n) {
            p <<= 1U;
        }
        return p;
    }

    [[nodiscard]] std::size_t index_of(const Key& key) const {
        return Hash{}(key) & shard_mask_;
    }

    [[nodiscard]] Shard& shard_for(const Key& key) { return *shards_[index_of(key)]; }

    [[nodiscard]] const Shard& shard_for(const Key& key) const { return *shards_[index_of(key)]; }

    std::size_t                            shard_mask_;
    std::vector<std::unique_ptr<Shard>>    shards_;
};

}  // namespace strutdb::core
