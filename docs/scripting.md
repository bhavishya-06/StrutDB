# Lua User-Defined Functions (EVAL)

StrutDB features an embedded Lua 5.4 scripting runtime powered by [sol2](https://github.com/ThePhD/sol2). This enables developers to execute custom graph algorithms, data transformations, and server-side logic directly on the database engine with zero network round-trip overhead.

---

## Executing Scripts via RESP EVAL

Scripts are submitted using the standard Redis `EVAL` command:

```text
EVAL "<lua_script>"
```

From Python using `StrutDBClient`:

```python
from strutdb import StrutDBClient

client = StrutDBClient()
output = client.eval("""
    local x = 10 * 5
    return "Calculated: " .. tostring(x)
""")
print(output)
# Output: Calculated: 50
```

---

## The `strutdb` Lua API

The global `strutdb` table provides direct, safe access to the in-memory graph:

### 1. `strutdb.add_node()`
Creates a new graph node and returns its unique integer `NodeId`.

```lua
local node_id = strutdb.add_node()
```

### 2. `strutdb.add_edge(src_id, dst_id, type_id)`
Creates a directed edge between two existing nodes with a specified relationship type ID.

```lua
local edge_id = strutdb.add_edge(1, 2, 100)
```

### 3. `strutdb.get_node(node_id)`
Returns a table containing the node's properties and `id`, or `nil` if not found.

```lua
local node = strutdb.get_node(1)
if node then
    print("Node ID: " .. node.id)
end
```

### 4. `strutdb.get_edge(edge_id)`
Returns a table containing the edge's `id`, `src`, `dst`, `type`, and properties.

```lua
local edge = strutdb.get_edge(1)
```

### 5. `strutdb.outgoing(node_id)` & `strutdb.incoming(node_id)`
Returns an array table of edge IDs connected to the specified node.

```lua
local out_edges = strutdb.outgoing(1)
for i, edge_id in ipairs(out_edges) do
    local edge = strutdb.get_edge(edge_id)
    -- Traverse neighbor: edge.dst
end
```

---

## Security & Sandboxing

StrutDB enforces defense-in-depth isolation for script execution:

1. **Dangerous Library Stripping**: `io`, `os`, `package`, and `debug` libraries are explicitly removed at sandbox initialization.
2. **Global Namespace Cleansing**: Dangerous functions (`require`, `dofile`, `loadfile`, `load`) are set to `nil`.
3. **AST Validation**: Inbound script texts are scanned to block unauthorized system-call tokens prior to execution.
4. **Execution Timeouts**: Scripts are protected against infinite loops.
