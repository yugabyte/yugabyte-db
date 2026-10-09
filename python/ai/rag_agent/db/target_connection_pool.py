import logging
from psycopg_pool import ConnectionPool as PsycopgPool
from typing import Optional


class TargetConnectionPool:
    """
    Singleton connection pool manager for wherever an AUTO_COLUMN_EMBEDDING
    worker writes the resulting embedding to.

    Kept as its own pool (distinct from the shared ConnectionPool) so
    source and target are independently configurable.
    COLUMN_EMBED_TARGET_DB_CONNECTION_STRING falls back to
    YUGABYTEDB_CONNECTION_STRING when unset, so that's a no-op config-wise,
    but AutoColumnEmbeddingProcessor's write path always goes through this
    pool uniformly rather than branching on whether target == the main DB.
    """

    _instance: Optional['TargetConnectionPool'] = None
    _pool: Optional[PsycopgPool] = None
    _logger = logging.getLogger(__name__)

    def __new__(cls):
        if cls._instance is None:
            cls._instance = super(TargetConnectionPool, cls).__new__(cls)
        return cls._instance

    @classmethod
    def initialize(
        cls,
        connection_string: str,
        min_size: int = 1,
        max_size: int = 5
    ) -> None:
        """
        Initialize the target connection pool.
        Should be called once at application startup.

        Args:
            connection_string (str): Target database connection string
            min_size (int): Minimum number of connections to keep open (default: 1)
            max_size (int): Maximum number of connections allowed (default: 5)
        """
        if not connection_string:
            raise ValueError("connection_string is required")

        if cls._pool is not None:
            cls._logger.warning(
                "TargetConnectionPool already initialized, skipping re-initialization"
            )
            return

        try:
            cls._pool = PsycopgPool(
                conninfo=connection_string,
                min_size=min_size,
                max_size=max_size
            )
            cls._logger.info(
                f"TargetConnectionPool initialized: min_size={min_size}, max_size={max_size}"
            )
        except Exception as e:
            cls._logger.error(f"Failed to initialize TargetConnectionPool: {str(e)}")
            raise

    @classmethod
    def get_connection(cls):
        """
        Get a connection from the pool.

        Returns:
            psycopg.Connection: A connection from the pool

        Raises:
            RuntimeError: If pool is not initialized
        """
        if cls._pool is None:
            raise RuntimeError(
                "TargetConnectionPool not initialized. "
                "Call TargetConnectionPool.initialize() first."
            )

        try:
            return cls._pool.getconn()
        except Exception as e:
            cls._logger.error(f"Failed to get connection from target pool: {str(e)}")
            raise

    @classmethod
    def return_connection(cls, conn) -> None:
        """
        Return a connection to the pool.

        Args:
            conn: The connection to return
        """
        if cls._pool is None:
            cls._logger.warning(
                "Attempted to return connection but target pool not initialized"
            )
            if conn:
                try:
                    conn.close()
                except Exception:
                    pass
            return

        try:
            cls._pool.putconn(conn)
        except Exception as e:
            cls._logger.error(f"Failed to return connection to target pool: {str(e)}")

    @classmethod
    def close_all(cls) -> None:
        """
        Close all connections in the pool.
        Should be called at application shutdown.
        """
        if cls._pool is None:
            return

        try:
            cls._pool.close()
            cls._pool = None
            cls._logger.info("TargetConnectionPool closed")
        except Exception as e:
            cls._logger.error(f"Error closing TargetConnectionPool: {str(e)}")

    @classmethod
    def get_pool_status(cls) -> dict:
        """
        Get the current status of the connection pool.

        Returns:
            dict: Pool status information
        """
        if cls._pool is None:
            return {"status": "not_initialized"}

        stats = cls._pool.get_stats()
        return {
            "status": "initialized",
            "min_size": cls._pool.min_size,
            "max_size": cls._pool.max_size,
            "size": stats.get("pool_size"),
            "available": stats.get("pool_available"),
        }
