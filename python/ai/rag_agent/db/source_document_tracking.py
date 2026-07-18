from db.connection_pool import ConnectionPool
from models.document_metadata import DocumentMetadata
from typing import Union, Dict, Any
from uuid import UUID
import logging


class SourceDocumentTracking:

    def __init__(self):
        self.connection_pool = ConnectionPool()
        self.logger = logging.getLogger(__name__)

    def insert_document_metadata(self, document_metadata: DocumentMetadata):
        """
            Insert document metadata into the dist_rag.documents table.

            Args:
                document_metadata (Union[DocumentMetadata, dict]): Document metadata to insert.
                    Can be a DocumentMetadata object or a dictionary containing:
                    - source_id (UUID, required)
                    - document_name (str, required)
                    - document_uri (str, required)
                    - document_checksum (str, required)
                    - status (str, optional; defaults to 'QUEUED')
            Returns:
                document_id (UUID): The ID of the inserted document.

            Raises:
                ValueError: If metadata is invalid
                Exception: If database insertion fails
            """
        connection = None
        try:
            connection = self.connection_pool.get_connection()
            cursor = connection.cursor()

            query = """
                INSERT INTO dist_rag.documents (
                    source_id,
                    document_name,
                    document_uri,
                    document_checksum,
                    status
                ) VALUES (%s, %s, %s, %s, %s)
                RETURNING document_id;
            """

            source_id = document_metadata.source_id
            document_name = document_metadata.document_name
            document_uri = document_metadata.document_uri
            document_checksum = document_metadata.document_checksum
            status = document_metadata.status

            cursor.execute(
                query,
                (source_id, document_name, document_uri,
                 document_checksum, status)
            )
            document_id = cursor.fetchone()[0]
            connection.commit()
            self.logger.info(
                f"Inserted document metadata with document_id: {document_id}"
            )
            return document_id
        except Exception as e:
            if connection:
                try:
                    connection.rollback()
                except Exception:
                    pass
            self.logger.error(f"Error inserting document metadata: {str(e)}")
            raise
        finally:
            if connection:
                self.connection_pool.return_connection(connection)

    def get_source_details(self, source_id: UUID) -> Dict[str, Any]:
        """
        Get source details from the dist_rag.sources table.
        """
        connection = None
        try:
            connection = self.connection_pool.get_connection()
            cursor = connection.cursor()
            self.logger.debug(
                f"DB: SourceDocumentTracking: Getting source details "
                f"for source_id: {source_id}"
            )
            cursor.execute(
                "SELECT * FROM dist_rag.sources WHERE id = %s",
                (source_id,)
            )
            row = cursor.fetchone()
            if row is None:
                return None
            colnames = [desc[0] for desc in cursor.description]
            return dict(zip(colnames, row))
        finally:
            if connection:
                self.connection_pool.return_connection(connection)

    def get_document_uris_by_source_id(self, source_id: UUID) -> list:
        """
        Get document URIs by source_id from the dist_rag.documents table.
        """
        connection = None
        try:
            connection = self.connection_pool.get_connection()
            cursor = connection.cursor()
            cursor.execute(
                "SELECT document_uri FROM dist_rag.documents "
                "WHERE source_id = %s",
                (source_id,)
            )
            rows = cursor.fetchall()
            return [row[0] for row in rows]
        except Exception as e:
            if connection:
                try:
                    connection.rollback()
                except Exception:
                    pass
            self.logger.error(f" Error retrieving document details: {str(e)}")
            raise
        finally:
            if connection:
                self.connection_pool.return_connection(connection)

    def update_document_status(self, document_id: UUID, status: str):
        """
        Update the status of a document.
        """
        connection = None
        try:
            connection = self.connection_pool.get_connection()
            cursor = connection.cursor()
            cursor.execute(
                "UPDATE dist_rag.documents SET status = %s "
                "WHERE document_id = %s",
                (status, document_id)
            )
            connection.commit()
            return True
        except Exception as e:
            if connection:
                try:
                    connection.rollback()
                except Exception:
                    pass
            self.logger.error(f"Error updating document status: {str(e)}")
            raise
        finally:
            if cursor:
                cursor.close()
            if connection:
                self.connection_pool.return_connection(connection)
