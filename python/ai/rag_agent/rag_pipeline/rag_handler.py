from embeddings import EmbeddingsGenerator
from db import YugabyteDBVectorStore, PipelineTracking, PipelineStatus
from source_location_crawlers import S3BucketCrawler
import logging


class RagPipelineHandler:
    """
    Orchestrates the RAG pipeline by integrating document ingestion, embedding, and storage
    into YugabyteDB vector store.
    """
    def __init__(self,
                 table_name='pg_rag_default_store',
                 vector_dimension=1536,
                 pipeline_config=None,
                 embed_function=None):
        """
        Args:
            db_connection_string (str): YugabyteDB connection string.
            table_name (str): Name of the vector store table.
            vector_dimension (int): The dimensions of the embeddings.
            pipeline_config (dict, optional): Configuration for the pipeline.
            embed_function (callable, optional): Function to generate
                embeddings (defaults to global).
        """
        self.logger = logging.getLogger(__name__)
        self.vector_store = YugabyteDBVectorStore()
        self.pipeline_tracking = PipelineTracking()

    def _ingest_document(
        self,
        pipeline_id,
        source_id,
        document_id,
        document_uri,
        table_name,
        metadata=None,
        chunk_kwargs=None,
        embedding_model_params=None
    ):
        """
        Runs the pipeline: splits document, creates embeddings, and stores in YugabyteDB.

        Args:
            document_uri (str): Document URI to process.
            metadata (dict, optional): Additional metadata to store.
            document_id (int, optional): document identifier of the file to be ingested.
            chunk_kwargs (dict, optional): Additional arguments for text chunking (e.g. chunk_size).
        """

        self.logger.debug(f"Ingesting document: {document_uri} with chunk_kwargs: {chunk_kwargs}")

        # nikhil-todo: make embedding_model configurable.
        embedding_model = embedding_model_params.get('model')
        if not embedding_model:
            self.logger.error(
                f"Embedding model not found in embedding_model_params: "
                f"{embedding_model_params}"
            )
            raise ValueError(
                f"Embedding model not found in embedding_model_params: "
                f"{embedding_model_params}"
            )

        embedding_dimension = embedding_model_params.get('dimensions')
        if not embedding_dimension:
            self.logger.error(
                f"Embedding dimension not found in embedding_model_params: "
                f"{embedding_model_params}"
            )
            raise ValueError(
                f"Embedding dimension not found in embedding_model_params: "
                f"{embedding_model_params}"
            )

        embedder = EmbeddingsGenerator(
            embedding_model=embedding_model,
            embedding_model_params=embedding_model_params
        )
        # 1. Process file and Generate embeddings
        embedding_iterator = embedder.generate_embeddings(
            pipeline_id=pipeline_id, file_location=document_uri, chunk_args=chunk_kwargs)

        # 2. Insert into vector store
        self.logger.debug(f"Inserting embeddings into vector store for document: {document_uri}")
        self.vector_store.insert_embeddings(
            document_id=document_id,
            table_name=table_name,
            embedding_iterator=embedding_iterator,
            metadata=metadata or {},
            pipeline_id=pipeline_id
        )
        return True

    def start_processing(
        self,
        source_id,
        document_id,
        document_uri,
        table_name,
        metadata=None,
        chunk_kwargs=None,
        embedding_model_params=None
    ):
        """
        Starts the processing of the document.
        """
        try:
            self.logger.info(f"Starting processing of document: {document_uri}")
            pipeline_id = self.pipeline_tracking.insert_pipeline_details(
                document_id=document_id,
                document_name=document_uri,
                pipeline_status=PipelineStatus.PROCESSING.value
            )
            self._ingest_document(
                source_id=source_id,
                document_id=document_id,
                pipeline_id=pipeline_id,
                document_uri=document_uri,
                table_name=table_name,
                metadata=metadata,
                chunk_kwargs=chunk_kwargs,
                embedding_model_params=embedding_model_params
            )
            self.logger.info(f"Processing of document: {document_uri} completed")
            self.vector_store.create_index(table_name=table_name)
            self.logger.info(f"Vector Index created successfully on {table_name}")

            self.pipeline_tracking.update_pipeline_status(
                pipeline_id=pipeline_id,
                pipeline_status=PipelineStatus.COMPLETED.value)
            return True
        except Exception as e:
            self.logger.error(f"Error during RAG pipeline processing: {str(e)}")
            self.pipeline_tracking.update_pipeline_status(
                pipeline_id=pipeline_id,
                pipeline_status=PipelineStatus.FAILED.value)
            self.pipeline_tracking.record_pipeline_error(
                pipeline_id=pipeline_id, error_message=str(e))
            raise
