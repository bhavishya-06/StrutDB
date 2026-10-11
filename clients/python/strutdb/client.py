import redis
from typing import Any, Optional


class StrutDBClient:
    """Thin Python client for StrutDB graph database wrapping Redis RESP protocol."""

    def __init__(
        self,
        host: str = "localhost",
        port: int = 6379,
        db: int = 0,
        decode_responses: bool = True,
        **kwargs: Any,
    ):
        """Initialize connection to StrutDB server."""
        self.host = host
        self.port = port
        self.client = redis.Redis(
            host=host,
            port=port,
            db=db,
            decode_responses=decode_responses,
            protocol=2,
            **kwargs,
        )

    def ping(self) -> bool:
        """Send PING to check server health."""
        res = self.client.ping()
        return bool(res)

    def execute(self, query_or_cmd: str, *args: Any) -> Any:
        """Execute a raw Cypher query or command against StrutDB and return the result."""
        query_or_cmd = query_or_cmd.strip()
        if not query_or_cmd:
            raise ValueError("Query cannot be empty")
        if args:
            return self.client.execute_command(query_or_cmd, *args)
        if query_or_cmd.upper().startswith("EVAL "):
            script = query_or_cmd[5:].strip()
            if (script.startswith('"') and script.endswith('"')) or (script.startswith("'") and script.endswith("'")):
                script = script[1:-1]
            return self.client.execute_command("EVAL", script)
        return self.client.execute_command(query_or_cmd)

    def eval(self, script: str) -> Any:
        """Execute a custom Lua script via the EVAL command."""
        script = script.strip()
        if not script:
            raise ValueError("Script cannot be empty")
        return self.client.execute_command("EVAL", script)

    def close(self) -> None:
        """Close the underlying client connection."""
        self.client.close()

    def __enter__(self) -> "StrutDBClient":
        return self

    def __exit__(self, exc_type: Any, exc_val: Any, exc_tb: Any) -> None:
        self.close()
