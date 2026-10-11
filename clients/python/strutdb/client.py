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

    def execute(self, cypher_query: str) -> Any:
        """Execute a raw Cypher query against StrutDB and return the result."""
        cypher_query = cypher_query.strip()
        if not cypher_query:
            raise ValueError("Query cannot be empty")
        return self.client.execute_command(cypher_query)

    def close(self) -> None:
        """Close the underlying client connection."""
        self.client.close()

    def __enter__(self) -> "StrutDBClient":
        return self

    def __exit__(self, exc_type: Any, exc_val: Any, exc_tb: Any) -> None:
        self.close()
