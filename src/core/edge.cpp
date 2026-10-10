#include "core/edge.hpp"

#include <mutex>

namespace strutdb::core {

GraphEdge::GraphEdge(EdgeId id_, NodeId src_, NodeId dst_, TypeId type_)
    : id(id_), src(src_), dst(dst_), type(type_) {}

void GraphEdge::set_property(std::string key, PropertyValue value) {
    std::unique_lock<std::shared_mutex> lock(state_mutex_);
    properties_.insert_or_assign(std::move(key), std::move(value));
}

std::optional<PropertyValue> GraphEdge::get_property(std::string_view key) const {
    std::shared_lock<std::shared_mutex> lock(state_mutex_);
    const auto it = properties_.find(std::string(key));
    if (it == properties_.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool GraphEdge::erase_property(std::string_view key) {
    std::unique_lock<std::shared_mutex> lock(state_mutex_);
    return properties_.erase(std::string(key)) > 0;
}

std::unordered_map<std::string, PropertyValue> GraphEdge::snapshot_properties() const {
    std::shared_lock<std::shared_mutex> lock(state_mutex_);
    return properties_;
}

}  // namespace strutdb::core
