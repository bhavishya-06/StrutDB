import os
import sys

# Ensure clients/python is in sys.path
repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, os.path.join(repo_root, "clients", "python"))

from strutdb import StrutDBClient


def run_lua_tests():
    print("=" * 60)
    print("       StrutDB sol2 Embedded Lua Engine Integration Test")
    print("=" * 60)

    print("\n[Step 1] Connecting to StrutDB on localhost:6379...")
    client = StrutDBClient(host="localhost", port=6379)
    assert client.ping(), "Failed to ping StrutDB server"
    print("  -> Connectivity verified.")

    # 1. Custom Arithmetic & String formatting in Lua
    print("\n[Step 2] Executing custom Lua arithmetic and string formatting...")
    lua_script_1 = """
        local x = 42
        local y = 58
        local sum = x + y
        return string.format("Computed sum: %d + %d = %d", x, y, sum)
    """
    res1 = client.eval(lua_script_1)
    print(f"  -> Result: {res1}")
    assert "100" in str(res1), f"Unexpected result: {res1}"

    # 2. Custom Algorithm (Fibonacci sequence in Lua)
    print("\n[Step 3] Executing custom Lua algorithm (Fibonacci computation)...")
    lua_script_2 = """
        local function fib(n)
            local a, b = 0, 1
            for i = 1, n do
                a, b = b, a + b
            end
            return a
        end
        return string.format("Fibonacci(10) = %d", fib(10))
    """
    res2 = client.eval(lua_script_2)
    print(f"  -> Result: {res2}")
    assert "55" in str(res2), f"Unexpected result: {res2}"

    # 3. Native Graph API invocation from Lua (strutdb namespace)
    print("\n[Step 4] Executing custom Lua graph algorithm using strutdb API...")
    lua_script_3 = """
        -- Create two nodes
        local n1 = strutdb.add_node()
        local n2 = strutdb.add_node()

        -- Create a directed edge with type ID 100
        local edge_id = strutdb.add_edge(n1, n2, 100)

        -- Query outgoing edges
        local out_edges = strutdb.outgoing(n1)
        local out_count = #out_edges

        return string.format(
            "Graph Lua UDF: Created nodes (%d, %d), edge %d. Node %d outgoing edge count: %d",
            n1, n2, edge_id, n1, out_count
        )
    """
    res3 = client.eval(lua_script_3)
    print(f"  -> Result: {res3}")
    assert "Graph Lua UDF" in str(res3), f"Unexpected result: {res3}"

    print("\n============================================================")
    print("ALL LUA ENGINE TESTS PASSED SUCCESSFULLY!")
    print("Sol2 C++ embedded engine successfully compiled & executed logic.")
    print("============================================================")

    client.close()


if __name__ == "__main__":
    try:
        run_lua_tests()
    except Exception as e:
        print(f"\nLUA TEST FAILED with error: {e}", file=sys.stderr)
        sys.exit(1)
