#pragma once

#include "core/error.hpp"
#include "core/types.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace strutdb::parser {

enum class RelDirection {
    Inbound,
    Outbound,
    Bidirectional,
};

enum class ClauseKind {
    Match,
    Create,
    Return,
};

enum class ReturnItemKind {
    Identifier,
    Property,
    Star,
    Literal,
};

struct NodePattern {
    std::string                              alias;
    std::vector<std::string>                 labels;
    std::vector<strutdb::core::Property>     properties;
};

struct RelPattern {
    std::string                              alias;
    std::vector<std::string>                 types;
    RelDirection                             direction = RelDirection::Outbound;
    std::vector<strutdb::core::Property>     properties;
};

struct PathPattern {
    std::vector<NodePattern> nodes;
    std::vector<RelPattern>  rels;
};

struct MatchClause {
    bool                      optional = false;
    std::vector<PathPattern>  paths;
};

struct CreateClause {
    std::vector<PathPattern> paths;
};

struct ReturnItem {
    ReturnItemKind               kind = ReturnItemKind::Identifier;
    std::string                  alias;
    std::string                  identifier;
    std::string                  property;
    strutdb::core::PropertyValue literal;
};

struct ReturnClause {
    bool                      distinct = false;
    bool                      star     = false;
    std::vector<ReturnItem>   items;
};

struct Clause {
    ClauseKind    kind = ClauseKind::Match;
    MatchClause   match;
    CreateClause  create;
    ReturnClause  ret;
};

struct Query {
    std::vector<Clause> clauses;
};

class CypherParser {
public:
    static constexpr std::size_t kMaxQueryBytes = 1U << 20;  // 1 MiB
    static constexpr std::size_t kMaxCString    = 64U * 1024U;

    [[nodiscard]] strutdb::core::Result<Query> parse(std::string_view cypher) const;
};

}  // namespace strutdb::parser
