import logging
from psycopg_pool import ConnectionPool as PsycopgPool
from typing import Optional


class SystemConnectionPool:
    """
    Singleton connection pool manager for the meko_system database.

    The Langfuse project mapping lives in the meko_system DB, which may be a
    different database than the one served by the shared ``ConnectionPool``.
    This pool points directly at that database (YUGABYTEDB_SYSTEM_CONN_STRING)
    and is kept small since it only serves occasional key lookups.
    """

    _instance: Optional['SystemConnectionPool'] = None
    _pool: Optional[PsycopgPool] = None
    _logger = logging.getLogger(__name__)

    def __new__(cls):
        if cls._instance is None:
            cls._instance = super(SystemConnectionPool, cls).__new__(cls)
        return cls._instance

    @classmethod
    def initialize(
        cls,
        connection_string: str,
        min_size: int = 1,
        max_size: int = 1
    ) -> None:
        """
        Initialize the system connection pool.
        Should be called once at application startup.

        Args:
            connection_string (str): meko_system YugabyteDB connection string
            min_size (int): Minimum number of connections to keep open (default: 1)
            max_size (int): Maximum number of connections allowed (default: 1)
        """
        if not connection_string:
            raise ValueError("connection_string is required")

        if cls._pool is not None:
            cls._logger.warning(
                "SystemConnectionPool already initialized, skipping re-initialization"
            )
            return

        try:
            cls._pool = PsycopgPool(
                conninfo=connection_string,
                min_size=min_size,
                max_size=max_size
            )
            cls._logger.info(
                f"SystemConnectionPool initialized: min_size={min_size}, max_size={max_size}"
            )
        except Exception as e:
            cls._logger.error(f"Failed to initialize SystemConnectionPool: {str(e)}")
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
                "SystemConnectionPool not initialized. "
                "Call SystemConnectionPool.initialize() first."
            )

        try:
            return cls._pool.getconn()
        except Exception as e:
            cls._logger.error(f"Failed to get connection from system pool: {str(e)}")
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
                "Attempted to return connection but system pool not initialized"
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
            cls._logger.error(f"Failed to return connection to system pool: {str(e)}")

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
            cls._logger.info("SystemConnectionPool closed")
        except Exception as e:
            cls._logger.error(f"Error closing SystemConnectionPool: {str(e)}")

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
