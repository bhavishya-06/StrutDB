# Custom Lua Algorithms & Server-Side UDFs

StrutDB features an embedded **Lua 5.4** runtime powered by modern C++ bindings via [sol2](https://github.com/ThePhD/sol2). 

This capability enables developers to execute complex graph algorithms, custom scoring heuristics, pathfinding logic, and atomic transactional mutations directly inside the database process, eliminating network latency.

---

## 1. Why Lua? The Data Locality Imperative

In graph analytics, client-side querying suffers from the **Latency Penalty of Remote Traversals**. 

Consider an algorithm that needs to evaluate a 4-hop neighborhood to compute a personalized recommendation score:

```
+----------------------------------------------------------------------------------------------------+
|                               CLIENT-SIDE TRAVERSAL (THE WRONG WAY)                                |
|                                                                                                    |
|   Hop 1: Client queries neighbors of Node A.       ---> Network Round-Trip 1 (5ms)                 |
|   Hop 2: Client queries neighbors of 10 nodes.     ---> Network Round-Trip 2 (15ms)                |
|   Hop 3: Client queries neighbors of 100 nodes.    ---> Network Round-Trip 3 (45ms)                |
|   Hop 4: Client queries neighbors of 1000 nodes.   ---> Network Round-Trip 4 (120ms)               |
|                                                                                                    |
|   Total Latency: ~185ms  |  Wire Overhead: Megabytes of intermediate IDs serialized over TCP       |
+----------------------------------------------------------------------------------------------------+
                                                vs.
+----------------------------------------------------------------------------------------------------+
|                               STRUTDB IN-PROCESS LUA (THE RIGHT WAY)                               |
|                                                                                                    |
|   Client sends Lua script to StrutDB via Redis EVAL:                                               |
|   [Client] --(Single EVAL command)----------------------------------------> [StrutDB Server]      |
|                                                                                   |                |
|   In-Memory Engine (C++ / sol2):                                                  |                |
|   - Traverses Hops 1, 2, 3, and 4 in C++ memory via direct pointers               |                |
|   - Evaluates adjacency lists in L1/L2 cache (< 0.2ms total)                      |                |
|   - Computes final recommendation score                                           |                |
|                                                                                   v                |
|   [Client] <--(Returns Single Computed Result Float/String)---------------- [StrutDB Server]      |
|                                                                                                    |
|   Total Latency: ~1.2ms (99% Reduction!)  |  Wire Overhead: Exactly 1 Network Round-Trip           |
+----------------------------------------------------------------------------------------------------+
```

By bringing the computation to the data rather than shipping the graph across the network, StrutDB achieves sub-millisecond execution times for graph heuristics that would otherwise cripple microservice architectures.

---

## 2. The `strutdb` Lua API Specification

Within the embedded Lua environment, StrutDB exposes the global `strutdb` table. Every function maps directly to C++ engine methods.

### 1. `strutdb.add_node() -> integer`
Allocates a new vertex in the in-memory graph and returns its globally unique 64-bit integer `NodeId`.

```lua
local node_id = strutdb.add_node()
-- Returns: integer (e.g. 1)
```

### 2. `strutdb.add_edge(src_id, dst_id, type_id) -> integer`
Creates a directed edge from `src_id` to `dst_id` with the specified relationship `type_id`. Returns the unique `EdgeId`.

```lua
local edge_id = strutdb.add_edge(1, 2, 100)
-- Returns: integer (e.g. 1)
```

### 3. `strutdb.outgoing(node_id) -> table`
Retrieves an array of all outgoing edge IDs originating from `node_id`.

```lua
local out_edges = strutdb.outgoing(1)
-- Returns: table array of integers, e.g. {1, 4, 9}
```

### 4. `strutdb.incoming(node_id) -> table`
Retrieves an array of all incoming edge IDs terminating at `node_id`.

```lua
local in_edges = strutdb.incoming(2)
-- Returns: table array of integers
```

### 5. `strutdb.get_node(node_id) -> table or nil`
Returns a table containing the node's attributes, or `nil` if the node does not exist.

```lua
local node = strutdb.get_node(1)
if node then
    print("Node ID:", node.id)
end
```

### 6. `strutdb.get_edge(edge_id) -> table or nil`
Returns a table containing `{id, src, dst, type}` for the specified edge, or `nil`.

```lua
local edge = strutdb.get_edge(1)
if edge then
    print("Edge connects " .. edge.src .. " to " .. edge.dst)
end
```

---

## 3. Heavily Commented Algorithm Example: Multi-Hop Influence Scorer

Below is an annotated Lua algorithm demonstrating how to execute a breadth-first search (BFS) up to a configurable depth, calculate an influence score based on topological decay, and return a formatted summary:

```lua
-- ============================================================================
-- StrutDB Custom Lua Algorithm: Multi-Hop Influence & Centrality Scorer
-- ============================================================================

-- Step 1: Define algorithm parameters
local start_node_id = 1      -- Target seed node for influence calculation
local max_depth = 2          -- Maximum traversal depth (2-hop neighborhood)
local decay_factor = 0.5     -- Exponential decay factor per hop distance

-- Step 2: Initialize traversal state tables
local visited = {}           -- Set to track already visited node IDs (prevents cycles)
local queue = {}             -- Queue for Breadth-First Search (BFS)
local total_influence = 0.0  -- Aggregated influence score
local reached_nodes = 0      -- Total unique nodes reached

-- Step 3: Mark starting node as visited (distance 0)
visited[start_node_id] = true

-- Step 4: Seed queue with initial outgoing neighbors (depth 1)
local initial_edges = strutdb.outgoing(start_node_id)
for _, edge_id in ipairs(initial_edges) do
    local edge = strutdb.get_edge(edge_id)
    if edge and not visited[edge.dst] then
        visited[edge.dst] = true
        table.insert(queue, { node_id = edge.dst, depth = 1 })
    end
end

-- Step 5: Process BFS queue
local head = 1
while head <= #queue do
    -- Dequeue candidate vertex
    local current = queue[head]
    head = head + 1

    local current_node = current.node_id
    local current_depth = current.depth

    -- Accumulate decayed influence score: (1.0 / (depth * decay))
    total_influence = total_influence + (1.0 * (decay_factor ^ (current_depth - 1)))
    reached_nodes = reached_nodes + 1

    -- Explore next-hop neighbors if below maximum depth
    if current_depth < max_depth then
        local next_edges = strutdb.outgoing(current_node)
        for _, next_edge_id in ipairs(next_edges) do
            local next_edge = strutdb.get_edge(next_edge_id)
            if next_edge and not visited[next_edge.dst] then
                visited[next_edge.dst] = true
                table.insert(queue, {
                    node_id = next_edge.dst,
                    depth = current_depth + 1
                })
            end
        end
    end
end

-- Step 6: Return formatted result string to the client
return string.format(
    "Influence Analysis: Node %d reached %d vertices across %d hops with total score %.2f",
    start_node_id, reached_nodes, max_depth, total_influence
)
```

---

## 4. Executing Lua from Python (`StrutDBClient`)

You can execute Lua scripts using the Python SDK's `eval()` method or via the standard Redis `execute("EVAL", ...)` command.

### Using `client.eval()`

```python
from strutdb import StrutDBClient

lua_script = """
    local x = 42
    local y = 58
    return string.format("Sum computed inside StrutDB: %d", x + y)
"""

with StrutDBClient(host="localhost", port=6379) as client:
    result = client.eval(lua_script)
    print("Result:", result)
    # Output: Result: Sum computed inside StrutDB: 100
```

### Complete End-to-End Graph Script Execution

```python
from strutdb import StrutDBClient

def test_lua_graph_algorithm():
    with StrutDBClient(host="localhost", port=6379) as client:
        print("[1] Building test graph via server-side Lua...")
        
        setup_script = """
            -- Create 4 nodes forming a diamond topology:
            --     (1) -> (2) -> (4)
            --      |      ^
            --      v      |
            --     (3) ----+
            local n1 = strutdb.add_node()
            local n2 = strutdb.add_node()
            local n3 = strutdb.add_node()
            local n4 = strutdb.add_node()

            strutdb.add_edge(n1, n2, 1)
            strutdb.add_edge(n1, n3, 1)
            strutdb.add_edge(n3, n2, 1)
            strutdb.add_edge(n2, n4, 1)

            return {n1, n2, n3, n4}
        """
        node_ids = client.eval(setup_script)
        print(f"    Created node IDs: {node_ids}")

        print("\n[2] Executing degree-centrality scoring algorithm...")
        scoring_script = f"""
            local target = {node_ids[0]}
            local out_edges = strutdb.outgoing(target)
            local degree = #out_edges
            
            local second_hop_count = 0
            for _, edge_id in ipairs(out_edges) do
                local edge = strutdb.get_edge(edge_id)
                if edge then
                    local next_out = strutdb.outgoing(edge.dst)
                    second_hop_count = second_hop_count + #next_out
                end
            end

            return string.format(
                "Node %d has out-degree %d and reaches %d downstream edges",
                target, degree, second_hop_count
            )
        """
        output = client.eval(scoring_script)
        print(f"    Algorithm Output: {output}")

if __name__ == "__main__":
    test_lua_graph_algorithm()
```

Output:
```text
[1] Building test graph via server-side Lua...
    Created node IDs: [1, 2, 3, 4]

[2] Executing degree-centrality scoring algorithm...
    Algorithm Output: Node 1 has out-degree 2 and reaches 2 downstream edges
```

---

## 5. Security & Hardened Sandboxing Architecture

Allowing user-submitted code inside a database engine requires strict isolation. StrutDB enforces multiple layers of defense:

```
[Inbound Script String via EVAL]
               |
               v
[1. Static Security Token Validator]
     - Scans for forbidden tokens ("require", "dofile", "loadfile", "os.", "io.", "debug.")
     - Rejects query immediately if violations are found
               |
               v
[2. sol2 Sandbox State Creation]
     - Strips dangerous base libraries (io, os, package, debug)
     - Overwrites dangerous globals to nil (require, dofile, load)
               |
               v
[3. Scoped 'strutdb' Global Table]
     - Binds strictly validated C++ accessors
     - Enforces const-correctness and bounds-checking
               |
               v
[4. Protected Execution (sol2::protected_function)]
     - Traps runtime errors and Lua panics safely
     - Returns clean RESP Error frames (-ERR <lua_error>) to client
```

### 1. Library Stripping
The Lua environment initializes only safe standard libraries (`base`, `string`, `table`, `math`). The following libraries are omitted:
- `io`: Completely removed (no filesystem access).
- `os`: Completely removed (no system calls or process execution).
- `debug`: Completely removed (no stack inspection or reflection).
- `package`: Completely removed (no dynamic module loading).

### 2. Global Namespace Cleansing
Dangerous reflection and loader functions are explicitly assigned `nil`:
```cpp
lua["require"]  = sol::nil;
lua["dofile"]   = sol::nil;
lua["loadfile"] = sol::nil;
lua["load"]     = sol::nil;
```

### 3. Static Token Scanning
Prior to compilation, the incoming script text is inspected for unauthorized keywords or library access patterns. Any attempt to invoke `require` or manipulate OS primitives is rejected with an immediate error before the Lua virtual machine compiles the bytecode.

### 4. Exception Trapping
All script invocations use `sol::protected_function`. Syntax errors, arithmetic div-by-zero, or missing table keys are trapped within C++ `try/catch` boundaries and converted into clean RESP error messages without risking server crashes.
