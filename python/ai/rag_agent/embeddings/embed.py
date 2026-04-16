from db.active_pipeline_tracking import PipelineTracking
from langchain_openai import OpenAIEmbeddings
from pdf_processing import PDFProcessor
import logging
import psycopg
import os
import mimetypes


class EmbeddingsGenerator:
    """Generates embeddings for various file types (text, PDF, video)."""

    def __init__(
        self,
        embedding_model: str = "text-embedding-ada-002",
        llm_api_key: str = None,
        embedding_model_params: dict = None
    ):
        """
        Initialize the EmbeddingsGenerator.

        Args:
            embedding_model (str): Model name for OpenAI embeddings.
                Defaults to "text-embedding-ada-002".
            llm_api_key (str, optional): OpenAI API key.
                If None, uses environment variable.
        """
        self.embedding_model = embedding_model
        self.embedding_dimensions = embedding_model_params.get('dimensions')
        # nikhil-todo: add support for other LLMs.
        self.llm_api_key = llm_api_key or os.getenv("OPENAI_API_KEY")
        self.embedder = OpenAIEmbeddings(
            model=self.embedding_model,
            openai_api_key=self.llm_api_key,
            dimensions=self.embedding_dimensions
        )

        # self.model = ChatOpenAI(temperature=0.6, model="gpt-4o-mini",
        #                         callbacks=[ConsoleCallbackHandler()])

        self.pdf_processor = PDFProcessor()
        self.pipeline_tracking = PipelineTracking()

    def _generate_embeddings_for_text_files(
        self,
        pipeline_id: int,
        file_location: str,
        chunk_args=None
    ):
        """Generate embeddings for text files by chunking and embedding."""
        from rag_pipeline import stream_partition_and_chunk
        yielded_count = 0
        chunk_count = 0
        empty_chunk_count = 0
        for chunk_text in stream_partition_and_chunk(
            pipeline_id, file_location, chunk_args
        ):
            chunk_count += 1
            if chunk_text.strip():
                try:
                    embedding_vector = self.embedder.embed_documents(
                        [chunk_text]
                    )[0]
                    # Update the embeddings generated count for the pipeline
                    yielded_count += 1

                    # Update the embedding progress for the pipeline
                    try:
                        self.pipeline_tracking.update_chunks_processed(
                            pipeline_id=pipeline_id,
                            chunks_count=chunk_count
                        )
                    except Exception as e:
                        logging.error(
                            f"Failed to update embeddings generated: {str(e)}"
                        )

                    yield chunk_text, embedding_vector
                except Exception as e:
                    logging.error(
                        f"Failed to generate embedding for chunk "
                        f"{chunk_count} in file {file_location}: {str(e)}"
                    )
                    # Continue to next chunk instead of stopping
                    continue
            else:
                empty_chunk_count += 1

        # Update the embedding progress for the pipeline
        try:
            self.pipeline_tracking.update_chunks_processed(
                pipeline_id=pipeline_id, chunks_count=chunk_count
            )
        except Exception as e:
            logging.error(f"Failed to update embeddings generated: {str(e)}")

        logging.info(
            f"Finished generating embeddings for {file_location}: "
            f"{chunk_count} total chunks, {empty_chunk_count} "
            f"empty/whitespace chunks, {yielded_count} embeddings yielded"
        )

    def _generate_embeddings_for_pdf_files(
        self,
        pipeline_id: int,
        file_location: str,
        chunk_args=None
    ):
        """Generate embeddings for PDF files."""
        yielded_count = 0
        chunk_count = 0
        for chunk_text in self.pdf_processor.process_pdf_data(file_location):
            chunk_count += 1
            embedding_vector = self.embedder.embed_documents(
                [chunk_text.page_content]
            )[0]
            yielded_count += 1

            # Update the embeddings generated count for the pipeline
            try:
                self.pipeline_tracking.update_chunks_processed(
                    pipeline_id=pipeline_id, chunks_count=chunk_count
                )
            except Exception as e:
                logging.error(
                    f"Failed to update embeddings generated: {str(e)}"
                )

            yield chunk_text.page_content, embedding_vector

        # Update the embedding progress for the pipeline
        try:
            self.pipeline_tracking.update_chunks_processed(
                pipeline_id=pipeline_id, chunks_count=chunk_count
            )
        except Exception as e:
            logging.error(f"Failed to update embeddings generated: {str(e)}")

        logging.info(
            f"Finished generating embeddings for {file_location}: "
            f"{chunk_count} total chunks, {yielded_count} embeddings yielded"
        )

    def _generate_embeddings_for_video_files(self, file_location: str, chunk_args=None):
        """Generate embeddings for video files."""
        pass

    def generate_embeddings(self, pipeline_id: int, file_location: str, chunk_args=None):
        """
        Generator that yields (chunk_text, embedding_vector) tuples.

        Args:
            file_location (str): Path to the file (local or s3://...).
            chunk_args (dict, optional): Arguments for chunking. Default splitter settings if None.

        Yields:
            Tuple[str, list[float]]: (chunk_text, embedding_vector)

        Raises:
            ValueError: If the file type is not supported.
        """
        if chunk_args is None:
            chunk_args = {}

        logging.info(
            f"Generating embedding using model: {self.embedding_model} "
            f"for file: {file_location}"
        )

        file_type, _ = mimetypes.guess_type(file_location)
        if file_type == 'text/plain' or file_type == 'application/json':
            return self._generate_embeddings_for_text_files(
                pipeline_id, file_location, chunk_args
            )
        elif file_type == 'application/pdf':
            return self._generate_embeddings_for_pdf_files(
                pipeline_id, file_location, chunk_args
            )
        elif file_type == 'video/mp4':
            return self._generate_embeddings_for_video_files(
                file_location, chunk_args
            )
        else:
            raise ValueError(f"Unsupported file type: {file_type}")

    def generate_user_prompt_embeddings(self, user_prompt: str) -> list[float]:
        """Generate embeddings for user prompt."""

        return self.embedder.embed_query(user_prompt)
