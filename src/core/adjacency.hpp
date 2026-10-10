#pragma once

#include "core/hashmap.hpp"
#include "core/types.hpp"

#include <algorithm>
#include <vector>

namespace strutdb::core {

struct AdjEntry {
    EdgeId edge_id     = kInvalidEdgeId;
    NodeId neighbor_id = kInvalidNodeId;
};

// CSR-style adjacency stored as ID lists. No GraphNode*/shared_ptr edges, so
// the topology cannot pin node lifetimes or create ownership cycles.
class AdjacencyList {
public:
    bool add_out(NodeId src, AdjEntry entry) { return append(out_, src, entry); }
    bool add_in(NodeId dst, AdjEntry entry) { return append(in_, dst, entry); }

    [[nodiscard]] std::vector<AdjEntry> out(NodeId src) const {
        return out_.find(src).value_or(std::vector<AdjEntry>{});
    }

    [[nodiscard]] std::vector<AdjEntry> in(NodeId dst) const {
        return in_.find(dst).value_or(std::vector<AdjEntry>{});
    }

    bool remove_out(NodeId src, EdgeId edge_id) { return remove(out_, src, edge_id); }
    bool remove_in(NodeId dst, EdgeId edge_id) { return remove(in_, dst, edge_id); }

    void clear_vertex(NodeId id) {
        out_.erase(id);
        in_.erase(id);
    }

private:
    static bool append(ShardedHashMap<NodeId, std::vector<AdjEntry>>& map,
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
        // A racing insert can lose; fold into the winner.
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

    static bool remove(ShardedHashMap<NodeId, std::vector<AdjEntry>>& map,
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

    ShardedHashMap<NodeId, std::vector<AdjEntry>> out_;
    ShardedHashMap<NodeId, std::vector<AdjEntry>> in_;
};

}  // namespace strutdb::core
