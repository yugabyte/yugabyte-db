"""
WorkQueueTask - Standard object for work queue tasks.

This module defines the WorkQueueTask class used to represent tasks retrieved
from the distributed work queue during polling operations.
"""

from dataclasses import dataclass
from typing import Optional, Dict, Any
from uuid import UUID
from datetime import datetime


@dataclass
class WorkQueueTask:
    """
    Standard task object for work queue items in the RAG pipeline.

    This class represents a task polled from the distributed work queue,
    containing all necessary information for a worker to process the task.

    Attributes:
        id (UUID): The unique identifier of the work queue task.
        task_type (str): The type of task (e.g., "S3_SOURCE_INGEST", "PDF_PROCESS").
        task_details (Dict[str, Any]): JSON payload containing task-specific details.
        lease_token (UUID): Token used to prove ownership of the lease during task processing.
        lease_expires_at (datetime): Timestamp when the lease expires;
            task must be completed before this.
    """

    id: UUID
    task_type: str
    task_details: Dict[str, Any]
    lease_token: UUID
    lease_expires_at: datetime

    def to_dict(self) -> Dict[str, Any]:
        """
        Convert the WorkQueueTask object to a dictionary.

        Returns:
            Dict[str, Any]: Dictionary representation of the task.
        """
        return {
            "id": self.id,
            "task_type": self.task_type,
            "task_details": self.task_details,
            "lease_token": self.lease_token,
            "lease_expires_at": self.lease_expires_at
        }

    @staticmethod
    def from_db_row(row: tuple) -> "WorkQueueTask":
        """
        Create a WorkQueueTask object from a database row.

        Args:
            row (tuple): Tuple from RETURNING clause of work queue poll query.
                        Expected format: (id, task_type, task_details,
                        lease_token, lease_expires_at)

        Returns:
            WorkQueueTask: A new WorkQueueTask instance.

        Raises:
            IndexError: If row doesn't have the expected 5 elements.
            TypeError: If field types are invalid.
        """
        if len(row) != 5:
            raise IndexError(f"Expected 5 fields in row, got {len(row)}")

        return WorkQueueTask(
            id=row[0],
            task_type=row[1],
            task_details=row[2],
            lease_token=row[3],
            lease_expires_at=row[4]
        )

    @staticmethod
    def from_dict(data: Dict[str, Any]) -> "WorkQueueTask":
        """
        Create a WorkQueueTask object from a dictionary.

        Args:
            data (Dict[str, Any]): Dictionary containing task fields.

        Returns:
            WorkQueueTask: A new WorkQueueTask instance.

        Raises:
            KeyError: If required fields are missing.
            TypeError: If field types are invalid.
        """
        return WorkQueueTask(
            id=data["id"],
            task_type=data["task_type"],
            task_details=data["task_details"],
            lease_token=data["lease_token"],
            lease_expires_at=data["lease_expires_at"]
        )

    def validate(self) -> bool:
        """
        Validate that all required fields are present and valid.

        Returns:
            bool: True if valid, raises ValueError otherwise.

        Raises:
            ValueError: If any field is invalid.
        """
        if not isinstance(self.id, UUID):
            raise ValueError(f"id must be a UUID, got {type(self.id)}")

        if not self.task_type or not isinstance(self.task_type, str):
            raise ValueError("task_type must be a non-empty string")

        if not isinstance(self.task_details, dict):
            raise ValueError(f"task_details must be a dict, got {type(self.task_details)}")

        if not isinstance(self.lease_token, UUID):
            raise ValueError(f"lease_token must be a UUID, got {type(self.lease_token)}")

        if not isinstance(self.lease_expires_at, datetime):
            raise ValueError(
                f"lease_expires_at must be a datetime, "
                f"got {type(self.lease_expires_at)}"
            )

        return True
