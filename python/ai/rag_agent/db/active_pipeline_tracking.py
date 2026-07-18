import logging
import psycopg
import json
import uuid
from datetime import datetime
from typing import Optional, Dict, Any
from enum import Enum
from db.connection_pool import ConnectionPool


class PipelineStatus(Enum):
    """Enum for pipeline status values"""
    PROCESSING = 'PROCESSING'
    COMPLETED = 'COMPLETED'
    FAILED = 'FAILED'


class PipelineTracking:
    """
    Class to manage the reporting and lifecycle of the RAG pipeline.
    Handles pipeline status tracking, error logging, and metadata management.
    """

    def __init__(self):
        """
        Initialize the PipelineTracking instance.
        Uses the shared ConnectionPool singleton for database access.
        """
        self.pool = ConnectionPool()
        self.logger = logging.getLogger(__name__)
        self.logger.info("PipelineTracking initialized")

    def _get_connection(self):
        """Get a connection from the pool."""
        return self.pool.get_connection()

    def _return_connection(self, conn):
        """Return a connection to the pool."""
        self.pool.return_connection(conn)

    def insert_pipeline_details(
        self,
        document_id: str,
        document_name: str,
        pipeline_status: str = 'PROCESSING',
        metadata: Optional[Dict[str, Any]] = None
    ) -> str:
        """
        Insert a new pipeline record when preprocessing of a document starts.

        Args:
            document_id (str): UUID reference to the document in dist_rag.documents table
            document_name (str): Name of the document being processed
            pipeline_status (str): Initial pipeline status (default: 'PROCESSING')
            metadata (dict, optional): Document metadata captured at pipeline start

        Returns:
            str: pipeline_id (UUID) of the newly created record
        """
        conn = None
        try:
            conn = self._get_connection()
            cur = conn.cursor()

            # Prepare metadata snapshot containing all config at time of execution
            metadata_snapshot = {
                "document_id": document_id,
                "document_name": document_name,
                "metadata": metadata or {}
            }

            # Insert the pipeline record
            cur.execute(
                """
                INSERT INTO dist_rag.pipeline_details (
                    document_id,
                    document_name,
                    status,
                    metadata_snapshot,
                    chunks_processed,
                    embeddings_persisted
                ) VALUES (%s, %s, %s, %s, %s, %s)
                RETURNING pipeline_id
                """,
                (
                    document_id,
                    document_name,
                    pipeline_status,
                    json.dumps(metadata_snapshot),
                    0,
                    0
                )
            )
            pipeline_id = cur.fetchone()[0]
            conn.commit()
            cur.close()

            self.logger.info(
                f"Inserted pipeline record: pipeline_id={pipeline_id}, "
                f"document_id={document_id}, document_name={document_name}"
            )

            return pipeline_id

        except Exception as e:
            if conn:
                conn.rollback()
            self.logger.error(f"Failed to insert pipeline record: {str(e)}")
            raise

        finally:
            if conn:
                self._return_connection(conn)

    def update_pipeline_status(
        self,
        pipeline_id: str,
        pipeline_status: str,
        current_step: Optional[str] = None
    ) -> bool:
        """
        Update the status of a pipeline record.

        Args:
            pipeline_id (str): UUID of the pipeline to update
            pipeline_status (str): New status value
            current_step (str, optional): Current step being executed

        Returns:
            bool: True if update was successful
        """
        conn = None
        try:
            conn = self._get_connection()
            cur = conn.cursor()

            cur.execute(
                """
                UPDATE dist_rag.pipeline_details
                SET status = %s,
                    current_step = %s
                WHERE pipeline_id = %s
                """,
                (pipeline_status, current_step, pipeline_id)
            )

            conn.commit()
            cur.close()

            self.logger.info(
                f"Updated pipeline status: pipeline_id={pipeline_id}, "
                f"status={pipeline_status}, current_step={current_step}"
            )

            return True

        except Exception as e:
            if conn:
                conn.rollback()
            self.logger.error(f"Failed to update pipeline status: {str(e)}")
            raise

        finally:
            if conn:
                self._return_connection(conn)

    def mark_pipeline_completed(self, pipeline_id: str) -> bool:
        """
        Mark a pipeline as completed.

        Args:
            pipeline_id (str): UUID of the pipeline to complete

        Returns:
            bool: True if update was successful
        """
        conn = None
        try:
            conn = self._get_connection()
            cur = conn.cursor()

            cur.execute(
                """
                UPDATE dist_rag.pipeline_details
                SET status = %s,
                    completed_at = NOW()
                WHERE pipeline_id = %s
                """,
                (PipelineStatus.COMPLETED.value, pipeline_id)
            )

            conn.commit()
            cur.close()

            self.logger.info(f"Marked pipeline as completed: pipeline_id={pipeline_id}")

            return True

        except Exception as e:
            if conn:
                conn.rollback()
            self.logger.error(f"Failed to mark pipeline as completed: {str(e)}")
            raise

        finally:
            if conn:
                self._return_connection(conn)

    def record_pipeline_error(
        self,
        pipeline_id: str,
        error_message: str
    ) -> bool:
        """
        Record an error for a pipeline.

        Args:
            pipeline_id (str): UUID of the pipeline with error
            error_message (str): Error message to record

        Returns:
            bool: True if update was successful
        """
        conn = None
        try:
            conn = self._get_connection()
            cur = conn.cursor()

            cur.execute(
                """
                UPDATE dist_rag.pipeline_details
                SET status = %s,
                    last_error_message = %s
                WHERE pipeline_id = %s
                """,
                (PipelineStatus.FAILED.value, error_message, pipeline_id)
            )

            conn.commit()
            cur.close()

            self.logger.error(
                f"Recorded error for pipeline: pipeline_id={pipeline_id}, "
                f"error={error_message}"
            )

            return True

        except Exception as e:
            if conn:
                conn.rollback()
            self.logger.error(f"Failed to record pipeline error: {str(e)}")
            raise

        finally:
            if conn:
                self._return_connection(conn)

    def update_chunks_processed(self, pipeline_id: str, chunks_count: int) -> bool:
        """
        Update the number of chunks processed for a pipeline.

        Args:
            pipeline_id (str): UUID of the pipeline to update
            chunks_count (int): Number of chunks to add to the current count

        Returns:
            bool: True if update was successful
        """
        conn = None
        try:
            conn = self._get_connection()
            cur = conn.cursor()

            cur.execute(
                """
                UPDATE dist_rag.pipeline_details
                SET chunks_processed = %s
                WHERE pipeline_id = %s
                """,
                (chunks_count, pipeline_id)
            )

            conn.commit()
            cur.close()

            self.logger.info(
                f"Updated chunks processed: pipeline_id={pipeline_id}, "
                f"chunks_added={chunks_count}"
            )

            return True

        except Exception as e:
            if conn:
                conn.rollback()
            self.logger.error(f"Failed to update chunks processed: {str(e)}")
            raise

        finally:
            if conn:
                self._return_connection(conn)

    def update_embeddings_persisted(self, pipeline_id: str, embeddings_count: int) -> bool:
        """
        Update the number of embeddings persisted for a pipeline.

        Args:
            pipeline_id (str): UUID of the pipeline to update
            embeddings_count (int): Number of embeddings to add to the current count

        Returns:
            bool: True if update was successful
        """
        conn = None
        try:
            conn = self._get_connection()
            cur = conn.cursor()

            cur.execute(
                """
                UPDATE dist_rag.pipeline_details
                SET embeddings_persisted = embeddings_persisted + %s
                WHERE pipeline_id = %s
                """,
                (embeddings_count, pipeline_id)
            )

            conn.commit()
            cur.close()

            self.logger.info(
                f"Updated embeddings persisted: pipeline_id={pipeline_id}, "
                f"embeddings_added={embeddings_count}"
            )

            return True

        except Exception as e:
            if conn:
                conn.rollback()
            self.logger.error(f"Failed to update embeddings persisted: {str(e)}")
            raise

        finally:
            if conn:
                self._return_connection(conn)

    def get_pipeline_status(self, pipeline_id: str) -> Optional[Dict[str, Any]]:
        """
        Retrieve the current status and details of a pipeline.

        Args:
            pipeline_id (str): UUID of the pipeline to retrieve

        Returns:
            dict: Pipeline details or None if not found
        """
        conn = None
        try:
            conn = self._get_connection()
            cur = conn.cursor()

            cur.execute(
                """
                SELECT pipeline_id, document_id, document_name, status, created_at,
                       last_started_at, completed_at, chunks_processed, embeddings_persisted,
                       metadata_snapshot, current_step, last_error_message
                FROM dist_rag.pipeline_details
                WHERE pipeline_id = %s
                """,
                (pipeline_id,)
            )

            result = cur.fetchone()
            cur.close()

            if not result:
                self.logger.warning(f"Pipeline not found: pipeline_id={pipeline_id}")
                return None

            # Convert to dictionary for easier access
            pipeline_dict = {
                "pipeline_id": result[0],
                "document_id": result[1],
                "document_name": result[2],
                "status": result[3],
                "created_at": result[4],
                "last_started_at": result[5],
                "completed_at": result[6],
                "chunks_processed": result[7],
                "embeddings_persisted": result[8],
                "metadata_snapshot": (
                    json.loads(result[9])
                    if isinstance(result[9], str) else result[9]
                ),
                "current_step": result[10],
                "last_error_message": result[11]
            }

            return pipeline_dict

        except Exception as e:
            self.logger.error(f"Failed to retrieve pipeline status: {str(e)}")
            raise

        finally:
            if conn:
                self._return_connection(conn)

    def get_pipelines_by_document(self, document_id: str) -> list:
        """
        Retrieve all pipelines for a specific document.

        Args:
            document_id (str): Document UUID to filter by

        Returns:
            list: List of pipeline records
        """
        conn = None
        try:
            conn = self._get_connection()
            cur = conn.cursor()

            cur.execute(
                """
                SELECT pipeline_id, document_id, document_name, status, created_at,
                       last_started_at, completed_at, chunks_processed, embeddings_persisted,
                       metadata_snapshot, current_step, last_error_message
                FROM dist_rag.pipeline_details
                WHERE document_id = %s
                ORDER BY created_at DESC
                """,
                (document_id,)
            )

            results = cur.fetchall()
            cur.close()

            pipelines = []

            for result in results:
                pipeline_dict = {
                    "pipeline_id": result[0],
                    "document_id": result[1],
                    "document_name": result[2],
                    "status": result[3],
                    "created_at": result[4],
                    "last_started_at": result[5],
                    "completed_at": result[6],
                    "chunks_processed": result[7],
                    "embeddings_persisted": result[8],
                    "metadata_snapshot": (
                        json.loads(result[9])
                        if isinstance(result[9], str) else result[9]
                    ),
                    "current_step": result[10],
                    "last_error_message": result[11]
                }
                pipelines.append(pipeline_dict)

            self.logger.info(f"Retrieved {len(pipelines)} pipelines for document_id={document_id}")
            return pipelines

        except Exception as e:
            self.logger.error(f"Failed to retrieve pipelines for document: {str(e)}")
            raise

        finally:
            if conn:
                self._return_connection(conn)

    def close(self):
        """
        Note: Connections are now managed by the ConnectionPool singleton.
        Individual instances don't need to close connections.
        """
        self.logger.info("PipelineTracking instance closed (pool managed by singleton)")

    def __del__(self):
        """Destructor for cleanup"""
        try:
            self.close()
        except Exception:
            pass
