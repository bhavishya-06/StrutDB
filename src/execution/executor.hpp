#pragma once

#include "core/error.hpp"
#include "core/graph.hpp"
#include "core/types.hpp"
#include "parser/parser.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace strutdb::execution {

// Result row: maps variable names to their values (nodes, edges, or literals)
struct Row {
    std::unordered_map<std::string, core::PropertyValue> columns;
};

// Query result set
struct ResultSet {
    std::vector<Row> rows;
};

// Evaluation context: holds bindings from previous clauses
struct ExecutionContext {
    std::unordered_map<std::string, std::shared_ptr<core::GraphNode>> node_bindings;
    std::unordered_map<std::string, std::shared_ptr<core::GraphEdge>> edge_bindings;
    std::unordered_map<std::string, core::PropertyValue>         literal_bindings;
};

class Executor {
public:
    explicit Executor(std::shared_ptr<core::Graph> graph);

    Executor(const Executor&)            = delete;
    Executor& operator=(const Executor&) = delete;
    Executor(Executor&&)                 = delete;
    Executor& operator=(Executor&&)      = delete;
    ~Executor()                          = default;

    [[nodiscard]] core::Result<ResultSet> execute(const parser::Query& query);

private:
    [[nodiscard]] core::Result<ResultSet> execute_match(const parser::MatchClause& match);
    [[nodiscard]] core::Result<ResultSet> execute_create(const parser::CreateClause& create);
    [[nodiscard]] core::Result<ResultSet> execute_return(const parser::ReturnClause& ret,
                                                         const ExecutionContext& ctx);

    // BFS traversal for MATCH clauses on the adjacency list
    [[nodiscard]] core::Result<std::vector<ExecutionContext>> bfs_match(
        const parser::PathPattern& path,
        const ExecutionContext&   start_ctx);

    [[nodiscard]] core::Result<bool> matches_node_pattern(const parser::NodePattern& pattern,
                                                          std::shared_ptr<core::GraphNode> node);
    [[nodiscard]] core::Result<bool> matches_rel_pattern(const parser::RelPattern& pattern,
                                                         std::shared_ptr<core::GraphEdge> edge);

    std::shared_ptr<core::Graph> graph_;
};

}  // namespace strutdb::execution
