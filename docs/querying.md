# Querying with Cypher

StrutDB provides native execution of the Cypher graph query language. Queries are parsed into an Abstract Syntax Tree (AST) and executed directly against the in-memory graph index.

---

## Supported Cypher Clauses

### 1. `CREATE`
Used to create nodes and directed relationships.

#### Create Individual Nodes
```cypher
CREATE (u:User {id: 1, name: 'Alice'})
```

#### Batch Node Creation
```cypher
CREATE (u1:User {id: 1, name: 'Alice'}), (u2:User {id: 2, name: 'Bob'})
```

#### Create Relationships
```cypher
MATCH (a:User {name: 'Alice'}), (b:User {name: 'Bob'})
CREATE (a)-[:KNOWS]->(b)
```

#### Direct Path Creation
```cypher
CREATE (a:User {name: 'Alice'})-[:KNOWS]->(b:User {name: 'Bob'})-[:KNOWS]->(c:User {name: 'Charlie'})
```

---

### 2. `MATCH` & `RETURN`
Used to search for graph patterns and project result rows.

#### Single Node Retrieval
```cypher
MATCH (u:User {name: 'Alice'})
RETURN u.name, u.id
```

#### 1-Hop Relationship Traversal
```cypher
MATCH (a:User)-[:KNOWS]->(b:User)
RETURN a.name, b.name
```

#### Multi-Hop Traversal
```cypher
MATCH (a:User {name: 'Alice'})-[:KNOWS]->(b:User)-[:KNOWS]->(c:User)
RETURN a.name, b.name, c.name
```

---

## RESP Query Wire Protocol

Queries can be executed over standard Redis connections:

```bash
# Using redis-cli
redis-cli -p 6379 "MATCH (a:User)-[:KNOWS]->(b:User) RETURN a.name, b.name"
```

The database returns standard RESP arrays representing table rows and columns.
