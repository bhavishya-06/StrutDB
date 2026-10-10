#include "core/adjacency.hpp"

#include <algorithm>
#include <mutex>
#include <utility>

namespace strutdb::core {

bool AdjacencyList::add_out(NodeId src, AdjEntry entry) { return append(out_, src, entry); }
bool AdjacencyList::add_in(NodeId dst, AdjEntry entry) { return append(in_, dst, entry); }

std::vector<AdjEntry> AdjacencyList::out(NodeId src) const {
    return out_.find(src).value_or(std::vector<AdjEntry>{});
}

std::vector<AdjEntry> AdjacencyList::in(NodeId dst) const {
    return in_.find(dst).value_or(std::vector<AdjEntry>{});
}

bool AdjacencyList::remove_out(NodeId src, EdgeId edge_id) { return remove(out_, src, edge_id); }
bool AdjacencyList::remove_in(NodeId dst, EdgeId edge_id) { return remove(in_, dst, edge_id); }

void AdjacencyList::clear_vertex(NodeId id) {
    out_.erase(id);
    in_.erase(id);
}

void AdjacencyList::unlink_neighbor(NodeId deleted) {
    const auto strip = [deleted](std::vector<AdjEntry>& edges) {
        std::erase_if(edges, [deleted](const AdjEntry& e) { return e.neighbor_id == deleted; });
    };
    const auto outgoing = out(deleted);
    for (const AdjEntry& e : outgoing) {
        in_.with_exclusive(e.neighbor_id, strip);
    }
    const auto incoming = in(deleted);
    for (const AdjEntry& e : incoming) {
        out_.with_exclusive(e.neighbor_id, strip);
    }
}

bool AdjacencyList::append(ShardedHashMap<NodeId, std::vector<AdjEntry>>& map,
                           NodeId key,
                           AdjEntry entry) {
    bool appended = false;
    const bool exists = map.with_exclusive(key, [&](std::vector<AdjEntry>& edges) {
        edges.push_back(entry);
        appended = true;
    });
    if (exists) {
        return appended;
    }
    std::vector<AdjEntry> first{entry};
    map.insert(key, std::move(first));
    map.with_exclusive(key, [&](std::vector<AdjEntry>& edges) {
        if (edges.empty() || edges.back().edge_id != entry.edge_id) {
            const auto it = std::find_if(edges.begin(), edges.end(),
                [&](const AdjEntry& e) { return e.edge_id == entry.edge_id; });
            if (it == edges.end()) {
                edges.push_back(entry);
            }
        }
    });
    return true;
}

bool AdjacencyList::remove(ShardedHashMap<NodeId, std::vector<AdjEntry>>& map,
                           NodeId key,
                           EdgeId edge_id) {
    bool removed = false;
    map.with_exclusive(key, [&](std::vector<AdjEntry>& edges) {
        const auto old = edges.size();
        std::erase_if(edges, [&](const AdjEntry& e) { return e.edge_id == edge_id; });
        removed = edges.size() != old;
    });
    return removed;
}

}  // namespace strutdb::core
