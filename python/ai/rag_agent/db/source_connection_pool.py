import logging
from psycopg_pool import ConnectionPool as PsycopgPool
from typing import Optional


class SourceConnectionPool:
    """
    Singleton connection pool manager for wherever an AUTO_COLUMN_EMBEDDING
    worker reads source text from.

    Deliberately generic -- this worker type's whole point is "source
    table.column (text) -> destination table.column (vector)" for whatever
    source a task's task_details describes, so the pool itself has no
    Langfuse-specific (or any other source-specific) knowledge. For the
    conversation-embedding use case this happens to point at Langfuse's own
    backing database (a different logical database than the shared pool,
    on the same physical cluster -- see rag_pipeline.source_readers.
    read_from_langfuse), but a different deployment could point
    COLUMN_EMBED_SOURCE_DB_CONNECTION_STRING at anything else entirely;
    nothing here needs to change either way.
    """

    _instance: Optional['SourceConnectionPool'] = None
    _pool: Optional[PsycopgPool] = None
    _logger = logging.getLogger(__name__)

    def __new__(cls):
        if cls._instance is None:
            cls._instance = super(SourceConnectionPool, cls).__new__(cls)
        return cls._instance

    @classmethod
    def initialize(
        cls,
        connection_string: str,
        min_size: int = 1,
        max_size: int = 5
    ) -> None:
        """
        Initialize the source connection pool.
        Should be called once at application startup.

        Args:
            connection_string (str): Source database connection string
            min_size (int): Minimum number of connections to keep open (default: 1)
            max_size (int): Maximum number of connections allowed (default: 5)
        """
        if not connection_string:
            raise ValueError("connection_string is required")

        if cls._pool is not None:
            cls._logger.warning(
                "SourceConnectionPool already initialized, skipping re-initialization"
            )
            return

        try:
            cls._pool = PsycopgPool(
                conninfo=connection_string,
                min_size=min_size,
                max_size=max_size
            )
            cls._logger.info(
                f"SourceConnectionPool initialized: min_size={min_size}, max_size={max_size}"
            )
        except Exception as e:
            cls._logger.error(f"Failed to initialize SourceConnectionPool: {str(e)}")
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
                "SourceConnectionPool not initialized. "
                "Call SourceConnectionPool.initialize() first."
            )

        try:
            return cls._pool.getconn()
        except Exception as e:
            cls._logger.error(f"Failed to get connection from source pool: {str(e)}")
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
                "Attempted to return connection but source pool not initialized"
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
            cls._logger.error(f"Failed to return connection to source pool: {str(e)}")

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
            cls._logger.info("SourceConnectionPool closed")
        except Exception as e:
            cls._logger.error(f"Error closing SourceConnectionPool: {str(e)}")

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
