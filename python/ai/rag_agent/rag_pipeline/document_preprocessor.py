import logging
from work_queue.task_router import TaskProcessor
from db.connection_pool import ConnectionPool
from models.work_queue_task import WorkQueueTask
from rag_pipeline.rag_handler import RagPipelineHandler
from db.source_document_tracking import SourceDocumentTracking
from uuid import UUID
from typing import Dict, Any


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

    def _retrieve_embedding_parameters(self, index_id: UUID) -> Dict[str, Any]:
        """
        Retrieve the embedding parameters for the source.
        """

        connection = None
        try:
            connection = self.connection_pool.get_connection()
            cursor = connection.cursor()
            query = """
                SELECT embedding_model_params
                FROM dist_rag.vector_indexes
                WHERE id = %s
            """
            cursor.execute(query, (str(index_id),))
            result = cursor.fetchone()
            if result:
                return result[0]  # embedding_model_params is the first column
            else:
                self.logger.error(f"No vector_indexes entry found for index_id: {index_id}")
                return {}

        except Exception as e:
            connection.rollback()
            self.logger.error(
                f"Error fetching embedding_model_params for index_id {index_id}: {str(e)}"
            )
            return {}
        finally:
            if cursor:
                cursor.close()
            if connection:
                self.connection_pool.return_connection(connection)
        return None

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

    def _retrieve_rag_index_name(self, index_id: UUID) -> str:
        """
        Retrieve the RAG index name.
        """
        connection = None
        try:
            connection = self.connection_pool.get_connection()
            cursor = connection.cursor()
            query = """
                SELECT index_name
                FROM dist_rag.vector_indexes
                WHERE id = %s
            """
            cursor.execute(query, (str(index_id),))
            result = cursor.fetchone()
            if result:
                return result[0]  # name is the first column
        except Exception as e:
            connection.rollback()
            self.logger.error(f"Error fetching RAG index name for index_id {index_id}: {str(e)}")
            return None
        finally:
            if cursor:
                cursor.close()
            if connection:
                self.connection_pool.return_connection(connection)
        return None

    def process(self, task: WorkQueueTask) -> Dict[str, Any]:
        """
        Process the task.
        """

        self.logger.info(f"Processing task: {task.id}")

        try:
            # Extract task details
            task_details = task.task_details
            if not task_details:
                raise ValueError("Task details are missing or empty")

            index_id = task_details.get('index_id')
            source_id = task_details.get('source_id')
            document_id = task_details.get('document_id')
            document_uri = task_details.get('document_uri')

            # Validate extracted parameters
            if not all([index_id, source_id, document_id, document_uri]):
                raise ValueError(
                    f"Missing required parameters - index_id: {index_id}, "
                    f"source_id: {source_id}, document_id: {document_id}, "
                    f"document_uri: {document_uri}"
                )

            # Retrieve required parameters
            try:
                embedding_model_params = self._retrieve_embedding_parameters(
                    index_id=index_id
                )
                if not embedding_model_params:
                    raise ValueError(
                        f"Failed to retrieve embedding parameters for index_id: {index_id}"
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

            try:
                rag_index_name = self._retrieve_rag_index_name(index_id=index_id)
                if not rag_index_name:
                    raise ValueError(f"Failed to retrieve RAG index name for index_id: {index_id}")
            except Exception as e:
                self.logger.error(f"Error retrieving RAG index name: {str(e)}")
                raise

            self.source_document_tracking.update_document_status(
                document_id=document_id, status="PROCESSING"
            )
            # Start processing
            try:
                self.rag_handler.start_processing(
                    source_id=source_id,
                    document_id=document_id,
                    document_uri=document_uri,
                    table_name=rag_index_name,
                    metadata=source_metadata,
                    chunk_kwargs=chunking_params,
                    embedding_model_params=embedding_model_params
                )
                self.logger.info(f"Task {task.id} processed successfully")
                self.source_document_tracking.update_document_status(
                    document_id=document_id, status="COMPLETED"
                )
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
            self.source_document_tracking.update_document_status(
                document_id=document_id, status="FAILED"
            )
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
            self.source_document_tracking.update_document_status(
                document_id=document_id, status="FAILED"
            )
            return {
                "status": "error",
                "task_id": task.id,
                "error_type": type(e).__name__,
                "error_message": str(e)
            }
