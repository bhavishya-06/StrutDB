import sys
import redis

def run_tests():
    print("Connecting to StrutDB on localhost:6379...")
    r = redis.Redis(host="localhost", port=6379, decode_responses=True, protocol=2)

    # 1. Ping
    print("\n[Step 1] Sending PING...")
    ping_res = r.ping()
    print(f"PING response: {ping_res}")
    assert ping_res is True or ping_res == "PONG", f"Expected True or 'PONG', got {ping_res}"
    print("  -> Connectivity verified.")

    # 2. CREATE nodes
    print("\n[Step 2] Creating nodes (Alice and Bob)...")
    create_nodes_query = "CREATE (n:User {name: 'Alice'}), (m:User {name: 'Bob'})"
    res1 = r.execute_command(create_nodes_query)
    print(f"CREATE nodes response: {res1}")

    # 3. CREATE edge
    print("\n[Step 3] Creating edge KNOWS between Alice and Bob...")
    create_edge_query = "MATCH (a:User {name: 'Alice'}), (b:User {name: 'Bob'}) CREATE (a)-[:KNOWS]->(b)"
    res2 = r.execute_command(create_edge_query)
    print(f"CREATE edge response: {res2}")

    # 4. MATCH and RETURN
    print("\n[Step 4] Querying relationship...")
    match_query = "MATCH (a:User)-[:KNOWS]->(b:User) RETURN a.name, b.name"
    res3 = r.execute_command(match_query)
    print(f"MATCH RETURN response: {res3}")

    # Assert correctness
    assert res3 is not None, "Query returned None"
    res_str = str(res3)
    assert "Alice" in res_str and "Bob" in res_str, f"Expected Alice and Bob in results, got {res3}"
    
    print("\n==========================================")
    print("ALL TESTS PASSED SUCCESSFULLY!")
    print(f"Retrieved Relationship: {res3}")
    print("==========================================")

if __name__ == "__main__":
    try:
        run_tests()
    except Exception as e:
        print(f"\nTEST FAILED with error: {e}", file=sys.stderr)
        sys.exit(1)
