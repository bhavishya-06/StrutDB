# Architecture & Engineering Deep-Dive

StrutDB is an enterprise-grade native graph database engineered in **C++20**. It is architected for maximal parallelism, zero unnecessary memory allocations, and minimal thread contention.

This document provides an exhaustive, low-level engineering deep-dive into StrutDB's internal architecture, exploring its network subsystem, query processing engine, in-memory concurrency model, and persistent storage subsystem.

---

## High-Level System Architecture

The StrutDB process is organized into distinct, decoupled subsystems:

```
+-------------------------------------------------------------------------------------------------------------------+
|                                                 Client Applications                                               |
|                              (Python SDK, redis-cli, Node.js, Go, Rust, Java, C#)                                 |
+----------------------------------------------------------+--------------------------------------------------------+
                                                           |
                                                           | TCP Stream (Port 6379, RESP2)
                                                           v
+-------------------------------------------------------------------------------------------------------------------+
| Network Subsystem (Asynchronous Linux epoll Engine)                                                              |
|                                                                                                                   |
|  +-----------------------------+   +------------------------------------+   +----------------------------------+  |
|  | Socket Multiplexer          |   | ClientSession Manager              |   | Streaming RESP Parser            |  |
|  | - epoll_create1(0)          |   | - Input Buffer: std::vector<uint8> |   | - Finite State Machine (FSM)     |  |
|  | - Level-Triggered epoll_wait|   | - Output Buffer: std::vector<uint8>|   | - Zero-copy string slice parsing |  |
|  | - O_NONBLOCK Socket Flags   |   | - Per-session lifecycle tracking   |   | - Pipelined command processing   |  |
|  +--------------+--------------+   +-----------------+------------------+   +----------------+-----------------+  |
+-----------------|------------------------------------|---------------------------------------|--------------------+
                  |                                    |                                       |
                  +------------------------------------+---------------------------------------+
                                                       |
                                                       v Parsed Commands
+-------------------------------------------------------------------------------------------------------------------+
| Command Dispatcher & Routing Layer                                                                                |
|                                                                                                                   |
|     +-----------------------------------+    +----------------------------------+    +----------------------+     |
|     | openCypher AST Query Engine       |    | Embedded Lua Script Engine (sol2)|    | Built-in KV Engine   |     |
|     | Commands: CREATE, MATCH, RETURN   |    | Command: EVAL <script>           |    | Commands: PING, GET, |     |
|     |                                   |    |                                  |    |           SET, DEL   |     |
|     +-----------------+-----------------+    +----------------+-----------------+    +----------+-----------+     |
+-----------------------|---------------------------------------|---------------------------------|-----------------+
                        |                                       |                                 |
                        v                                       v                                 v
+-------------------------------------------------------------------------------------------------------------------+
| Graph Execution Engine                                                                                            |
|                                                                                                                   |
|  +-------------------------------------+   +------------------------------------+   +--------------------------+  |
|  | libcypher-parser Front-End          |   | ExecutionContext Variable Binder   |   | Breadth-First Matcher    |  |
|  | - AST Tokenization & Tree Gen       |   | - Node Bindings (node_bindings)    |   | - Multi-hop path search  |  |
|  | - Clause Splitter (CREATE / MATCH)  |   | - Edge Bindings (edge_bindings)    |   | - Cartesian row builder  |  |
|  | - Expression & Predicate Validator  |   | - Literal Bindings (literals)      |   | - Column projection      |  |
|  +-------------------------------------+   +------------------------------------+   +--------------------------+  |
+----------------------------------------------------------+--------------------------------------------------------+
                                                           |
                                                           v Internal Graph API
+-------------------------------------------------------------------------------------------------------------------+
| In-Memory Graph Core Engine                                                                                       |
|                                                                                                                   |
|  +-------------------------------------------------------------------------------------------------------------+  |
|  | Concurrency & Synchronization Hierarchy                                                                      |  |
|  | 1. std::shared_mutex rw_mutex_: Shared for Readers, Exclusive for Writers                                   |  |
|  | 2. Shard Mutexes: std::mutex guarding individual partitions (acquired only under rw_mutex_)                 |  |
|  | 3. Unlocked internal helpers (*_unlocked) preventing non-recursive mutex deadlocks                          |  |
|  +-------------------------------------------------------------------------------------------------------------+  |
|                                                                                                                   |
|  +-------------------------------------------+                 +-----------------------------------------------+  |
|  | Sharded Topological Storage (64 Shards)   |                 | CSR Adjacency Index (Compressed Sparse Row)   |  |
|  | - ShardedHashMap<NodeId, GraphNode>       |                 | - ShardedHashMap<NodeId, vector<AdjEntry>>    |  |
|  | - ShardedHashMap<EdgeId, GraphEdge>       |                 |   out_ (Outgoing edges: edge_id, dst_id)      |  |
|  | - ShardedHashMap<string, PropertyValue>   |                 | - ShardedHashMap<NodeId, vector<AdjEntry>>    |  |
|  |   kv_ (Fast Key-Value store)              |                 |   in_ (Incoming edges: edge_id, src_id)       |  |
|  +-------------------------------------------+                 +-----------------------------------------------+  |
+----------------------------------------------------------+--------------------------------------------------------+
                                                           |
                                                           v Synchronous Flush / WAL Playback
+-------------------------------------------------------------------------------------------------------------------+
| Persistent Storage Engine (Embedded RocksDB)                                                                      |
|                                                                                                                   |
|  +----------------------+   +-----------------------+   +------------------------+   +-------------------------+  |
|  | Write-Ahead Log (WAL)|   | Active & Flush MemTable|  | Level 0..N SSTables    |   | Block Cache & Bloom     |  |
|  | - Synchronous append |   | - SkipList in-memory  |   | - Sorted String Tables |   | - High-locality filter  |  |
|  | - Crash durability   |   | - Concurrent flushes  |   | - Leveled compaction   |   | - Instant point lookups |  |
|  +----------------------+   +-----------------------+   +------------------------+   +-------------------------+  |
+-------------------------------------------------------------------------------------------------------------------+
```

