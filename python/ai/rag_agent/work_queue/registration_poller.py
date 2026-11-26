"""
RegistrationPoller - discovers and claims rows for a registration-based
column-embedding mapping, without any ``dist_rag.work_queue`` task involved.

Two-phase, lock-free claim, per registered mapping:

  Phase 1 (candidates, no lock): plain SELECTs against the destination
  table LEFT JOINed with ``dist_rag.column_embedding_progress``, split into
  two capped sub-claims -- a live/default tier (``destination_priority_column``
  IS NULL or 0) and a backlog tier (any other value) that is ALWAYS attempted
  for up to ``destination_reserved_backfill_slots``, regardless of how much
  the live tier found. Without this split, a live tier that alone fills
  ``destination_claim_batch_size`` every cycle would starve a large backlog
  completely, not just slow it down. Skipped entirely (one plain query, no
  priority predicate) when the registration has no
  ``destination_priority_column`` configured.

  Phase 2 (claim, atomic): one bulk UPSERT into
  ``dist_rag.column_embedding_progress`` covering every phase-1 candidate at
  once, ``RETURNING`` only the row_pks that actually won. Two workers can see
  the same candidate in phase 1; only one wins here -- no exception, no lock
  contention, no ``SKIP LOCKED``, since correctness comes entirely from the
  UPSERT's conditional ``WHERE`` guard racing against a unique key, not from
  locking phase 1's SELECTs.

Constraint worth being explicit about: this requires the destination table
and ``dist_rag.column_embedding_progress`` to be reachable in the SAME
query, i.e. the same logical database -- there is no cross-database
join/dblink/FDW in play here (same limitation as the source-read side, see
rag_pipeline.source_readers). In practice this holds because
TargetConnectionPool defaults to the same database dist_rag is installed in
(YUGABYTEDB_CONNECTION_STRING) when COLUMN_EMBED_TARGET_DB_CONNECTION_STRING
is unset --
a registration whose destination genuinely lives in a different logical
database would need dist_rag installed there too, which is out of scope for
this design.
"""

import logging
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Any, Dict, List, Optional, Tuple

from psycopg import sql

from db.registration_cache import ColumnEmbeddingRegistration
from db.target_connection_pool import TargetConnectionPool

logger = logging.getLogger(__name__)


@dataclass
class ClaimedRow:
    """One destination row this worker just won the claim for, carrying
    everything ``AutoColumnEmbeddingProcessor`` needs without a second
    round-trip: which registration it belongs to, and its full column data
    (for filter-template row-derivation and fairness bucketing)."""

    registration: ColumnEmbeddingRegistration
    row: Dict[str, Any]
    claimed_at: datetime


def _qualified_table(registration: ColumnEmbeddingRegistration) -> sql.Composed:
    return sql.SQL("{}.{}").format(
        sql.Identifier(registration.destination_schema),
        sql.Identifier(registration.destination_table),
    )


def _key_predicate(
    registration: ColumnEmbeddingRegistration, tenant: Optional[Any], pk_expr: sql.Composable
) -> Tuple[sql.Composable, List[Any]]:
    """WHERE fragment locating destination rows by key, plus its leading
    params (the caller appends the pk value(s)).

    The key is compared in its own type, never ``pk::text``: a cast on the
    column rules out every index, so each lookup became a full scan of the
    destination table. When the registration names a tenant column it is
    added as an equality too, because a table keyed ``(tenant HASH, id ASC)``
    (e.g. ``datapacks.conversation_history_search``) can only seek on the
    PK when the hash column is bound.
    """
    pk = sql.Identifier(registration.destination_pk_column)
    if registration.destination_tenant_column and tenant is not None:
        tenant_col = sql.Identifier(registration.destination_tenant_column)
        return sql.SQL("{} = %s AND {} = {}").format(tenant_col, pk, pk_expr), [tenant]
    return sql.SQL("{} = {}").format(pk, pk_expr), []


_PROGRESS_STATE_CLAIMABLE = sql.SQL(
    "(p.dest_row_pk_in_text IS NULL"
    " OR (p.status IN ('QUEUED', 'FAILED')"
    "     AND (p.next_retry_at IS NULL OR p.next_retry_at <= NOW()))"
    " OR (p.status = 'IN_PROGRESS'"
    "     AND p.claimed_at < NOW() - INTERVAL '1 second' * %(stale_seconds)s))"
)


