import logging
import psycopg
import os
import json
import hashlib
import uuid

from psycopg import sql
from db.connection_pool import ConnectionPool
from db.active_pipeline_tracking import PipelineTracking
from langfuse import get_client
from observability import meko_observe


@meko_observe(name="Execute SQL / YugabyteDB Vector Store", as_type="span",
              capture_input=False, capture_output=False)
def execute_sql(cur, query: str, params=None, many: bool = False) -> int:
    if many:
        cur.executemany(query, params)
    elif params is not None:
        cur.execute(query, params)
    else:
        cur.execute(query)
    rowcount = cur.rowcount
    is_langfuse_enabled = os.getenv("ENABLE_LANGFUSE_TRACING", "false") == "true"
    if is_langfuse_enabled:
        query_str = query.as_string(cur) if hasattr(query, 'as_string') else query
        get_client().update_current_span(
            input={
                "query": query_str.strip(),
                "mode": "executemany" if many else "execute",
                **({"batch_size": len(params)} if many and params is not None else {}),
                **({"param_count": len(params)} if not many and params is not None else {}),
            },
            output={"rowcount": rowcount}
        )

    return rowcount


# Chunk ids derive from (document_id, chunk position, chunk text) under this
# namespace. Re-ingesting the same content produces the same ids, so the
# insert skips the chunks an earlier, interrupted run already committed. A
# chunk whose text changed gets a new id, so new content is never dropped in
# favor of a stale chunk at the same position.
CHUNK_ID_NAMESPACE = uuid.UUID("6f1c8a52-3d0e-4b6a-9a51-2f6d7c0e9b13")


def chunk_id(document_id, position: int, chunk_text: str = "") -> str:
    digest = hashlib.sha256(chunk_text.encode("utf-8")).hexdigest()
    return str(uuid.uuid5(CHUNK_ID_NAMESPACE, f"{document_id}:{position}:{digest}"))