---

## 1. Network Subsystem: Asynchronous epoll Engine

StrutDB operates a custom, high-throughput network subsystem designed to sustain tens of thousands of concurrent client connections with negligible kernel context-switching overhead.

### Socket Multiplexing via Linux `epoll`

The network server uses Linux's scalable I/O multiplexing facility (`epoll`):

- **Instance Creation**: Initialized with `epoll_create1(0)`.
- **Non-Blocking Sockets**: Sockets are placed into non-blocking mode using POSIX `fcntl`:
  ```cpp
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  ```
- **Event Notification**: Sockets register interest with `EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP`.
- **Event Loop Dispatch**: The core event loop executes `epoll_wait` with a maximum event batch size (`kMaxEvents = 1024`):
  ```cpp
  int nfds = epoll_wait(epoll_fd_, events.data(), kMaxEvents, -1);
  ```

### Zero-Copy Buffering & `ClientSession`

Every connected client is represented by an allocated `ClientSession` object:
- **Input Buffer**: `std::vector<std::uint8_t> input_buffer_` dynamically reserved to `4096` bytes. Incoming network chunks from `::recv()` append directly to this contiguous memory slab.
- **Output Buffer**: `std::vector<std::uint8_t> output_buffer_` buffers serialized RESP responses.
- **Backpressure Handling**: If a socket's TCP send buffer is full, `::send()` returns `EWOULDBLOCK` or `EAGAIN`. StrutDB keeps the unsent bytes buffered and activates `EPOLLOUT` on the socket, flushing when the kernel socket buffer becomes writable.

### Streaming RESP Parser Finite State Machine (FSM)

