# strutdb-py

Python client SDK for **StrutDB**, a high-performance in-memory C++20 graph database.

## Installation

Install via pip:

```bash
pip install -e ./clients/python
```

Or ensure `redis>=4.5.0` is installed.

## Quickstart

```python
from strutdb import StrutDBClient

# Initialize client connection
client = StrutDBClient(host="localhost", port=6379)

# Verify connectivity
if client.ping():
    print("Connected to StrutDB!")

# Create nodes
client.execute("CREATE (n:User {name: 'Alice'}), (m:User {name: 'Bob'})")

# Create relationship
client.execute("MATCH (a:User {name: 'Alice'}), (b:User {name: 'Bob'}) CREATE (a)-[:KNOWS]->(b)")

# Query graph
results = client.execute("MATCH (a:User)-[:KNOWS]->(b:User) RETURN a.name, b.name")
print("Results:", results)
# Output: [['Alice', 'Bob']]

client.close()
```

### Context Manager Support

```python
with StrutDBClient(host="localhost", port=6379) as client:
    res = client.execute("MATCH (a:User)-[:KNOWS]->(b:User) RETURN a.name, b.name")
    print(res)
```
