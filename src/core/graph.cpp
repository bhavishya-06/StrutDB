#include "core/graph.hpp"

#include <mutex>
#include <utility>

namespace strutdb::core {

Result<PropertyValue> Graph::get(std::string_view key) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    auto found = kv_.find(std::string(key));
    if (!found) {
        return Result<PropertyValue>{PropertyValue{std::monostate{}}, CoreError::KeyNotFound};
    }
    return Result<PropertyValue>{std::move(*found), CoreError::Ok};
}

void Graph::set(std::string key, PropertyValue value) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    kv_.upsert(std::move(key), std::move(value));
}

bool Graph::del(std::string_view key) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    return kv_.erase(std::string(key));
}

std::shared_ptr<GraphNode> Graph::add_node() {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    const NodeId id = next_node_id_.fetch_add(1, std::memory_order_relaxed);
    auto node = std::make_shared<GraphNode>(id);
    nodes_.insert(id, node);
    return node;
}

Result<std::shared_ptr<GraphEdge>> Graph::add_edge(NodeId src, NodeId dst, TypeId type) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    return add_edge_unlocked(src, dst, type);
}

Result<std::shared_ptr<GraphEdge>> Graph::add_edge_unlocked(NodeId src, NodeId dst, TypeId type) {
    if (src == kInvalidNodeId || dst == kInvalidNodeId) {
        return Result<std::shared_ptr<GraphEdge>>{nullptr, CoreError::InvalidArgument};
    }
    if (!nodes_.contains(src)) {
        return Result<std::shared_ptr<GraphEdge>>{nullptr, CoreError::SrcNodeNotFound};
    }
    if (!nodes_.contains(dst)) {
        return Result<std::shared_ptr<GraphEdge>>{nullptr, CoreError::DstNodeNotFound};
    }

    const EdgeId id = next_edge_id_.fetch_add(1, std::memory_order_relaxed);
    auto edge = std::make_shared<GraphEdge>(id, src, dst, type);
    edges_.insert(id, edge);
    adjacency_.add_out(src, AdjEntry{id, dst});
    adjacency_.add_in(dst, AdjEntry{id, src});
    return Result<std::shared_ptr<GraphEdge>>{edge, CoreError::Ok};
}

std::shared_ptr<GraphNode> Graph::get_node(NodeId id) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    return nodes_.find(id).value_or(nullptr);
}

std::shared_ptr<GraphEdge> Graph::get_edge(EdgeId id) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    return edges_.find(id).value_or(nullptr);
}

bool Graph::delete_node(NodeId id) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    return delete_node_unlocked(id);
}

bool Graph::delete_edge(EdgeId id) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    return delete_edge_unlocked(id);
}

bool Graph::delete_node_unlocked(NodeId id) {
    if (!nodes_.contains(id)) {
        return false;
    }

    const auto outgoing = adjacency_.out(id);
    const auto incoming = adjacency_.in(id);

    for (const AdjEntry& e : outgoing) {
        delete_edge_unlocked(e.edge_id);
    }
    for (const AdjEntry& e : incoming) {
        delete_edge_unlocked(e.edge_id);
    }

    adjacency_.unlink_neighbor(id);
    adjacency_.clear_vertex(id);
    return nodes_.erase(id);
}

bool Graph::delete_edge_unlocked(EdgeId id) {
    auto found = edges_.find(id);
    if (!found) {
        return false;
    }
    const std::shared_ptr<GraphEdge> edge = *found;
    adjacency_.remove_out(edge->src, id);
    adjacency_.remove_in(edge->dst, id);
    return edges_.erase(id);
}

std::vector<AdjEntry> Graph::outgoing(NodeId src) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    return adjacency_.out(src);
}

std::vector<AdjEntry> Graph::incoming(NodeId dst) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    return adjacency_.in(dst);
}

}  // namespace strutdb::core
