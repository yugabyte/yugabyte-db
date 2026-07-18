from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse
from db.connection_pool import ConnectionPool
from work_queue.poller import Poller
from work_queue.task_router import get_router
from work_queue.task_type_keys import TaskTypeKeys
from rag_pipeline import CreateSourceProcessorForAWS_S3, DocumentPreprocessor, UserPromptEmbedder
from models.work_queue_task import WorkQueueTask
from pydantic import BaseModel
from typing import Dict, Any
from contextlib import asynccontextmanager
import asyncio
import logging
import os
import sys
import threading
import time
import signal
import psycopg
import random

# Configure logging - use basicConfig with forcing to override uvicorn
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
    handlers=[
        logging.StreamHandler(sys.stdout),
        logging.FileHandler('app.log')
    ],
    force=True  # Force override of existing handlers
)

logger = logging.getLogger(__name__)

# Global Poller instance and polling thread
poller = None
poller_thread = None
polling_active = False

embedding_generation_poller = None
embedding_generation_poller_thread = None
embedding_generation_poller_active = False


def route_task(task: WorkQueueTask) -> Dict[str, Any]:
    """
    Process a task by routing it to the appropriate processor.

    Args:
        task: Task dictionary with id, task_type, task_details, etc.
    """
    try:
        router = get_router()
        result = router.route_task(task)
        logger.info(f"Task processing result: {result}")
        return result
    except Exception as e:
        logger.error(f"Failed to process task {task.id}: {str(e)}")


def embedding_generation_worker():
    """
    Synchronous worker thread that continuously generates embeddings for user prompts.
    """
    global embedding_generation_poller

    logger.info("Embedding generation worker thread started")

    try:
        embedding_generation_poller = Poller()
        while embedding_generation_poller_active:
            try:
                # Get the worker ID from environment or generate one
                import uuid
                # nik-todo: make worker_id configurable from CLI args.
                worker_id = str(uuid.uuid4())
                # nik-todo: make lease_duration configurable from CLI args.
                lease_duration = int(os.getenv("TASK_LEASE_DURATION", "600"))

                # Poll for a task
                task = embedding_generation_poller.poll_embedding_generation(
                    worker_id=worker_id,
                    lease_duration_seconds=lease_duration
                )

                if task:
                    logger.info(
                        f"Acquired task: id={task.id}, "
                        f"type={task.task_type}, "
                        f"lease_token={task.lease_token}"
                    )
                    status = route_task(task)
                else:
                    # No task available, sleep briefly before polling again
                    sleep_duration = 1
                    time.sleep(sleep_duration)
                    logger.info(
                        f"No user prompt embedding task available, sleeping "
                        f"for {sleep_duration} seconds before polling again"
                    )

            except Exception as e:
                logger.error(f"Error in polling loop: {e}")
                time.sleep(60)  # Back off before retrying
                logger.info(
                    "Error in polling loop, sleeping for 60 seconds "
                    "before retrying"
                )
    finally:
        logger.info("Embedding generation worker thread shutting down")


def generate_embeddings():
    """
    Generate embeddings for user prompts.
    """
    return True


def polling_worker():
    """
    Synchronous worker thread that continuously polls for work queue tasks.
    Gets tasks from the queue and starts processing them.
    """
    global poller

    logger.info("Polling worker thread started")

    try:
        poller = Poller()

        while polling_active:
            try:
                # Get the worker ID from environment or generate one
                import uuid
                # nik-todo: make worker_id configurable from CLI args.
                worker_id = str(uuid.uuid4())
                # nik-todo: make lease_duration configurable from CLI args.
                lease_duration = int(os.getenv("TASK_LEASE_DURATION", "600"))

                # Poll for a task
                task = poller.poll(
                    worker_id=worker_id,
                    lease_duration_seconds=lease_duration
                )

                if task:
                    logger.info(
                        f"Acquired task: id={task.id}, "
                        f"type={task.task_type}, "
                        f"lease_token={task.lease_token}"
                    )
                    status = route_task(task)
                else:
                    # No task available, sleep briefly before polling again
                    sleep_duration = 60  # 1 minute
                    time.sleep(sleep_duration)
                    logger.info(
                        f"No task available, sleeping for "
                        f"{sleep_duration} seconds before polling again"
                    )

            except Exception as e:
                logger.error(f"Error in polling loop: {e}")
                time.sleep(60)  # Back off before retrying
                logger.info(
                    "Error in polling loop, sleeping for 60 seconds "
                    "before retrying"
                )
    finally:
        logger.info("Polling worker thread shutting down")


