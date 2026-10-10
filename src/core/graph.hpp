#pragma once

#include "core/adjacency.hpp"
#include "core/edge.hpp"
#include "core/error.hpp"
#include "core/hashmap.hpp"
#include "core/node.hpp"
#include "core/types.hpp"

#include <atomic>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

namespace strutdb::core {

// In-memory engine: KV store + graph under one graph-level std::shared_mutex.
//
// Lock protocol (never invert):
//   1. rw_mutex_          — shared for GET/readers, exclusive for writers
//   2. shard mutexes      — acquired only while rw_mutex_ is already held
//   3. node/edge state    — never acquired by Graph writers; only by accessors
// Nested Graph APIs use *_unlocked helpers so rw_mutex_ is never taken twice
// (std::shared_mutex is not recursive — that would deadlock).
class Graph {
public:
    Graph() = default;

    Graph(const Graph&)            = delete;
    Graph& operator=(const Graph&) = delete;
    Graph(Graph&&)                 = delete;
    Graph& operator=(Graph&&)      = delete;
    ~Graph()                       = default;

    // --- Key-Value (GET / SET / DEL) ---
    [[nodiscard]] Result<PropertyValue> get(std::string_view key) const;
    void set(std::string key, PropertyValue value);
    bool del(std::string_view key);

    // --- Graph ---
    std::shared_ptr<GraphNode> add_node();
    Result<std::shared_ptr<GraphEdge>> add_edge(NodeId src, NodeId dst, TypeId type);

    [[nodiscard]] std::shared_ptr<GraphNode> get_node(NodeId id) const;
    [[nodiscard]] std::shared_ptr<GraphEdge> get_edge(EdgeId id) const;

    bool delete_node(NodeId id);
    bool delete_edge(EdgeId id);

    [[nodiscard]] std::vector<AdjEntry> outgoing(NodeId src) const;
    [[nodiscard]] std::vector<AdjEntry> incoming(NodeId dst) const;

    // Phase-1 names kept as aliases.
    std::shared_ptr<GraphNode> create_node() { return add_node(); }
    Result<std::shared_ptr<GraphEdge>> create_edge(NodeId src, NodeId dst, TypeId type) {
        return add_edge(src, dst, type);
    }

private:
    Result<std::shared_ptr<GraphEdge>> add_edge_unlocked(NodeId src, NodeId dst, TypeId type);
    bool delete_node_unlocked(NodeId id);
    bool delete_edge_unlocked(EdgeId id);

    mutable std::shared_mutex                          rw_mutex_;
    ShardedHashMap<std::string, PropertyValue>         kv_;
    ShardedHashMap<NodeId, std::shared_ptr<GraphNode>> nodes_;
    ShardedHashMap<EdgeId, std::shared_ptr<GraphEdge>> edges_;
    AdjacencyList                                      adjacency_;
    std::atomic<NodeId>                                next_node_id_{1};
    std::atomic<EdgeId>                                next_edge_id_{1};
};

}  // namespace strutdb::core
