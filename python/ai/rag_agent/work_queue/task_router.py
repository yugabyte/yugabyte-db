"""
Task Router - Routes tasks to appropriate processors based on task type.

This module implements the Registry Pattern for task routing, allowing:
- Dynamic registration of task processors
- Loose coupling between task types and processors
- Clean separation of concerns
- Easy extensibility for new task types
"""

import logging
from typing import Callable, Dict, Any, Optional
from abc import ABC, abstractmethod
from enum import Enum

from models.work_queue_task import WorkQueueTask


class TaskType(Enum):
    """Enum for supported task types"""
    # Add your task types here
    CREATE_SOURCE_AWS_S3 = "CREATE_SOURCE_AWS_S3"
    DOCUMENT_PREPROCESSING = "PREPROCESS"
    # Add more as needed


class TaskProcessor(ABC):
    """
    Abstract base class for task processors.
    All concrete processors should inherit from this.
    """

    @abstractmethod
    def process(self, task: WorkQueueTask) -> Dict[str, Any]:
        """
        Process a task.

        Args:
            task: WorkQueueTask object containing id, task_type,
                task_details, lease_token, lease_expires_at

        Returns:
            Result dictionary with status and any relevant output

        Raises:
            Exception: Any processing errors should be raised
        """
        pass

    @abstractmethod
    def validate(self, task: WorkQueueTask) -> bool:
        """
        Validate if the task can be processed by this processor.

        Args:
            task: WorkQueueTask object

        Returns:
            True if valid, False otherwise
        """
        pass


class TaskRouter:
    """
    Routes tasks to appropriate processors based on task type.

    Uses the Registry Pattern to maintain a mapping of task types to processors.
    This allows dynamic registration and loose coupling.

    Example:
        router = TaskRouter()

        # Register processors
        router.register(TaskType.DOCUMENT_EMBEDDING.value, DocumentEmbeddingProcessor())
        router.register(TaskType.DOCUMENT_CHUNKING.value, DocumentChunkingProcessor())

        # Route a task
        result = router.route_task(task)
    """

    def __init__(self):
        self._registry: Dict[str, TaskProcessor] = {}
        self.logger = logging.getLogger(__name__)

    def register(self, task_type: str, processor: TaskProcessor) -> None:
        """
        Register a processor for a specific task type.

        Args:
            task_type: The task type identifier (e.g., "DOCUMENT_EMBEDDING")
            processor: The TaskProcessor instance to handle this task type

        Raises:
            ValueError: If task_type is already registered
        """
        if task_type in self._registry:
            raise ValueError(f"Task type '{task_type}' is already registered")

        self._registry[task_type] = processor
        self.logger.info(f"Registered processor for task type: {task_type}")

    def unregister(self, task_type: str) -> None:
        """
        Unregister a processor for a specific task type.

        Args:
            task_type: The task type identifier
        """
        if task_type in self._registry:
            del self._registry[task_type]
            self.logger.info(f"Unregistered processor for task type: {task_type}")

    def route_task(self, task: WorkQueueTask) -> Dict[str, Any]:
        """
        Route a task to the appropriate processor.

        Args:
            task: WorkQueueTask object with task_type and task_details

        Returns:
            Result dictionary with processing status and output

        Raises:
            ValueError: If task_type is not registered
            Exception: Any processing errors
        """
        task_type = task.task_type

        if not task_type:
            raise ValueError("Task must have a valid 'task_type' field")

        if task_type not in self._registry:
            raise ValueError(
                f"No processor registered for task type '{task_type}'. "
                f"Available types: {list(self._registry.keys())}"
            )

        processor = self._registry[task_type]

        # Validate task
        if not processor.validate(task):
            raise ValueError(f"Task validation failed for type '{task_type}'")

        try:
            self.logger.info(f"Routing task {task.id} of type '{task_type}' to processor")
            result = processor.process(task)
            return result
        except Exception as e:
            self.logger.error(f"Error processing task {task.id}: {str(e)}")
            return {
                'status': 'error',
                'task_id': str(task.id),
                'error': str(e)
            }

    def is_registered(self, task_type: str) -> bool:
        """
        Check if a task type is registered.

        Args:
            task_type: The task type identifier

        Returns:
            True if registered, False otherwise
        """
        return task_type in self._registry

    def get_registered_types(self) -> list:
        """
        Get all registered task types.

        Returns:
            List of registered task type identifiers
        """
        return list(self._registry.keys())


# Global router instance (singleton pattern)
_router_instance: Optional[TaskRouter] = None


def get_router() -> TaskRouter:
    """
    Get or create the global TaskRouter instance.

    Returns:
        TaskRouter instance
    """
    global _router_instance
    if _router_instance is None:
        _router_instance = TaskRouter()
    return _router_instance


def register_processor(task_type: str, processor: TaskProcessor) -> None:
    """
    Register a processor globally.

    Args:
        task_type: The task type identifier
        processor: The TaskProcessor instance
    """
    get_router().register(task_type, processor)


def route_task(task: WorkQueueTask) -> Dict[str, Any]:
    """
    Route a task to the appropriate processor globally.

    Args:
        task: WorkQueueTask object

    Returns:
        Result dictionary
    """
    return get_router().route_task(task)