StrutDB parses the Redis Serialization Protocol (RESP2) using an incremental, zero-copy state machine:

```
[Start] ---> (First byte check: '*', '$', '+', ':', '-')
   |
   +---> '*' (Array Length) --------> Read count ---> Iterate elements
   |
   +---> '$' (Bulk String Length) --> Read size  ---> Buffer payload bytes ---> \r\n
   |
   +---> '+' (Simple String) -------> Scan to \r\n
   |
   +---> ':' (Integer) -------------> Scan to \r\n
   |
   +---> [Complete] ---------------> Dispatch Command
```

The `RespParser` class transitions between deterministic states (`ParseState`):
- `ParseState::Start`: Identifies the type prefix (`*` for Arrays, `$` for Bulk Strings).
- `ParseState::ArrayLength` & `ParseState::ArrayElement`: Decodes multi-argument commands (e.g., `["MATCH", "(u:User)..."]`).
- `ParseState::BulkStringLength` & `ParseState::BulkStringData`: Reads raw binary payloads safely without string copies.

Because commands arrive in arbitrary TCP chunks, the parser maintains internal byte offsets (`consumed`). It only advances the buffer when a full grammatical token has been verified, preventing incomplete frames from corrupting server state.

---

## 2. Query Subsystem: openCypher Parsing & Execution

StrutDB implements declarative graph querying based on openCypher. Queries are parsed into an Abstract Syntax Tree (AST) and executed against in-memory graph topologies.

### AST Generation with `libcypher-parser`

When a query string arrives (e.g., `MATCH (a:User)-[:KNOWS]->(b:User) RETURN a.name, b.name`), it is processed by the C++ parser wrapper:

```cpp
cypher_parse_result_t* result = cypher_parse(
    query_str.c_str(),
    NULL,
    NULL,
    CYPHER_PARSE_ONLY_STATEMENTS
);
```

1. **Tokenization & Grammar Verification**: `libcypher-parser` converts the input string into a structured AST of `cypher_astnode_t` records.
2. **Clause Extraction**: The AST is recursively walked to extract clauses:
   - `CYPHER_AST_STATEMENT` $\to$ `CYPHER_AST_QUERY`
   - `CYPHER_AST_MATCH_CLAUSE` $\to$ Extracted into `parser::MatchClause`.
   - `CYPHER_AST_CREATE_CLAUSE` $\to$ Extracted into `parser::CreateClause`.
   - `CYPHER_AST_RETURN_CLAUSE` $\to$ Extracted into `parser::ReturnClause`.

### Pattern Path Extraction

Path patterns (such as `(a:User)-[:KNOWS]->(b:User)`) are decomposed into an alternating sequence of **Node Patterns** and **Relationship Patterns**:

- **Node Pattern (`parser::NodePattern`)**:
  - `variable`: e.g., `"a"`
  - `labels`: e.g., `{"User"}`
  - `properties`: key-value map of literal constraints, e.g., `{"name": "Alice"}`
- **Relationship Pattern (`parser::RelPattern`)**:
  - `variable`: relationship variable alias
  - `rel_types`: e.g., `{"KNOWS"}`
  - `direction`: Directional arrow (`->` for Outgoing, `<-` for Incoming, or undirected)

### Execution Engine & Breadth-First Matching

The `Executor` class maps the AST clauses to physical memory operations:

```
[Cypher Query AST]
        |
        v
[execute()]
   |---> execute_create(): If CREATE clause present
   |        |--> Resolves or creates Node instances in Graph
   |        |--> Inserts GraphEdge with TypeId
   |        |--> Updates AdjacencyList out_ and in_ maps
   |
   |---> execute_match(): If MATCH clause present
            |--> BFS Path Matcher (bfs_match())
            |       |--> Seed matching nodes satisfying initial pattern
            |       |--> For each candidate, traverse outgoing/incoming edges
            |       |--> Verify edge type and destination node label
            |       |--> Bind variables into ExecutionContext
            |
            |--> execute_return():
                    |--> Project variables from ExecutionContext
                    |--> Extract requested properties into Row
                    |--> Construct final ResultSet
```

