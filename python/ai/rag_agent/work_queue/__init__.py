"""
Data models for the RAG preprocessor.

This module contains standard data classes used across different document processors.
"""

from work_queue.task_type_keys import TaskTypeKeys
from work_queue.task_router import TaskRouter

__all__ = ["TaskRouter", "TaskTypeKeys"]