class LeaseLostError(RuntimeError):
    """The task's lease was lost mid-insert; another worker now owns the document."""


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

    def _table_exists(self, conn, table_name, table_schema):
        """Check if the table exists in the given schema using information_schema."""
        cur = conn.cursor()
        try:
            # Query information_schema to check if table exists
            execute_sql(
                cur,
                """
                SELECT EXISTS (
                    SELECT 1 FROM information_schema.tables
                    WHERE table_schema = %s AND table_name = %s
                )
                """,
                (table_schema, table_name)
            )
            result = cur.fetchone()[0]
            cur.close()
            conn.commit()  # Commit the transaction to avoid returning connection in INTRANS state
            return result
        except Exception as e:
            cur.close()
            conn.rollback()  # Rollback explicitly on error
            raise

    def _ensure_table_exists(self, table_name, schema):
        """Ensure the vector store table exists in the given schema."""
        conn = None
        try:
            conn = self._get_connection()

            # Check if table exists first
            if self._table_exists(conn, table_name, schema):
                self.logger.info(f"Table '{schema}.{table_name}' exists.")
                return True

            self.logger.info(f"Table '{schema}.{table_name}' doesn't exist.")
            return False

        except Exception as e:
            if conn:
                conn.rollback()
            self.logger.error(f"Failed to ensure table exists: {str(e)}")
            raise

        finally:
            if conn:
                self._return_connection(conn)

    @meko_observe(name="Insert Embeddings / YugabyteDB Vector Store", as_type="span")
    def insert_embeddings(
        self,
        document_id,
        table_name,
        embedding_iterator,
        pipeline_id,
        schema=None,
        metadata=None,
        batch_size=100,
        tenant_id=None,
        lease_check=None
    ):
        """
        Insert (chunk_text, embedding_vector) from iterable generator into the vector store.

        Args:
            document_id: UUID of the source document
            table_name: Name of the table to insert into
            embedding_iterator: Generator yielding (chunk_text, embedding_vector) tuples
            pipeline_id: Pipeline tracking id
            schema: Schema the backing vector table lives in. If None, falls back
                to the schema this instance was constructed with (default 'public').
                Resolved per-call so a single store instance can write to indexes
                that live in different schemas.
            metadata: Optional metadata dictionary
            batch_size: Number of records to insert per batch
            tenant_id: Optional UUID identifying the tenant. When None, the
                row is written with SQL NULL (i.e. "no tenant").
            lease_check: Optional callable returning False once the task's
                lease is lost. Checked before each batch commits, so a worker
                whose task was redelivered stops writing chunks for it.
        """
        # Resolve schema per call (caller-supplied beats constructor default).
        target_schema = schema or self.schema
        if not self._ensure_table_exists(table_name, target_schema):
            raise ValueError(f"Table '{target_schema}.{table_name}' doesn't exist.")

        conn = None
        try:
            conn = self._get_connection()
            cur = conn.cursor()

            # Convert metadata to JSON string if it's a dict
            metadata_json = json.dumps(metadata) if metadata is not None else json.dumps({})

            insert_stmt = sql.SQL(
                "INSERT INTO {schema}.{table} "
                "(id, chunk_text, embeddings, document_id, tenant_id, "
                "metadata_filters) VALUES (%s, %s, %s, %s, %s, %s) "
                "ON CONFLICT DO NOTHING"
            ).format(
                schema=sql.Identifier(target_schema),
                table=sql.Identifier(table_name),
            )

            batch = []
            total_inserted = 0
            items_yielded = 0
            for chunk_text, embedding_vector in embedding_iterator:
                items_yielded += 1
                # A deterministic id makes a re-run idempotent: chunks an
                # interrupted run committed (rate limit, a worker that died)
                # conflict and are skipped instead of stored twice. The
                # chunker is deterministic for a given file and settings.
                batch.append(
                    (chunk_id(document_id, items_yielded, chunk_text), chunk_text,
                     embedding_vector, document_id, tenant_id, metadata_json)
                )
                if len(batch) >= batch_size:
                    rows_inserted = execute_sql(
                        cur,
                        insert_stmt,
                        batch,
                        many=True
                    )
                    total_inserted += rows_inserted
                    self._check_lease(lease_check, document_id)
                    # update_embeddings_persisted adds to the stored count.
                    self.pipeline_tracking.update_embeddings_persisted(
                        pipeline_id=pipeline_id,
                        embeddings_count=rows_inserted
                    )
                    self.logger.info(
                        f"Inserted batch of {rows_inserted} rows into "
                        f"{target_schema}.{table_name} (total so far: "
                        f"{total_inserted}, items yielded: {items_yielded})"
                    )
                    conn.commit()
                    batch.clear()

            # Insert any remaining items in the batch
            if batch:
                rows_inserted = execute_sql(
                    cur,
                    insert_stmt,
                    batch,
                    many=True
                )
                total_inserted += rows_inserted
                self._check_lease(lease_check, document_id)
                self.pipeline_tracking.update_embeddings_persisted(
                    pipeline_id=pipeline_id,
                    embeddings_count=rows_inserted
                )
                self.logger.info(
                    f"Inserted final batch of {rows_inserted} rows into "
                    f"{target_schema}.{table_name} (total: {total_inserted})"
                )
                conn.commit()

            self.logger.info(
                f"Completed inserting all {total_inserted} embeddings into "
                f"{target_schema}.{table_name} (total items yielded by "
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

    @staticmethod
    def _check_lease(lease_check, document_id):
        if lease_check is not None and not lease_check():
            raise LeaseLostError(
                f"Lease lost while inserting embeddings for document {document_id}"
            )

    @meko_observe(name="Create Index / YugabyteDB Vector Store", as_type="span")
    def create_index(
        self,
        table_name,
        distance_metric="vector_cosine_ops",
        index_type="ybhnsw",
        table_schema=None
    ):
        """
        Create an index on the embeddings column of the table.

        Args:
            table_name: Backing vector table to index.
            distance_metric: pgvector ops class (e.g. vector_cosine_ops).
            index_type: Index method (currently always ybhnsw).
            table_schema: Schema the table lives in. If None, falls back to
                the schema this instance was constructed with.
        """
        target_schema = table_schema or self.schema
        conn = None
        try:
            conn = self._get_connection()
            cur = conn.cursor()

            # Try to create the index, ignore error if it already exists
            sql_create_index = sql.SQL(
                "CREATE INDEX IF NOT EXISTS {idx_name} "
                "ON {schema}.{table} "
                "USING ybhnsw (embeddings {ops_class})"
            ).format(
                idx_name=sql.Identifier(f"idx_{table_name}_embeddings"),
                schema=sql.Identifier(target_schema),
                table=sql.Identifier(table_name),
                ops_class=sql.SQL(distance_metric),
            )
            execute_sql(cur, sql_create_index)

            conn.commit()
            cur.close()
            self.logger.info(f"Index created successfully on {target_schema}.{table_name}")
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