#### The `ExecutionContext` Variable Binder

During query evaluation, intermediate matches are stored in an `ExecutionContext`:

```cpp
struct ExecutionContext {
    std::unordered_map<std::string, std::shared_ptr<core::GraphNode>> node_bindings;
    std::unordered_map<std::string, std::shared_ptr<core::GraphEdge>> edge_bindings;
    std::unordered_map<std::string, core::PropertyValue>              literal_bindings;
};
```

When evaluating multi-hop chains (`(a)-[:KNOWS]->(b)-[:KNOWS]->(c)`), the BFS matcher iteratively extends each valid binding context. If a path fails to satisfy an edge type or property constraint, that branch is pruned immediately, avoiding unnecessary traversals.

---

## 3. Storage Subsystem: In-Memory Engine & RocksDB

StrutDB uses a hybrid architecture that pairs a high-performance in-memory layout with persistent disk storage.

### 64-Shard Concurrent Hash Maps (`ShardedHashMap`)

To eliminate global table lock contention, StrutDB partitions all node and edge indices across **64 discrete shards**:

```cpp
template <typename Key, typename Value>
class ShardedHashMap {
    static constexpr std::size_t kNumShards = 64;
    struct Shard {
        mutable std::mutex                  mutex;
        std::unordered_map<Key, Value>      map;
    };
    std::array<Shard, kNumShards>           shards_;
};
```

#### Why 64 Shards?
1. **CPU Cache Isolation**: Placing shard buckets on separate cache lines reduces cache invalidation and cache line bouncing across CPU cores.
2. **Concurrent Mutation**: 64 worker threads can write to different partitions of the graph simultaneously without blocking one another.
3. **Partition Hashing**: Keys are distributed via `std::hash<Key>{}(key) % 64`.

### Strict Two-Phase Lock Protocol

Deadlocks are a common failure mode in concurrent graph databases due to complex traversals and cross-node locking. StrutDB enforces a strict, non-invertible lock hierarchy:

```
+-----------------------------------------------------------+
| Level 1: Graph-Wide std::shared_mutex (rw_mutex_)         |
|   - Acquired SHARED by readers (GET, traversals)          |
|   - Acquired EXCLUSIVE by writers (add_node, add_edge)    |
+-----------------------------+-----------------------------+
                              |
                              v
+-----------------------------------------------------------+
| Level 2: Shard Mutexes (std::mutex per Shard)             |
|   - Acquired ONLY while rw_mutex_ is already held         |
|   - Guard individual unordered_map partitions             |
+-----------------------------+-----------------------------+
                              |
                              v
+-----------------------------------------------------------+
| Level 3: Individual Node & Edge Accessors                 |
|   - Never acquired by Graph writers                       |
|   - Internal helpers use *_unlocked methods               |
+-----------------------------------------------------------+
```

!!! danger "Preventing Recursive Deadlocks"
    `std::shared_mutex` in C++20 is **not recursive**. If a thread holding `rw_mutex_` in shared or exclusive mode attempts to acquire it again, the thread will deadlock. StrutDB prevents this by implementing internal `*_unlocked` methods (e.g., `add_edge_unlocked()`, `delete_node_unlocked()`), ensuring that outer API boundaries manage the lock lifecycle cleanly.

### CSR-Style Adjacency Lists

Adjacency lists are stored using a Compressed Sparse Row (CSR) design:

```cpp
struct AdjEntry {
    EdgeId edge_id     = kInvalidEdgeId;
    NodeId neighbor_id = kInvalidNodeId;
};

class AdjacencyList {
    ShardedHashMap<NodeId, std::vector<AdjEntry>> out_;
    ShardedHashMap<NodeId, std::vector<AdjEntry>> in_;
};
```

