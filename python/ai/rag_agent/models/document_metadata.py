"""
DocumentMetadata - Standard object for document metadata across all processors.

This module defines the standard DocumentMetadata class used by all document
processors (AWS S3, video, PDF, etc.) to ensure consistency when creating
and tracking documents in the RAG pipeline.
"""

from dataclasses import dataclass
from typing import Optional, Dict, Any
from uuid import UUID


@dataclass
class DocumentMetadata:
    """
    Standard metadata object for documents in the RAG pipeline.

    This class provides a consistent interface for all document processors
    to create document metadata before insertion into the database.

    Attributes:
        source_id (UUID): The unique identifier of the source that this document belongs to.
        document_name (str): The name/identifier of the document (e.g., filename).
        document_uri (str): The URI/path where the document can be accessed (e.g., S3 URI).
        document_checksum (str): Checksum or hash of the document for change detection.
        status (str): Current status of the document (default: "QUEUED").
                     Valid values: "QUEUED", "PROCESSING", "COMPLETED", "FAILED"
    """

    source_id: UUID
    document_name: str
    document_uri: str
    document_checksum: str
    status: str = "QUEUED"

    def to_dict(self) -> Dict[str, Any]:
        """
        Convert the DocumentMetadata object to a dictionary.

        Returns:
            Dict[str, Any]: Dictionary representation of the metadata.
        """
        return {
            "source_id": self.source_id,
            "document_name": self.document_name,
            "document_uri": self.document_uri,
            "document_checksum": self.document_checksum,
            "status": self.status
        }

    @staticmethod
    def from_dict(data: Dict[str, Any]) -> "DocumentMetadata":
        """
        Create a DocumentMetadata object from a dictionary.

        Args:
            data (Dict[str, Any]): Dictionary containing document metadata fields.

        Returns:
            DocumentMetadata: A new DocumentMetadata instance.

        Raises:
            KeyError: If required fields are missing.
            TypeError: If field types are invalid.
        """
        return DocumentMetadata(
            source_id=data["source_id"],
            document_name=data["document_name"],
            document_uri=data["document_uri"],
            document_checksum=data["document_checksum"],
            status=data.get("status", "QUEUED")
        )

    def validate(self) -> bool:
        """
        Validate that all required fields are present and valid.

        Returns:
            bool: True if valid, raises ValueError otherwise.

        Raises:
            ValueError: If any field is invalid.
        """
        if not isinstance(self.source_id, UUID):
            raise ValueError(f"source_id must be a UUID, got {type(self.source_id)}")

        if not self.document_name or not isinstance(self.document_name, str):
            raise ValueError("document_name must be a non-empty string")

        if not self.document_uri or not isinstance(self.document_uri, str):
            raise ValueError("document_uri must be a non-empty string")

        if not self.document_checksum or not isinstance(self.document_checksum, str):
            raise ValueError("document_checksum must be a non-empty string")

        if not self.status or not isinstance(self.status, str):
            raise ValueError("status must be a non-empty string")

        valid_statuses = {"QUEUED", "PROCESSING", "COMPLETED", "FAILED"}
        if self.status not in valid_statuses:
            raise ValueError(
                f"status must be one of {valid_statuses}, got '{self.status}'"
            )

        return True
