import os
import random
import sys
import time

# Ensure strutdb package from clients/python is importable
repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, os.path.join(repo_root, "clients", "python"))

from strutdb import StrutDBClient


def run_benchmark():
    print("=" * 64, flush=True)
    print("       StrutDB High-Performance Graph Engine Stress Test", flush=True)
    print("=" * 64, flush=True)

    # 1. Connect and verify
    print("\n[1/4] Connecting to StrutDB on localhost:6379...", flush=True)
    client = StrutDBClient(host="localhost", port=6379)
    try:
        ping_ok = client.ping()
        print(f"      Connection verified: PING -> {ping_ok}", flush=True)
    except Exception as e:
        print(f"      Failed to connect: {e}", file=sys.stderr, flush=True)
        sys.exit(1)

    # 2. Batch insert 10,000 User nodes
    total_nodes = 10000
    node_batch_size = 20
    node_queries = total_nodes // node_batch_size
    print(
        f"\n[2/4] Batch inserting {total_nodes:,} User nodes "
        f"({node_batch_size} per query, {node_queries} queries)...",
        flush=True,
    )

    start_time = time.perf_counter()
    for batch_idx in range(node_queries):
        offset = batch_idx * node_batch_size
        nodes = [f"(u{i}:User {{id: {offset + i}, name: 'User_{offset + i}'}})" for i in range(node_batch_size)]
        query = f"CREATE {', '.join(nodes)}"
        client.execute(query)

        if (batch_idx + 1) % 100 == 0 or (batch_idx + 1) == node_queries:
            elapsed = time.perf_counter() - start_time
            done = (batch_idx + 1) * node_batch_size
            print(
                f"      Progress: {done:>6,}/{total_nodes:,} nodes "
                f"({done / elapsed:>8.1f} nodes/sec, {(batch_idx + 1) / elapsed:>6.1f} QPS)",
                flush=True,
            )
    node_duration = time.perf_counter() - start_time

    node_write_throughput = total_nodes / node_duration
    node_qps = node_queries / node_duration
    print(f"      -> Completed node insertion in {node_duration:.3f} s", flush=True)
    print(f"      -> Node Ingestion Rate:       {node_write_throughput:,.1f} nodes/sec", flush=True)
    print(f"      -> Node Write Throughput:     {node_qps:,.1f} QPS", flush=True)

    # 3. Batch insert 50,000 KNOWS edges between random users
    total_edges = 50000
    edge_batch_size = 10
    edge_queries = total_edges // edge_batch_size
    print(
        f"\n[3/4] Batch inserting {total_edges:,} KNOWS edges "
        f"({edge_batch_size} per query, {edge_queries} queries)...",
        flush=True,
    )

    random.seed(42)

    start_time = time.perf_counter()
    for batch_idx in range(edge_queries):
        edges = []
        for i in range(edge_batch_size):
            src_id = random.randint(0, total_nodes - 1)
            dst_id = random.randint(0, total_nodes - 1)
            edges.append(f"(a{i}:User {{id: {src_id}}})-[:KNOWS]->(b{i}:User {{id: {dst_id}}})")

        # In batch 0, include a connected 2-hop path (User_42 -> User_43 -> User_44)
        if batch_idx == 0:
            edges[0] = "(a0:User {name: 'User_42'})-[:KNOWS]->(b0:User {name: 'User_43'})-[:KNOWS]->(c0:User {name: 'User_44'})"
            edges.pop()  # Keep total count accurate

        query = f"CREATE {', '.join(edges)}"
        client.execute(query)

        if (batch_idx + 1) % 1000 == 0 or (batch_idx + 1) == edge_queries:
            elapsed = time.perf_counter() - start_time
            done = (batch_idx + 1) * edge_batch_size
            print(
                f"      Progress: {done:>6,}/{total_edges:,} edges "
                f"({done / elapsed:>8.1f} edges/sec, {(batch_idx + 1) / elapsed:>6.1f} QPS)",
                flush=True,
            )
    edge_duration = time.perf_counter() - start_time

    edge_write_throughput = total_edges / edge_duration
    edge_qps = edge_queries / edge_duration
    print(f"      -> Completed edge insertion in {edge_duration:.3f} s", flush=True)
    print(f"      -> Edge Ingestion Rate:       {edge_write_throughput:,.1f} edges/sec", flush=True)
    print(f"      -> Edge Write Throughput:     {edge_qps:,.1f} QPS", flush=True)

    # Combined write throughput
    total_writes = total_nodes + total_edges
    total_write_duration = node_duration + edge_duration
    overall_write_qps = (node_queries + edge_queries) / total_write_duration
    overall_entities_per_sec = total_writes / total_write_duration

    # 4. Multi-hop Cypher traversal read benchmark
    traversal_query = (
        "MATCH (a:User {name: 'User_42'})-[:KNOWS]->(b:User)-[:KNOWS]->(c:User) "
        "RETURN a.name, b.name, c.name"
    )
    print(f"\n[4/4] Executing multi-hop Cypher traversal read benchmark...", flush=True)
    print(f"      Query: {traversal_query}", flush=True)

    # Warmup
    warmup_res = client.execute(traversal_query)

    latencies = []
    iterations = 100
    for _ in range(iterations):
        t0 = time.perf_counter()
        res = client.execute(traversal_query)
        latencies.append((time.perf_counter() - t0) * 1000.0)

    avg_latency = sum(latencies) / len(latencies)
    min_latency = min(latencies)
    max_latency = max(latencies)
    sorted_latencies = sorted(latencies)
    p50_latency = sorted_latencies[int(len(sorted_latencies) * 0.50)]
    p95_latency = sorted_latencies[int(len(sorted_latencies) * 0.95)]
    p99_latency = sorted_latencies[int(len(sorted_latencies) * 0.99)]

    # 5. Final Report
    print("\n" + "=" * 64, flush=True)
    print("                    BENCHMARK RESULTS REPORT", flush=True)
    print("=" * 64, flush=True)
    print(f"  Total Entities Inserted:     {total_writes:,} (10,000 nodes + 50,000 edges)", flush=True)
    print(f"  Total Write Queries:         {node_queries + edge_queries:,}", flush=True)
    print(f"  Total Write Duration:        {total_write_duration:.3f} s", flush=True)
    print("-" * 64, flush=True)
    print(f"  Node Ingestion Rate:         {node_write_throughput:,.1f} nodes/sec", flush=True)
    print(f"  Node Write Throughput:       {node_qps:,.1f} QPS", flush=True)
    print(f"  Edge Ingestion Rate:         {edge_write_throughput:,.1f} edges/sec", flush=True)
    print(f"  Edge Write Throughput:       {edge_qps:,.1f} QPS", flush=True)
    print(f"  Combined Entity Write Rate:  {overall_entities_per_sec:,.1f} entities/sec", flush=True)
    print(f"  Combined Write QPS:          {overall_write_qps:,.1f} queries/sec", flush=True)
    print("-" * 64, flush=True)
    print("  Read Traversal Latency (Multi-Hop Cypher):", flush=True)
    print(f"    - Avg Traversal Latency:   {avg_latency:.2f} ms", flush=True)
    print(f"    - P50 Traversal Latency:   {p50_latency:.2f} ms", flush=True)
    print(f"    - P95 Traversal Latency:   {p95_latency:.2f} ms", flush=True)
    print(f"    - P99 Traversal Latency:   {p99_latency:.2f} ms", flush=True)
    print(f"    - Min / Max Latency:       {min_latency:.2f} ms / {max_latency:.2f} ms", flush=True)
    print(f"    - Traversal Result Rows:   {warmup_res}", flush=True)
    print("=" * 64, flush=True)

    client.close()


if __name__ == "__main__":
    run_benchmark()