#### Architectural Advantages:
1. **Zero Smart-Pointer Cycles**: `AdjEntry` stores raw primitive 64-bit integer IDs (`NodeId`, `EdgeId`) rather than `std::shared_ptr<GraphNode>`. This completely avoids cyclical pointer references, eliminating reference-counting memory leaks.
2. **Contiguous Vector Storage**: Neighbors are stored in contiguous `std::vector<AdjEntry>` buffers, maximizing L1/L2 data cache prefetching during graph traversals.

---

## 4. RocksDB Persistence & Durability Mechanics

StrutDB guarantees ACID durability across system crashes and container restarts by integrating an embedded **RocksDB** storage engine.

```
Write Path:
Client Request ---> Graph Mutation
                          |
                          +---> 1. Write-Ahead Log (WAL) on Disk (fsync)
                          |
                          +---> 2. In-Memory Active MemTable (SkipList)
                          |
                          +---> 3. StrutDB In-Memory Shards & Adjacency List
```

### Key Mapping Scheme

Graph topologies map directly to RocksDB's Log-Structured Merge-tree (LSM) key-value format:

| Object Type | Key Prefix | Key Format | Value Format |
| :--- | :--- | :--- | :--- |
| **Node** | `n:` | `n:<node_id>` | Serialized JSON / Binary Property Bag |
| **Edge** | `e:` | `e:<edge_id>` | Packed struct: `{src_id, dst_id, type_id, properties}` |
| **Outgoing Adjacency** | `adj:out:` | `adj:out:<node_id>` | Binary vector of `{edge_id, neighbor_id}` tuples |
| **Incoming Adjacency** | `adj:in:` | `adj:in:<node_id>` | Binary vector of `{edge_id, neighbor_id}` tuples |
| **Metadata** | `meta:` | `meta:max_ids` | Packed 64-bit integers `{max_node_id, max_edge_id}` |

### LSM-Tree Mechanics

1. **Write-Ahead Log (WAL)**: Every write is appended sequentially to the WAL file on disk before returning success to the client, guaranteeing crash recovery with $\mathcal{O}(1)$ write latency.
2. **Active MemTable**: Writes enter an in-memory concurrent SkipList.
3. **Flushing**: When the active MemTable reaches its capacity, it freezes into an immutable MemTable and flushes to disk as a Level 0 (L0) Sorted String Table (SSTable).
4. **Leveled Compaction**: Background worker threads merge overlapping SSTable key ranges from Level $L$ to Level $L+1$, removing deleted records and maintaining bounded read amplification.
5. **Recovery on Startup**: If the StrutDB process restarts, the engine inspects the `/var/lib/strutdb` directory, replays any uncommitted WAL records, loads the SSTables, and reconstructs the in-memory 64-shard hash maps and CSR adjacency lists.

---

## Summary of Architectural Specifications

| Parameter | Specification |
| :--- | :--- |
| **Language Standard** | C++20 (`-std=c++20`, GCC 13+ / Clang 16+) |
| **Network Engine** | POSIX `epoll`, non-blocking I/O, level-triggered |
| **Wire Protocol** | Redis RESP2 (Simple strings, bulk strings, integers, arrays, errors) |
| **Port** | `6379` (Default Redis TCP port) |
| **Shard Count** | 64 shards (`ShardedHashMap`) |
| **Concurrency Primitives** | `std::shared_mutex` (Graph-level), `std::mutex` (Shard-level) |
| **Adjacency Representation** | Compressed Sparse Row (CSR) ID vectors (`AdjEntry`) |
| **Cypher Parser** | `libcypher-parser` with recursive AST transformation |
| **Scripting Runtime** | Lua 5.4 embedded via `sol2` |
| **Persistent Storage** | RocksDB Log-Structured Merge-tree (LSM) |
| **Persistence Directory** | `/var/lib/strutdb` (Docker host mount: `./strutdb-data`) |
