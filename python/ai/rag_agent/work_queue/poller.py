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

    def poll(self, worker_id: str, lease_duration_seconds: int = 600) -> Optional[WorkQueueTask]:
        """
        Poll the work queue for new tasks.

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
                WHERE task_status = 'QUEUED'
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
