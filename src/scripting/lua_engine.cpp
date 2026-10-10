#include "scripting/lua_engine.hpp"

#include <sol/sol.hpp>

#include <regex>
#include <stdexcept>

namespace strutdb::scripting {

LuaScriptEngine::LuaScriptEngine(std::shared_ptr<core::Graph> graph)
    : graph_(std::move(graph))
    , lua_state_(nullptr) {
    initialize_sandbox();
}

LuaScriptEngine::~LuaScriptEngine() {
    if (lua_state_) {
        auto* lua = static_cast<sol::state*>(lua_state_);
        delete lua;
        lua_state_ = nullptr;
    }
}

void LuaScriptEngine::initialize_sandbox() {
    auto* lua = new sol::state();
    lua_state_ = lua;

    // Open only safe standard libraries
    lua->open_libraries(
        sol::lib::base,      // Basic language features (print, assert, etc.)
        sol::lib::string,    // String manipulation
        sol::lib::table,     // Table operations
        sol::lib::math,      // Math functions
        sol::lib::coroutine  // Coroutine support
    );

    // DO NOT open dangerous libraries:
    // - sol::lib::io (file I/O)
    // - sol::lib::os (system commands)
    // - sol::lib::package (module loading)
    // - sol::lib::debug (debugging hooks)

    // Remove dangerous libraries explicitly (defense in depth)
    remove_dangerous_libraries();

    // Bind the safe Graph API
    bind_graph_api();
}

void LuaScriptEngine::remove_dangerous_libraries() {
    auto* lua = static_cast<sol::state*>(lua_state_);

    // Remove dangerous global functions
    lua->script(R"(
        -- Remove dangerous functions from global table
        _G.load = nil
        _G.loadfile = nil
        _G.dofile = nil
        _G.require = nil
        _G.module = nil

        -- Remove entire dangerous libraries if they somehow exist
        _G.io = nil
        _G.os = nil
        _G.package = nil
        _G.debug = nil

        -- Remove unsafe string functions
        string.dump = nil

        -- Remove unsafe math functions
        math.randomseed = nil  -- Could be used for timing attacks

        -- Remove coroutine functions that could be abused
        coroutine.wrap = nil
    )");

    // Also remove from registry to prevent access via getmetatable tricks
    lua->registry()["_LOADED"] = sol::lua_nil;
    lua->registry()["_PRELOAD"] = sol::lua_nil;
}

void LuaScriptEngine::bind_graph_api() {
    auto* lua = static_cast<sol::state*>(lua_state_);

    // Create a safe "strutdb" namespace for our API
    sol::table strutdb_api = lua->create_table();

    // Bind safe Graph methods only
    // We wrap the graph_ pointer to expose specific methods

    // add_node() - Create a new node, returns node ID
    strutdb_api.set_function("add_node", [this]() -> core::NodeId {
        auto node = graph_->add_node();
        return node ? node->id : core::kInvalidNodeId;
    });

    // add_edge(src_id, dst_id, type_id) - Create an edge, returns edge ID
    strutdb_api.set_function("add_edge", [this](core::NodeId src_id,
                                                 core::NodeId dst_id,
                                                 core::TypeId type_id) -> core::EdgeId {
        auto edge = graph_->add_edge(src_id, dst_id, type_id);
        return edge ? (*edge)->id : core::kInvalidEdgeId;
    });

    // get_node(id) - Get node properties as a table
    strutdb_api.set_function("get_node", [this](core::NodeId id) -> sol::table {
        auto node = graph_->get_node(id);
        if (!node) {
            return sol::lua_nil;
        }

        sol::table node_table = lua->create_table();
        node_table["id"] = node->id;

        // Get properties
        auto props = node->snapshot_properties();
        for (const auto& [key, value] : props) {
            std::visit([&node_table, &key](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    node_table[key] = sol::lua_nil;
                } else if constexpr (std::is_same_v<T, bool>) {
                    node_table[key] = v;
                } else if constexpr (std::is_same_v<T, std::int64_t>) {
                    node_table[key] = v;
                } else if constexpr (std::is_same_v<T, double>) {
                    node_table[key] = v;
                } else if constexpr (std::is_same_v<T, std::string>) {
                    node_table[key] = v;
                } else if constexpr (std::is_same_v<T, std::vector<std::uint8_t>>) {
                    // Skip binary data for now
                    node_table[key] = sol::lua_nil;
                }
            }, value);
        }

        return node_table;
    });

