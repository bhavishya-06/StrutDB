# StrutDB

<div align="center">

![StrutDB Banner](https://img.shields.io/badge/StrutDB-Graph%20Database-indigo?style=for-the-badge&logo=database)
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen?style=for-the-badge&logo=github-actions&logoColor=white)](https://github.com/bhavishya-06/StrutDB/actions)
[![Release](https://img.shields.io/badge/release-v1.0.0-blue?style=for-the-badge&logo=git&logoColor=white)](https://github.com/bhavishya-06/StrutDB/releases)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-20-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white)](https://en.cppreference.com/w/cpp/20)
[![Protocol](https://img.shields.io/badge/protocol-Redis%20RESP2-DC382D?style=for-the-badge&logo=redis&logoColor=white)](https://redis.io/docs/reference/protocol-spec/)
[![Storage](https://img.shields.io/badge/storage-RocksDB%20LSM-1f2937?style=for-the-badge)](https://rocksdb.org/)
[![License](https://img.shields.io/badge/license-Apache%202.0-orange?style=for-the-badge)](LICENSE)

**A high-performance native C++20 graph database with RocksDB persistence, openCypher AST query execution, sandboxed Lua User-Defined Functions, and Redis RESP protocol compatibility.**

[Documentation](https://bhavishya-06.github.io/StrutDB/) • [Quickstart](#the-ultimate-quickstart-guide) • [Architecture](#system-architecture) • [Python SDK](#python-sdk-integration) • [Cypher Querying](#cypher-querying) • [Lua Scripting](#lua-user-defined-functions)

</div>

---

## Table of Contents

- [Overview](#overview)
- [Why StrutDB?](#why-strutdb)
- [Core Capabilities](#core-capabilities)
- [System Architecture](#system-architecture)
- [The Ultimate Quickstart Guide](#the-ultimate-quickstart-guide)
  - [Step 1: Clone the Repository](#step-1-clone-the-repository)
  - [Step 2: Build the Container Image](#step-2-build-the-container-image)
  - [Step 3: Launch with Docker Compose](#step-3-launch-with-docker-compose)
  - [Step 4: Install the Python SDK](#step-4-install-the-python-sdk)
  - [Step 5: Run Your First Graph Application](#step-5-run-your-first-graph-application)
- [Python SDK Integration](#python-sdk-integration)
- [Cypher Querying](#cypher-querying)
- [Lua User-Defined Functions](#lua-user-defined-functions)
- [Wire Protocol Compatibility](#wire-protocol-compatibility)
- [Roadmap & Release Milestones](#roadmap--release-milestones)
- [License](#license)

---

## Overview

**StrutDB** is an enterprise-grade, in-memory graph database engineered in modern C++20. Designed for ultra-low latency graph traversals, real-time relationship querying, and high-concurrency transactional workloads, StrutDB unifies lock-free in-memory adjacency list indexes with persistent Log-Structured Merge-tree (LSM) disk storage via embedded RocksDB.

By implementing the Redis Serialization Protocol (RESP2) on top of an asynchronous Linux `epoll` event loop, StrutDB can be accessed natively by standard Redis client libraries in any programming language, eliminating the need for bulky custom database drivers.

---

## Why StrutDB?

### The Relational Join Collapse

Traditional relational databases (RDBMS) model relationships through foreign keys and join tables. When evaluating deep or cyclic queries—such as multi-hop social graphs, fraud rings, identity graphs, or supply-chain dependencies—relational databases must execute multiple nested table scans and index joins:

$$\text{Join Complexity} = \mathcal{O}(N_1 \times N_2 \times \dots \times N_k)$$

As the depth of the graph traversal increases ($k \ge 3$), relational engines suffer from catastrophic Cartesian explosion, high buffer-pool contention, and steep latency penalties.

### Index-Free Adjacency

StrutDB implements **Index-Free Adjacency (IFA)**. In StrutDB, nodes maintain direct memory references to their incoming and outgoing edges using Compressed Sparse Row (CSR) style adjacency vectors. 

Traversing from a node to its neighbor does **not** perform an index lookup:

$$\text{Traversal Complexity} = \mathcal{O}(\text{degree}(v))$$

Whether your dataset has one thousand nodes or one billion nodes, traversing a relationship takes sub-microsecond time because each edge lookup resolves directly through contiguous memory pointers.

### Eliminating the N+1 Query Tax with Server-Side Lua

In traditional architectures, multi-hop traversals often force microservices to execute N+1 database queries across the network:
1. Client fetches entity $A$.
2. Client queries for all edges of $A$ ($N$ items).
3. Client fires $N$ subsequent network requests for each neighbor $B_i$.

StrutDB completely eliminates this latency tax by running an embedded, sandboxed **Lua 5.4 runtime (sol2)** directly inside the database process. Developers can upload graph algorithms via Redis `EVAL`, execute custom traversals or centrality heuristics against memory in $\mathcal{O}(1)$ hops, and return the aggregated result in a single network round-trip.

---

## Core Capabilities

### 1. High-Performance C++20 Core
- **64-Shard Concurrent Hash Maps**: The graph topology is partitioned across 64 discrete shards. Concurrent read operations traverse shards without global lock contention, while write operations lock only the target shard.
- **CSR-Style Adjacency Lists**: Dedicated incoming and outgoing edge vectors store lightweight node and edge identifiers, preventing cyclic pointer references and memory fragmentation.
- **Strict Two-Phase Lock Protocol**: Thread-safe synchronization enforced via `std::shared_mutex`. Reader threads acquire shared access while graph modifications acquire exclusive access, eliminating data races and deadlocks.

### 2. Embedded RocksDB Durability
- **ACID-Compliant LSM Storage**: Graph updates are persisted to an embedded RocksDB instance.
- **Write-Ahead Logging (WAL)**: Every node, edge, and property modification is written to the WAL before being committed to memory, ensuring crash consistency and durability across restarts.
- **Zero-Loss Docker Volume**: Automatically persists state to the host filesystem (`./strutdb-data` $\to$ `/var/lib/strutdb`).

### 3. Declarative openCypher Query Engine
- **AST Parsing with `libcypher-parser`**: Parses standard openCypher queries into structured Abstract Syntax Trees.
- **Expressive Clause Support**: Supports declarative graph mutations and queries via `CREATE`, `MATCH`, and `RETURN`.
- **Breadth-First Search (BFS) Traversal**: Execution engine maps pattern paths to breadth-first traversals across in-memory adjacency indices, populating variable binding contexts with zero heap overhead.

### 4. Sandboxed Lua Scripting Runtime
- **Modern C++ Bindings via `sol2`**: Embedded Lua 5.4 runtime providing seamless C++ and Lua interoperability.
- **Native `strutdb` Lua API**: Built-in methods (`strutdb.add_node()`, `strutdb.add_edge()`, `strutdb.outgoing()`, `strutdb.incoming()`, `strutdb.get_node()`, `strutdb.get_edge()`) for high-speed algorithmic execution.
- **Hardened Sandbox**: Strips dangerous system libraries (`os`, `io`, `debug`, `package`) and enforces static token verification before compiling scripts.

### 5. Asynchronous Linux `epoll` Network Server
- **Non-Blocking I/O Multiplexing**: Handles thousands of concurrent client connections with low CPU overhead.
- **RESP2 Wire Protocol**: Full compatibility with Redis commands (`PING`, `GET`, `SET`, `DEL`, `EVAL`) and openCypher queries passed over plain Redis sockets.

---

## System Architecture

```
                                    +------------------------------------------+
                                    |       Client Layer (Any Language)        |
                                    |   Python, Go, Node.js, Rust, redis-cli   |
                                    +--------------------+---------------------+
                                                         |
                                                         | TCP (Port 6379, RESP2)
                                                         v
+-------------------------------------------------------------------------------------------------------------------+
| StrutDB Server Process                                                                                            |
|                                                                                                                   |
|  +-------------------------------------------------------------------------------------------------------------+  |
|  | Network Layer (Linux epoll Non-Blocking Event Loop)                                                         |  |
|  | - Level-Triggered Socket Multiplexing                                                                       |  |
|  | - Zero-Copy Ring Buffers (ClientSession)                                                                    |  |
|  | - Streaming RESP State-Machine Parser                                                                       |  |
|  +-----------------------+-------------------------------------------------------------+-----------------------+  |
|                          |                                                             |                          |
|         Cypher Command   |                                            Redis EVAL       |         Key-Value        |
|                          v                                                             v                          v
|  +--------------------------------+                         +----------------------------------+  +------------+  |
|  | Query Parsing Layer            |                         | Sandboxed Lua Scripting Engine   |  | KV Engine  |  |
|  | - libcypher-parser             |                         | - Lua 5.4 / sol2 Modern Bindings |  | - GET      |  |
|  | - AST Node & Pattern Extract   |                         | - Security Token Validator       |  | - SET      |  |
|  +---------------+----------------+                         +----------------+-----------------+  | - DEL      |  |
|                  |                                                           |                    +-----+------+  |
|                  v                                                           v                          |         |
|  +---------------------------------------------------------------------------------------------+        |         |
|  | Execution Engine                                                                            |        |         |
|  | - Breadth-First Pattern Matching (BFS)                                                      |        |         |
|  | - Variable Context Binding (Row / Column Projection)                                        |        |         |
|  +-----------------------------------------------+---------------------------------------------+        |         |
|                                                  |                                                      |         |
|                                                  v                                                      v         |
|  +-------------------------------------------------------------------------------------------------------------+  |
|  | In-Memory Graph Core Engine                                                                                 |  |
|  | - 64-Shard Concurrent Hash Maps (Nodes & Edges)                                                             |  |
|  | - Lock Protocol: std::shared_mutex (Reader/Writer) -> Shard Mutexes                                         |  |
|  | - CSR Adjacency Index: Inverted Incoming & Outgoing Edge ID Tables                                          |  |
|  +-----------------------------------------------+-------------------------------------------------------------+  |
|                                                  |                                                                |
|                                                  v Synchronous Flush / WAL                                        |
|  +-------------------------------------------------------------------------------------------------------------+  |
|  | Persistent Storage Engine (RocksDB)                                                                         |  |
|  | - LSM-Tree Storage Engine mounted at /var/lib/strutdb                                                       |  |
|  | - Write-Ahead Logging (WAL) + SSTables + Block Cache                                                        |  |
|  +-------------------------------------------------------------------------------------------------------------+  |
+-------------------------------------------------------------------------------------------------------------------+
```

---

## The Ultimate Quickstart Guide

This over-explained, foolproof guide walks through setting up StrutDB from scratch on any machine with Docker and Python installed.

### Step 1: Clone the Repository

Begin by cloning the StrutDB repository to your local machine:

```bash
git clone https://github.com/bhavishya-06/StrutDB.git
cd StrutDB
```

Verify your project structure:
```bash
ls -la
# You will see CMakeLists.txt, Dockerfile, docker-compose.yml, src/, clients/, and docs/
```

### Step 2: Build the Container Image

StrutDB uses a multi-stage Dockerfile to ensure that the final production image is lightweight and completely stripped of compilation bloat:
1. **Builder Stage**: Installs `gcc-13`, `g++-13`, `cmake`, and `ninja-build`. It compiles `libcypher-parser` and `sol2`/`Lua 5.4` dependencies, then builds the C++20 database binary with maximum compiler optimizations (`-O3`).
2. **Runtime Stage**: Creates a clean, minimal Debian Bookworm runner containing only the required shared runtime libraries and the compiled `strutdb` executable.

Run the build command:

```bash
docker build -t strutdb:latest .
```

> **Why do we do this?** Compiling inside Docker guarantees that the C++20 compiler flags, standard library versions, and platform-specific POSIX `epoll` headers match the deployment environment identically, eliminating any host OS mismatch.

### Step 3: Launch with Docker Compose

Run the database in the background using Docker Compose:

```bash
docker compose up -d
```

Let's inspect what happens under the hood in `docker-compose.yml`:
- **Image**: Runs `strutdb:latest`.
- **Port Mapping (`6379:6379`)**: Binds port 6379 on your host machine to port 6379 inside the container.
- **Volume Mount (`./strutdb-data:/var/lib/strutdb`)**: Creates a local directory named `strutdb-data/` on your host. StrutDB directs RocksDB to write all MemTable flushes and Write-Ahead Logs here. Even if the container is stopped or destroyed, your graph data remains safe on your host disk.

Verify that the container is running and healthy:

```bash
docker ps --filter "name=strutdb-server"
```

You can test connectivity immediately with `redis-cli` or `nc`:

```bash
redis-cli -p 6379 PING
# Expected Response: PONG
```

### Step 4: Install the Python SDK

StrutDB provides an official Python SDK (`strutdb-py`) located in `clients/python`. Install it into your Python environment:

```bash
pip install ./clients/python
```

> **What does this do?** This installs the `strutdb` package into your active Python environment. It bundles `StrutDBClient`, which handles socket connection pooling, automatic command formatting, RESP parsing, and exception mapping.

### Step 5: Run Your First Graph Application

Create a Python script named `quickstart.py`:

```python
from strutdb import StrutDBClient

def main():
    print("Connecting to StrutDB...")
    
    # Initialize connection to localhost:6379
    with StrutDBClient(host="localhost", port=6379) as client:
        # 1. Health check
        assert client.ping(), "Could not connect to StrutDB"
        print(" Connected to StrutDB successfully!\n")

        # 2. Create Graph Entities via openCypher
        print("Creating User nodes and relationship...")
        client.execute("CREATE (u1:User {name: 'Alice'}), (u2:User {name: 'Bob'})")
        client.execute("MATCH (a:User {name: 'Alice'}), (b:User {name: 'Bob'}) CREATE (a)-[:KNOWS]->(b)")
        print(" Nodes & Edge created in memory!\n")

        # 3. Query Relationships via openCypher Pattern Matching
        print("Traversing graph using MATCH query...")
        results = client.execute("MATCH (a:User)-[:KNOWS]->(b:User) RETURN a.name, b.name")
        print(f" Traversal Result: {results}\n")

        # 4. Execute a Custom Graph Algorithm via Server-Side Lua
        print("Running server-side Lua algorithm...")
        lua_script = """
            -- Create two dynamic nodes
            local n1 = strutdb.add_node()
            local n2 = strutdb.add_node()
            
            -- Connect with relationship type 100
            local edge = strutdb.add_edge(n1, n2, 100)
            
            -- Query outgoing degree
            local outgoing = strutdb.outgoing(n1)
            
            return string.format("Created edge %d between %d and %d. Degree: %d", edge, n1, n2, #outgoing)
        """
        lua_output = client.eval(lua_script)
        print(f" Lua Execution Output: {lua_output}")

if __name__ == "__main__":
    main()
```

Run the script:

```bash
python quickstart.py
```

Expected output:
```text
Connecting to StrutDB...
 Connected to StrutDB successfully!

Creating User nodes and relationship...
 Nodes & Edge created in memory!

Traversing graph using MATCH query...
 Traversal Result: [['Alice', 'Bob']]

Running server-side Lua algorithm...
 Lua Execution Output: Created edge 2 between 3 and 4. Degree: 1
```

---

## Python SDK Integration

The `StrutDBClient` provides a clean, thread-safe, and ergonomic API:

```python
from strutdb import StrutDBClient

# Explicit client management
client = StrutDBClient(
    host="localhost",
    port=6379,
    db=0,
    decode_responses=True
)

try:
    # Execute any Cypher query
    records = client.execute("MATCH (n:User) RETURN n.name")
    for row in records:
        print(row)
finally:
    client.close()

# Or using context managers
with StrutDBClient() as client:
    res = client.execute("CREATE (n:Server {ip: '10.0.0.1'})")
```

### Methods Reference

| Method | Parameters | Return Type | Description |
| :--- | :--- | :--- | :--- |
| `ping()` | *None* | `bool` | Returns `True` if the server is healthy and responding to RESP commands. |
| `execute(query_or_cmd, *args)` | `query_or_cmd: str`, `*args: Any` | `Any` | Executes an openCypher query, Redis command, or arbitrary RESP instruction. |
| `eval(script)` | `script: str` | `Any` | Evaluates a Lua script server-side using the `EVAL` command and returns the decoded result. |
| `close()` | *None* | `None` | Safely closes the underlying socket connection. |

---

## Cypher Querying

StrutDB supports openCypher declarative patterns for creating and querying graph topologies.

### Creating Nodes and Edges

```cypher
// Create individual nodes with property bags
CREATE (u:User {name: 'Ada Lovelace', role: 'Engineer'})

// Batch create multiple nodes
CREATE (a:User {name: 'Alice'}), (b:User {name: 'Bob'}), (c:User {name: 'Charlie'})

// Connect existing nodes using MATCH and CREATE
MATCH (a:User {name: 'Alice'}), (b:User {name: 'Bob'})
CREATE (a)-[:KNOWS]->(b)
```

### Multi-Hop Traversals

```cypher
// 1-Hop Traversal
MATCH (a:User)-[:KNOWS]->(b:User)
RETURN a.name, b.name

// 2-Hop Relationship Traversal
MATCH (a:User {name: 'Alice'})-[:KNOWS]->(b:User)-[:KNOWS]->(c:User)
RETURN a.name, b.name, c.name
```

---

## Lua User-Defined Functions

Execute custom server-side graph logic directly within StrutDB's memory space using the global `strutdb` table:

```lua
-- Discover all 2-hop neighbors without transferring intermediate data over the network
local start_node = 1
local visited = {}
local two_hop_neighbors = {}

local first_hop = strutdb.outgoing(start_node)
for _, edge_id in ipairs(first_hop) do
    local edge = strutdb.get_edge(edge_id)
    if edge then
        local second_hop = strutdb.outgoing(edge.dst)
        for _, second_edge_id in ipairs(second_hop) do
            local second_edge = strutdb.get_edge(second_edge_id)
            if second_edge and second_edge.dst ~= start_node then
                table.insert(two_hop_neighbors, second_edge.dst)
            end
        end
    end
end

return string.format("Found %d 2-hop neighbors", #two_hop_neighbors)
```

---

## Wire Protocol Compatibility

StrutDB speaks standard **Redis Serialization Protocol (RESP2)**. You can connect to StrutDB using any standard Redis driver in any ecosystem:

### Node.js (ioredis)
```javascript
const Redis = require("ioredis");
const redis = new Redis({ host: "localhost", port: 6379 });

async function run() {
  await redis.call("CREATE", "(u:Node {id: 1})");
  const result = await redis.call("MATCH", "(u:Node) RETURN u.id");
  console.log(result);
}
run();
```

### Go (go-redis)
```go
package main

import (
    "context"
    "fmt"
    "github.com/redis/go-redis/v9"
)

func main() {
    ctx := context.Background()
    rdb := redis.NewClient(&redis.Options{Addr: "localhost:6379"})
    
    val, err := rdb.Do(ctx, "MATCH", "(a)-[:KNOWS]->(b)", "RETURN", "a.name, b.name").Result()
    if err != nil {
        panic(err)
    }
    fmt.Println("Result:", val)
}
```

---

## Roadmap & Release Milestones

- [x] **Phase 1: In-Memory Core**: 64-shard concurrent hash table, thread-safe CSR adjacency list.
- [x] **Phase 2: Cypher AST Engine**: `libcypher-parser` integration, AST walking, and BFS pattern matcher.
- [x] **Phase 3: Lua Scripting Sandbox**: `sol2` Lua 5.4 integration, sandbox isolation, and graph UDF API.
- [x] **Phase 4: Network Server**: Linux `epoll` asynchronous TCP server, streaming RESP parser.
- [x] **Phase 5: Containerization**: Multi-stage Docker build, Ninja build system, CI/CD workflow.
- [x] **Phase 6: Persistence & Tooling**: Persistent RocksDB volume, Python SDK (`strutdb-py`), live documentation site.
- [ ] **Phase 7: Distributed Clustering**: Raft consensus replication and distributed query partitioning.

---

## License

StrutDB is licensed under the [Apache 2.0 License](LICENSE).
