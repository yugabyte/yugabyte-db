"""
RegistrationCache - TTL-refreshed view of active column-embedding
registrations.

Registering a source table.column -> destination table.column mapping
(``dist_rag.register_column_embedding``) is a one-time operator action, not
something that happens on the worker's hot path. This cache exists purely so
the worker's poll loop doesn't re-query ``dist_rag.column_embedding_registrations``
(joined with ``dist_rag.vector_indexes`` for embedding-model config) on every
single cycle -- it refreshes at most once every ``refresh_interval_seconds``
and serves cached results in between.
"""

import logging
import time
import threading
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional
from uuid import UUID

from db.connection_pool import ConnectionPool

logger = logging.getLogger(__name__)


@dataclass
class ColumnEmbeddingRegistration:
    """One row from ``dist_rag.column_embedding_registrations``, joined with
    its ``dist_rag.vector_indexes`` row for ``ai_provider``/
    ``embedding_model_params`` -- read-only, never via ``init_vector_index``.
    """

    id: UUID
    registration_name: str
    status: str
    vector_index_id: UUID
    ai_provider: str
    embedding_model_params: Dict[str, Any]
    destination_schema: str
    destination_table: str
    destination_pk_column: str
    destination_embedding_column: str
    destination_tenant_column: Optional[str]
    destination_priority_column: Optional[str]
    destination_stale_claim_seconds: int
    destination_claim_batch_size: int
    destination_reserved_backfill_slots: int
    source_connection: str
    source_schema: str
    source_table: str
    source_text_columns: List[str]
    source_filter_by_columns: List[Dict[str, Any]]
    source_order_by: Optional[Dict[str, Any]] = field(default=None)

    @staticmethod
    def from_db_row(row: tuple) -> "ColumnEmbeddingRegistration":
        return ColumnEmbeddingRegistration(
            id=row[0],
            registration_name=row[1],
            status=row[2],
            vector_index_id=row[3],
            ai_provider=row[4],
            embedding_model_params=row[5] or {},
            destination_schema=row[6],
            destination_table=row[7],
            destination_pk_column=row[8],
            destination_embedding_column=row[9],
            destination_tenant_column=row[10],
            destination_priority_column=row[11],
            destination_stale_claim_seconds=row[12],
            destination_claim_batch_size=row[13],
            destination_reserved_backfill_slots=row[14],
            source_connection=row[15],
            source_schema=row[16],
            source_table=row[17],
            source_text_columns=list(row[18] or []),
            source_filter_by_columns=row[19] or [],
            source_order_by=row[20],
        )


_ACTIVE_REGISTRATIONS_QUERY = """
    SELECT
        r.id, r.registration_name, r.status, r.vector_index_id,
        vi.ai_provider, vi.embedding_model_params,
        r.destination_schema, r.destination_table, r.destination_pk_column,
        r.destination_embedding_column, r.destination_tenant_column,
        r.destination_priority_column, r.destination_stale_claim_seconds,
        r.destination_claim_batch_size, r.destination_reserved_backfill_slots,
        r.source_connection, r.source_schema, r.source_table,
        r.source_text_columns, r.source_filter_by_columns, r.source_order_by
    FROM dist_rag.column_embedding_registrations r
    JOIN dist_rag.vector_indexes vi ON vi.id = r.vector_index_id
    WHERE r.status = 'ACTIVE'
    ORDER BY r.registration_name
"""


class RegistrationCache:
    """Refreshes active registrations from the DB at most once every
    ``refresh_interval_seconds``; ``get_active()`` always returns the
    most recently loaded snapshot (empty list before the first successful
    refresh)."""

    def __init__(self, refresh_interval_seconds: int = 30):
        self._refresh_interval_seconds = refresh_interval_seconds
        self._connection_pool = ConnectionPool()
        self._lock = threading.Lock()
        self._registrations: List[ColumnEmbeddingRegistration] = []
        self._last_refreshed_at: float = 0.0

    def get_active(self) -> List[ColumnEmbeddingRegistration]:
        with self._lock:
            if time.time() - self._last_refreshed_at >= self._refresh_interval_seconds:
                self._refresh()
            return list(self._registrations)

    def _refresh(self) -> None:
        connection = None
        try:
            connection = self._connection_pool.get_connection()
            cursor = connection.cursor()
            cursor.execute(_ACTIVE_REGISTRATIONS_QUERY)
            rows = cursor.fetchall()
            connection.commit()
            self._registrations = [
                ColumnEmbeddingRegistration.from_db_row(row) for row in rows
            ]
            self._last_refreshed_at = time.time()
        except Exception as e:
            if connection:
                try:
                    connection.rollback()
                except Exception:
                    pass
            logger.error(f"Failed to refresh active column-embedding registrations: {e}")
            # Deliberately swallow: keep serving whatever was cached before
            # (self._registrations is untouched above) -- a transient DB
            # blip shouldn't stop already-known registrations from being
            # polled. _last_refreshed_at is also left untouched, so the next
            # get_active() call retries the refresh immediately rather than
            # waiting out the rest of refresh_interval_seconds.
        finally:
            if connection:
                self._connection_pool.return_connection(connection)
