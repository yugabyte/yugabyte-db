import logging
import os
import threading
import time
from contextlib import contextmanager
import psycopg
import json
from datetime import datetime
from typing import Optional, Dict, Any, List
from enum import Enum
from db.capabilities import Capabilities
from db.connection_pool import ConnectionPool
from models.work_queue_task import WorkQueueTask


class Poller:
    """
    Class to poll the work queue for new tasks.
    """
    def __init__(self):
        self.connection_pool = ConnectionPool()

    def poll(
        self,
        worker_id: str,
        document_types: List[str],
        lease_duration_seconds: int = 600,
    ) -> Optional[WorkQueueTask]:
        """
        Poll the work queue for new tasks.

        Args:
            worker_id: Identifier for the current worker
            document_types: Non-empty list of MIME types to filter PREPROCESS
                tasks. CREATE_SOURCE tasks are always eligible regardless of
                this filter.
            lease_duration_seconds: Duration in seconds for which the task lease is valid

        Returns:
            WorkQueueTask with task details or None if no task available
        """
        connection = None
        try:
            # Get a fresh connection for this poll operation
            connection = self.connection_pool.get_connection()
            cursor = connection.cursor()

            self._run_maintenance(connection, cursor)
            claim = """
            UPDATE dist_rag.work_queue
            SET task_status = 'IN_PROGRESS',
                current_worker = %s,
                started_at = CURRENT_TIMESTAMP,
                lease_token = gen_random_uuid(),
                lease_acquired_at = CURRENT_TIMESTAMP,
                lease_expires_at = CURRENT_TIMESTAMP + INTERVAL '1 second' * %s
            WHERE (id, task_type) = (
                SELECT id, task_type FROM dist_rag.work_queue
                WHERE task_status = 'QUEUED' AND {match}
                  AND created_at <= CURRENT_TIMESTAMP
                ORDER BY created_at ASC LIMIT 1
                FOR UPDATE SKIP LOCKED
            )
            RETURNING id, task_type, task_details, lease_token, lease_expires_at;
            """
            # Sources first: a source's documents are only queued once it
            # completes, so a CREATE_SOURCE stuck behind a preprocessing
            # backlog starves its own documents. Then one document type at a
            # time: each statement reads a single row from
            # idx_work_queue_status_type_doctype_created_at, where an IN list
            # over the types would walk past other types' older rows. The
            # starting type rotates on every poll so no type waits behind
            # another type's backlog. A CREATE_SOURCE task has no document
            # type; pinning that index column to NULL lets ORDER BY created_at
            # read one row instead of every PREPROCESS row of the backlog.
            start = self._next_type_offset(len(document_types))
            types = document_types[start:] + document_types[:start]
            # A task whose worker died goes first, so a mid-flight document
            # doesn't wait behind the queued backlog.
            result = self._claim_expired(cursor, worker_id, document_types,
                                         lease_duration_seconds)
            claims = [] if result else (
                [("task_type = 'CREATE_SOURCE' AND task_details->>'document_type' IS NULL", ())]
                + [("task_type = 'PREPROCESS' AND task_details->>'document_type' = %s",
                    (doc_type,)) for doc_type in types]
            )
            for match, params in claims:
                cursor.execute(
                    claim.format(match=match),
                    (worker_id, lease_duration_seconds, *params),
                )
                result = cursor.fetchone()
                if result:
                    break
            connection.commit()

            if result:
                return WorkQueueTask.from_db_row(result)
            return None

        except Exception as e:
            if connection:
                try:
                    connection.rollback()
                except Exception:
                    pass
            logging.error(f"Error polling work queue: {str(e)}")
            raise

        finally:
            # Always return the connection to the pool
            if connection:
                self.connection_pool.return_connection(connection)

    def poll_embedding_generation(
        self, worker_id: str, lease_duration_seconds: int = 600
    ) -> Optional[WorkQueueTask]:
        """
        Poll the work queue for new embedding generation tasks.

        Args:
            worker_id: Identifier for the current worker
            lease_duration_seconds: Duration in seconds for which the task lease is valid

        Returns:
            WorkQueueTask with task details or None if no task available
        """
        connection = None
        try:
            # Get a fresh connection for this poll operation
            connection = self.connection_pool.get_connection()
            cursor = connection.cursor()

            query = """
            UPDATE dist_rag.work_queue
            SET task_status = 'IN_PROGRESS',
                current_worker = %s,
                started_at = CURRENT_TIMESTAMP,
                lease_token = gen_random_uuid(),
                lease_acquired_at = CURRENT_TIMESTAMP,
                lease_expires_at = CURRENT_TIMESTAMP + INTERVAL '1 second' * %s
            WHERE (id, task_type) = (
                SELECT id, task_type FROM dist_rag.work_queue
                WHERE task_status = 'QUEUED_EMBEDDING_GENERATION'
                ORDER BY created_at ASC LIMIT 1
                FOR UPDATE SKIP LOCKED
            )
            RETURNING id, task_type, task_details, lease_token, lease_expires_at;
            """

            cursor.execute(query, (worker_id, lease_duration_seconds))
            result = cursor.fetchone()
            connection.commit()

            if result:
                return WorkQueueTask.from_db_row(result)
            return None

        except Exception as e:
            if connection:
                try:
                    connection.rollback()
                except Exception:
                    pass
            logging.error(f"Error polling work queue: {str(e)}")
            raise

        finally:
            # Always return the connection to the pool
            if connection:
                self.connection_pool.return_connection(connection)

    def _run_maintenance(self, connection, cursor) -> None:
        """Sweep orphaned documents, then commit.

        It runs and commits on its own, so a claim that fails afterwards
        can't roll it back. A failure is logged and rolled back, never raised:
        maintenance must not stop a worker from claiming tasks. Its timer
        hasn't advanced, so the next poll retries it.
        """
        try:
            self._maybe_sweep_orphans(cursor)
            connection.commit()
        except Exception as e:
            try:
                connection.rollback()
            except Exception:
                pass
            logging.error(f"Work queue maintenance (_maybe_sweep_orphans) failed: {str(e)}")

    def _next_type_offset(self, count: int) -> int:
        """Rotate which document type a poll tries first."""
        offset = getattr(self, "_type_offset", 0) % max(count, 1)
        self._type_offset = offset + 1
        return offset

    _RESET_TO_QUEUED = (
        "task_status = 'QUEUED', current_worker = NULL, lease_token = NULL, "
        "lease_acquired_at = NULL, lease_expires_at = NULL"
    )

    # A task whose lease expires this many times is failed instead of run
    # again, so a task that kills its worker every time doesn't loop forever.
    # Failed lease renewals during a database outage also count, so keep
    # room for one: don't lower this to 1.
    MAX_EXPIRED_CLAIMS = int(os.getenv("TASK_MAX_EXPIRED_CLAIMS", "3"))

    # The new count, computed from the row being updated.
    _EXPIRED_COUNT = "COALESCE((task_details->>'expired_claims')::int, 0) + 1"

    def _claim_expired(self, cursor, worker_id, document_types,
                       lease_duration_seconds) -> Optional[tuple]:
        """Claim a task whose worker died holding it, or fail it at the cap.

        The claim renews the lease under a new token, which fences the old
        worker out of every task, document and source write. When the
        expired_claims count reaches MAX_EXPIRED_CLAIMS, the same UPDATE
        fails the task and clears its lease instead, and its document or
        source is failed before the caller commits, so all three commit
        together. A crash before that commit leaves the task as it was, and
        the next claim makes the same decision.

        Returns the claimed row for WorkQueueTask.from_db_row, or None.
        """
        failing = f"{self._EXPIRED_COUNT} >= %(max)s"
        cursor.execute(
            f"""
            UPDATE dist_rag.work_queue
            SET task_details = jsonb_set(task_details, '{{expired_claims}}',
                    to_jsonb({self._EXPIRED_COUNT})),
                task_status = CASE WHEN {failing} THEN 'FAILED' ELSE 'IN_PROGRESS'
                              END::dist_rag.task_queue_status_enum,
                completed_at = CASE WHEN {failing} THEN CURRENT_TIMESTAMP END,
                current_worker = %(worker)s,
                started_at = CURRENT_TIMESTAMP,
                lease_token = CASE WHEN {failing} THEN NULL ELSE gen_random_uuid() END,
                lease_acquired_at = CURRENT_TIMESTAMP,
                lease_expires_at = CASE WHEN {failing} THEN NULL
                    ELSE CURRENT_TIMESTAMP + INTERVAL '1 second' * %(lease)s END
            WHERE (id, task_type) = (
                SELECT id, task_type FROM dist_rag.work_queue
                WHERE task_status = 'IN_PROGRESS'
                  AND lease_expires_at < CURRENT_TIMESTAMP
                  AND (task_type = 'CREATE_SOURCE'
                       OR task_details->>'document_type' = ANY(%(types)s))
                ORDER BY lease_expires_at LIMIT 1
                FOR UPDATE SKIP LOCKED
            )
            RETURNING id, task_type, task_details, lease_token, lease_expires_at,
                      task_status;
            """,
            {"worker": worker_id, "lease": lease_duration_seconds,
             "max": self.MAX_EXPIRED_CLAIMS, "types": list(document_types)},
        )
        row = cursor.fetchone()
        if not row:
            return None
        task_id, task_type, details, _, _, status = row
        if status != "FAILED":
            logging.warning(f"Reclaimed task {task_id} after its lease expired")
            return row[:5]
        details = details or {}
        if task_type == "PREPROCESS" and details.get("document_id"):
            cursor.execute(
                "UPDATE dist_rag.documents SET status = 'FAILED' "
                "WHERE document_id = %s AND status <> 'COMPLETED'",
                (details["document_id"],),
            )
        elif task_type == "CREATE_SOURCE" and details.get("source_id"):
            cursor.execute(
                "UPDATE dist_rag.sources SET status = 'FAILED' "
                "WHERE id = %s AND status <> 'COMPLETED'",
                (details["source_id"],),
            )
        logging.error(
            f"Failed task {task_id} ({task_type}): its lease expired "
            f"{self.MAX_EXPIRED_CLAIMS} times"
        )
        return None

    _SWEEP_EVERY_SECONDS = int(os.getenv("ORPHAN_SWEEP_INTERVAL_SECONDS", "300"))
    _SWEEP_LIMIT = 1000

    def _win_sweep(self, cursor) -> bool:
        """True if this worker may sweep now.

        On 0.0.4, one compare-and-set on dist_rag.maintenance_runs lets one
        worker per interval sweep for the whole cluster. It commits before the
        sweep, so other pollers don't wait on the row for the sweep's duration;
        a crash after the commit only delays the next sweep by one interval.
        On 0.0.3 the table doesn't exist and every worker sweeps on its own
        timer, as before.
        """
        if not Capabilities.has(cursor, Capabilities.MAINTENANCE_RUNS):
            return True
        try:
            cursor.execute(
                "UPDATE dist_rag.maintenance_runs SET last_run_at = CURRENT_TIMESTAMP "
                "WHERE name = 'orphan_sweep' "
                "AND last_run_at < CURRENT_TIMESTAMP - INTERVAL '1 second' * %s "
                "RETURNING name",
                (self._SWEEP_EVERY_SECONDS,),
            )
        except psycopg.errors.UndefinedTable:
            Capabilities.forget(Capabilities.MAINTENANCE_RUNS)
            raise
        won = cursor.fetchone() is not None
        cursor.connection.commit()
        return won

    def _maybe_sweep_orphans(self, cursor) -> None:
        """Queue documents left with no live task for an index they belong to.

        A safety net, not the main path: the crawler queues each document in
        the same transaction it inserts it, but the api-server still inserts
        documents without tasks, and older agents left documents PROCESSING
        with no task. Only sources completed over five minutes ago are swept,
        so a document whose task is about to be queued is left alone; a
        double queue is rejected by uq_live_preprocess on 0.0.4, and on 0.0.3
        claim_document skips a document another task holds.
        idx_documents_unfinished keeps this from scanning every finished
        document.
        """
        now = time.monotonic()
        if now - getattr(self, "_last_sweep", 0.0) < self._SWEEP_EVERY_SECONDS:
            return
        if not self._win_sweep(cursor):
            self._last_sweep = now
            return
        cursor.execute(
            """
            SELECT DISTINCT d.document_id, d.source_id, d.status
            FROM dist_rag.documents d
            JOIN dist_rag.sources s ON s.id = d.source_id
            JOIN dist_rag.vector_index_source_mappings m ON m.source_id = d.source_id
            WHERE d.status IN ('QUEUED', 'PROCESSING')
              AND s.status = 'COMPLETED'
              AND s.completed_at < CURRENT_TIMESTAMP - INTERVAL '5 minutes'
              AND NOT EXISTS (
                  SELECT 1 FROM dist_rag.work_queue w
                  WHERE w.task_type = 'PREPROCESS'
                    AND w.task_status IN ('QUEUED', 'IN_PROGRESS')
                    AND w.task_details->>'document_id' = d.document_id::text
                    AND w.task_details->>'index_id' = m.index_id::text)
            LIMIT %s
            """,
            (self._SWEEP_LIMIT,),
        )
        orphans = cursor.fetchall()
        if not orphans:
            self._last_sweep = now
            return
        stuck = [str(doc) for doc, _, status in orphans if status == 'PROCESSING']
        if stuck:
            cursor.execute(
                "UPDATE dist_rag.documents SET status = 'QUEUED' "
                "WHERE document_id = ANY(%s::uuid[]) AND status = 'PROCESSING'",
                (stuck,),
            )
        for source_id in {str(src) for _, src, _ in orphans}:
            cursor.execute(
                "SELECT dist_rag._queue_source_documents(%s, m.index_id) "
                "FROM dist_rag.vector_index_source_mappings m WHERE m.source_id = %s",
                (source_id, source_id),
            )
        self._last_sweep = now
        logging.warning(
            f"Queued {len(orphans)} document(s) with no live task "
            f"({len(stuck)} reset from PROCESSING)"
        )

    @staticmethod
    def _fence(work_queue_id, lease_token):
        """WHERE clause and params matching a task only while this lease holds it.

        Another worker reclaims an expired task under the same id, so acting
        on the id alone would let a worker whose lease lapsed finish, fail or
        release the task a second worker now holds.
        """
        if lease_token is None:
            return "id = %s", (str(work_queue_id),)
        return "id = %s AND lease_token = %s", (str(work_queue_id), str(lease_token))

    def release(self, work_queue_id, delay_seconds: int, lease_token=None) -> bool:
        """Put a task back on the queue, not to be picked up for delay_seconds.

        For transient failures such as an embedding rate limit: the document
        is retried later instead of being marked FAILED. Each release adds one
        to task_details.attempt, so callers can cap the retries.
        """
        where, params = self._fence(work_queue_id, lease_token)
        return self._execute_dml(
            sql=(
                f"UPDATE dist_rag.work_queue SET {self._RESET_TO_QUEUED}, "
                "created_at = CURRENT_TIMESTAMP + INTERVAL '1 second' * %s, "
                "task_details = jsonb_set(task_details, '{attempt}', "
                "to_jsonb(COALESCE((task_details->>'attempt')::int, 0) + 1)) "
                f"WHERE {where} RETURNING id;"
            ),
            params=(int(delay_seconds), *params),
            work_queue_id=work_queue_id,
            operation="release-for-retry",
        )

    def holds_lease(self, work_queue_id, lease_token) -> bool:
        """True while this lease still holds the task and has not expired."""
        connection = None
        try:
            connection = self.connection_pool.get_connection()
            cursor = connection.cursor()
            cursor.execute(
                "SELECT 1 FROM dist_rag.work_queue "
                "WHERE id = %s AND lease_token = %s AND task_status = 'IN_PROGRESS' "
                "AND lease_expires_at > CURRENT_TIMESTAMP",
                (str(work_queue_id), str(lease_token)),
            )
            held = cursor.fetchone() is not None
            connection.commit()
            return held
        except Exception:
            if connection:
                try:
                    connection.rollback()
                except Exception:
                    pass
            raise
        finally:
            if connection:
                self.connection_pool.return_connection(connection)

    def mark_completed(self, work_queue_id, lease_token=None) -> bool:
        """
        Delete a finished work_queue row.

        Completion is modeled by DELETE rather than UPDATE so the queue
        stays compact -- otherwise every completed task would accumulate
        forever. Authoritative completion state lives in
        ``dist_rag.documents`` / ``dist_rag.sources`` (set by callers
        before this method runs) and in ``dist_rag.pipeline_details``,
        so removing the dispatch row loses no audit data.

        The DELETE also means a poll that reclaims expired IN_PROGRESS rows
        can't resurrect a finished task: there's nothing left to reclaim.

        Args:
            work_queue_id: The work_queue row id (UUID or str).
            lease_token: When given, delete the row only while this lease
                holds it.

        Returns:
            True if a row was deleted, False if no row matched (already
            gone). A False return is benign -- callers should log but not
            retry.
        """
        where, params = self._fence(work_queue_id, lease_token)
        return self._execute_dml(
            sql=f"DELETE FROM dist_rag.work_queue WHERE {where} RETURNING id;",
            params=params,
            work_queue_id=work_queue_id,
            operation="delete-completed",
        )

    def mark_failed(self, work_queue_id, lease_token=None) -> bool:
        """
        Mark a work_queue row as terminally FAILED.

        Failed rows are kept (not deleted) so a reconciliation process can
        scan ``dist_rag.work_queue WHERE task_status = 'FAILED'`` and
        decide what to do with them out-of-band (retry, alert, archive).
        The lease is cleared and the status leaves IN_PROGRESS, so no poll
        reclaims the row.

        Args:
            work_queue_id: The work_queue row id (UUID or str).
            lease_token: When given, update the row only while this lease
                holds it.

        Returns:
            True if a row was updated, False otherwise.
        """
        where, params = self._fence(work_queue_id, lease_token)
        return self._execute_dml(
            sql=(
                "UPDATE dist_rag.work_queue "
                "SET task_status = 'FAILED', "
                "    completed_at = CURRENT_TIMESTAMP, "
                "    lease_token = NULL, "
                "    lease_expires_at = NULL "
                f"WHERE {where} "
                "RETURNING id;"
            ),
            params=params,
            work_queue_id=work_queue_id,
            operation="mark-failed",
        )

    def _execute_dml(self, sql: str, params: tuple, work_queue_id, operation: str) -> bool:
        """Shared connection / commit / rollback boilerplate.

        Runs ``sql`` with ``params`` and returns whether the ``RETURNING``
        clause produced a row. ``operation`` is only used for error logs
        so the two callers can be told apart in the journal.
        """
        connection = None
        try:
            connection = self.connection_pool.get_connection()
            cursor = connection.cursor()
            cursor.execute(sql, params)
            affected = cursor.fetchone() is not None
            connection.commit()
            if not affected:
                logging.warning(
                    f"work_queue row {work_queue_id} not found during "
                    f"{operation}; the row may already be gone"
                )
            return affected
        except Exception as e:
            if connection:
                try:
                    connection.rollback()
                except Exception:
                    pass
            logging.error(
                f"Error during {operation} for work_queue {work_queue_id}: "
                f"{str(e)}"
            )
            raise
        finally:
            if connection:
                self.connection_pool.return_connection(connection)

    def renew_lease(self, work_queue_id: str, lease_token: str, lease_duration_seconds: int = 600):
        """
        Renew the lease for a task.

        Args:
            work_queue_id: ID of the work queue
            lease_token: Token to renew the lease
            lease_duration_seconds: Duration in seconds for which the task lease is valid
        """
        connection = None
        try:
            connection = self.connection_pool.get_connection()
            cursor = connection.cursor()

            query = """
            UPDATE dist_rag.work_queue
            SET lease_expires_at = CURRENT_TIMESTAMP + INTERVAL '1 second' * %s
            WHERE id = %s AND lease_token = %s
            RETURNING lease_expires_at;
            """
            cursor.execute(query, (lease_duration_seconds, work_queue_id, lease_token))
            result = cursor.fetchone()
            connection.commit()
            return result
        except Exception as e:
            if connection:
                try:
                    connection.rollback()
                except Exception:
                    pass
            logging.error(f"Error renewing lease for task {work_queue_id}: {str(e)}")
            raise
        finally:
            if connection:
                self.connection_pool.return_connection(connection)

    @contextmanager
    def keep_lease(self, task, lease_duration_seconds: int):
        """Renew a task's lease every third of its duration while the block runs.

        A poll reclaims any IN_PROGRESS task whose lease has expired, so a
        task that runs longer than its lease would be redelivered to a second
        worker while the first is still working on it. Two missed renewals in
        a row still leave the lease valid.
        """
        stop = threading.Event()
        interval = max(lease_duration_seconds / 3, 1)

        def renew():
            while not stop.wait(interval):
                try:
                    if not self.renew_lease(str(task.id), str(task.lease_token),
                                            lease_duration_seconds):
                        logging.warning(f"Lease for task {task.id} was lost; it may run twice")
                        return
                except Exception:
                    pass  # renew_lease logged it; try again next interval

        thread = threading.Thread(target=renew, name=f"lease-{task.id}", daemon=True)
        thread.start()
        try:
            yield
        finally:
            stop.set()
            thread.join(timeout=interval)
