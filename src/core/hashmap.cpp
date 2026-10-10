#include "core/adjacency.hpp"
#include "core/edge.hpp"
#include "core/hashmap.hpp"
#include "core/node.hpp"
#include "core/types.hpp"

#include <memory>
#include <string>
#include <vector>

namespace strutdb::core {

template class ShardedHashMap<std::string, PropertyValue>;
template class ShardedHashMap<NodeId, std::shared_ptr<GraphNode>>;
template class ShardedHashMap<EdgeId, std::shared_ptr<GraphEdge>>;
template class ShardedHashMap<NodeId, std::vector<AdjEntry>>;

}  // namespace strutdb::core