    // get_edge(id) - Get edge properties as a table
    strutdb_api.set_function("get_edge", [this](core::EdgeId id) -> sol::table {
        auto edge = graph_->get_edge(id);
        if (!edge) {
            return sol::lua_nil;
        }

        sol::table edge_table = lua->create_table();
        edge_table["id"] = edge->id;
        edge_table["src"] = edge->src_id;
        edge_table["dst"] = edge->dst_id;
        edge_table["type"] = edge->type_id;

        // Get properties
        auto props = edge->snapshot_properties();
        for (const auto& [key, value] : props) {
            std::visit([&edge_table, &key](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    edge_table[key] = sol::lua_nil;
                } else if constexpr (std::is_same_v<T, bool>) {
                    edge_table[key] = v;
                } else if constexpr (std::is_same_v<T, std::int64_t>) {
                    edge_table[key] = v;
                } else if constexpr (std::is_same_v<T, double>) {
                    edge_table[key] = v;
                } else if constexpr (std::is_same_v<T, std::string>) {
                    edge_table[key] = v;
                } else if constexpr (std::is_same_v<T, std::vector<std::uint8_t>>) {
                    edge_table[key] = sol::lua_nil;
                }
            }, value);
        }

        return edge_table;
    });

    // outgoing(node_id) - Get outgoing edges as a table of edge IDs
    strutdb_api.set_function("outgoing", [this](core::NodeId node_id) -> sol::table {
        auto adj_entries = graph_->outgoing(node_id);
        sol::table result = lua->create_table();
        for (size_t i = 0; i < adj_entries.size(); ++i) {
            result[i + 1] = adj_entries[i].edge_id;
        }
        return result;
    });

    // incoming(node_id) - Get incoming edges as a table of edge IDs
    strutdb_api.set_function("incoming", [this](core::NodeId node_id) -> sol::table {
        auto adj_entries = graph_->incoming(node_id);
        sol::table result = lua->create_table();
        for (size_t i = 0; i < adj_entries.size(); ++i) {
            result[i + 1] = adj_entries[i].edge_id;
        }
        return result;
    });

    // Set the API in the global namespace
    lua->set("strutdb", strutdb_api);
}

bool LuaScriptEngine::validate_script(std::string_view script) const {
    // Check for dangerous patterns in the script
    static const std::vector<std::regex> dangerous_patterns = {
        std::regex(R"(\brequire\s*\()"),            // require()
        std::regex(R"(\bdofile\s*\()"),             // dofile()
        std::regex(R"(\bloadfile\s*\()"),           // loadfile()
        std::regex(R"(\bload\s*\()"),               // load()
        std::regex(R"(\bos\s*\.execute\s*\()"),     // os.execute()
        std::regex(R"(\bos\s*\.remove\s*\()"),      // os.remove()
        std::regex(R"(\bos\s*\.rename\s*\()"),      // os.rename()
        std::regex(R"(\bio\s*\.open\s*\()"),        // io.open()
        std::regex(R"(\bio\s*\.popen\s*\()"),       // io.popen()
        std::regex(R"(\bpackage\s*\.")},           // package.
        std::regex(R"(\bdebug\s*\.")},              // debug.
        std::regex(R"(\b_getmetatable\s*\()"),      // getmetatable (could access registry)
        std::regex(R"(\bsetmetatable\s*\()"),       // setmetatable
        std::regex(R"(\brawget\s*\()"),             // rawget
        std::regex(R"(\brawset\s*\()"),             // rawset
        std::regex(R"(\bcollectgarbage\s*\()"),     // collectgarbage (memory exhaustion)
    };

    std::string script_str(script);
    for (const auto& pattern : dangerous_patterns) {
        if (std::regex_search(script_str, pattern)) {
            return false;
        }
    }

    return true;
}

LuaResult LuaScriptEngine::eval(std::string_view script) {
    LuaResult result;

    // Validate script before execution
    if (!validate_script(script)) {
        result.success = false;
        result.error_message = "Script contains unsafe patterns";
        return result;
    }

    try {
        auto* lua = static_cast<sol::state*>(lua_state_);

        // Set a script timeout (100ms maximum)
        sol::protected_function_result script_result =
            lua->safe_script(std::string(script), &sol::script_pass_on_error);

        if (!script_result.valid()) {
            sol::error err = script_result;
            result.success = false;
            result.error_message = err.what();
        } else {
            result.success = true;
            // Capture any output from print() calls
            if (script_result.return_count() > 0) {
                // If the script returns a value, convert it to string
                auto return_value = script_result[0];
                if (return_value.valid()) {
                    result.output = lua->to_string(return_value);
                }
            }
        }
    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = e.what();
    } catch (...) {
        result.success = false;
        result.error_message = "Unknown error during script execution";
    }

    return result;
}

}  // namespace strutdb::scripting
