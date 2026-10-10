#pragma once

#include "core/adjacency.hpp"
#include "core/edge.hpp"
#include "core/hashmap.hpp"
#include "core/node.hpp"
#include "core/types.hpp"

#include <atomic>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace strutdb::core {

// In-memory graph. Owns nodes and edges via shared_ptr so readers can hold a
// refcount after dropping shard locks. Topology is ID-based (AdjacencyList),
// so shared_ptr graphs cannot leak through pointer cycles.
class Graph {
public:
    Graph() = default;

    Graph(const Graph&)            = delete;
    Graph& operator=(const Graph&) = delete;
    Graph(Graph&&)                 = delete;
    Graph& operator=(Graph&&)      = delete;
    ~Graph()                       = default;

    std::shared_ptr<GraphNode> create_node() {
        const NodeId id = next_node_id_.fetch_add(1, std::memory_order_relaxed);
        auto node = std::make_shared<GraphNode>(id);
        nodes_.insert(id, node);
        return node;
    }

    [[nodiscard]] std::shared_ptr<GraphNode> get_node(NodeId id) const {
        auto found = nodes_.find(id);
        return found.value_or(nullptr);
    }

    bool delete_node(NodeId id) {
        auto outgoing = adjacency_.out(id);
        auto incoming = adjacency_.in(id);
        for (const AdjEntry& e : outgoing) {
            delete_edge(e.edge_id);
        }
        for (const AdjEntry& e : incoming) {
            delete_edge(e.edge_id);
        }
        adjacency_.clear_vertex(id);
        return nodes_.erase(id);
    }

    std::shared_ptr<GraphEdge> create_edge(NodeId src, NodeId dst, TypeId type) {
        if (!nodes_.contains(src) || !nodes_.contains(dst)) {
            return nullptr;
        }
        const EdgeId id = next_edge_id_.fetch_add(1, std::memory_order_relaxed);
        auto edge = std::make_shared<GraphEdge>(id, src, dst, type);
        edges_.insert(id, edge);
        adjacency_.add_out(src, AdjEntry{id, dst});
        adjacency_.add_in(dst, AdjEntry{id, src});
        return edge;
    }

    [[nodiscard]] std::shared_ptr<GraphEdge> get_edge(EdgeId id) const {
        auto found = edges_.find(id);
        return found.value_or(nullptr);
    }

    bool delete_edge(EdgeId id) {
        auto found = edges_.find(id);
        if (!found) {
            return false;
        }
        const std::shared_ptr<GraphEdge> edge = *found;
        adjacency_.remove_out(edge->src, id);
        adjacency_.remove_in(edge->dst, id);
        return edges_.erase(id);
    }

    [[nodiscard]] std::vector<AdjEntry> outgoing(NodeId src) const { return adjacency_.out(src); }
    [[nodiscard]] std::vector<AdjEntry> incoming(NodeId dst) const { return adjacency_.in(dst); }

private:
    ShardedHashMap<NodeId, std::shared_ptr<GraphNode>> nodes_;
    ShardedHashMap<EdgeId, std::shared_ptr<GraphEdge>> edges_;
    AdjacencyList                                      adjacency_;
    std::atomic<NodeId>                                next_node_id_{1};
    std::atomic<EdgeId>                                next_edge_id_{1};
};

}  // namespace strutdb::core
