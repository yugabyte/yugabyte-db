import logging
import psycopg
import json
from datetime import datetime
from typing import Optional, Dict, Any, List
from enum import Enum
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

            placeholders = ','.join(['%s'] * len(document_types))
            query = f"""
            UPDATE dist_rag.work_queue
            SET task_status = 'IN_PROGRESS',
                current_worker = %s,
                started_at = CURRENT_TIMESTAMP,
                lease_token = gen_random_uuid(),
                lease_acquired_at = CURRENT_TIMESTAMP,
                lease_expires_at = CURRENT_TIMESTAMP + INTERVAL '1 second' * %s
            WHERE (id, task_type) = (
                SELECT id, task_type FROM dist_rag.work_queue
                WHERE task_status = 'QUEUED'
                  AND (
                      task_type = 'CREATE_SOURCE'
                      OR (task_type = 'PREPROCESS'
                          AND task_details->>'document_type' IN ({placeholders}))
                  )
                ORDER BY created_at ASC LIMIT 1
                FOR UPDATE SKIP LOCKED
            )
            RETURNING id, task_type, task_details, lease_token, lease_expires_at;
            """

            cursor.execute(query, (worker_id, lease_duration_seconds, *document_types))
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

    def mark_completed(self, work_queue_id) -> bool:
        """
        Delete a finished work_queue row.

        Completion is modeled by DELETE rather than UPDATE so the queue
        stays compact -- otherwise every completed task would accumulate
        forever. Authoritative completion state lives in
        ``dist_rag.documents`` / ``dist_rag.sources`` (set by callers
        before this method runs) and in ``dist_rag.pipeline_details``,
        so removing the dispatch row loses no audit data.

        The DELETE also means the SQL-side reaper that re-queues expired
        IN_PROGRESS rows can't resurrect a finished task: there's nothing
        left to resurrect.

        Args:
            work_queue_id: The work_queue row id (UUID or str).

        Returns:
            True if a row was deleted, False if no row matched (already
            gone). A False return is benign -- callers should log but not
            retry.
        """
        return self._execute_dml(
            sql="DELETE FROM dist_rag.work_queue WHERE id = %s RETURNING id;",
            params=(str(work_queue_id),),
            work_queue_id=work_queue_id,
            operation="delete-completed",
        )

    def mark_failed(self, work_queue_id) -> bool:
        """
        Mark a work_queue row as terminally FAILED.

        Failed rows are kept (not deleted) so a reconciliation process can
        scan ``dist_rag.work_queue WHERE task_status = 'FAILED'`` and
        decide what to do with them out-of-band (retry, alert, archive).
        The lease is cleared so the SQL-side reaper won't re-queue the
        row on lease expiry.

        Args:
            work_queue_id: The work_queue row id (UUID or str).

        Returns:
            True if a row was updated, False otherwise.
        """
        return self._execute_dml(
            sql=(
                "UPDATE dist_rag.work_queue "
                "SET task_status = 'FAILED', "
                "    completed_at = CURRENT_TIMESTAMP, "
                "    lease_token = NULL, "
                "    lease_expires_at = NULL "
                "WHERE id = %s "
                "RETURNING id;"
            ),
            params=(str(work_queue_id),),
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
