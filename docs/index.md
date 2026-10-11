# Welcome to StrutDB

**StrutDB** is a high-performance in-memory graph database engineered in native modern C++20. It combines in-memory adjacency index structures, embedded RocksDB persistence, open Cypher query execution, and sandboxed Lua scripting with an asynchronous Redis RESP event loop.

---

## Core Capabilities

- **Ultra-Low Latency Traversals**: Lock-free & sharded hash map adjacency indexes designed for multi-hop pattern matching.
- **Durable Persistence**: Backed by embedded RocksDB LSM-trees for crash resilience and zero data loss across container restarts.
- **Open Cypher Support**: Declarative graph pattern queries parsing standard `MATCH`, `CREATE`, and `RETURN` clauses.
- **Custom Lua UDFs**: Extensible user-defined algorithms and graph traversals via sol2 and Redis `EVAL`.
- **RESP Network Protocol**: Direct compatibility with any Redis client driver across Python, Go, Node.js, and Java.

---

## Quickstart

### Launch with Docker

```bash
docker run -d -p 6379:6379 \
  -v $(pwd)/strutdb-data:/var/lib/strutdb \
  --name strutdb-server strutdb:latest
```

### Connect using Python SDK

```python
from strutdb import StrutDBClient

with StrutDBClient(host="localhost", port=6379) as client:
    # Verify connectivity
    print("Ping:", client.ping())

    # Create nodes and relationships
    client.execute("CREATE (u1:User {name: 'Alice'}), (u2:User {name: 'Bob'}), (u1)-[:KNOWS]->(u2)")

    # Execute traversal
    rows = client.execute("MATCH (a:User)-[:KNOWS]->(b:User) RETURN a.name, b.name")
    print("Found relationship:", rows)
```

---

## System Requirements

- **Linux / Docker**: Ubuntu 22.04+ or any Docker-compatible Linux environment.
- **Compiler**: C++20 capable compiler (`GCC 13+` or `Clang 16+`).
- **Dependencies**: Ninja, CMake 3.22+, pthreads.
