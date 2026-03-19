"""
Example Task Processors - Concrete implementations of TaskProcessor.

This module shows how to implement specific processors for different task type.
Each processor handles a specific type of work and can be registered with the TaskRouter.
"""

import logging
import threading
import time
from datetime import datetime
from typing import Dict, Any
from uuid import UUID
from work_queue.task_router import TaskProcessor
from models.work_queue_task import WorkQueueTask
from models.document_metadata import DocumentMetadata
from source_location_crawlers import S3BucketCrawler
from db.connection_pool import ConnectionPool
from work_queue.poller import Poller
from db.source_document_tracking import SourceDocumentTracking


class CreateSourceProcessorForAWS_S3(TaskProcessor):
    """
    Processor for DOCUMENT_EMBEDDING tasks.
    Handles document embedding and chunking operations.
    """

    # Global lease renewal configuration (in seconds)
    # Default: 900 seconds = 15 minutes
    lease_renewal_interval_seconds = 900
    _config_lock = threading.Lock()

    def __init__(self):
        """
        Initialize the processor.

        """
        self.logger = logging.getLogger(__name__)
        self.s3_crawler = S3BucketCrawler()
        self.connection_pool = ConnectionPool()
        self.poller = Poller()
        self.source_document_tracking = SourceDocumentTracking()
        self._lease_renewal_threads = {}  # Track active renewal threads

    def validate(self, task: WorkQueueTask) -> bool:
        """
        Validate document embedding task.

        Args:
            task: WorkQueueTask object

        Returns:
            True if valid
        """
        task_details = task.task_details
        print(f"task_details: {task_details}")

        # Validate required fields in task_details
        required_fields = ['source_id']
        for field in required_fields:
            if field not in task_details:
                self.logger.error(f"Missing required field in task_details: {field}")
                return False

        return True

    def _retrieve_files_from_S3(self, bucket_name, folder_prefix: str = ""):
        """
        Retrieve files from S3.
        """
        logging.info(
            f"Retrieving files from S3 bucket: {bucket_name}, "
            f"folder prefix: {folder_prefix}"
        )
        files_metadata = self.s3_crawler.get_folder_files_metadata(
            bucket_name=bucket_name, folder_prefix=folder_prefix
        )
        logging.info(
            f"For bucket:{bucket_name}, folder prefix: {folder_prefix} - "
            f"found {len(files_metadata)} files to process"
        )

        return files_metadata

    def _get_documents_from_source(self, source_uri: str, source_id: UUID):

        if source_uri.startswith("s3://"):
            # Retrieve bucket name and folder prefix from s3:// URI.
            uri_without_prefix = source_uri.replace("s3://", "", 1)
            parts = uri_without_prefix.split("/", 1)
            bucket_name = parts[0]
            folder_prefix = parts[1] if len(parts) > 1 else ""
            files_metadata = self._retrieve_files_from_S3(
                bucket_name=bucket_name, folder_prefix=folder_prefix
            )

            for file_metadata in files_metadata:
                # nik-todo: generate s3 url from files_metadata
                # Build S3 URI
                print(f"Building S3 URI for file: {file_metadata}")
                s3_uri = self.s3_crawler.build_s3_uri(bucket_name, file_metadata['Key'])

                # Create standard DocumentMetadata object
                document_metadata = DocumentMetadata(
                    source_id=source_id,
                    document_name=file_metadata['Key'],
                    document_uri=s3_uri,
                    document_checksum=file_metadata['ETag'],
                    status="QUEUED"
                )

                # Insert using the standard metadata object
                self.source_document_tracking.insert_document_metadata(document_metadata)
        else:
            logging.error(f"Invalid source URI: {source_uri}")
            raise ValueError(f"Invalid source URI: {source_uri}")

    def _renew_lease_periodically(self, work_queue_id: str, lease_token: str):
        """
        Background thread that renews the lease periodically.
        Uses the globally configured interval.
        """
        while True:
            try:
                # Get current interval (thread-safe)
                interval = self.get_lease_renewal_interval()
                time.sleep(interval)

                # Stop if this thread should no longer run (task completed)
                if not self._lease_renewal_threads.get(work_queue_id):
                    break

                self.logger.info(f"Renewing lease for task {work_queue_id}")
                self.poller.renew_lease(work_queue_id, lease_token)
            except Exception as e:
                self.logger.error(f"Error renewing lease for task {work_queue_id}: {str(e)}")
                break

    @classmethod
    def set_lease_renewal_interval(cls, interval_seconds: int):
        """
        Dynamically configure the lease renewal interval.

        Args:
            interval_seconds: Interval in seconds for lease renewal
        """
        with cls._config_lock:
            cls.lease_renewal_interval_seconds = interval_seconds
            logging.getLogger(__name__).info(
                f"Lease renewal interval updated to {interval_seconds} seconds"
            )

    @classmethod
    def get_lease_renewal_interval(cls) -> int:
        """
        Get the current lease renewal interval.

        Returns:
            Lease renewal interval in seconds
        """
        with cls._config_lock:
            return cls.lease_renewal_interval_seconds

    def _update_create_source_status(self, work_queue_id: str, status: str):
        """
        Update the status of a create source task.
        """
        connection = None
        try:
            connection = self.connection_pool.get_connection()
            cursor = connection.cursor()
            query = """
            UPDATE dist_rag.sources
            SET status = %s
            WHERE id = %s
            RETURNING id;
            """
            cursor.execute(query, (status, work_queue_id))
            result = cursor.fetchone()
            connection.commit()
        except Exception as e:
            if connection:
                try:
                    connection.rollback()
                except Exception:
                    pass
            logging.error(f"Error updating create source status for task {work_queue_id}: {str(e)}")
            raise
        finally:
            if connection:
                self.connection_pool.return_connection(connection)

    def _mark_task_completed(self, work_queue_id: UUID, source_id: UUID):
        """
        Mark a task as completed.
        """
        connection = None
        try:
            connection = self.connection_pool.get_connection()
            cursor = connection.cursor()

            # Update work_queue table
            query_work_queue = """
            UPDATE dist_rag.work_queue
            SET task_status = 'COMPLETED',
                completed_at = CURRENT_TIMESTAMP
            WHERE id = %s;
            """
            cursor.execute(query_work_queue, (work_queue_id,))

            # Update sources table
            query_sources = """
            UPDATE dist_rag.sources
            SET status = 'COMPLETED',
                completed_at = CURRENT_TIMESTAMP
            WHERE id = %s
            RETURNING id;
            """
            cursor.execute(query_sources, (source_id,))
            result = cursor.fetchone()
            connection.commit()
            return result
        except Exception as e:
            if connection:
                try:
                    connection.rollback()
                except Exception:
                    pass
            logging.error(f"Error marking completion for task {work_queue_id}: {str(e)}")
            raise
        finally:
            if connection:
                self.connection_pool.return_connection(connection)

    def process(self, task: WorkQueueTask) -> Dict[str, Any]:
        """
        Process a document embedding task.

        Args:
            task: WorkQueueTask object with document details

        Returns:
            Result dictionary with processing status

        Raises:
            RuntimeError: If RagPipelineHandler not initialized
            Exception: Any processing errors
        """
        if not self.s3_crawler:
            raise RuntimeError("S3 Crawler not initialized")

        task_details = task.task_details

        try:
            work_queue_id = task.id
            lease_token = task.lease_token
            lease_expires_at = task.lease_expires_at
            source_id = task_details.get('source_id')
            source_uri = task_details.get('source_uri')

            # Check if the lease has expired
            if lease_expires_at < datetime.now():
                self.logger.error(f"Lease expired for task {work_queue_id}")
                return {
                    'message': 'Lease expired',
                    'work_queue_id': work_queue_id
                }

            # Start background lease renewal thread
            renewal_thread = threading.Thread(
                target=self._renew_lease_periodically,
                args=(str(work_queue_id), str(lease_token)),
                daemon=True
            )
            self._lease_renewal_threads[str(work_queue_id)] = renewal_thread
            renewal_thread.start()
            self.logger.info(
                f"Started lease renewal thread for task {work_queue_id} "
                f"(interval: {self.get_lease_renewal_interval()} seconds)"
            )

            self.logger.info(
                f"Processing create source task: source_id={source_id}"
            )

            self._update_create_source_status(work_queue_id=work_queue_id, status="IN_PROGRESS")

            # nik-todo: fetch source details from source_id.
            source_details = self.source_document_tracking.get_source_details(source_id=source_id)
            if not source_details:
                self.logger.error(f"Source details not found for source_id: {source_id}")
                return {
                    'message': 'Source details not found',
                    'source_id': source_id
                }

            source_uri = source_details.get('source_uri')
            self._get_documents_from_source(source_uri=source_uri, source_id=source_id)

            self._mark_task_completed(work_queue_id=work_queue_id, source_id=source_id)

            from datetime import timezone
            completed_at = datetime.now(timezone.utc).isoformat()
            self.logger.info(f"Task {work_queue_id} marked as completed at {completed_at}")
            # Stop the renewal thread once task completes
            self._lease_renewal_threads.pop(str(work_queue_id), None)

            return {
                'task_id': work_queue_id,
                'status': 'success',
                'completed_at': completed_at
            }

        except Exception as e:
            # Clean up the renewal thread on error
            self._lease_renewal_threads.pop(str(work_queue_id), None)
            self._update_create_source_status(work_queue_id=work_queue_id, status="FAILED")
            self.logger.error(f"Error processing create source task: {str(e)}")
            raise