def wait_for_extension_creation():
    """
    Wait for the extension to be created using a direct connection (not from pool).
    This avoids exhausting the connection pool during startup.
    """
    global polling_active, embedding_generation_poller_active

    logger.info("Waiting for extension creation...")
    retry_count = 0
    max_retries = 720  # ~60 minutes with 5 second sleep

    db_connection_string = os.getenv("YUGABYTEDB_CONNECTION_STRING")
    if not db_connection_string:
        raise ValueError(
            "YUGABYTEDB_CONNECTION_STRING environment variable "
            "is required"
        )

    while retry_count < max_retries:
        connection = None
        cursor = None
        try:
            # Use a direct connection, not from the pool
            connection = psycopg.connect(db_connection_string)
            cursor = connection.cursor()
            cursor.execute(
                "SELECT * FROM pg_extension WHERE extname = 'pg_dist_rag'"
            )
            extension = cursor.fetchone()
            if extension:
                logger.info("Extension created, starting polling workers...")
                polling_active = True
                embedding_generation_poller_active = True
                return True
            else:
                logger.debug("Extension not yet created, will retry...")
        except Exception as e:
            logger.warning(
                f"Extension check failed, retrying... "
                f"({retry_count + 1}/{max_retries}): {e}"
            )
        finally:
            # Always cleanup resources
            if cursor:
                try:
                    cursor.close()
                except Exception:
                    pass
            if connection:
                try:
                    connection.close()
                except Exception:
                    pass

        retry_count += 1
        if retry_count < max_retries:
            time.sleep(5)  # Wait 5 seconds before retrying

    logger.error(f"Extension not created after {max_retries} retries")
    return False


@asynccontextmanager
async def lifespan(app: FastAPI):
    """
    Lifespan context manager for startup and shutdown events.
    """
    # Startup
    global poller_thread, embedding_generation_poller_thread
    try:
        logger.info("Starting up application...")

        # Wait for the extension to be created
        if not wait_for_extension_creation():
            raise RuntimeError(
                "Extension was not created in time. "
                "Shutting down application."
            )

        # Get database connection string from environment variable
        db_connection_string = os.getenv("YUGABYTEDB_CONNECTION_STRING")

        # Initialize the connection pool FIRST
        ConnectionPool.initialize(db_connection_string)
        logger.info("ConnectionPool initialized successfully")

        # Initialize task router and register processors
        router = get_router()
        router.register(
            TaskTypeKeys.CREATE_SOURCE,
            CreateSourceProcessorForAWS_S3()
        )
        router.register(
            TaskTypeKeys.DOCUMENT_PREPROCESSING,
            DocumentPreprocessor()
        )
        router.register(
            TaskTypeKeys.USER_PROMPT_EMBEDDING,
            UserPromptEmbedder()
        )
        logger.info("Task processors registered successfully")

        # Start the polling worker thread
        poller_thread = threading.Thread(target=polling_worker, daemon=True)
        poller_thread.start()
        logger.info("Polling worker thread started successfully")

        # Start the polling worker thread for embedding generation
        # embedding_generation_poller_thread = threading.Thread(
        #     target=embedding_generation_worker, daemon=True
        # )
        # embedding_generation_poller_thread.start()
        # logger.info("Embedding generation worker thread started")

    except Exception as e:
        logger.error(f"Failed to start up application: {e}")
        raise

    yield  # Application runs here

    # Shutdown
    logger.info("Shutting down application...")
    try:
        # Stop the polling worker thread
        polling_active = False
        if poller_thread and poller_thread.is_alive():
            poller_thread.join(timeout=5)
            logger.info("Polling worker thread stopped")

        ConnectionPool.close_all()
        logger.info("ConnectionPool closed successfully")
    except Exception as e:
        logger.error(f"Error during shutdown: {e}")


async def health_check():
    """
    Health check endpoint to verify the API is running
    """
    status = "healthy"

    return status


async def main():
    """
    Main entry point for the long-running RAG preprocessor.
    """
    # Create a temporary FastAPI app just to use the lifespan context manager
    temp_app = FastAPI(lifespan=lifespan)

    # The lifespan context manager handles startup and shutdown
    async with lifespan(temp_app):
        logger.info("RAG Preprocessor is running. Press Ctrl+C to stop.")
        try:
            # Keep the process running indefinitely
            while True:
                await asyncio.sleep(1)
        except KeyboardInterrupt:
            logger.info("Received interrupt signal, shutting down gracefully...")
        except asyncio.CancelledError:
            logger.info("Task cancelled, shutting down gracefully...")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        logger.info("Process terminated by keyboard interrupt.")
    except Exception as e:
        logger.error(f"Unexpected error in main: {e}", exc_info=True)


# nik-todos
# - add sql function support for index creation status.
# - work_queue task status should be updated to COMPLETED or FAILED after documentprocessing.
# - when document list are downloaded from the object store, the status should be NOT_STARTED.
