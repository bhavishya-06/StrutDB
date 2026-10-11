# Querying with openCypher & The Python SDK

StrutDB provides native support for the declarative **openCypher** graph query language, paired with an ergonomic, production-ready Python SDK (`strutdb-py`).

This guide details how to query StrutDB from Python, break down the mechanics of the Python driver, construct expressive openCypher patterns, and work with the data structures returned over the RESP wire protocol.

---

## 1. The Python SDK (`strutdb-py`) Deep-Dive

The official Python client (`StrutDBClient`) is a thin, high-performance wrapper around the industry-standard `redis-py` driver, customized for StrutDB's query and scripting syntax.

### Installation

Install the package directly from your local repository or project build directory:

```bash
pip install ./clients/python
```

### Connection Management & Socket Pooling

Under the hood, `StrutDBClient` initializes a thread-safe connection pool using RESP2 protocol semantics:

```python
from strutdb import StrutDBClient

# Explicit initialization
client = StrutDBClient(
    host="localhost",         # Target StrutDB server host
    port=6379,                # Default StrutDB port
    db=0,                     # Database index (0)
    decode_responses=True,    # Automatically decode RESP bytes to UTF-8 strings
    socket_timeout=5.0,       # Timeout for read/write operations (seconds)
    socket_connect_timeout=2.0 # Timeout for establishing connection (seconds)
)
```

#### Safe Resource Management with Context Managers

To prevent socket leaks and ensure connections are released promptly, use Python's `with` statement:

```python
with StrutDBClient(host="localhost", port=6379) as client:
    # Verify server liveness
    is_alive = client.ping()
    print(f"Server health: {is_alive}")
    
    # Execute graph operations...
# Connection automatically closed when exiting the block
```

### Complete API Reference

| Method | Signature | Description |
| :--- | :--- | :--- |
| `ping()` | `() -> bool` | Transmits a `PING` command; returns `True` if the server responds with `PONG`. |
| `execute(query_or_cmd, *args)` | `(str, *args: Any) -> Any` | Executes a raw openCypher query, Redis command, or arbitrary RESP instruction. |
| `eval(script)` | `(str) -> Any` | Dispatches a custom Lua script via the Redis `EVAL` command. |
| `close()` | `() -> None` | Closes the underlying socket connection pool. |

---

## 2. Declarative openCypher Guide

StrutDB parses openCypher queries into an Abstract Syntax Tree (AST) using `libcypher-parser` and executes them directly against in-memory index-free adjacency lists.

### Clause 1: `CREATE` (Graph Mutations)

The `CREATE` clause writes new vertices (nodes) and directed relationships (edges) into the graph.

#### Creating Standalone Nodes
Nodes are specified inside parentheses `(alias:Label {properties})`:

```python
# Create a single user node
client.execute("CREATE (u:User {name: 'Alice', age: 30, active: true})")

# Create multiple nodes with different labels and property maps
client.execute("""
    CREATE (e1:Engineer {name: 'Grace Hopper', language: 'COBOL'}),
           (e2:Engineer {name: 'Alan Turing', language: 'Logic'}),
           (d:Department {title: 'Computing'})
""")
```

#### Creating Directed Relationships
Relationships are specified inside brackets `-[alias:TYPE]->`:

```python
# Connect existing nodes using MATCH and CREATE
client.execute("""
    MATCH (a:Engineer {name: 'Grace Hopper'}), (d:Department {title: 'Computing'})
    CREATE (a)-[:MEMBER_OF]->(d)
""")
```

#### Creating Entire Paths Atomically
You can create nodes and the connecting relationships in a single atomic query:

```python
client.execute("""
    CREATE (a:User {name: 'Alice'})-[:FOLLOWS]->(b:User {name: 'Bob'})-[:FOLLOWS]->(c:User {name: 'Charlie'})
""")
```

---

### Clause 2: `MATCH` & `RETURN` (Pattern Traversals)

The `MATCH` clause searches the in-memory graph for topological subgraphs matching a declarative pattern. The `RETURN` clause projects matching variables into tabular rows.

#### Single-Node Pattern Matching
Find all nodes with a given label and property:

```python
results = client.execute("MATCH (u:User {name: 'Alice'}) RETURN u.name, u.age")
print(results)
# Output: [['Alice', '30']]
```

#### 1-Hop Relationship Traversal
Find all direct relationships between users:

```python
results = client.execute("MATCH (a:User)-[:FOLLOWS]->(b:User) RETURN a.name, b.name")
print(results)
# Output: [['Alice', 'Bob'], ['Bob', 'Charlie']]
```

#### Multi-Hop Traversal (2+ Hops)
Traverse arbitrarily deep relationship chains without relational joins:

```python
# 2-Hop "Friends of Friends" traversal
query = """
    MATCH (a:User {name: 'Alice'})-[:FOLLOWS]->(b:User)-[:FOLLOWS]->(c:User)
    RETURN a.name, b.name, c.name
"""
results = client.execute(query)
print(results)
# Output: [['Alice', 'Bob', 'Charlie']]
```

---

## 3. Wire Protocol & Returned Data Structures

Understanding how StrutDB formats query results over the wire enables robust application development.

### The RESP Wire Format

StrutDB encodes tabular query results as standard **RESP Arrays of Arrays**:

