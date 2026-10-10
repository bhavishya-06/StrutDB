#include "core/node.hpp"

#include <algorithm>
#include <mutex>

namespace strutdb::core {

GraphNode::GraphNode(NodeId id_) : id(id_) {}

void GraphNode::add_label(LabelId label) {
    std::unique_lock<std::shared_mutex> lock(state_mutex_);
    if (std::find(labels_.begin(), labels_.end(), label) == labels_.end()) {
        labels_.push_back(label);
    }
}

bool GraphNode::remove_label(LabelId label) {
    std::unique_lock<std::shared_mutex> lock(state_mutex_);
    const auto old = labels_.size();
    std::erase(labels_, label);
    return labels_.size() != old;
}

std::vector<LabelId> GraphNode::snapshot_labels() const {
    std::shared_lock<std::shared_mutex> lock(state_mutex_);
    return labels_;
}

void GraphNode::set_property(std::string key, PropertyValue value) {
    std::unique_lock<std::shared_mutex> lock(state_mutex_);
    properties_.insert_or_assign(std::move(key), std::move(value));
}

std::optional<PropertyValue> GraphNode::get_property(std::string_view key) const {
    std::shared_lock<std::shared_mutex> lock(state_mutex_);
    const auto it = properties_.find(std::string(key));
    if (it == properties_.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool GraphNode::erase_property(std::string_view key) {
    std::unique_lock<std::shared_mutex> lock(state_mutex_);
    return properties_.erase(std::string(key)) > 0;
}

std::unordered_map<std::string, PropertyValue> GraphNode::snapshot_properties() const {
    std::shared_lock<std::shared_mutex> lock(state_mutex_);
    return properties_;
}

}  // namespace strutdb::core
