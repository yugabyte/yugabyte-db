from db.connection_pool import ConnectionPool
from models.document_metadata import DocumentMetadata
from typing import Union, Dict, Any, Optional
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
                document_id (UUID): The ID of the inserted document, or None
                when a row for the same source and URI already exists.

            Raises:
                ValueError: If metadata is invalid
                Exception: If database insertion fails
            """
        connection = None
        try:
            connection = self.connection_pool.get_connection()
            cursor = connection.cursor()

            # No conflict target: the statement must work both before and
            # after pg_dist_rag 0.0.4 adds UNIQUE (source_id, document_uri).
            # With that index, a re-listed file (a retried or redelivered
            # CREATE_SOURCE) or a file the api-server already inserted is
            # skipped instead of failing the crawl.
            query = """
                INSERT INTO dist_rag.documents (
                    source_id,
                    document_name,
                    document_uri,
                    document_checksum,
                    document_type,
                    status
                ) VALUES (%s, %s, %s, %s, %s, %s)
                ON CONFLICT DO NOTHING
                RETURNING document_id;
            """

            source_id = document_metadata.source_id
            document_name = document_metadata.document_name
            document_uri = document_metadata.document_uri
            document_checksum = document_metadata.document_checksum
            document_type = document_metadata.document_type
            status = document_metadata.status

            cursor.execute(
                query,
                (source_id, document_name, document_uri,
                 document_checksum, document_type, status)
            )
            row = cursor.fetchone()
            connection.commit()
            if row is None:
                self.logger.info(
                    f"Document {document_uri} of source {source_id} already exists"
                )
                return None
            document_id = row[0]
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

    def get_document_status(self, document_id: UUID) -> Optional[str]:
        """
        Look up the current status of a document in ``dist_rag.documents``.

        Returns ``None`` when the row doesn't exist. Callers use this for
        idempotency checks (e.g. skip processing a document whose status
        is already ``COMPLETED``); failures to look up are treated as
        "unknown -- proceed" by the caller.
        """
        connection = None
        cursor = None
        try:
            connection = self.connection_pool.get_connection()
            cursor = connection.cursor()
            cursor.execute(
                "SELECT status FROM dist_rag.documents WHERE document_id = %s",
                (str(document_id),),
            )
            row = cursor.fetchone()
            return row[0] if row else None
        except Exception as e:
            if connection:
                try:
                    connection.rollback()
                except Exception:
                    pass
            self.logger.warning(
                f"Failed to look up status for document_id {document_id}: "
                f"{str(e)}"
            )
            return None
        finally:
            if cursor:
                cursor.close()
            if connection:
                self.connection_pool.return_connection(connection)

    def claim_document(self, document_id, task_id, lease_token=None,
                       trace_id: Optional[str] = None) -> bool:
        """Mark a document PROCESSING for this task's lease, unless a live lease holds it.

        Two tasks for one document (from re-queuing a source) would otherwise
        both embed it. The claim records the task id and lease token. A
        document left PROCESSING can be reclaimed only when the lease that
        claimed it is no longer live: the reaper re-queued its task, or the
        lease expired. A worker whose lease lapsed therefore can't share the
        document with the one that took over. A claim made by an older agent,
        which recorded no task, is also reclaimable; a claim made without a
        lease token never is.

        Returns False when a live lease holds the document or it is done.
        """
        connection = None
        try:
            connection = self.connection_pool.get_connection()
            cursor = connection.cursor()
            lease = None if lease_token is None else str(lease_token)
            cursor.execute(
                "UPDATE dist_rag.documents "
                "SET status = 'PROCESSING', "
                "    document_details = jsonb_set("
                "        COALESCE(document_details, '{}'::jsonb),"
                "        '{meko}',"
                "        COALESCE(COALESCE(document_details, '{}'::jsonb)->'meko',"
                "                 '{}'::jsonb)"
                "            || jsonb_build_object('claimed_by', %s::text,"
                "                                  'claim_lease', %s::text)"
                "            || CASE WHEN %s::text IS NULL THEN '{}'::jsonb"
                "                    ELSE jsonb_build_object('trace_id', %s::text) END,"
                "        true"
                "    ) "
                "WHERE document_id = %s "
                "  AND (status NOT IN ('PROCESSING', 'COMPLETED') "
                "       OR (status = 'PROCESSING' "
                "           AND (document_details->'meko'->>'claimed_by' IS NULL "
                "                OR (document_details->'meko'->>'claim_lease' IS NOT NULL "
                "                    AND NOT EXISTS ("
                "                        SELECT 1 FROM dist_rag.work_queue w "
                "                        WHERE w.id = "
                "                              (document_details->'meko'->>'claimed_by')::uuid "
                "                          AND w.lease_token = "
                "                              (document_details->'meko'->>'claim_lease')::uuid "
                "                          AND w.task_status = 'IN_PROGRESS' "
                "                          AND w.lease_expires_at > CURRENT_TIMESTAMP))))) "
                "RETURNING document_id",
                (str(task_id), lease, trace_id, trace_id, str(document_id)),
            )
            claimed = cursor.fetchone() is not None
            connection.commit()
            return claimed
        except Exception as e:
            if connection:
                try:
                    connection.rollback()
                except Exception:
                    pass
            logging.error(f"Error claiming document {document_id}: {str(e)}")
            raise
        finally:
            if connection:
                self.connection_pool.return_connection(connection)

    def update_document_status(
        self,
        document_id: UUID,
        status: str,
        trace_id: Optional[str] = None,
    ):
        """
        Update the status of a document.

        When *trace_id* is supplied the ``document_details`` JSONB column is
        set (or merged) with ``{"meko": {"trace_id": "<value>"}}``.
        """
        connection = None
        try:
            connection = self.connection_pool.get_connection()
            cursor = connection.cursor()
            if trace_id:
                cursor.execute(
                    "UPDATE dist_rag.documents "
                    "SET status = %s, "
                    "    document_details = jsonb_set("
                    "        jsonb_set("
                    "            COALESCE(document_details, '{}'::jsonb),"
                    "            '{meko}',"
                    "            COALESCE(COALESCE(document_details, '{}'::jsonb)->'meko',"
                    "                '{}'::jsonb),"
                    "            true"
                    "        ),"
                    "        '{meko,trace_id}',"
                    "        to_jsonb(%s::text),"
                    "        true"
                    "    ) "
                    "WHERE document_id = %s",
                    (status, trace_id, document_id),
                )
            else:
                cursor.execute(
                    "UPDATE dist_rag.documents SET status = %s "
                    "WHERE document_id = %s",
                    (status, document_id),
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
