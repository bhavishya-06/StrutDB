#pragma once

#include "core/types.hpp"

#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace strutdb::core {

// Graph vertex. Identity is NodeId; topology lives in AdjacencyList so this
// type never holds pointers to other nodes (prevents shared_ptr cycles).
// labels_ and properties_ share one RW mutex so label+property updates cannot
// deadlock with each other on the same node.
struct GraphNode {
    NodeId id = kInvalidNodeId;

    GraphNode() = default;
    explicit GraphNode(NodeId id_);

    GraphNode(const GraphNode&)            = delete;
    GraphNode& operator=(const GraphNode&) = delete;
    GraphNode(GraphNode&&)                 = delete;
    GraphNode& operator=(GraphNode&&)      = delete;
    ~GraphNode()                           = default;

    void add_label(LabelId label);
    bool remove_label(LabelId label);
    [[nodiscard]] std::vector<LabelId> snapshot_labels() const;

    void set_property(std::string key, PropertyValue value);
    [[nodiscard]] std::optional<PropertyValue> get_property(std::string_view key) const;
    bool erase_property(std::string_view key);
    [[nodiscard]] std::unordered_map<std::string, PropertyValue> snapshot_properties() const;

private:
    mutable std::shared_mutex                          state_mutex_;
    std::vector<LabelId>                               labels_;
    std::unordered_map<std::string, PropertyValue>     properties_;
};

}  // namespace strutdb::core
