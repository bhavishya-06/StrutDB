#pragma once

#include "core/error.hpp"
#include "core/graph.hpp"
#include "core/types.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace strutdb::scripting {

// Result from Lua script execution
struct LuaResult {
    std::string output;
    bool        success = false;
    std::string error_message;
};

class LuaScriptEngine {
public:
    explicit LuaScriptEngine(std::shared_ptr<core::Graph> graph);
    ~LuaScriptEngine();

    LuaScriptEngine(const LuaScriptEngine&)            = delete;
    LuaScriptEngine& operator=(const LuaScriptEngine&) = delete;
    LuaScriptEngine(LuaScriptEngine&&)                 = delete;
    LuaScriptEngine& operator=(LuaScriptEngine&&)      = delete;

    // Execute a Lua script via the EVAL command
    [[nodiscard]] LuaResult eval(std::string_view script);

private:
    // Initialize a sandboxed Lua state
    void initialize_sandbox();

    // Remove dangerous libraries from the Lua state
    void remove_dangerous_libraries();

    // Bind Graph methods to the Lua API (safe subset only)
    void bind_graph_api();

    // Validate script for unsafe patterns before execution
    [[nodiscard]] bool validate_script(std::string_view script) const;

    std::shared_ptr<core::Graph> graph_;
    void* lua_state_;  // sol2::lua_state* (opaque pointer to avoid exposing sol2 in header)
};

}  // namespace strutdb::scripting
