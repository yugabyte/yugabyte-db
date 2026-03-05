import logging
import psycopg
import os
import json

from psycopg import sql
from db.connection_pool import ConnectionPool
from db.active_pipeline_tracking import PipelineTracking


class YugabyteDBVectorStore:
    """
    Class to manage a persistent YugabyteDB vector embedding store connection
    for the lifecycle of the app.
    """
    def __init__(
        self,
        schema: str = "public"
    ):
        """
        Initialize the vector store, connect to YugabyteDB.
        Uses the shared ConnectionPool singleton for database access.
        """
        self.schema = schema
        self.pool = ConnectionPool()
        self.logger = logging.getLogger(__name__)
        self.pipeline_tracking = PipelineTracking()
        self.logger.info("YugabyteDBVectorStore initialized")

    def _get_connection(self):
        """Get a connection from the pool."""
        return self.pool.get_connection()

    def _return_connection(self, conn):
        """Return a connection to the pool."""
        self.pool.return_connection(conn)

    def _table_exists(self, conn, table_name):
        """Check if the table exists using information_schema."""
        cur = conn.cursor()
        try:
            # Query information_schema to check if table exists
            cur.execute(
                """
                SELECT EXISTS (
                    SELECT 1 FROM information_schema.tables
                    WHERE table_name = %s
                )
                """,
                (table_name,)
            )
            result = cur.fetchone()[0]
            cur.close()
            conn.commit()  # Commit the transaction to avoid returning connection in INTRANS state
            return result
        except Exception as e:
            cur.close()
            conn.rollback()  # Rollback explicitly on error
            raise

    def _ensure_table_exists(self, table_name):
        """Ensure the vector store table exists, creating it only if needed."""
        conn = None
        try:
            conn = self._get_connection()

            # Check if table exists first
            if self._table_exists(conn, table_name):
                self.logger.info(f"Table '{table_name}' exists.")
                return True

            self.logger.info(f"Table '{self.schema}.{table_name}' doesn't exist.")
            return False

        except Exception as e:
            if conn:
                conn.rollback()
            self.logger.error(f"Failed to ensure table exists: {str(e)}")
            raise

        finally:
            if conn:
                self._return_connection(conn)

    def insert_embeddings(
        self,
        document_id,
        table_name,
        embedding_iterator,
        pipeline_id,
        schema="public",
        metadata=None,
        batch_size=100
    ):
        """
        Insert (chunk_text, embedding_vector) from iterable generator into the vector store.

        Args:
            document_id: UUID of the source document
            table_name: Name of the table to insert into
            embedding_iterator: Generator yielding (chunk_text, embedding_vector) tuples
            metadata: Optional metadata dictionary
            batch_size: Number of records to insert per batch
        """
        if not self._ensure_table_exists(table_name):
            raise ValueError(f"Table '{self.schema}.{table_name}' doesn't exist.")

        conn = None
        try:
            conn = self._get_connection()
            cur = conn.cursor()

            # Convert metadata to JSON string if it's a dict
            metadata_json = json.dumps(metadata) if metadata is not None else json.dumps({})

            batch = []
            total_inserted = 0
            items_yielded = 0
            for chunk_text, embedding_vector in embedding_iterator:
                items_yielded += 1
                batch.append(
                    (chunk_text, embedding_vector, document_id, metadata_json)
                )
                if len(batch) >= batch_size:
                    cur.executemany(
                        f"INSERT INTO {self.schema}.{table_name} "
                        "(chunk_text, embeddings, document_id, "
                        "metadata_filters) VALUES (%s, %s, %s, %s)",
                        batch,
                    )
                    rows_inserted = cur.rowcount
                    total_inserted += rows_inserted
                    self.pipeline_tracking.update_embeddings_persisted(
                        pipeline_id=pipeline_id,
                        embeddings_count=total_inserted
                    )
                    self.logger.info(
                        f"Inserted batch of {rows_inserted} rows into "
                        f"{self.schema}.{table_name} (total so far: "
                        f"{total_inserted}, items yielded: {items_yielded})"
                    )
                    conn.commit()
                    batch.clear()

            # Insert any remaining items in the batch
            if batch:
                cur.executemany(
                    f"INSERT INTO {self.schema}.{table_name} "
                    "(chunk_text, embeddings, document_id, "
                    "metadata_filters) VALUES (%s, %s, %s, %s)",
                    batch
                )
                rows_inserted = cur.rowcount
                total_inserted += rows_inserted
                self.pipeline_tracking.update_embeddings_persisted(
                    pipeline_id=pipeline_id,
                    embeddings_count=total_inserted
                )
                self.logger.info(
                    f"Inserted final batch of {rows_inserted} rows into "
                    f"{self.schema}.{table_name} (total: {total_inserted})"
                )
                conn.commit()

            self.logger.info(
                f"Completed inserting all {total_inserted} embeddings into "
                f"{self.schema}.{table_name} (total items yielded by "
                f"generator: {items_yielded})"
            )
            cur.close()

        except Exception as e:
            if conn:
                conn.rollback()
            self.logger.error(f"Failed to insert embeddings: {str(e)}")
            raise

        finally:
            if conn:
                self._return_connection(conn)

    def create_index(
        self,
        table_name,
        distance_metric="vector_cosine_ops",
        index_type="ybhnsw"
    ):
        """
        Create an index on the embeddings column of the table.
        """
        conn = None
        try:
            conn = self._get_connection()
            cur = conn.cursor()

            # Try to create the index, ignore error if it already exists
            sql_create_index = f"""
                CREATE INDEX IF NOT EXISTS idx_{table_name}_embeddings
                ON {self.schema}.{table_name}
                USING ybhnsw (embeddings {distance_metric});
            """
            cur.execute(sql_create_index)

            conn.commit()
            cur.close()
            self.logger.info(f"Index created successfully on {self.schema}.{table_name}")
        except Exception as e:
            if conn:
                conn.rollback()
            self.logger.error(f"Failed to create index: {str(e)}")
            raise

        finally:
            if conn:
                self._return_connection(conn)
        return True

    def close(self):
        """
        Note: Connections are now managed by the ConnectionPool singleton.
        Individual instances don't need to close connections.
        """
        self.logger.info("YugabyteDBVectorStore instance closed (pool managed by singleton)")

    def __del__(self):
        """Destructor for emergency cleanup"""
        try:
            self.close()
        except Exception:
            pass
