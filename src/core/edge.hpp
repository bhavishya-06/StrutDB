#pragma once

#include "core/types.hpp"

#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace strutdb::core {

// Directed relationship. Endpoints are IDs, never owning pointers.
struct GraphEdge {
    EdgeId id   = kInvalidEdgeId;
    NodeId src  = kInvalidNodeId;
    NodeId dst  = kInvalidNodeId;
    TypeId type = 0;

    GraphEdge() = default;
    GraphEdge(EdgeId id_, NodeId src_, NodeId dst_, TypeId type_);

    GraphEdge(const GraphEdge&)            = delete;
    GraphEdge& operator=(const GraphEdge&) = delete;
    GraphEdge(GraphEdge&&)                 = delete;
    GraphEdge& operator=(GraphEdge&&)      = delete;
    ~GraphEdge()                           = default;

    void set_property(std::string key, PropertyValue value);
    [[nodiscard]] std::optional<PropertyValue> get_property(std::string_view key) const;
    bool erase_property(std::string_view key);
    [[nodiscard]] std::unordered_map<std::string, PropertyValue> snapshot_properties() const;

private:
    mutable std::shared_mutex                      state_mutex_;
    std::unordered_map<std::string, PropertyValue> properties_;
};

}  // namespace strutdb::core
