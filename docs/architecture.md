# Architecture & System Design

StrutDB is structured into modular layers designed for maximal concurrency, zero memory overhead, and minimal cross-thread contention.

---

## Architecture Diagram

```
+-----------------------------------------------------------+
|                      Client Layer                         |
|      (redis-py, redis-cli, strutdb-py, custom RESP)       |
+-----------------------------+-----------------------------+
                              | RESP 2 (Port 6379)
                              v
+-----------------------------------------------------------+
|                   Network & Event Loop                    |
|             Linux epoll, Non-blocking I/O,                |
|             Zero-copy Buffering, RESP Parser              |
+--------------+-----------------------------+--------------+
               |                             |
               v Cypher Queries              v Lua EVAL
+-----------------------------+  +--------------------------+
|       Parser Layer          |  |     Scripting Engine     |
|    libcypher-parser AST     |  |       Lua 5.4 / sol2     |
|   Pattern & Clause Trees    |  |     Sandboxed Runtime    |
+--------------+--------------+  +-------------+------------+
               |                               |
               v                               v
+-----------------------------------------------------------+
|                     Execution Engine                      |
|          BFS Graph Pattern Matcher & Context Binder       |
+-----------------------------+-----------------------------+
                              |
                              v
+-----------------------------------------------------------+
|                    Core Graph Engine                      |
|       64-Shard Concurrent Hash Maps, Adjacency Lists,     |
|        Dynamic Property Bags, RW-Mutex Lock Protocol      |
+-----------------------------+-----------------------------+
                              |
                              v
+-----------------------------------------------------------+
|                 Persistent Storage Engine                 |
|               RocksDB LSM-Tree (/var/lib/strutdb)         |
+-----------------------------------------------------------+
```

---

## Component Deep Dive

### 1. In-Memory Graph Core (`src/core/`)
- **Sharded Hash Maps**: 64-shard concurrent table structure. Writers lock only the affected shard, allowing parallel reads across shards without global table lock contention.
- **Lock Protocol**: Two-phase hierarchy:
  1. `rw_mutex_`: Reader/writer shared mutex for graph-wide snapshot isolation.
  2. Shard Mutexes: Fine-grained mutexes acquired only under `rw_mutex_`.
  3. Node/Edge Mutexes: Never acquired by graph writers, preventing deadlocks.
- **Adjacency Lists**: Dedicated incoming and outgoing edge indices for rapid directional neighbor traversal.

### 2. Network Layer (`src/network/`)
- **Epoll Event Multiplexing**: Non-blocking sockets managed via edge/level-triggered Linux `epoll`.
- **RESP 2 Protocol**: Compliant with Redis Serialization Protocol specifications (simple strings, errors, integers, bulk strings, arrays).

### 3. Parsing & Execution (`src/parser/`, `src/execution/`)
- **Cypher AST**: Converts raw query strings into structured pattern expressions using `libcypher-parser`.
- **BFS Traversal**: Explores graph connections using breadth-first search against adjacency entries, binding variables into execution context rows.

### 4. Storage Engine (`src/storage/`)
- Backed by RocksDB for durability and crash recovery, flushing node and edge updates to persistent storage.
