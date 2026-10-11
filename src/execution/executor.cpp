#include "execution/executor.hpp"

#include <algorithm>
#include <queue>
#include <ranges>

namespace strutdb::execution {

Executor::Executor(std::shared_ptr<core::Graph> graph)
    : graph_(std::move(graph)) {}

core::Result<ResultSet> Executor::execute(const parser::Query& query) {
    std::vector<ExecutionContext> contexts;
    contexts.push_back(ExecutionContext{});
    ResultSet result;

    for (const auto& clause : query.clauses) {
        switch (clause.kind) {
            case parser::ClauseKind::Match: {
                std::vector<ExecutionContext> new_contexts;
                for (const auto& ctx : contexts) {
                    std::vector<ExecutionContext> path_contexts = {ctx};
                    for (const auto& path : clause.match.paths) {
                        std::vector<ExecutionContext> next_path_contexts;
                        for (const auto& p_ctx : path_contexts) {
                            auto bfs_res = bfs_match(path, p_ctx);
                            if (bfs_res) {
                                next_path_contexts.insert(next_path_contexts.end(),
                                                          bfs_res.value.begin(),
                                                          bfs_res.value.end());
                            }
                        }
                        path_contexts = std::move(next_path_contexts);
                    }
                    new_contexts.insert(new_contexts.end(), path_contexts.begin(), path_contexts.end());
                }
                contexts = std::move(new_contexts);
                break;
            }
            case parser::ClauseKind::Create: {
                if (contexts.empty()) {
                    contexts.push_back(ExecutionContext{});
                }
                std::vector<ExecutionContext> updated_contexts;
                for (auto& ctx : contexts) {
                    for (const auto& path : clause.create.paths) {
                        for (const auto& node_pattern : path.nodes) {
                            std::shared_ptr<core::GraphNode> node;
                            if (!node_pattern.alias.empty() && ctx.node_bindings.contains(node_pattern.alias)) {
                                node = ctx.node_bindings[node_pattern.alias];
                            } else {
                                node = graph_->add_node();
                                if (!node) {
                                    return core::Result<ResultSet>{{}, core::CoreError::InvalidArgument};
                                }
                                for (const auto& prop : node_pattern.properties) {
                                    node->set_property(prop.key, prop.value);
                                }
                                for (const auto& label : node_pattern.labels) {
                                    node->add_label(static_cast<core::LabelId>(std::hash<std::string>{}(label)));
                                }
                                if (!node_pattern.alias.empty()) {
                                    ctx.node_bindings[node_pattern.alias] = node;
                                }
                            }
                        }

                        for (size_t i = 0; i < path.rels.size(); ++i) {
                            const auto& rel = path.rels[i];
                            const auto& src_node = path.nodes[i];
                            const auto& dst_node = path.nodes[i + 1];

                            auto src_id = ctx.node_bindings[src_node.alias]->id;
                            auto dst_id = ctx.node_bindings[dst_node.alias]->id;

                            core::TypeId type_id = 1;
                            if (!rel.types.empty()) {
                                type_id = static_cast<core::TypeId>(std::hash<std::string>{}(rel.types[0]));
                            }

                            auto edge_result = graph_->add_edge(src_id, dst_id, type_id);
                            if (!edge_result) {
                                return core::Result<ResultSet>{{}, edge_result.error};
                            }

                            auto edge = edge_result.value;
                            for (const auto& prop : rel.properties) {
                                edge->set_property(prop.key, prop.value);
                            }

                            if (!rel.alias.empty()) {
                                ctx.edge_bindings[rel.alias] = edge;
                            }
                        }
                    }
                    updated_contexts.push_back(ctx);
                }
                contexts = std::move(updated_contexts);
                break;
            }
            case parser::ClauseKind::Return: {
                for (const auto& ctx : contexts) {
                    auto return_result = execute_return(clause.ret, ctx);
                    if (!return_result) {
                        return core::Result<ResultSet>{{}, return_result.error};
                    }
                    result.rows.insert(result.rows.end(),
                                       return_result.value.rows.begin(),
                                       return_result.value.rows.end());
                }
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
                        core::PropertyValue val = static_cast<std::int64_t>(ctx.node_bindings.at(item.identifier)->id);
                        row.columns[item.alias.empty() ? item.identifier : item.alias] = val;
                        row.values.push_back(val);
                    } else if (ctx.edge_bindings.contains(item.identifier)) {
                        core::PropertyValue val = static_cast<std::int64_t>(ctx.edge_bindings.at(item.identifier)->id);
                        row.columns[item.alias.empty() ? item.identifier : item.alias] = val;
                        row.values.push_back(val);
                    } else if (ctx.literal_bindings.contains(item.identifier)) {
                        core::PropertyValue val = ctx.literal_bindings.at(item.identifier);
                        row.columns[item.alias.empty() ? item.identifier : item.alias] = val;
                        row.values.push_back(val);
                    }
                    break;
                }
                case parser::ReturnItemKind::Literal: {
                    row.columns[item.alias.empty() ? item.identifier : item.alias] = item.literal;
                    row.values.push_back(item.literal);
                    break;
                }
                case parser::ReturnItemKind::Property: {
                    std::string col = item.alias.empty() ? (item.identifier + "." + item.property) : item.alias;
                    if (ctx.node_bindings.contains(item.identifier)) {
                        auto val = ctx.node_bindings.at(item.identifier)->get_property(item.property);
                        if (val) {
                            row.columns[col] = *val;
                            row.values.push_back(*val);
                        }
                    } else if (ctx.edge_bindings.contains(item.identifier)) {
                        auto val = ctx.edge_bindings.at(item.identifier)->get_property(item.property);
                        if (val) {
                            row.columns[col] = *val;
                            row.values.push_back(*val);
                        }
                    }
                    break;
                }
                case parser::ReturnItemKind::Star: {
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
                if (!node_pattern.alias.empty() && ctx.node_bindings.contains(node_pattern.alias)) {
                    auto bound_node = ctx.node_bindings.at(node_pattern.alias);
                    auto match = matches_node_pattern(node_pattern, bound_node);
                    if (match && match.value) {
                        if (path.nodes.size() == 1 && path.rels.empty()) {
                            results.push_back(ctx);
                        } else {
                            next_queue.push(ctx);
                        }
                    }
                } else {
                    for (core::NodeId id = 1; id < graph_->max_node_id(); ++id) {
                        auto node = graph_->get_node(id);
                        if (!node) continue;
                        auto match = matches_node_pattern(node_pattern, node);
                        if (match && match.value) {
                            ExecutionContext new_ctx = ctx;
                            if (!node_pattern.alias.empty()) {
                                new_ctx.node_bindings[node_pattern.alias] = node;
                            }
                            if (path.nodes.size() == 1 && path.rels.empty()) {
                                results.push_back(std::move(new_ctx));
                            } else {
                                next_queue.push(std::move(new_ctx));
                            }
                        }
                    }
                }
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
            core::LabelId target_id = static_cast<core::LabelId>(std::hash<std::string>{}(label_str));
            if (std::find(node_labels.begin(), node_labels.end(), target_id) == node_labels.end()) {
                return core::Result<bool>{false};
            }
        }
    }

    // Check properties
    if (!pattern.properties.empty()) {
        auto node_props = node->snapshot_properties();
        for (const auto& prop : pattern.properties) {
            auto it = node_props.find(prop.key);
            if (it == node_props.end() || it->second != prop.value) {
                return core::Result<bool>{false};
            }
        }
    }

    return core::Result<bool>{true};
}

core::Result<bool> Executor::matches_rel_pattern(const parser::RelPattern& pattern,
                                                 std::shared_ptr<core::GraphEdge> edge) {
    // Check types
    if (!pattern.types.empty()) {
        for (const auto& type_str : pattern.types) {
            core::TypeId target_id = static_cast<core::TypeId>(std::hash<std::string>{}(type_str));
            if (edge->type != target_id) {
                return core::Result<bool>{false};
            }
        }
    }

    // Check properties
    if (!pattern.properties.empty()) {
        auto edge_props = edge->snapshot_properties();
        for (const auto& prop : pattern.properties) {
            auto it = edge_props.find(prop.key);
            if (it == edge_props.end() || it->second != prop.value) {
                return core::Result<bool>{false};
            }
        }
    }

    return core::Result<bool>{true};
}

}  // namespace strutdb::execution
