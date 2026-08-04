# !/usr/bin/env python3
"""
Test suite for YugabyteDBVectorStore class.

This test file focuses on testing the core functionality of YugabyteDBVectorStore
including initialization, table existence checks, embedding insertion, and connection management.
"""

import pytest
import json
from unittest.mock import Mock, patch, MagicMock, call

from db.yugabytedb_vector_store import YugabyteDBVectorStore

PIPELINE_ID = 1


def _create_store(mock_pool_cls, mock_pt_cls):
    """Helper to create a YugabyteDBVectorStore with mocked dependencies."""
    mock_pool = Mock()
    mock_pool_cls.return_value = mock_pool
    mock_pt = Mock()
    mock_pt_cls.return_value = mock_pt
    mock_conn = Mock()
    mock_cur = Mock()
    mock_cur.rowcount = 0
    mock_pool.get_connection.return_value = mock_conn
    mock_conn.cursor.return_value = mock_cur
    store = YugabyteDBVectorStore()
    return store, mock_pool, mock_pt, mock_conn, mock_cur


class TestYugabyteDBVectorStore:
    """Test cases for YugabyteDBVectorStore class."""

    @patch('db.yugabytedb_vector_store.PipelineTracking')
    @patch('db.yugabytedb_vector_store.ConnectionPool')
    def test_init_valid_parameters(self, mock_pool_cls, mock_pt_cls):
        """Test initialization with valid parameters."""
        store, mock_pool, mock_pt, mock_conn, mock_cur = _create_store(
            mock_pool_cls, mock_pt_cls
        )

        assert store.schema == "public"
        assert store.pool == mock_pool
        assert store.pipeline_tracking == mock_pt

    @patch('db.yugabytedb_vector_store.PipelineTracking')
    @patch('db.yugabytedb_vector_store.ConnectionPool')
    def test_init_custom_schema(self, mock_pool_cls, mock_pt_cls):
        """Test initialization with custom schema."""
        mock_pool = Mock()
        mock_pool_cls.return_value = mock_pool
        mock_pt = Mock()
        mock_pt_cls.return_value = mock_pt

        store = YugabyteDBVectorStore(schema="custom_schema")

        assert store.schema == "custom_schema"

    @patch('db.yugabytedb_vector_store.PipelineTracking')
    @patch('db.yugabytedb_vector_store.ConnectionPool')
    def test_init_default_parameters(self, mock_pool_cls, mock_pt_cls):
        """Test initialization with default parameters."""
        store, mock_pool, mock_pt, mock_conn, mock_cur = _create_store(
            mock_pool_cls, mock_pt_cls
        )

        assert store.schema == "public"

    @patch('db.yugabytedb_vector_store.PipelineTracking')
    @patch('db.yugabytedb_vector_store.ConnectionPool')
    def test_ensure_table_exists_true(self, mock_pool_cls, mock_pt_cls):
        """Test _ensure_table_exists returns True when table exists."""
        store, mock_pool, mock_pt, mock_conn, mock_cur = _create_store(
            mock_pool_cls, mock_pt_cls
        )
        mock_cur.fetchone.return_value = (True,)

        result = store._ensure_table_exists("test_table")

        assert result is True

    @patch('db.yugabytedb_vector_store.PipelineTracking')
    @patch('db.yugabytedb_vector_store.ConnectionPool')
    def test_ensure_table_exists_false(self, mock_pool_cls, mock_pt_cls):
        """Test _ensure_table_exists returns False when table does not exist."""
        store, mock_pool, mock_pt, mock_conn, mock_cur = _create_store(
            mock_pool_cls, mock_pt_cls
        )
        mock_cur.fetchone.return_value = (False,)

        result = store._ensure_table_exists("nonexistent_table")

        assert result is False

    @patch('db.yugabytedb_vector_store.PipelineTracking')
    @patch('db.yugabytedb_vector_store.ConnectionPool')
    def test_insert_embeddings_basic(self, mock_pool_cls, mock_pt_cls):
        """Test basic embedding insertion functionality."""
        store, mock_pool, mock_pt, mock_conn, mock_cur = _create_store(
            mock_pool_cls, mock_pt_cls
        )
        # _ensure_table_exists should return True
        mock_cur.fetchone.return_value = (True,)

        embedding_iterator = [
            ("chunk 1", [0.1, 0.2, 0.3]),
            ("chunk 2", [0.4, 0.5, 0.6])
        ]

        store.insert_embeddings(
            document_id=1,
            table_name="test_table",
            embedding_iterator=embedding_iterator,
            pipeline_id=PIPELINE_ID,
            metadata={"source": "test"},
            batch_size=16
        )

        mock_cur.executemany.assert_called_once()
        call_args = mock_cur.executemany.call_args
        assert "INSERT INTO" in call_args[0][0]
        assert "test_table" in call_args[0][0]
        mock_conn.commit.assert_called()

    @patch('db.yugabytedb_vector_store.PipelineTracking')
    @patch('db.yugabytedb_vector_store.ConnectionPool')
    def test_insert_embeddings_batch_processing(self, mock_pool_cls, mock_pt_cls):
        """Test embedding insertion with batch processing."""
        store, mock_pool, mock_pt, mock_conn, mock_cur = _create_store(
            mock_pool_cls, mock_pt_cls
        )
        mock_cur.fetchone.return_value = (True,)

        embedding_iterator = [
            (f"chunk {i}", [0.1, 0.2, 0.3]) for i in range(20)
        ]

        store.insert_embeddings(
            document_id=1,
            table_name="test_table",
            embedding_iterator=embedding_iterator,
            pipeline_id=PIPELINE_ID,
            metadata={"source": "test"},
            batch_size=5
        )

        # 20 items / 5 batch_size = 4 batches
        assert mock_cur.executemany.call_count == 4

    @patch('db.yugabytedb_vector_store.PipelineTracking')
    @patch('db.yugabytedb_vector_store.ConnectionPool')
    def test_insert_embeddings_remaining_batch(self, mock_pool_cls, mock_pt_cls):
        """Test insertion of remaining items after batching."""
        store, mock_pool, mock_pt, mock_conn, mock_cur = _create_store(
            mock_pool_cls, mock_pt_cls
        )
        mock_cur.fetchone.return_value = (True,)

        embedding_iterator = [
            ("chunk 1", [0.1, 0.2, 0.3]),
            ("chunk 2", [0.4, 0.5, 0.6])
        ]

        store.insert_embeddings(
            document_id=1,
            table_name="test_table",
            embedding_iterator=embedding_iterator,
            pipeline_id=PIPELINE_ID,
            metadata={"source": "test"},
            batch_size=5
        )

        # 2 items < 5 batch_size, so only the remainder batch fires
        assert mock_cur.executemany.call_count == 1

    @patch('db.yugabytedb_vector_store.PipelineTracking')
    @patch('db.yugabytedb_vector_store.ConnectionPool')
    def test_close_connections(self, mock_pool_cls, mock_pt_cls):
        """Test proper connection cleanup."""
        store, mock_pool, mock_pt, mock_conn, mock_cur = _create_store(
            mock_pool_cls, mock_pt_cls
        )

        store.close()
        # close() now just logs; connections managed by pool singleton

    @patch('db.yugabytedb_vector_store.PipelineTracking')
    @patch('db.yugabytedb_vector_store.ConnectionPool')
    def test_destructor(self, mock_pool_cls, mock_pt_cls):
        """Test destructor calls close method."""
        store, mock_pool, mock_pt, mock_conn, mock_cur = _create_store(
            mock_pool_cls, mock_pt_cls
        )

        store.__del__()
        # Should not raise

    @patch('db.yugabytedb_vector_store.PipelineTracking')
    @patch('db.yugabytedb_vector_store.ConnectionPool')
    def test_destructor_with_exception(self, mock_pool_cls, mock_pt_cls):
        """Test destructor handles exceptions gracefully."""
        store, mock_pool, mock_pt, mock_conn, mock_cur = _create_store(
            mock_pool_cls, mock_pt_cls
        )

        with patch.object(store, 'close', side_effect=Exception("Test exception")):
            store.__del__()  # Should not raise

    @patch('db.yugabytedb_vector_store.PipelineTracking')
    @patch('db.yugabytedb_vector_store.ConnectionPool')
    def test_insert_embeddings_empty_iterator(self, mock_pool_cls, mock_pt_cls):
        """Test insert_embeddings with empty iterator."""
        store, mock_pool, mock_pt, mock_conn, mock_cur = _create_store(
            mock_pool_cls, mock_pt_cls
        )
        mock_cur.fetchone.return_value = (True,)

        store.insert_embeddings(
            document_id=1,
            table_name="test_table",
            embedding_iterator=[],
            pipeline_id=PIPELINE_ID,
            metadata={"source": "test"},
            batch_size=16
        )

        mock_cur.executemany.assert_not_called()

    @patch('db.yugabytedb_vector_store.PipelineTracking')
    @patch('db.yugabytedb_vector_store.ConnectionPool')
    def test_insert_embeddings_none_metadata(self, mock_pool_cls, mock_pt_cls):
        """Test insert_embeddings with None metadata."""
        store, mock_pool, mock_pt, mock_conn, mock_cur = _create_store(
            mock_pool_cls, mock_pt_cls
        )
        mock_cur.fetchone.return_value = (True,)

        embedding_iterator = [("chunk 1", [0.1, 0.2, 0.3])]

        store.insert_embeddings(
            document_id=1,
            table_name="test_table",
            embedding_iterator=embedding_iterator,
            pipeline_id=PIPELINE_ID,
            metadata=None,
            batch_size=16
        )

        call_args = mock_cur.executemany.call_args
        data = call_args[0][1]
        # metadata is serialized as JSON '{}' when None
        assert data == [("chunk 1", [0.1, 0.2, 0.3], 1, '{}')]

    @patch('db.yugabytedb_vector_store.PipelineTracking')
    @patch('db.yugabytedb_vector_store.ConnectionPool')
    def test_insert_embeddings_large_batch(self, mock_pool_cls, mock_pt_cls):
        """Test embedding insertion with large batch size."""
        store, mock_pool, mock_pt, mock_conn, mock_cur = _create_store(
            mock_pool_cls, mock_pt_cls
        )
        mock_cur.fetchone.return_value = (True,)

        embedding_iterator = [
            (f"chunk {i}", [0.1, 0.2, 0.3]) for i in range(5)
        ]

        store.insert_embeddings(
            document_id=1,
            table_name="test_table",
            embedding_iterator=embedding_iterator,
            pipeline_id=PIPELINE_ID,
            metadata={"source": "test"},
            batch_size=100
        )

        assert mock_cur.executemany.call_count == 1

    @patch('db.yugabytedb_vector_store.PipelineTracking')
    @patch('db.yugabytedb_vector_store.ConnectionPool')
    def test_insert_embeddings_single_item_batch(self, mock_pool_cls, mock_pt_cls):
        """Test embedding insertion with batch size of 1."""
        store, mock_pool, mock_pt, mock_conn, mock_cur = _create_store(
            mock_pool_cls, mock_pt_cls
        )
        mock_cur.fetchone.return_value = (True,)

        embedding_iterator = [
            ("chunk 1", [0.1, 0.2, 0.3]),
            ("chunk 2", [0.4, 0.5, 0.6])
        ]

        store.insert_embeddings(
            document_id=1,
            table_name="test_table",
            embedding_iterator=embedding_iterator,
            pipeline_id=PIPELINE_ID,
            metadata={"source": "test"},
            batch_size=1
        )

        assert mock_cur.executemany.call_count == 2

    @patch('db.yugabytedb_vector_store.PipelineTracking')
    @patch('db.yugabytedb_vector_store.ConnectionPool')
    def test_insert_embeddings_complex_metadata(self, mock_pool_cls, mock_pt_cls):
        """Test embedding insertion with complex metadata."""
        store, mock_pool, mock_pt, mock_conn, mock_cur = _create_store(
            mock_pool_cls, mock_pt_cls
        )
        mock_cur.fetchone.return_value = (True,)

        embedding_iterator = [("chunk 1", [0.1, 0.2, 0.3])]

        complex_metadata = {
            "source": "test_document.pdf",
            "page": 1,
            "section": "introduction",
            "author": "test_author",
            "timestamp": "2024-01-01T00:00:00Z",
            "tags": ["important", "review"],
            "nested": {"key": "value", "number": 42}
        }

        store.insert_embeddings(
            document_id=1,
            table_name="test_table",
            embedding_iterator=embedding_iterator,
            pipeline_id=PIPELINE_ID,
            metadata=complex_metadata,
            batch_size=16
        )

        call_args = mock_cur.executemany.call_args
        data = call_args[0][1]
        assert data[0][0] == "chunk 1"
        assert data[0][1] == [0.1, 0.2, 0.3]
        assert data[0][2] == 1
        assert json.loads(data[0][3]) == complex_metadata

    @patch('db.yugabytedb_vector_store.PipelineTracking')
    @patch('db.yugabytedb_vector_store.ConnectionPool')
    def test_insert_embeddings_table_not_exists(self, mock_pool_cls, mock_pt_cls):
        """Test insert_embeddings raises when table doesn't exist."""
        store, mock_pool, mock_pt, mock_conn, mock_cur = _create_store(
            mock_pool_cls, mock_pt_cls
        )
        mock_cur.fetchone.return_value = (False,)

        embedding_iterator = [("chunk 1", [0.1, 0.2, 0.3])]

        with pytest.raises(ValueError, match="doesn't exist"):
            store.insert_embeddings(
                document_id=1,
                table_name="nonexistent_table",
                embedding_iterator=embedding_iterator,
                pipeline_id=PIPELINE_ID,
                metadata={"source": "test"},
                batch_size=16
            )

    @patch('db.yugabytedb_vector_store.PipelineTracking')
    @patch('db.yugabytedb_vector_store.ConnectionPool')
    def test_create_index(self, mock_pool_cls, mock_pt_cls):
        """Test create_index executes correct SQL."""
        store, mock_pool, mock_pt, mock_conn, mock_cur = _create_store(
            mock_pool_cls, mock_pt_cls
        )

        result = store.create_index(table_name="test_table")

        assert result is True
        mock_cur.execute.assert_called_once()
        call_sql = mock_cur.execute.call_args[0][0]
        assert "CREATE INDEX IF NOT EXISTS" in call_sql
        assert "test_table" in call_sql
        mock_conn.commit.assert_called()


if __name__ == "__main__":
    pytest.main([__file__])
