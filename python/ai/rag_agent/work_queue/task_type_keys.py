"""
Processor Keywords and Constants.

This module defines all constants for task processors that are registered with the TaskRouter.
It includes task type identifiers, processor names, configuration defaults, and other
processor-related constants to maintain consistency across the system.
"""

# Task Type Constants
# These correspond to TaskType enum values in task_router.py


class TaskTypeKeys:
    """Constants for task types processed by different processors."""

    # AWS/S3 related tasks
    # nik-todo: add other source types here.
    CREATE_SOURCE = "CREATE_SOURCE"

    # Document preprocessing tasks
    DOCUMENT_PREPROCESSING = "PREPROCESS"

    # Document chunking tasks
    DOCUMENT_CHUNKING = "DOCUMENT_CHUNKING"

    # Document embedding tasks
    DOCUMENT_EMBEDDING = "DOCUMENT_EMBEDDING"

    # Data processing tasks
    DATA_PROCESSING = "DATA_PROCESSING"

    # User prompt embedding tasks
    USER_PROMPT_EMBEDDING = "USER_PROMPT_EMBEDDING"
