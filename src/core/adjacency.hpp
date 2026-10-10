#pragma once

#include "core/hashmap.hpp"
#include "core/types.hpp"

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
    bool add_out(NodeId src, AdjEntry entry);
    bool add_in(NodeId dst, AdjEntry entry);

    [[nodiscard]] std::vector<AdjEntry> out(NodeId src) const;
    [[nodiscard]] std::vector<AdjEntry> in(NodeId dst) const;

    bool remove_out(NodeId src, EdgeId edge_id);
    bool remove_in(NodeId dst, EdgeId edge_id);

    void clear_vertex(NodeId id);

    // Drop every AdjEntry on either side that names `deleted` as neighbor.
    void unlink_neighbor(NodeId deleted);

private:
    static bool append(ShardedHashMap<NodeId, std::vector<AdjEntry>>& map,
                       NodeId key,
                       AdjEntry entry);
    static bool remove(ShardedHashMap<NodeId, std::vector<AdjEntry>>& map,
                       NodeId key,
                       EdgeId edge_id);

    ShardedHashMap<NodeId, std::vector<AdjEntry>> out_;
    ShardedHashMap<NodeId, std::vector<AdjEntry>> in_;
};

}  // namespace strutdb::core
