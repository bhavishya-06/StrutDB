#pragma once

#include "core/types.hpp"

#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace strutdb::core {

// Graph vertex. Identity is NodeId; topology lives in AdjacencyList so this
// type never holds pointers to other nodes (prevents shared_ptr cycles).
struct GraphNode {
    NodeId id = kInvalidNodeId;
    std::vector<LabelId> labels;

    GraphNode() = default;
    explicit GraphNode(NodeId id_) : id(id_) {}

    GraphNode(const GraphNode&)            = delete;
    GraphNode& operator=(const GraphNode&) = delete;
    GraphNode(GraphNode&&)                 = delete;
    GraphNode& operator=(GraphNode&&)      = delete;
    ~GraphNode()                           = default;

    void set_property(std::string key, PropertyValue value) {
        std::unique_lock<std::shared_mutex> lock(properties_mutex_);
        properties_.insert_or_assign(std::move(key), std::move(value));
    }

    [[nodiscard]] std::optional<PropertyValue> get_property(std::string_view key) const {
        std::shared_lock<std::shared_mutex> lock(properties_mutex_);
        const auto it = properties_.find(std::string(key));
        if (it == properties_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    bool erase_property(std::string_view key) {
        std::unique_lock<std::shared_mutex> lock(properties_mutex_);
        return properties_.erase(std::string(key)) > 0;
    }

    [[nodiscard]] std::unordered_map<std::string, PropertyValue> snapshot_properties() const {
        std::shared_lock<std::shared_mutex> lock(properties_mutex_);
        return properties_;
    }

private:
    mutable std::shared_mutex properties_mutex_;
    std::unordered_map<std::string, PropertyValue> properties_;
};

}  // namespace strutdb::core
