# StrutDB

**A High-Performance Native C++20 Graph Database**

StrutDB is an in-memory, high-throughput graph database engineered from the ground up in modern C++20. Designed for low-latency graph traversals and real-time relationship querying, StrutDB combines in-memory adjacency list indexes with persistent LSM-tree storage, open Cypher query execution, sandboxed Lua User-Defined Functions (UDFs), and a non-blocking asynchronous event loop implementing the Redis RESP protocol.

---

## Key Features

- **Blazing Fast In-Memory Engine**: Lock-free & fine-grained concurrent sharded hash tables with adjacency lists designed for sub-millisecond graph traversals.
- **Embedded RocksDB Persistence**: ACID-compliant LSM-tree backing store ensuring graph durability across restarts without sacrificing in-memory read throughput.
- **Declarative Cypher AST Parser**: Native execution of Cypher patterns (`MATCH`, `CREATE`, `RETURN`, multi-hop traversals) powered by recursive AST parsing.
- **Sandboxed Lua Scripting (sol2)**: Dynamic User-Defined Functions (UDFs) and graph algorithms executed safely in a sandboxed Lua 5.4 environment via Redis `EVAL`.
- **Asynchronous RESP Network Layer**: High-concurrency Linux epoll event loop accepting standard Redis RESP protocol connections over port `6379`.
- **Thin Python SDK (`strutdb-py`)**: Ergonomic developer client library enabling one-line query and script execution.

---

## 1-Command Docker Quickstart

Launch StrutDB with persistent volume storage in seconds:

```bash
docker run -d -p 6379:6379 -v $(pwd)/strutdb-data:/var/lib/strutdb --name strutdb-server strutdb:latest
```

Or using Docker Compose:

```bash
docker compose up -d
```

Verify connectivity using any Redis CLI or `nc`:

```bash
redis-cli -p 6379 PING
# Output: PONG
```

---

## Python SDK Quickstart

Install dependencies and connect to StrutDB using the native Python client:

```python
from strutdb import StrutDBClient

# Initialize connection (localhost:6379)
client = StrutDBClient(host="localhost", port=6379)

# 1. Health check
print("Ping:", client.ping())

# 2. Create nodes and relationships via Cypher
client.execute("CREATE (a:User {name: 'Alice'}), (b:User {name: 'Bob'}), (a)-[:KNOWS]->(b)")

# 3. Query multi-hop relationships
results = client.execute("MATCH (a:User)-[:KNOWS]->(b:User) RETURN a.name, b.name")
print("Relationships:", results)
# Output: [['Alice', 'Bob']]

# 4. Execute sandboxed Lua algorithms via EVAL
lua_script = """
    local n1 = strutdb.add_node()
    local n2 = strutdb.add_node()
    local edge = strutdb.add_edge(n1, n2, 1)
    return "Created edge " .. tostring(edge) .. " between nodes " .. tostring(n1) .. " and " .. tostring(n2)
"""
result = client.eval(lua_script)
print("Lua Output:", result)

client.close()
```

---

## Architecture Overview

```
                      +-----------------------------+
                      |   Client (RESP / redis-py)  |
                      +--------------+--------------+
                                     |
                                     v
                      +-----------------------------+
                      |     Linux epoll Server      |
                      |        (Port 6379)          |
                      +--------------+--------------+
                                     |
           +-------------------------+-------------------------+
           |                                                   |
           v                                                   v
+---------------------+                             +---------------------+
| Cypher AST Parser   |                             |   sol2 Lua Engine   |
| (libcypher-parser)  |                             | (Sandboxed EVAL)    |
+----------+----------+                             +----------+----------+
           |                                                   |
           +-------------------------+-------------------------+
                                     |
                                     v
                      +-----------------------------+
                      |    Execution Engine (BFS)   |
                      +--------------+--------------+
                                     |
                                     v
                      +-----------------------------+
                      |  In-Memory Sharded Graph    |
                      |   (Adjacency Lists & KV)    |
                      +--------------+--------------+
                                     |
                                     v
                      +-----------------------------+
                      |    RocksDB Storage Engine   |
                      |    (/var/lib/strutdb LSM)   |
                      +-----------------------------+
```

---

## License

Apache 2.0 License.
