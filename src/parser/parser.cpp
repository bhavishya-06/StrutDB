#include "parser/parser.hpp"

#include <charconv>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <system_error>
#include <utility>

extern "C" {
#include <cypher-parser.h>
}

namespace strutdb::parser {
namespace {

struct ParseResultDeleter {
    void operator()(cypher_parse_result_t* result) const noexcept {
        if (result != nullptr) {
            cypher_parse_result_free(result);
        }
    }
};

using UniqueParseResult = std::unique_ptr<cypher_parse_result_t, ParseResultDeleter>;

[[nodiscard]] std::string copy_cstr(const char* raw) {
    if (raw == nullptr) {
        return {};
    }
    const std::size_t n = strnlen(raw, CypherParser::kMaxCString);
    return std::string(raw, n);
}

[[nodiscard]] bool type_is(const cypher_astnode_t* node, cypher_astnode_type_t type) {
    return node != nullptr && cypher_astnode_instanceof(node, type);
}

[[nodiscard]] strutdb::core::Result<strutdb::core::PropertyValue>
literal_value(const cypher_astnode_t* node) {
    using strutdb::core::CoreError;
    using strutdb::core::PropertyValue;
    using strutdb::core::Result;

    if (node == nullptr) {
        return Result<PropertyValue>{{}, CoreError::UnsupportedAst};
    }
    if (type_is(node, CYPHER_AST_STRING)) {
        return Result<PropertyValue>{copy_cstr(cypher_ast_string_get_value(node)), CoreError::Ok};
    }
    if (type_is(node, CYPHER_AST_INTEGER)) {
        const std::string s = copy_cstr(cypher_ast_integer_get_valuestr(node));
        std::int64_t v = 0;
        const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
        if (ec != std::errc{} || ptr != s.data() + s.size()) {
            return Result<PropertyValue>{{}, CoreError::ParseError};
        }
        return Result<PropertyValue>{v, CoreError::Ok};
    }
    if (type_is(node, CYPHER_AST_FLOAT)) {
        const std::string s = copy_cstr(cypher_ast_float_get_valuestr(node));
        char* end = nullptr;
        const double v = std::strtod(s.c_str(), &end);
        if (end == s.c_str() || end != s.c_str() + s.size()) {
            return Result<PropertyValue>{{}, CoreError::ParseError};
        }
        return Result<PropertyValue>{v, CoreError::Ok};
    }
    if (type_is(node, CYPHER_AST_TRUE)) {
        return Result<PropertyValue>{true, CoreError::Ok};
    }
    if (type_is(node, CYPHER_AST_FALSE)) {
        return Result<PropertyValue>{false, CoreError::Ok};
    }
    if (type_is(node, CYPHER_AST_NULL)) {
        return Result<PropertyValue>{std::monostate{}, CoreError::Ok};
    }
    return Result<PropertyValue>{{}, CoreError::UnsupportedAst};
}

[[nodiscard]] strutdb::core::Result<std::vector<strutdb::core::Property>>
copy_map(const cypher_astnode_t* props) {
    using strutdb::core::CoreError;
    using strutdb::core::Property;
    using strutdb::core::Result;

    std::vector<Property> out;
    if (props == nullptr) {
        return Result<std::vector<Property>>{std::move(out), CoreError::Ok};
    }
    if (type_is(props, CYPHER_AST_PARAMETER)) {
        return Result<std::vector<Property>>{{}, CoreError::UnsupportedAst};
    }
    if (!type_is(props, CYPHER_AST_MAP)) {
        return Result<std::vector<Property>>{{}, CoreError::UnsupportedAst};
    }

    const unsigned int n = cypher_ast_map_nentries(props);
    out.reserve(n);
    for (unsigned int i = 0; i < n; ++i) {
        const cypher_astnode_t* key = cypher_ast_map_get_key(props, i);
        const cypher_astnode_t* val = cypher_ast_map_get_value(props, i);
        if (!type_is(key, CYPHER_AST_PROP_NAME)) {
            return Result<std::vector<Property>>{{}, CoreError::UnsupportedAst};
        }
        auto lit = literal_value(val);
        if (!lit.ok()) {
            return Result<std::vector<Property>>{{}, lit.error};
        }
        out.push_back(Property{copy_cstr(cypher_ast_prop_name_get_value(key)), std::move(lit.value)});
    }
    return Result<std::vector<Property>>{std::move(out), CoreError::Ok};
}

[[nodiscard]] RelDirection copy_direction(enum cypher_rel_direction d) {
    switch (d) {
        case CYPHER_REL_INBOUND:
            return RelDirection::Inbound;
        case CYPHER_REL_BIDIRECTIONAL:
            return RelDirection::Bidirectional;
        case CYPHER_REL_OUTBOUND:
        default:
            return RelDirection::Outbound;
    }
}

[[nodiscard]] strutdb::core::Result<NodePattern> copy_node_pattern(const cypher_astnode_t* node) {
    using strutdb::core::CoreError;
    using strutdb::core::Result;

    NodePattern out;
    if (!type_is(node, CYPHER_AST_NODE_PATTERN)) {
        return Result<NodePattern>{{}, CoreError::UnsupportedAst};
    }
    if (const cypher_astnode_t* id = cypher_ast_node_pattern_get_identifier(node)) {
        out.alias = copy_cstr(cypher_ast_identifier_get_name(id));
    }
    const unsigned int nlabels = cypher_ast_node_pattern_nlabels(node);
    out.labels.reserve(nlabels);
    for (unsigned int i = 0; i < nlabels; ++i) {
        const cypher_astnode_t* label = cypher_ast_node_pattern_get_label(node, i);
        out.labels.push_back(copy_cstr(cypher_ast_label_get_name(label)));
    }
    auto props = copy_map(cypher_ast_node_pattern_get_properties(node));
    if (!props.ok()) {
        return Result<NodePattern>{{}, props.error};
    }
    out.properties = std::move(props.value);
    return Result<NodePattern>{std::move(out), CoreError::Ok};
}

[[nodiscard]] strutdb::core::Result<RelPattern> copy_rel_pattern(const cypher_astnode_t* node) {
    using strutdb::core::CoreError;
    using strutdb::core::Result;

    RelPattern out;
    if (!type_is(node, CYPHER_AST_REL_PATTERN)) {
        return Result<RelPattern>{{}, CoreError::UnsupportedAst};
    }
    out.direction = copy_direction(cypher_ast_rel_pattern_get_direction(node));
    if (const cypher_astnode_t* id = cypher_ast_rel_pattern_get_identifier(node)) {
        out.alias = copy_cstr(cypher_ast_identifier_get_name(id));
    }
    const unsigned int ntypes = cypher_ast_rel_pattern_nreltypes(node);
    out.types.reserve(ntypes);
    for (unsigned int i = 0; i < ntypes; ++i) {
        const cypher_astnode_t* t = cypher_ast_rel_pattern_get_reltype(node, i);
        out.types.push_back(copy_cstr(cypher_ast_reltype_get_name(t)));
    }
    auto props = copy_map(cypher_ast_rel_pattern_get_properties(node));
    if (!props.ok()) {
        return Result<RelPattern>{{}, props.error};
    }
    out.properties = std::move(props.value);
    return Result<RelPattern>{std::move(out), CoreError::Ok};
}

[[nodiscard]] const cypher_astnode_t* unwrap_path(const cypher_astnode_t* path) {
    if (path == nullptr) {
        return nullptr;
    }
    if (type_is(path, CYPHER_AST_NAMED_PATH)) {
        return unwrap_path(cypher_ast_named_path_get_path(path));
    }
    if (type_is(path, CYPHER_AST_SHORTEST_PATH)) {
        return unwrap_path(cypher_ast_shortest_path_get_path(path));
    }
    return path;
}

[[nodiscard]] strutdb::core::Result<PathPattern> copy_path(const cypher_astnode_t* path_node) {
    using strutdb::core::CoreError;
    using strutdb::core::Result;

    const cypher_astnode_t* path = unwrap_path(path_node);
    if (!type_is(path, CYPHER_AST_PATTERN_PATH)) {
        return Result<PathPattern>{{}, CoreError::UnsupportedAst};
    }

    PathPattern out;
    const unsigned int n = cypher_ast_pattern_path_nelements(path);
    for (unsigned int i = 0; i < n; ++i) {
        const cypher_astnode_t* el = cypher_ast_pattern_path_get_element(path, i);
        if ((i % 2U) == 0U) {
            auto node = copy_node_pattern(el);
            if (!node.ok()) {
                return Result<PathPattern>{{}, node.error};
            }
            out.nodes.push_back(std::move(node.value));
        } else {
            auto rel = copy_rel_pattern(el);
            if (!rel.ok()) {
                return Result<PathPattern>{{}, rel.error};
            }
            out.rels.push_back(std::move(rel.value));
        }
    }
    return Result<PathPattern>{std::move(out), CoreError::Ok};
}

[[nodiscard]] strutdb::core::Result<std::vector<PathPattern>>
copy_pattern(const cypher_astnode_t* pattern) {
    using strutdb::core::CoreError;
    using strutdb::core::Result;

    std::vector<PathPattern> out;
    if (!type_is(pattern, CYPHER_AST_PATTERN)) {
        return Result<std::vector<PathPattern>>{{}, CoreError::UnsupportedAst};
    }
    const unsigned int n = cypher_ast_pattern_npaths(pattern);
    out.reserve(n);
    for (unsigned int i = 0; i < n; ++i) {
        auto path = copy_path(cypher_ast_pattern_get_path(pattern, i));
        if (!path.ok()) {
            return Result<std::vector<PathPattern>>{{}, path.error};
        }
        out.push_back(std::move(path.value));
    }
    return Result<std::vector<PathPattern>>{std::move(out), CoreError::Ok};
}

[[nodiscard]] strutdb::core::Result<ReturnItem> copy_return_item(const cypher_astnode_t* proj) {
    using strutdb::core::CoreError;
    using strutdb::core::Result;

    ReturnItem item;
    if (!type_is(proj, CYPHER_AST_PROJECTION)) {
        return Result<ReturnItem>{{}, CoreError::UnsupportedAst};
    }
    if (const cypher_astnode_t* alias = cypher_ast_projection_get_alias(proj)) {
        item.alias = copy_cstr(cypher_ast_identifier_get_name(alias));
    }

    const cypher_astnode_t* expr = cypher_ast_projection_get_expression(proj);
    if (type_is(expr, CYPHER_AST_IDENTIFIER)) {
        item.kind = ReturnItemKind::Identifier;
        item.identifier = copy_cstr(cypher_ast_identifier_get_name(expr));
        if (item.alias.empty()) {
            item.alias = item.identifier;
        }
        return Result<ReturnItem>{std::move(item), CoreError::Ok};
    }
    if (type_is(expr, CYPHER_AST_PROPERTY_OPERATOR)) {
        const cypher_astnode_t* base = cypher_ast_property_operator_get_expression(expr);
        const cypher_astnode_t* prop = cypher_ast_property_operator_get_prop_name(expr);
        if (!type_is(base, CYPHER_AST_IDENTIFIER) || !type_is(prop, CYPHER_AST_PROP_NAME)) {
            return Result<ReturnItem>{{}, CoreError::UnsupportedAst};
        }
        item.kind = ReturnItemKind::Property;
        item.identifier = copy_cstr(cypher_ast_identifier_get_name(base));
        item.property = copy_cstr(cypher_ast_prop_name_get_value(prop));
        if (item.alias.empty()) {
            item.alias = item.identifier + "." + item.property;
        }
        return Result<ReturnItem>{std::move(item), CoreError::Ok};
    }
    auto lit = literal_value(expr);
    if (lit.ok()) {
        item.kind = ReturnItemKind::Literal;
        item.literal = std::move(lit.value);
        return Result<ReturnItem>{std::move(item), CoreError::Ok};
    }
    return Result<ReturnItem>{{}, CoreError::UnsupportedAst};
}

[[nodiscard]] strutdb::core::Result<Clause> copy_clause(const cypher_astnode_t* clause) {
    using strutdb::core::CoreError;
    using strutdb::core::Result;

    Clause out;
    if (type_is(clause, CYPHER_AST_MATCH)) {
        out.kind = ClauseKind::Match;
        out.match.optional = cypher_ast_match_is_optional(clause);
        if (cypher_ast_match_get_predicate(clause) != nullptr) {
            return Result<Clause>{{}, CoreError::UnsupportedAst};
        }
        auto paths = copy_pattern(cypher_ast_match_get_pattern(clause));
        if (!paths.ok()) {
            return Result<Clause>{{}, paths.error};
        }
        out.match.paths = std::move(paths.value);
        return Result<Clause>{std::move(out), CoreError::Ok};
    }
    if (type_is(clause, CYPHER_AST_CREATE)) {
        out.kind = ClauseKind::Create;
        auto paths = copy_pattern(cypher_ast_create_get_pattern(clause));
        if (!paths.ok()) {
            return Result<Clause>{{}, paths.error};
        }
        out.create.paths = std::move(paths.value);
        return Result<Clause>{std::move(out), CoreError::Ok};
    }
    if (type_is(clause, CYPHER_AST_RETURN)) {
        out.kind = ClauseKind::Return;
        out.ret.distinct = cypher_ast_return_is_distinct(clause);
        out.ret.star = cypher_ast_return_has_include_existing(clause);
        if (out.ret.star) {
            ReturnItem star;
            star.kind = ReturnItemKind::Star;
            star.alias = "*";
            out.ret.items.push_back(std::move(star));
        }
        const unsigned int n = cypher_ast_return_nprojections(clause);
        for (unsigned int i = 0; i < n; ++i) {
            auto item = copy_return_item(cypher_ast_return_get_projection(clause, i));
            if (!item.ok()) {
                return Result<Clause>{{}, item.error};
            }
            out.ret.items.push_back(std::move(item.value));
        }
        return Result<Clause>{std::move(out), CoreError::Ok};
    }
    return Result<Clause>{{}, CoreError::UnsupportedAst};
}

}  // namespace

strutdb::core::Result<Query> CypherParser::parse(std::string_view cypher) const {
    using strutdb::core::CoreError;
    using strutdb::core::Result;

    if (cypher.size() > kMaxQueryBytes) {
        return Result<Query>{{}, CoreError::QueryTooLarge};
    }
    if (cypher.find('\0') != std::string_view::npos) {
        return Result<Query>{{}, CoreError::ParseError};
    }

    UniqueParseResult raw{cypher_uparse(cypher.data(), cypher.size(), nullptr, nullptr,
                                        CYPHER_PARSE_ONLY_STATEMENTS | CYPHER_PARSE_SINGLE)};
    if (!raw) {
        return Result<Query>{{}, CoreError::ParseError};
    }
    if (cypher_parse_result_nerrors(raw.get()) > 0U) {
        return Result<Query>{{}, CoreError::ParseError};
    }

    const unsigned int ndirectives = cypher_parse_result_ndirectives(raw.get());
    if (ndirectives == 0U) {
        return Result<Query>{{}, CoreError::ParseError};
    }

    Query query;
    for (unsigned int d = 0; d < ndirectives; ++d) {
        const cypher_astnode_t* directive = cypher_parse_result_get_directive(raw.get(), d);
        if (!type_is(directive, CYPHER_AST_STATEMENT)) {
            return Result<Query>{{}, CoreError::UnsupportedAst};
        }
        const cypher_astnode_t* body = cypher_ast_statement_get_body(directive);
        if (!type_is(body, CYPHER_AST_QUERY)) {
            return Result<Query>{{}, CoreError::UnsupportedAst};
        }
        const unsigned int nclauses = cypher_ast_query_nclauses(body);
        for (unsigned int c = 0; c < nclauses; ++c) {
            auto clause = copy_clause(cypher_ast_query_get_clause(body, c));
            if (!clause.ok()) {
                return Result<Query>{{}, clause.error};
            }
            query.clauses.push_back(std::move(clause.value));
        }
    }

    return Result<Query>{std::move(query), CoreError::Ok};
}

}  // namespace strutdb::parser
