#pragma once

#include "core/types.hpp"

#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace strutdb::core {

// Directed relationship. Endpoints are IDs, never owning pointers.
struct GraphEdge {
    EdgeId id  = kInvalidEdgeId;
    NodeId src = kInvalidNodeId;
    NodeId dst = kInvalidNodeId;
    TypeId type = 0;

    GraphEdge() = default;
    GraphEdge(EdgeId id_, NodeId src_, NodeId dst_, TypeId type_)
        : id(id_), src(src_), dst(dst_), type(type_) {}

    GraphEdge(const GraphEdge&)            = delete;
    GraphEdge& operator=(const GraphEdge&) = delete;
    GraphEdge(GraphEdge&&)                 = delete;
    GraphEdge& operator=(GraphEdge&&)      = delete;
    ~GraphEdge()                           = default;

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
