# StrutDB: Native High-Performance Graph Database

Welcome to the official documentation for **StrutDB**, an enterprise-grade, high-performance in-memory graph database engineered from the ground up in modern **C++20**.

StrutDB unifies the sub-microsecond traversal speeds of lock-free in-memory index-free adjacency lists with the crash resilience and ACID durability of embedded **RocksDB** LSM-trees, openCypher declarative graph querying, sandboxed **Lua 5.4** User-Defined Functions, and the universal compatibility of the **Redis RESP2** wire protocol.

---

## The Vision: Why Build a Native Graph Database?

Modern applications are overwhelmingly defined by interconnected networks of entities: social graphs, financial fraud rings, real-time recommendation engines, identity access trees, and supply-chain logistics.

Despite this, most engineering teams continue to force graph data models into traditional Relational Database Management Systems (RDBMS) or Document stores. This mismatch creates severe architectural bottlenecks.

```
+----------------------------------------------------------------------------------------------------+
|                               THE RELATIONAL JOIN BOTTLENECK                                       |
|                                                                                                    |
|   Query: "Find all friends of friends who bought product X"                                       |
|                                                                                                    |
|   RDBMS (Foreign Keys & B-Trees):                                                                  |
|   [Users] --(B-Tree Scan)--> [Friendships] --(Nested Loop Join)--> [Friendships]                  |
|                                                            --(Hash Join)--> [Orders]               |
|                                                            --(B-Tree Scan)--> [Products]           |
|                                                                                                    |
|   Complexity: O(N_1 * N_2 * ... * N_k)  --> Exponential join explosion & cache invalidation        |
+----------------------------------------------------------------------------------------------------+
                                                vs.
+----------------------------------------------------------------------------------------------------+
|                                STRUTDB INDEX-FREE ADJACENCY                                        |
|                                                                                                    |
|   StrutDB (Contiguous Memory Pointers):                                                            |
|   (User A) ===[Direct Pointer]===> (User B) ===[Direct Pointer]===> (User C)                       |
|                                                                                                    |
|   Complexity: O(degree(v))              --> Sub-microsecond pointer chasing in L1/L2 CPU cache     |
+----------------------------------------------------------------------------------------------------+
```

### The Failure of Relational Joins

In an RDBMS, relationships are simulated through foreign keys. Traversing a relationship requires looking up an ID inside an index (typically a B+Tree with $\mathcal{O}(\log N)$ depth). For a traversal of depth $k$, an RDBMS must perform $k$ nested index lookups and join operations:

$$\text{Latency}_{\text{RDBMS}} = \sum_{i=1}^k \mathcal{O}(\log N_i) \times \text{Join Cost}$$

As data grows into tens of millions of records, join operations blow past CPU cache lines, thrash buffer pools, trigger global table read locks, and degrade latency exponentially.

### The Power of Index-Free Adjacency

StrutDB utilizes **Index-Free Adjacency (IFA)**. Every node in StrutDB directly references its outgoing and incoming relationships via contiguous Compressed Sparse Row (CSR) memory structures.

When navigating from node $A$ to node $B$, the engine does **not** evaluate an index. It chases memory pointers directly. Traversal cost is strictly proportional to the number of edges connected to the node:

$$\text{Latency}_{\text{StrutDB}} = \mathcal{O}(\text{degree}(A))$$

Traversing relationships remains instantaneous, whether your database contains ten thousand nodes or ten billion nodes.

---

## Eliminating the N+1 Latency Tax with Server-Side Lua

A pervasive architectural issue in graph querying is the **N+1 Query Problem**:

1. A backend service queries the database for user $A$ (1 network round-trip).
2. The database returns $N$ friend IDs.
3. The backend service fires $N$ subsequent network queries to retrieve details for each friend ($N$ round-trips).

Even over high-speed 10GbE network fabrics, performing multiple sequential network hops accumulates significant tail latency ($p99 > 150\text{ms}$).

```
Traditional Architecture (N+1 Network Tax):
Client  ---(1. Fetch Node A)---------------------> DB
Client  <--(Returns Node A with N edge IDs)------- DB
Client  ---(2. Fetch Neighbor 1)------------------> DB
Client  ---(3. Fetch Neighbor 2)------------------> DB
Client  ---(4. Fetch Neighbor N)------------------> DB
Total Network Hops: 1 + N Round-Trips (Heavy Latency)

StrutDB Embedded Lua Architecture (Zero Network Tax):
Client  ---(Single EVAL script with traversal)----> StrutDB (C++ Engine)
                                                      |--> Traverses 100+ hops in-memory
                                                      |--> Runs custom scoring heuristic
Client  <--(Returns Final Computed Result)--------- StrutDB
Total Network Hops: Exactly 1 Round-Trip!
```

StrutDB embeds a sandboxed **Lua 5.4 engine (`sol2`)** directly inside the database process. Developers can transmit custom algorithms, multi-hop breadth-first searches, or ranking formulas in a single Redis `EVAL` command. The logic executes natively against C++ memory structures, eliminating network round-trips entirely.

---

## Core Pillars & Architectural Highlights