```text
*1\r\n          <-- Top-level array: 1 row returned
*2\r\n          <-- Nested array: 2 columns in this row
$5\r\nAlice\r\n <-- Column 1: Bulk string (5 bytes)
$3\r\nBob\r\n   <-- Column 2: Bulk string (3 bytes)
```

When decoded by `StrutDBClient(decode_responses=True)`, this wire structure maps into nested Python lists:

```python
[
    ['Alice', 'Bob']
]
```

For queries returning multiple matching rows:

```python
[
    ['Alice', 'Bob', 'Charlie'],
    ['Alice', 'David', 'Eva'],
    ['Bob', 'Charlie', 'Frank']
]
```

### Result Structure Mapping

| Query Type | Cypher Example | Return Type in Python | Example Return Value |
| :--- | :--- | :--- | :--- |
| **Node / Edge Creation** | `CREATE (u:User {name: 'Alice'})` | `str` or `int` | `"OK"` or `1` |
| **Single Column Query** | `MATCH (u:User) RETURN u.name` | `List[List[str]]` | `[['Alice'], ['Bob'], ['Charlie']]` |
| **Multi-Column Traversal** | `MATCH (a)-[:KNOWS]->(b) RETURN a.name, b.name` | `List[List[str]]` | `[['Alice', 'Bob'], ['Bob', 'Charlie']]` |
| **Lua Script (`EVAL`)** | `EVAL "return {10, 20, 'StrutDB'}"` | `List[Union[int, str]]` | `[10, 20, 'StrutDB']` |

---

## 4. Robust Error Handling

StrutDB reports syntax errors, malformed Cypher patterns, or runtime execution failures using standard RESP Error frames (`-ERR <message>\r\n`). 

In Python, these errors raise `redis.exceptions.ResponseError`:

```python
import redis
from strutdb import StrutDBClient

with StrutDBClient() as client:
    try:
        # Intentionally malformed Cypher query
        client.execute("MATCH (u:User INVALID SYNTAX")
    except redis.exceptions.ResponseError as err:
        print(f"Caught database error: {err}")
    except redis.exceptions.ConnectionError as err:
        print(f"Network error: {err}")
```

---

## 5. End-to-End Enterprise Example

Below is a complete, production-style Python application demonstrating data ingestion, multi-hop relationship querying, and error handling:

```python
from typing import List
from strutdb import StrutDBClient

def run_enterprise_graph_pipeline():
    print("=" * 60)
    print("StrutDB Enterprise Graph Query Pipeline")
    print("=" * 60)

    with StrutDBClient(host="localhost", port=6379) as client:
        # Step 1: Healthcheck
        assert client.ping(), "StrutDB is unreachable"
        print("[✓] Connected to StrutDB on port 6379\n")

        # Step 2: Seed Graph Topology (Identity Graph)
        print("[+] Seeding enterprise identity topology...")
        client.execute("CREATE (dev1:Employee {id: 101, name: 'Alice', team: 'Core Engine'})")
        client.execute("CREATE (dev2:Employee {id: 102, name: 'Bob', team: 'Distributed Systems'})")
        client.execute("CREATE (mgr:Employee {id: 103, name: 'Carol', team: 'Engineering VP'})")
        client.execute("CREATE (proj:Project {name: 'StrutDB Core', status: 'Active'})")

        # Step 3: Establish Relationships
        print("[+] Establishing reporting and assignment edges...")
        client.execute("MATCH (a:Employee {name: 'Alice'}), (m:Employee {name: 'Carol'}) CREATE (a)-[:REPORTS_TO]->(m)")
        client.execute("MATCH (b:Employee {name: 'Bob'}), (m:Employee {name: 'Carol'}) CREATE (b)-[:REPORTS_TO]->(m)")
        client.execute("MATCH (a:Employee {name: 'Alice'}), (p:Project {name: 'StrutDB Core'}) CREATE (a)-[:ASSIGNED_TO]->(p)")
        client.execute("MATCH (b:Employee {name: 'Bob'}), (p:Project {name: 'StrutDB Core'}) CREATE (b)-[:ASSIGNED_TO]->(p)")

        # Step 4: Multi-Hop Pattern Matching
        # Query: Find all colleagues who report to the same manager and work on the same project
        print("\n[?] Querying shared reporting structures...")
        query = """
            MATCH (e1:Employee)-[:REPORTS_TO]->(mgr:Employee),
                  (e2:Employee)-[:REPORTS_TO]->(mgr:Employee)
            RETURN e1.name, e2.name, mgr.name
        """
        rows: List[List[str]] = client.execute(query)
        
        print("\nResults:")
        for row in rows:
            print(f"  Employee: {row[0]:<10} Colleague: {row[1]:<10} Manager: {row[2]}")

if __name__ == "__main__":
    run_enterprise_graph_pipeline()
```

Output:
```text
============================================================
StrutDB Enterprise Graph Query Pipeline
============================================================
[✓] Connected to StrutDB on port 6379

[+] Seeding enterprise identity topology...
[+] Establishing reporting and assignment edges...

[?] Querying shared reporting structures...

Results:
  Employee: Alice      Colleague: Bob        Manager: Carol
  Employee: Bob        Colleague: Alice      Manager: Carol
```