class RegistrationPoller:
    """Claims and finalizes rows for column-embedding registrations. One
    instance is shared across every active registration -- there is no
    per-registration state here, only per-call parameters."""

    def __init__(self):
        self.connection_pool = TargetConnectionPool()

    def claim_batch(
        self, registration: ColumnEmbeddingRegistration, worker_id: str
    ) -> List[ClaimedRow]:
        """Run phases 1-3 for one registration and return the rows this
        worker won, with full column data attached. Returns an empty list
        (not an error) when nothing is claimable right now."""
        connection = None
        try:
            connection = self.connection_pool.get_connection()
            cursor = connection.cursor()

            candidates = self._find_candidates(cursor, registration)
            if not candidates:
                connection.commit()
                return []

            won_pks = self._claim_candidates(
                cursor, registration, list(candidates), worker_id
            )
            connection.commit()
            if not won_pks:
                return []

            rows = self._fetch_rows(
                cursor, registration, {pk: candidates[pk] for pk in won_pks}
            )
            connection.commit()

            claimed_at = datetime.now(timezone.utc)
            return [ClaimedRow(registration=registration, row=row, claimed_at=claimed_at)
                    for row in rows]
        except Exception as e:
            if connection:
                try:
                    connection.rollback()
                except Exception:
                    pass
            logger.error(
                f"Error claiming batch for registration "
                f"{registration.registration_name!r}: {e}"
            )
            raise
        finally:
            if connection:
                self.connection_pool.return_connection(connection)

    def _find_candidates(
        self, cursor, registration: ColumnEmbeddingRegistration
    ) -> Dict[str, Tuple[Any, Any]]:
        """Candidate rows as ``{pk_text: (native_pk, tenant_or_None)}``, in
        claim order."""
        dest = _qualified_table(registration)
        pk = sql.Identifier(registration.destination_pk_column)
        embedding_col = sql.Identifier(registration.destination_embedding_column)
        params = {
            "reg_id": str(registration.id),
            "stale_seconds": registration.destination_stale_claim_seconds,
        }
        # Text form for the progress table; native pk (and tenant, when
        # configured) so later reads/writes can seek on the destination's PK.
        select_cols = sql.SQL("d.{pk}::text, d.{pk}").format(pk=pk)
        if registration.destination_tenant_column:
            select_cols = sql.SQL("{}, d.{}").format(
                select_cols, sql.Identifier(registration.destination_tenant_column)
            )

        if not registration.destination_priority_column:
            # No priority tiering configured -- one plain query, no split.
            query = sql.SQL(
                "SELECT {select_cols} FROM {dest} d"
                " LEFT JOIN dist_rag.column_embedding_progress p"
                "   ON p.registration_id = %(reg_id)s AND p.dest_row_pk_in_text = d.{pk}::text"
                " WHERE d.{embedding_col} IS NULL AND {progress_state}"
                " ORDER BY d.{pk} LIMIT %(limit)s"
            ).format(
                pk=pk, dest=dest, embedding_col=embedding_col, select_cols=select_cols,
                progress_state=_PROGRESS_STATE_CLAIMABLE,
            )
            cursor.execute(query, {**params, "limit": registration.destination_claim_batch_size})
            return self._candidate_keys(cursor.fetchall())

        priority = sql.Identifier(registration.destination_priority_column)
        candidates: Dict[str, Tuple[Any, Any]] = {}

        live_limit = max(
            registration.destination_claim_batch_size
            - registration.destination_reserved_backfill_slots,
            0,
        )
        if live_limit > 0:
            query_a = sql.SQL(
                "SELECT {select_cols} FROM {dest} d"
                " LEFT JOIN dist_rag.column_embedding_progress p"
                "   ON p.registration_id = %(reg_id)s AND p.dest_row_pk_in_text = d.{pk}::text"
                " WHERE d.{embedding_col} IS NULL"
                "   AND (d.{priority} IS NULL OR d.{priority} = 0)"
                "   AND {progress_state}"
                " ORDER BY d.{pk} LIMIT %(limit)s"
            ).format(
                pk=pk, dest=dest, embedding_col=embedding_col, priority=priority,
                select_cols=select_cols,
                progress_state=_PROGRESS_STATE_CLAIMABLE,
            )
            cursor.execute(query_a, {**params, "limit": live_limit})
            candidates.update(self._candidate_keys(cursor.fetchall()))

        if registration.destination_reserved_backfill_slots > 0:
            query_b = sql.SQL(
                "SELECT {select_cols} FROM {dest} d"
                " LEFT JOIN dist_rag.column_embedding_progress p"
                "   ON p.registration_id = %(reg_id)s AND p.dest_row_pk_in_text = d.{pk}::text"
                " WHERE d.{embedding_col} IS NULL"
                "   AND d.{priority} IS NOT NULL AND d.{priority} != 0"
                "   AND {progress_state}"
                " ORDER BY d.{priority} ASC, d.{pk} LIMIT %(limit)s"
            ).format(
                pk=pk, dest=dest, embedding_col=embedding_col, priority=priority,
                select_cols=select_cols,
                progress_state=_PROGRESS_STATE_CLAIMABLE,
            )
            cursor.execute(
                query_b,
                {**params, "limit": registration.destination_reserved_backfill_slots},
            )
            candidates.update(self._candidate_keys(cursor.fetchall()))

        return candidates

    @staticmethod
    def _candidate_keys(rows) -> Dict[str, Tuple[Any, Any]]:
        return {row[0]: (row[1], row[2] if len(row) > 2 else None) for row in rows}

    def _claim_candidates(
        self,
        cursor,
        registration: ColumnEmbeddingRegistration,
        candidate_pks: List[str],
        worker_id: str,
    ) -> List[str]:
        query = sql.SQL(
            "INSERT INTO dist_rag.column_embedding_progress"
            "    (registration_id, dest_row_pk_in_text, status, claimed_by, claimed_at, attempts)"
            " SELECT %(reg_id)s, x.dest_row_pk_in_text, 'IN_PROGRESS', %(worker_id)s, NOW(), 0"
            " FROM unnest(%(candidate_pks)s::text[]) AS x(dest_row_pk_in_text)"
            " ON CONFLICT (registration_id, dest_row_pk_in_text) DO UPDATE"
            "     SET status = 'IN_PROGRESS', claimed_by = %(worker_id)s, claimed_at = NOW()"
            "     WHERE column_embedding_progress.status IN ('QUEUED', 'FAILED')"
            "        OR (column_embedding_progress.status = 'IN_PROGRESS'"
            "            AND column_embedding_progress.claimed_at"
            "                < NOW() - INTERVAL '1 second' * %(stale_seconds)s)"
            " RETURNING dest_row_pk_in_text;"
        )
        cursor.execute(
            query,
            {
                "reg_id": str(registration.id),
                "worker_id": worker_id,
                "candidate_pks": candidate_pks,
                "stale_seconds": registration.destination_stale_claim_seconds,
            },
        )
        return [row[0] for row in cursor.fetchall()]

    def _fetch_rows(
        self,
        cursor,
        registration: ColumnEmbeddingRegistration,
        won: Dict[str, Tuple[Any, Any]],
    ) -> List[Dict[str, Any]]:
        """Read the won rows by key, one point-lookup query per tenant."""
        dest = _qualified_table(registration)
        by_tenant: Dict[Any, List[Any]] = {}
        for native_pk, tenant in won.values():
            by_tenant.setdefault(tenant, []).append(native_pk)

        rows: List[Dict[str, Any]] = []
        for tenant, native_pks in by_tenant.items():
            where, lead = _key_predicate(registration, tenant, sql.SQL("ANY(%s)"))
            cursor.execute(
                sql.SQL("SELECT * FROM {dest} WHERE {where}").format(dest=dest, where=where),
                lead + [native_pks],
            )
            columns = [desc[0] for desc in cursor.description]
            rows.extend(dict(zip(columns, row)) for row in cursor.fetchall())
        return rows

    def finalize_success(self, claimed_row: ClaimedRow, embedding: List[float]) -> None:
        """Write the embedding to the destination row and delete its
        progress row -- deletion (not a 'DONE' status) is the completion
        signal, since a non-NULL embedding column is itself sufficient for
        discovery to never find this row again."""
        registration = claimed_row.registration
        row_pk = str(claimed_row.row[registration.destination_pk_column])
        connection = None
        try:
            connection = self.connection_pool.get_connection()
            cursor = connection.cursor()
            tenant = (
                claimed_row.row.get(registration.destination_tenant_column)
                if registration.destination_tenant_column else None
            )
            where, lead = _key_predicate(registration, tenant, sql.SQL("%s"))
            update_query = sql.SQL("UPDATE {dest} SET {embedding_col} = %s WHERE {where}").format(
                dest=_qualified_table(registration),
                embedding_col=sql.Identifier(registration.destination_embedding_column),
                where=where,
            )
            cursor.execute(
                update_query,
                [embedding, *lead, claimed_row.row[registration.destination_pk_column]],
            )
            cursor.execute(
                "DELETE FROM dist_rag.column_embedding_progress"
                " WHERE registration_id = %s AND dest_row_pk_in_text = %s",
                (str(registration.id), row_pk),
            )
            connection.commit()
        except Exception as e:
            if connection:
                try:
                    connection.rollback()
                except Exception:
                    pass
            logger.error(
                f"Error finalizing success for {registration.registration_name!r} "
                f"row {row_pk}: {e}"
            )
            raise
        finally:
            if connection:
                self.connection_pool.return_connection(connection)

    def finalize_retry(
        self,
        claimed_row: ClaimedRow,
        error: str,
        max_attempts: int,
        backoff_base_seconds: float,
    ) -> str:
        """Increment attempts and either re-queue with exponential backoff
        or mark FAILED once max_attempts is reached -- decided and applied
        in one UPDATE so there's no separate read-then-write race. Returns
        the resulting status ('QUEUED' or 'FAILED') for the caller to log.
        This state lives on the DB row, so it survives a worker crash --
        unlike an in-memory retry queue, nothing here is lost on restart.
        """
        registration = claimed_row.registration
        row_pk = str(claimed_row.row[registration.destination_pk_column])
        connection = None
        try:
            connection = self.connection_pool.get_connection()
            cursor = connection.cursor()
            cursor.execute(
                """
                UPDATE dist_rag.column_embedding_progress
                SET attempts = attempts + 1,
                    last_error = %(error)s,
                    status = CASE WHEN attempts + 1 >= %(max_attempts)s
                                  THEN 'FAILED' ELSE 'QUEUED' END,
                    next_retry_at = CASE WHEN attempts + 1 >= %(max_attempts)s
                                  THEN NULL
                                  ELSE NOW() + INTERVAL '1 second'
                                       * (%(backoff_base)s * POWER(2, attempts))
                             END
                WHERE registration_id = %(reg_id)s AND dest_row_pk_in_text = %(row_pk)s
                RETURNING status;
                """,
                {
                    "error": error,
                    "max_attempts": max_attempts,
                    "backoff_base": backoff_base_seconds,
                    "reg_id": str(registration.id),
                    "row_pk": row_pk,
                },
            )
            result = cursor.fetchone()
            connection.commit()
            return result[0] if result else "FAILED"
        except Exception as e:
            if connection:
                try:
                    connection.rollback()
                except Exception:
                    pass
            logger.error(
                f"Error finalizing retry for {registration.registration_name!r} "
                f"row {row_pk}: {e}"
            )
            raise
        finally:
            if connection:
                self.connection_pool.return_connection(connection)

    def finalize_failure(self, claimed_row: ClaimedRow, error: str) -> None:
        """Mark a row permanently FAILED without incrementing/retrying --
        for permanent misses (e.g. the source row itself is gone) rather
        than transient errors. Kept (not deleted) for reconciliation,
        mirroring dist_rag.work_queue's own failed-row convention."""
        registration = claimed_row.registration
        row_pk = str(claimed_row.row[registration.destination_pk_column])
        connection = None
        try:
            connection = self.connection_pool.get_connection()
            cursor = connection.cursor()
            cursor.execute(
                "UPDATE dist_rag.column_embedding_progress"
                " SET status = 'FAILED', last_error = %s, next_retry_at = NULL"
                " WHERE registration_id = %s AND dest_row_pk_in_text = %s",
                (error, str(registration.id), row_pk),
            )
            connection.commit()
        except Exception as e:
            if connection:
                try:
                    connection.rollback()
                except Exception:
                    pass
            logger.error(
                f"Error finalizing failure for {registration.registration_name!r} "
                f"row {row_pk}: {e}"
            )
            raise
        finally:
            if connection:
                self.connection_pool.return_connection(connection)
