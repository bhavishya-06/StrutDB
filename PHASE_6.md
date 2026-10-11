# Phase 6: Persistence, Automation, and Client SDK

## 1. Overview
With Phases 1 through 5 complete—including the core graph engine, Cypher parser, Lua scripting, non-blocking epoll RESP server, and multi-stage Docker containerization—Phase 6 focuses on production readiness:
1. **Durable Persistence**: Mount Docker host volumes so RocksDB data persists across container restarts and re-deployments.
2. **CI/CD Automation**: Implement GitHub Actions workflows for continuous integration, testing, and multi-platform Docker container image publishing.
3. **Developer SDK (`strutdb-py`)**: Provide an idiomatic, lightweight Python client wrapper around StrutDB's RESP Cypher interface.

---

## 2. Milestone 1: Persistent Storage with Docker Volumes

### Problem Statement
In the default container deployment, RocksDB storage (`/data` or `/var/lib/strutdb`) lives in the container writable layer and is discarded when the container is recreated.

### Architecture & Implementation Plan
- **Database Storage Path**:
  - Configure StrutDB to store RocksDB WAL, SST, and manifest files under `/var/lib/strutdb/data`.
  - Expose an environment variable `STRUTDB_DATA_DIR` (default: `/var/lib/strutdb/data`).
- **Dockerfile Updates**:
  - Add `VOLUME ["/var/lib/strutdb/data"]` in `Dockerfile`.
  - Ensure the non-root application user has appropriate read/write permissions for `/var/lib/strutdb/data`.
- **Docker Compose Configuration**:
  - Create `docker-compose.yml` to define persistent named volumes:
    ```yaml
    version: '3.8'
    services:
      strutdb:
        image: strutdb:latest
        container_name: strutdb-server
        ports:
          - "6379:6379"
        volumes:
          - strutdb-data:/var/lib/strutdb/data
        restart: unless-stopped

    volumes:
      strutdb-data:
        driver: local
    ```
- **Verification Protocol**:
  - Seed nodes and edges into the running database.
  - Stop and remove the container (`docker rm -f strutdb-server`).
  - Start a new container with the same volume mount and verify that graph entities remain accessible.

---

## 3. Milestone 2: CI/CD Automation with GitHub Actions

### Objective
Automate build verification, code formatting/linting, unit and end-to-end testing, and Docker image publishing on every push and pull request.

### Workflow Specification (`.github/workflows/docker-build.yml`)
- **Triggers**:
  - `push` to `main` branch.
  - `pull_request` targeting `main`.
  - Manual trigger via `workflow_dispatch`.
- **Pipeline Jobs**:
  1. **Build & Unit Test (`build-test`)**:
     - Runner: `ubuntu-latest`.
     - Setup dependencies: `cmake`, `ninja-build`, `librocksdb-dev` (or FetchContent), `g++-12`.
     - Configure with `-G Ninja -DCMAKE_BUILD_TYPE=Release`.
     - Compile and execute test suite.
  2. **Docker Image Build & Integration Test (`docker-ci`)**:
     - Setup Docker Buildx.
     - Build multi-stage Docker image (`strutdb:test`).
     - Spin up container on port 6379.
     - Run `tests/e2e_test.py` against the containerized service.
  3. **Publishing / Release (on release tags)**:
     - Log in to GitHub Container Registry (`ghcr.io`).
     - Tag image as `ghcr.io/<org>/strutdb:latest` and `ghcr.io/<org>/strutdb:<version>`.
     - Push multi-arch (linux/amd64, linux/arm64) images.

---

## 4. Milestone 3: Python Client Wrapper (`strutdb-py`)

### Design Goals
Provide an ergonomic, high-level client interface that wraps the Redis RESP protocol and abstracts raw Cypher command execution into typed Python objects.

### Package Architecture
```
clients/python/
├── pyproject.toml
├── README.md
├── strutdb/
│   ├── __init__.py
│   ├── client.py        # Core StrutDB client class
│   ├── result.py        # Typed Node, Edge, Record, and ResultSet classes
│   └── exceptions.py    # QueryError, ConnectionError, TransactionError
└── tests/
    └── test_client.py
```

### Proposed API Surface
```python
import strutdb

# Connect to database
client = strutdb.Client(host="localhost", port=6379)

# Execute Cypher queries with parameters
client.query("CREATE (u:User {name: $name, age: $age})", {"name": "Alice", "age": 30})

# Query and iterate typed results
results = client.query("MATCH (a:User)-[r:KNOWS]->(b:User) RETURN a.name, b.name")
for record in results:
    print(record["a.name"], "knows", record["b.name"])
```

---

## 5. Execution Roadmap

| Step | Task | Status | Target Deliverable |
| :--- | :--- | :---: | :--- |
| **6.1** | Docker Volume Configuration | Planned | `docker-compose.yml`, updated `Dockerfile` |
| **6.2** | Persistence Integration Tests | Planned | `tests/test_persistence.py` |
| **6.3** | GitHub Actions CI/CD Pipeline | Planned | `.github/workflows/docker-build.yml` |
| **6.4** | `strutdb-py` Client Library | Planned | `clients/python/strutdb/` |
| **6.5** | Documentation & Release Notes | Planned | `docs/persistence.md`, `README.md` updates |
