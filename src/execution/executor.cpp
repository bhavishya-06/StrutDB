#include "execution/executor.hpp"

#include <algorithm>
#include <queue>
#include <ranges>

namespace strutdb::execution {

Executor::Executor(std::shared_ptr<core::Graph> graph)
    : graph_(std::move(graph)) {}

core::Result<ResultSet> Executor::execute(const parser::Query& query) {
    ExecutionContext ctx;
    ResultSet result;

    for (const auto& clause : query.clauses) {
        switch (clause.kind) {
            case parser::ClauseKind::Match: {
                auto match_result = execute_match(clause.match);
                if (!match_result) {
                    return core::Result<ResultSet>{{}, match_result.error};
                }
                result = std::move(match_result.value);
                break;
            }
            case parser::ClauseKind::Create: {
                auto create_result = execute_create(clause.create);
                if (!create_result) {
                    return core::Result<ResultSet>{{}, create_result.error};
                }
                result = std::move(create_result.value);
                break;
            }
            case parser::ClauseKind::Return: {
                auto return_result = execute_return(clause.ret, ctx);
                if (!return_result) {
                    return core::Result<ResultSet>{{}, return_result.error};
                }
                result = std::move(return_result.value);
                break;
            }
        }
    }

    return core::Result<ResultSet>{result};
}

core::Result<ResultSet> Executor::execute_match(const parser::MatchClause& match) {
    ResultSet result;

    // For each path pattern in the MATCH clause
    for (const auto& path : match.paths) {
        ExecutionContext start_ctx;

        // BFS from the first node
        auto bfs_result = bfs_match(path, start_ctx);
        if (!bfs_result) {
            return core::Result<ResultSet>{{}, bfs_result.error};
        }

        // Convert each execution context to a result row
        for (const auto& ctx : bfs_result.value) {
            Row row;
            for (const auto& [alias, node] : ctx.node_bindings) {
                row.columns[alias] = static_cast<std::int64_t>(node->id);
            }
            for (const auto& [alias, edge] : ctx.edge_bindings) {
                row.columns[alias] = static_cast<std::int64_t>(edge->id);
            }
            for (const auto& [alias, literal] : ctx.literal_bindings) {
                row.columns[alias] = literal;
            }
            result.rows.push_back(std::move(row));
        }
    }

    return core::Result<ResultSet>{result};
}

core::Result<ResultSet> Executor::execute_create(const parser::CreateClause& create) {
    ResultSet result;

    for (const auto& path : create.paths) {
        ExecutionContext ctx;

        // Create nodes in the path
        for (const auto& node_pattern : path.nodes) {
            auto node = graph_->add_node();
            if (!node) {
                return core::Result<ResultSet>{{}, core::CoreError::InvalidArgument};
            }

            // Set properties
            for (const auto& prop : node_pattern.properties) {
                node->set_property(prop.key, prop.value);
            }

            // Set labels (convert string labels to LabelId - for now using hash)
            for (const auto& label : node_pattern.labels) {
                // TODO: Implement label string-to-id mapping
                // node->add_label(label_to_id(label));
            }

            if (!node_pattern.alias.empty()) {
                ctx.node_bindings[node_pattern.alias] = node;
            }
        }

        // Create edges in the path
        for (size_t i = 0; i < path.rels.size(); ++i) {
            const auto& rel = path.rels[i];
            const auto& src_node = path.nodes[i];
            const auto& dst_node = path.nodes[i + 1];

            auto src_id = ctx.node_bindings[src_node.alias]->id;
            auto dst_id = ctx.node_bindings[dst_node.alias]->id;

            // TODO: Convert type string to TypeId
            core::TypeId type_id = 1;

            auto edge_result = graph_->add_edge(src_id, dst_id, type_id);
            if (!edge_result) {
                return core::Result<ResultSet>{{}, edge_result.error};
            }

            auto edge = edge_result.value;

            // Set properties
            for (const auto& prop : rel.properties) {
                edge->set_property(prop.key, prop.value);
            }

            if (!rel.alias.empty()) {
                ctx.edge_bindings[rel.alias] = edge;
            }
        }

        // Add a result row
        Row row;
        for (const auto& [alias, node] : ctx.node_bindings) {
            row.columns[alias] = static_cast<std::int64_t>(node->id);
        }
        for (const auto& [alias, edge] : ctx.edge_bindings) {
            row.columns[alias] = static_cast<std::int64_t>(edge->id);
        }
        result.rows.push_back(std::move(row));
    }

    return core::Result<ResultSet>{result};
}

core::Result<ResultSet> Executor::execute_return(const parser::ReturnClause& ret,
                                                 const ExecutionContext& ctx) {
    ResultSet result;

    if (ret.star) {
        // Return all bindings
        Row row;
        for (const auto& [alias, node] : ctx.node_bindings) {
            row.columns[alias] = static_cast<std::int64_t>(node->id);
        }
        for (const auto& [alias, edge] : ctx.edge_bindings) {
            row.columns[alias] = static_cast<std::int64_t>(edge->id);
        }
        for (const auto& [alias, literal] : ctx.literal_bindings) {
            row.columns[alias] = literal;
        }
        result.rows.push_back(std::move(row));
    } else {
        // Return specified items
        Row row;
        for (const auto& item : ret.items) {
            switch (item.kind) {
                case parser::ReturnItemKind::Identifier: {
                    if (ctx.node_bindings.contains(item.identifier)) {
                        row.columns[item.alias.empty() ? item.identifier : item.alias] =
                            static_cast<std::int64_t>(ctx.node_bindings.at(item.identifier)->id);
                    } else if (ctx.edge_bindings.contains(item.identifier)) {
                        row.columns[item.alias.empty() ? item.identifier : item.alias] =
                            static_cast<std::int64_t>(ctx.edge_bindings.at(item.identifier)->id);
                    } else if (ctx.literal_bindings.contains(item.identifier)) {
                        row.columns[item.alias.empty() ? item.identifier : item.alias] =
                            ctx.literal_bindings.at(item.identifier);
                    }
                    break;
                }
                case parser::ReturnItemKind::Literal: {
                    row.columns[item.alias.empty() ? item.identifier : item.alias] = item.literal;
                    break;
                }
                case parser::ReturnItemKind::Property: {
                    // TODO: Implement property access
                    break;
                }
                case parser::ReturnItemKind::Star: {
                    // Handled above
                    break;
                }
            }
        }
        result.rows.push_back(std::move(row));
    }

    return core::Result<ResultSet>{result};
}

core::Result<std::vector<ExecutionContext>> Executor::bfs_match(
    const parser::PathPattern& path,
    const ExecutionContext&   start_ctx) {
    std::vector<ExecutionContext> results;
    std::queue<ExecutionContext> queue;

    // Start with the initial context
    queue.push(start_ctx);

    // BFS through the path pattern
    for (size_t path_idx = 0; path_idx < path.nodes.size(); ++path_idx) {
        const auto& node_pattern = path.nodes[path_idx];
        std::queue<ExecutionContext> next_queue;

        while (!queue.empty()) {
            auto ctx = std::move(queue.front());
            queue.pop();

            // If this is the first node, start from all nodes in the graph
            if (path_idx == 0) {
                // Iterate through all nodes (simplified - in production use indexed lookup)
                // For now, we'll iterate through a range - this is a placeholder
                // In a real implementation, we'd use node labels/properties for filtering
                // TODO: Implement efficient node iteration with label/property indexing

                // For demonstration, we'll skip this iteration
                // In production, we'd iterate through nodes that match the pattern
                continue;
            }

            // If we have a previous node, traverse edges
            if (path_idx > 0 && !path.rels.empty()) {
                const auto& rel_pattern = path.rels[path_idx - 1];
                const auto& prev_node_pattern = path.nodes[path_idx - 1];

                if (ctx.node_bindings.contains(prev_node_pattern.alias)) {
                    auto prev_node = ctx.node_bindings[prev_node_pattern.alias];

                    // Get outgoing edges based on direction
                    std::vector<core::AdjEntry> adj_entries;
                    if (rel_pattern.direction == parser::RelDirection::Outbound ||
                        rel_pattern.direction == parser::RelDirection::Bidirectional) {
                        adj_entries = graph_->outgoing(prev_node->id);
                    }
                    if (rel_pattern.direction == parser::RelDirection::Inbound ||
                        rel_pattern.direction == parser::RelDirection::Bidirectional) {
                        auto incoming = graph_->incoming(prev_node->id);
                        adj_entries.insert(adj_entries.end(), incoming.begin(), incoming.end());
                    }

                    // Traverse each edge
                    for (const auto& adj : adj_entries) {
                        auto edge = graph_->get_edge(adj.edge_id);
                        if (!edge) {
                            continue;
                        }

                        // Check if edge matches the pattern
                        auto edge_match = matches_rel_pattern(rel_pattern, edge);
                        if (!edge_match || !edge_match.value) {
                            continue;
                        }

                        auto neighbor = graph_->get_node(adj.neighbor_id);
                        if (!neighbor) {
                            continue;
                        }

                        // Check if neighbor node matches the pattern
                        auto node_match = matches_node_pattern(node_pattern, neighbor);
                        if (!node_match || !node_match.value) {
                            continue;
                        }

                        // Create new context with this binding
                        ExecutionContext new_ctx = ctx;
                        if (!node_pattern.alias.empty()) {
                            new_ctx.node_bindings[node_pattern.alias] = neighbor;
                        }
                        if (!rel_pattern.alias.empty()) {
                            new_ctx.edge_bindings[rel_pattern.alias] = edge;
                        }

                        if (path_idx == path.nodes.size() - 1) {
                            // This is the last node, add to results
                            results.push_back(std::move(new_ctx));
                        } else {
                            // Continue BFS
                            next_queue.push(std::move(new_ctx));
                        }
                    }
                }
            }
        }

        queue = std::move(next_queue);
    }

    return core::Result<std::vector<ExecutionContext>>{results};
}

core::Result<bool> Executor::matches_node_pattern(const parser::NodePattern& pattern,
                                                  std::shared_ptr<core::GraphNode> node) {
    // Check labels
    if (!pattern.labels.empty()) {
        auto node_labels = node->snapshot_labels();
        for (const auto& label_str : pattern.labels) {
            // TODO: Convert label string to LabelId and check
            // For now, skip label matching
        }
    }

    // Check properties
    if (!pattern.properties.empty()) {
        auto node_props = node->snapshot_properties();
        for (const auto& prop : pattern.properties) {
            auto it = node_props.find(prop.key);
            if (it == node_props.end()) {
                return core::Result<bool>{false};
            }
            // TODO: Implement property value comparison
        }
    }

    return core::Result<bool>{true};
}

core::Result<bool> Executor::matches_rel_pattern(const parser::RelPattern& pattern,
                                                 std::shared_ptr<core::GraphEdge> edge) {
    // Check types
    if (!pattern.types.empty()) {
        // TODO: Convert type string to TypeId and check
        // For now, skip type matching
    }

    // Check properties
    if (!pattern.properties.empty()) {
        auto edge_props = edge->snapshot_properties();
        for (const auto& prop : pattern.properties) {
            auto it = edge_props.find(prop.key);
            if (it == edge_props.end()) {
                return core::Result<bool>{false};
            }
            // TODO: Implement property value comparison
        }
    }

    return core::Result<bool>{true};
}

}  // namespace strutdb::execution
