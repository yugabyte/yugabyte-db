import logging
import json
from typing import Dict, Any
from work_queue.task_router import TaskProcessor
from models.work_queue_task import WorkQueueTask
from db.connection_pool import ConnectionPool
from embeddings import EmbeddingsGenerator
from datetime import datetime


class UserPromptEmbedder(TaskProcessor):
    """
    Class to embed user prompt using OpenAI and perform similarity search against a PostgreSQL table
    with HNSW vector index.
    """

    def __init__(self):
        self.logger = logging.getLogger(__name__)
        self.connection_pool = ConnectionPool()

    def _store_user_prompt_embeddings(
        self,
        user_prompt_id: str,
        user_prompt_raw: str,
        user_prompt_vector: str,
        created_at: datetime
    ):
        """
        Store the embedding in the cache.
        """

        connection = None
        try:

            user_prompt_vector_json = {
                "embedding": user_prompt_vector,
            }

            connection = self.connection_pool.get_connection()
            cursor = connection.cursor()
            query = """
                INSERT INTO dist_rag._user_prompts_store (
                    id,
                    user_prompt_raw,
                    user_prompt_vector,
                    created_at
                ) VALUES (%s, %s, %s, %s)
            """
            cursor.execute(
                query,
                (user_prompt_id, user_prompt_raw,
                 json.dumps(user_prompt_vector_json), created_at)
            )
            connection.commit()
            cursor.close()

            return True
        except Exception as e:
            if connection:
                connection.rollback()
            self.logger.error(f"Error storing user prompt embeddings: {e}")
            raise
        finally:
            if cursor:
                cursor.close()
            if connection:
                self.connection_pool.return_connection(connection)

    def _retrieve_embedding_parameters(self, index_name: str) -> Dict[str, Any]:
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
                WHERE index_name = %s
            """
            cursor.execute(query, (str(index_name),))
            result = cursor.fetchone()
            if result:
                return result[0]  # embedding_model_params is the first column
            else:
                self.logger.error(f"No vector_indexes entry found for index_id: {index_name}")
                return {}

        except Exception as e:
            if connection:
                connection.rollback()
            self.logger.error(
                f"Error fetching embedding_model_params for index_name "
                f"{index_name}: {str(e)}"
            )
            raise
        finally:
            if cursor:
                cursor.close()
            if connection:
                self.connection_pool.return_connection(connection)
        return None

    def _embed_prompt(
        self,
        prompt: str,
        embedding_model_params: dict
    ):
        """
        Embed the prompt using the specified AI provider and embedding model parameters.
        """
        try:
            embedding_model = embedding_model_params.get('model')
            embedder = EmbeddingsGenerator(
                embedding_model=embedding_model,
                embedding_model_params=embedding_model_params
            )
            embedding = embedder.generate_user_prompt_embeddings(prompt)
            return embedding
        except Exception as e:
            self.logger.error(f"Error embedding prompt: {e}")
            raise

    def validate(self, task: WorkQueueTask) -> bool:
        """
        Validate the task.
        """
        task_details = task.task_details
        print(f"task_details: {task_details}")

        # Validate required fields in task_details
        if not task_details.get('user_prompt'):
            self.logger.error(f"Prompt is required for embedding generation task")
            return False
        if not task_details.get('index_name'):
            self.logger.error(f"Index name is required for embedding generation task")
            return False
        if not task_details.get('user_prompt_id'):
            self.logger.error(f"User prompt id is required for embedding generation task")
            return False

        return True

    def process(self, task: WorkQueueTask) -> Dict[str, Any]:
        """
        Process the task.
        """

        try:

            # Retrieve required parameters
            index_name = task.task_details.get('index_name')

            try:
                embedding_model_params = self._retrieve_embedding_parameters(
                    index_name=index_name
                )
                if not embedding_model_params:
                    raise ValueError(
                        f"Failed to retrieve embedding parameters for "
                        f"index_name: {index_name}"
                    )
            except Exception as e:
                self.logger.error(f"Error retrieving embedding parameters: {str(e)}")
                raise

            try:
                user_prompt_id = task.task_details.get('user_prompt_id')
                self.logger.info(f"Embedding user prompt: {user_prompt_id}")
                embedding = self._embed_prompt(
                    task.task_details.get('user_prompt'),
                    embedding_model_params
                )
                self.logger.info(
                    f"Succesfully generated embedding for user prompt id "
                    f"{user_prompt_id}"
                )
            except Exception as e:
                logging.error(
                    f"Failed to generate embeddings for user prompt id "
                    f"{user_prompt_id}: {str(e)}"
                )
                raise

            try:
                self._store_user_prompt_embeddings(
                    task.task_details.get('user_prompt_id'),
                    task.task_details.get('user_prompt'),
                    embedding,
                    datetime.utcnow()
                )
            except Exception as e:
                self.logger.error(
                    f"Error storing user prompt embeddings for user prompt id "
                    f"{task.task_details.get('user_prompt_id')}: {e}"
                )
                raise

            return {
                "status": "success",
                "task_id": task.id,
                "user_prompt_id": task.task_details.get('user_prompt_id')
            }

        except Exception as e:
            self.logger.error(
                f"Unexpected error while processing task {task.id}: {str(e)}",
                exc_info=True
            )
            return {
                "status": "error",
                "task_id": task.id,
                "error_type": type(e).__name__,
                "error_message": str(e)
            }
