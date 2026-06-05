import os
import logging
from contextlib import nullcontext
from work_queue.task_router import TaskProcessor
from work_queue.poller import Poller
from db.connection_pool import ConnectionPool
from models.work_queue_task import WorkQueueTask
from rag_pipeline.rag_handler import RagPipelineHandler
from db.source_document_tracking import SourceDocumentTracking
from uuid import UUID
from typing import Dict, Any, Optional, Tuple
from langfuse import Langfuse
from langfuse._client.get_client import _set_current_public_key
from observability import meko_observe


class DocumentPreprocessor(TaskProcessor):
    """
    Processor for DOCUMENT_PREPROCESSING tasks.
    Handles document preprocessing operations.
    """
    def __init__(self):
        """
        Initialize the processor.
        """
        self.logger = logging.getLogger(__name__)
        self.connection_pool = ConnectionPool()
        self.rag_handler = RagPipelineHandler()
        self.source_document_tracking = SourceDocumentTracking()
        # Used to finalize the work_queue row after process(). Without this
        # the row stays in IN_PROGRESS forever and the SQL-side reaper
        # re-queues it once the lease expires, causing already-completed
        # documents to be reprocessed on every poll cycle.
        self.poller = Poller()

    def _short_circuit_if_already_finalized(
        self, task: WorkQueueTask, document_id: UUID
    ) -> Optional[Dict[str, Any]]:
        """
        Skip processing if ``dist_rag.documents`` says the document is
        already in a terminal state.

        This is the idempotency guard against duplicate PREPROCESS tasks
        for the same document. They can show up via:

        - Historical work_queue rows from before MEKO-154 (mark_completed
          was an UPDATE, the SQL reaper re-queued anything left in
          IN_PROGRESS with an expired lease).
        - Future reconciliation jobs that re-enqueue FAILED tasks.
        - Manual re-enqueues during ops work.

        Returns:
            A result dict (and finalizes the work_queue row) if the
            document is already ``COMPLETED`` or ``FAILED``. ``None``
            otherwise, meaning the caller should proceed with normal
            processing.

        The work_queue side is always cleaned up so the queue doesn't
        keep handing this row back to a worker.
        """
        status = self.source_document_tracking.get_document_status(document_id)
        if status not in ("COMPLETED", "FAILED"):
            return None

        self.logger.warning(
            f"Skipping task {task.id}: document {document_id} is already "
            f"in terminal status '{status}'; finalizing work_queue row "
            f"without reprocessing"
        )
        self._finalize_task(task, succeeded=(status == "COMPLETED"))
        return {
            "status": "skipped",
            "task_id": task.id,
            "document_id": document_id,
            "reason": f"document already {status}",
        }

    def _finalize_task(self, task: WorkQueueTask, succeeded: bool) -> None:
        """Mark the work_queue row terminal so it isn't re-queued by the reaper.

        Wraps :meth:`Poller.mark_completed` / :meth:`Poller.mark_failed` so
        finalization failures can't mask the original processing outcome --
        the caller decides what status to return; we only log here.
        """
        try:
            if succeeded:
                self.poller.mark_completed(task.id)
            else:
                self.poller.mark_failed(task.id)
        except Exception as e:
            self.logger.error(
                f"Failed to finalize work_queue row for task {task.id} "
                f"(succeeded={succeeded}): {str(e)}"
            )

    def validate(self, task: WorkQueueTask) -> bool:
        """
        Validate the task.
        """
        task_details = task.task_details
        print(f"task_details: {task_details}")

        # Validate required fields in task_details
        required_fields = ['index_id', 'source_id', 'document_id', 'document_uri']
        for field in required_fields:
            if field not in task_details:
                self.logger.error(f"Missing required field in task_details: {field}")
                return False

        return True

    @meko_observe(name="Retrieve Embedding Parameters / DocumentPreprocessor", as_type="retriever")
    def _retrieve_embedding_parameters(
        self, index_id: UUID
    ) -> Tuple[Optional[str], Dict[str, Any]]:
        """
        Retrieve the AI provider and embedding model parameters for the index.

        Returns:
            Tuple of (ai_provider, embedding_model_params). Returns
            ``(None, {})`` when the row is missing or fetching failed.
        """

        connection = None
        try:
            connection = self.connection_pool.get_connection()
            cursor = connection.cursor()
            query = """
                SELECT ai_provider, embedding_model_params
                FROM dist_rag.vector_indexes
                WHERE id = %s
            """
            cursor.execute(query, (str(index_id),))
            result = cursor.fetchone()
            if result:
                return result[0], result[1]
            else:
                self.logger.error(f"No vector_indexes entry found for index_id: {index_id}")
                return None, {}

        except Exception as e:
            connection.rollback()
            self.logger.error(
                f"Error fetching ai_provider/embedding_model_params for "
                f"index_id {index_id}: {str(e)}"
            )
            return None, {}
        finally:
            if cursor:
                cursor.close()
            if connection:
                self.connection_pool.return_connection(connection)

    @meko_observe(name="Retrieve Chunking Parameters / DocumentPreprocessor", as_type="retriever")
    def _retrieve_chunking_parameters(self, index_id: UUID, source_id: UUID) -> Dict[str, Any]:
        """
        Retrieve the chunking parameters for the source.
        """
        connection = None
        try:
            connection = self.connection_pool.get_connection()
            cursor = connection.cursor()
            query = """
                SELECT chunk_params
                FROM dist_rag.vector_index_source_mappings
                WHERE index_id = %s AND source_id = %s
            """
            cursor.execute(query, (str(index_id), str(source_id)))
            result = cursor.fetchone()
            if result:
                return result[0]  # chunk_params is the first column
            else:
                self.logger.error(
                    f"No chunking parameters found for index_id: {self.index_id} "
                    f"and source_id: {source_id}"
                )
                return {}
        except Exception as e:
            connection.rollback()
            self.logger.error(
                f"Error fetching chunk_params for index_id {self.index_id} "
                f"and source_id {source_id}: {str(e)}"
            )
            return {}
        finally:
            if cursor:
                cursor.close()
            if connection:
                self.connection_pool.return_connection(connection)
        return None

    @meko_observe(name="Retrieve Source Metadata / DocumentPreprocessor", as_type="retriever")
    def _retrieve_source_metadata(self, source_id: UUID) -> Dict[str, Any]:
        """
        Retrieve the source metadata.
        """
        connection = None
        try:
            connection = self.connection_pool.get_connection()
            cursor = connection.cursor()
            query = """
                SELECT metadata
                FROM dist_rag.sources
                WHERE id = %s
            """
            cursor.execute(query, (str(source_id),))
            result = cursor.fetchone()
            if result:
                return result[0]  # source metadata is the first column
        except Exception as e:
            connection.rollback()
            self.logger.error(f"Error fetching source metadata for source_id {source_id}: {str(e)}")
            return {}
        finally:
            if cursor:
                cursor.close()
            if connection:
                self.connection_pool.return_connection(connection)
        return None

    @meko_observe(name="Retrieve RAG Index Name / DocumentPreprocessor", as_type="retriever")
    def _retrieve_rag_index_name(
        self, index_id: UUID
    ) -> Tuple[Optional[str], Optional[str]]:
        """
        Retrieve (index_name, schema_name) for the given index.
        """
        connection = None
        cursor = None
        try:
            connection = self.connection_pool.get_connection()
            cursor = connection.cursor()
            query = """
                SELECT index_name, schema_name
                FROM dist_rag.vector_indexes
                WHERE id = %s
            """
            cursor.execute(query, (str(index_id),))
            result = cursor.fetchone()
            if result:
                return result[0], result[1]
        except Exception as e:
            connection.rollback()
            self.logger.error(f"Error fetching RAG index name for index_id {index_id}: {str(e)}")
            return None, None
        finally:
            if cursor:
                cursor.close()
            if connection:
                self.connection_pool.return_connection(connection)

    def _resolve_langfuse_client(self, datapack_id: str) -> Tuple[str, str] | None:
        """
        Parse the datapack_id from document_uri and return a Langfuse client
        initialised with the matching project keys, or None if unavailable.
        """
        try:
            connection = self.connection_pool.get_connection()
            cursor = connection.cursor()
            try:
                cursor.execute(
                    "SELECT langfuse_public_key, langfuse_secret_key "
                    "FROM meko_system.langfuse_project_mapping WHERE datapack_id = %s::uuid",
                    (datapack_id,),
                )
                row = cursor.fetchone()
                if row:
                    public_key = row[0]
                    secret_key = row[1]
                    return public_key, secret_key
                else:
                    self.logger.warning(f"No Langfuse keys found for datapack_id={datapack_id}")
            finally:
                cursor.close()
                self.connection_pool.return_connection(connection)
        except Exception as e:
            self.logger.warning(
                f"Failed to resolve Langfuse keys for datapack_id={datapack_id}: {str(e)}"
            )

        return None

    def process(self, task: WorkQueueTask) -> Dict[str, Any]:
        """
        Process the task.
        """
        is_langfuse_enabled = os.getenv("ENABLE_LANGFUSE_TRACING", "false") == "true"
        if is_langfuse_enabled and task.task_details and task.task_details.get('tenant_id'):
            datapack_id = task.task_details.get('tenant_id')
        else:
            datapack_id = None
        self.logger.info(f"Processing task: {task.id} for datapack ID: {datapack_id}")
        transform_span = None
        langfuse_public_key = None
        tracing_context = nullcontext()
        observation_context = nullcontext()
        resolved_keys = self._resolve_langfuse_client(datapack_id)
        if is_langfuse_enabled:
            if resolved_keys and datapack_id:
                try:
                    langfuse_public_key, langfuse_secret_key = resolved_keys
                    langfuse_client = Langfuse(
                        public_key=langfuse_public_key,
                        secret_key=langfuse_secret_key
                    )
                    tracing_context = _set_current_public_key(langfuse_public_key)
                    observation_context = langfuse_client.start_as_current_observation(
                        as_type="agent",
                        name="Process Task / DocumentPreprocessor"
                    )
                except Exception as e:
                    self.logger.warning(
                        f"Langfuse initialization failed for task {task.id}; "
                        f"continuing without tracing: {str(e)}"
                    )
                    tracing_context = nullcontext()
                    observation_context = nullcontext()
            else:
                self.logger.info(
                    f"No Langfuse keys resolved for task {task.id}; continuing without tracing"
                )
        else:
            self.logger.info(f"Langfuse tracing is disabled for task {task.id}")
        with (tracing_context or nullcontext()):
            with (observation_context or nullcontext()) as active_span:
                transform_span = active_span
                document_id = None
                span_output = {"status": "error", "task_id": str(task.id)}

                try:
                    # Extract task details
                    task_details = task.task_details
                    if not task_details:
                        raise ValueError("Task details are missing or empty")

                    index_id = task_details.get('index_id')
                    source_id = task_details.get('source_id')
                    document_id = task_details.get('document_id')
                    document_uri = task_details.get('document_uri')
                    # tenant_id is optional in task_details for backward compatibility
                    # with tasks queued before the tenant_id column was added.
                    tenant_id = task_details.get('tenant_id')

                    # Validate extracted parameters
                    if not all([index_id, source_id, document_id, document_uri]):
                        raise ValueError(
                            f"Missing required parameters - index_id: {index_id}, "
                            f"source_id: {source_id}, document_id: {document_id}, "
                            f"document_uri: {document_uri}"
                        )

                    # Idempotency guard: bail out before any embedding work
                    # if this document was already processed (or already
                    # failed and is awaiting reconciliation).
                    short_circuit = self._short_circuit_if_already_finalized(
                        task, document_id
                    )
                    if short_circuit is not None:
                        # Skip updating the langfuse span output for short-circuit
                        # as the document is already processed or failed
                        transform_span = None
                        return short_circuit

                    # Retrieve required parameters
                    try:
                        ai_provider, embedding_model_params = (
                            self._retrieve_embedding_parameters(index_id=index_id)
                        )
                        if not embedding_model_params:
                            raise ValueError(
                                f"Failed to retrieve embedding parameters for index_id: {index_id}"
                            )
                        if not ai_provider:
                            raise ValueError(
                                f"Failed to retrieve ai_provider for index_id: {index_id}"
                            )
                    except Exception as e:
                        self.logger.error(f"Error retrieving embedding parameters: {str(e)}")
                        raise

                    try:
                        chunking_params = self._retrieve_chunking_parameters(
                            index_id=index_id, source_id=source_id
                        )
                        if not chunking_params:
                            raise ValueError(
                                f"Failed to retrieve chunking parameters for index_id: {index_id}, "
                                f"source_id: {source_id}"
                            )
                    except Exception as e:
                        self.logger.error(f"Error retrieving chunking parameters: {str(e)}")
                        raise

                    try:
                        source_metadata = self._retrieve_source_metadata(source_id=source_id)
                        if source_metadata is None:
                            self.logger.warning(
                                f"Source metadata is None for source_id: {source_id}, "
                                f"using empty dict"
                            )
                            source_metadata = {}
                    except Exception as e:
                        self.logger.error(f"Error retrieving source metadata: {str(e)}")
                        raise

                    # Fall back to looking up tenant_id on the source if it wasn't
                    # included in the task payload (e.g. older queued tasks).
                    if not tenant_id:
                        try:
                            source_details = (
                                self.source_document_tracking.get_source_details(
                                    source_id=source_id
                                )
                            )
                            if source_details:
                                tenant_id = source_details.get('tenant_id')
                        except Exception as e:
                            self.logger.warning(
                                f"Failed to resolve tenant_id from sources table "
                                f"for source_id {source_id}: {str(e)}"
                            )

                    # Retrieve RAG index name and schema
                    try:
                        rag_index_name, rag_schema_name = (
                            self._retrieve_rag_index_name(index_id=index_id)
                        )
                        if not rag_index_name:
                            raise ValueError(
                                f"Failed to retrieve RAG index name for "
                                f"index_id: {index_id}"
                            )
                    except Exception as e:
                        self.logger.error(f"Error retrieving RAG index name: {str(e)}")
                        raise

                    trace_id = (
                        transform_span.trace_id
                        if transform_span and hasattr(transform_span, "trace_id")
                        else None
                    )
                    self.source_document_tracking.update_document_status(
                        document_id=document_id,
                        status="PROCESSING",
                        trace_id=trace_id,
                    )

                    # Start processing
                    try:
                        self.rag_handler.start_processing(
                            source_id=source_id,
                            document_id=document_id,
                            document_uri=document_uri,
                            table_name=rag_index_name,
                            schema_name=rag_schema_name,
                            metadata=source_metadata,
                            chunk_kwargs=chunking_params,
                            embedding_model_params=embedding_model_params,
                            ai_provider=ai_provider,
                            tenant_id=tenant_id
                        )
                        self.logger.info(f"Task {task.id} processed successfully")
                        self.source_document_tracking.update_document_status(
                            document_id=document_id, status="COMPLETED"
                        )
                        self._finalize_task(task, succeeded=True)
                        span_output = {
                            "status": "success",
                            "task_id": str(task.id),
                            "document_id": str(document_id)
                        }
                        return {
                            "status": "success",
                            "task_id": task.id,
                            "document_id": document_id
                        }
                    except Exception as e:
                        self.logger.error(f"Error during RAG pipeline processing: {str(e)}")
                        raise

                except ValueError as e:
                    self.logger.error(f"Validation error while processing task {task.id}: {str(e)}")
                    if document_id:
                        self.source_document_tracking.update_document_status(
                            document_id=document_id, status="FAILED"
                        )
                    self._finalize_task(task, succeeded=False)
                    span_output = {
                        "status": "error",
                        "task_id": str(task.id),
                        "error_type": "ValidationError",
                        "error_message": str(e)
                    }
                    return {
                        "status": "error",
                        "task_id": task.id,
                        "error_type": "ValidationError",
                        "error_message": str(e)
                    }
                except Exception as e:
                    self.logger.error(
                        f"Unexpected error while processing task {task.id}: {str(e)}",
                        exc_info=True
                    )
                    if document_id:
                        self.source_document_tracking.update_document_status(
                            document_id=document_id, status="FAILED"
                        )
                    self._finalize_task(task, succeeded=False)
                    span_output = {
                        "status": "error",
                        "task_id": str(task.id),
                        "error_type": type(e).__name__,
                        "error_message": str(e)
                    }
                    return {
                        "status": "error",
                        "task_id": task.id,
                        "error_type": type(e).__name__,
                        "error_message": str(e)
                    }
                finally:
                    if is_langfuse_enabled and transform_span:
                        try:
                            transform_span.update(output=span_output)
                        except Exception as e:
                            self.logger.warning(
                                f"Failed to update Langfuse span for task {task.id}: {str(e)}"
                            )