| Pillar | Engineering Design | Production Benefit |
| :--- | :--- | :--- |
| **Concurrency Core** | 64-shard concurrent hash table with reader-writer shared mutexes | Zero lock contention on parallel reads; isolated shard locking on writes |
| **Topology Representation** | Compressed Sparse Row (CSR) index-free adjacency storing node/edge IDs | High cache locality; no cyclical pointer overhead or memory leaks |
| **Durability Layer** | Embedded RocksDB LSM-Tree with Write-Ahead Logging (WAL) | Zero data loss on container restart; immediate crash recovery |
| **Declarative Querying** | Recursive AST parsing with `libcypher-parser` | Native openCypher `CREATE`, `MATCH`, and `RETURN` pattern queries |
| **Extensibility** | Sandboxed Lua 5.4 environment via `sol2` | Custom server-side graph algorithms with strict token validation |
| **Wire Protocol** | Asynchronous Linux `epoll` socket loop serving Redis RESP2 | Works out-of-the-box with any standard Redis client in any language |

---

## Complete Step-by-Step Installation Guide

Follow this guide to get StrutDB compiled, deployed, and connected on your machine.

### System Prerequisites

Ensure the following tools are installed on your workstation:
- **Docker Engine** (v20.10 or later) & **Docker Compose**
- **Git**
- **Python 3.8+** with `pip`

---

### Step 1: Clone the Repository

Clone the StrutDB repository from GitHub and navigate into the project root:

```bash
git clone https://github.com/bhavishya-06/StrutDB.git
cd StrutDB
```

Verify the project structure:
```bash
ls -F
# CMakeLists.txt  Dockerfile  README.md  clients/  docs/  src/  tests/ ...
```

---

### Step 2: Build the Multi-Stage Docker Image

StrutDB is written in modern C++20 and requires modern compiler toolchains (`gcc-13`, `ninja`, `cmake`). To ensure deterministic compilation and zero host-dependency headaches, we build using our multi-stage `Dockerfile`:

```bash
docker build -t strutdb:latest .
```

!!! info "Why Multi-Stage Compilation?"
    1. **Stage 1 (Builder)**: Installs `gcc-13`, `g++-13`, `ninja-build`, and `cmake`. It fetches and compiles `libcypher-parser` and `sol2`/`Lua 5.4`, then builds the C++20 `strutdb` binary with `-O3` optimizations using Ninja.
    2. **Stage 2 (Runtime Runner)**: Copies only the compiled binary and essential shared libraries into a clean Debian Bookworm image, resulting in a minimal, secure container image free of compilers and header files.

---

### Step 3: Launch with Docker Compose

Launch the containerized StrutDB server using Docker Compose:

```bash
docker compose up -d
```

Check the running container:

```bash
docker ps --filter "name=strutdb-server"
```

!!! tip "Persistent Storage Mounting"
    The `docker-compose.yml` mounts the host directory `./strutdb-data` to `/var/lib/strutdb` inside the container. This is the exact path where the embedded RocksDB engine stores its Write-Ahead Log (WAL) and SSTable files. Even if you run `docker compose down`, your graph data remains saved on disk.

Verify server logs:

```bash
docker logs strutdb-server
```

You should see:
```text
StrutDB v0.1.0 - High-performance in-memory graph database
Starting server on port 6379...
Server listening on port 6379
```

---

### Step 4: Verify Network Connectivity

Because StrutDB speaks the Redis Serialization Protocol (RESP2), you can immediately test network connectivity using `redis-cli`, `nc`, or `telnet`:

```bash
redis-cli -p 6379 PING
```

Output:
```text
PONG
```

---

### Step 5: Install the Python SDK

StrutDB includes an official lightweight Python SDK (`strutdb-py`) located in `clients/python`. Install it directly into your local or virtual environment:

```bash
pip install ./clients/python
```

!!! note "What This Does"
    This installs the `strutdb` package into your Python environment. It provides `StrutDBClient`, which handles socket connection pooling, openCypher execution, Lua script dispatching, and automatic decoding of RESP structures into Python objects.

---

### Step 6: Run Your First Python Graph Script

Create a script named `hello_graph.py` and run it:

```python
from strutdb import StrutDBClient

# Connect to the local StrutDB server
with StrutDBClient(host="localhost", port=6379) as db:
    # 1. Verify health
    print("Ping status:", db.ping())

    # 2. Create nodes representing developers
    print("\nCreating nodes...")
    db.execute("CREATE (u1:Engineer {name: 'Ada Lovelace'}), (u2:Engineer {name: 'Grace Hopper'})")

    # 3. Create a relationship between them
    print("Creating relationship...")
    db.execute("MATCH (a:Engineer {name: 'Ada Lovelace'}), (b:Engineer {name: 'Grace Hopper'}) CREATE (a)-[:COLLABORATES_WITH]->(b)")

    # 4. Query the relationship
    print("Traversing graph...")
    results = db.execute("MATCH (a:Engineer)-[:COLLABORATES_WITH]->(b:Engineer) RETURN a.name, b.name")
    print("Query Results:", results)

    # 5. Run a custom server-side Lua algorithm
    print("\nRunning server-side Lua script...")
    lua_code = """
        local n1 = strutdb.add_node()
        local n2 = strutdb.add_node()
        local edge = strutdb.add_edge(n1, n2, 101)
        return string.format("Created nodes %d and %d connected by edge %d", n1, n2, edge)
    """
    output = db.eval(lua_code)
    print("Lua Output:", output)
```

Run the script:

```bash
python hello_graph.py
```

Expected output:
```text
Ping status: True

Creating nodes...
Creating relationship...
Traversing graph...
Query Results: [['Ada Lovelace', 'Grace Hopper']]

Running server-side Lua script...
Lua Output: Created nodes 3 and 4 connected by edge 2
```

Congratulations! You have a fully operational, high-performance C++20 graph database running locally. Explore the [Architecture Deep-Dive](architecture.md), [Querying Guide](querying.md), and [Lua Scripting Guide](scripting.md) to learn more.
