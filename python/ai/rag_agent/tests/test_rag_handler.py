# !/usr/bin/env python3
"""
Test suite for RagPipelineHandler class.

This test file focuses on testing the core functionality of RagPipelineHandler
including initialization, document ingestion, and pipeline orchestration.
"""

import pytest
from unittest.mock import Mock, patch, MagicMock
import logging

from rag_pipeline.rag_handler import RagPipelineHandler

PIPELINE_ID = 1
DEFAULT_EMBEDDING_PARAMS = {"model": "text-embedding-ada-002", "dimensions": 768}


def _create_handler(mock_vs_cls, mock_pt_cls):
    """Helper to create RagPipelineHandler with mocked dependencies."""
    mock_vector_store = Mock()
    mock_vs_cls.return_value = mock_vector_store
    mock_pipeline_tracking = Mock()
    mock_pt_cls.return_value = mock_pipeline_tracking
    handler = RagPipelineHandler()
    return handler, mock_vector_store, mock_pipeline_tracking


class TestRagPipelineHandler:
    """Test cases for RagPipelineHandler class."""

    @patch('rag_pipeline.rag_handler.PipelineTracking')
    @patch('rag_pipeline.rag_handler.YugabyteDBVectorStore')
    def test_init_valid_parameters(self, mock_vs_cls, mock_pt_cls):
        """Test initialization with valid parameters."""
        handler, mock_vs, mock_pt = _create_handler(mock_vs_cls, mock_pt_cls)

        assert handler.vector_store == mock_vs
        assert handler.pipeline_tracking == mock_pt
        mock_vs_cls.assert_called_once()

    @patch('rag_pipeline.rag_handler.PipelineTracking')
    @patch('rag_pipeline.rag_handler.YugabyteDBVectorStore')
    def test_init_default_parameters(self, mock_vs_cls, mock_pt_cls):
        """Test initialization with default parameters."""
        handler, mock_vs, mock_pt = _create_handler(mock_vs_cls, mock_pt_cls)

        assert handler.vector_store == mock_vs
        mock_vs_cls.assert_called_once()

    @patch('rag_pipeline.rag_handler.PipelineTracking')
    @patch('rag_pipeline.rag_handler.YugabyteDBVectorStore')
    def test_init_with_pipeline_config(self, mock_vs_cls, mock_pt_cls):
        """Test initialization with pipeline config and embed function."""
        mock_vs = Mock()
        mock_vs_cls.return_value = mock_vs
        mock_pt = Mock()
        mock_pt_cls.return_value = mock_pt
        mock_embed_function = Mock()

        handler = RagPipelineHandler(
            table_name="test_table",
            vector_dimension=768,
            pipeline_config={"chunk_size": 1000},
            embed_function=mock_embed_function
        )

        assert handler.vector_store == mock_vs

    @patch('rag_pipeline.rag_handler.EmbeddingsGenerator')
    @patch('rag_pipeline.rag_handler.PipelineTracking')
    @patch('rag_pipeline.rag_handler.YugabyteDBVectorStore')
    def test_ingest_document_basic(self, mock_vs_cls, mock_pt_cls, mock_embed_cls):
        """Test basic document ingestion functionality."""
        handler, mock_vs, mock_pt = _create_handler(mock_vs_cls, mock_pt_cls)

        mock_embedder = Mock()
        mock_embed_cls.return_value = mock_embedder
        mock_embedding_iterator = [
            ("chunk 1", [0.1, 0.2, 0.3]),
            ("chunk 2", [0.4, 0.5, 0.6])
        ]
        mock_embedder.generate_embeddings.return_value = mock_embedding_iterator

        result = handler._ingest_document(
            pipeline_id=PIPELINE_ID,
            source_id=123,
            document_id=456,
            document_uri="test_document.txt",
            table_name="test_table",
            metadata={"source": "test"},
            chunk_kwargs={"chunk_size": 500},
            embedding_model_params=DEFAULT_EMBEDDING_PARAMS
        )

        assert result is True
        mock_embedder.generate_embeddings.assert_called_once_with(
            pipeline_id=PIPELINE_ID,
            file_location="test_document.txt",
            chunk_args={"chunk_size": 500}
        )
        mock_vs.insert_embeddings.assert_called_once()

    @patch('rag_pipeline.rag_handler.EmbeddingsGenerator')
    @patch('rag_pipeline.rag_handler.PipelineTracking')
    @patch('rag_pipeline.rag_handler.YugabyteDBVectorStore')
    def test_ingest_document_no_metadata(self, mock_vs_cls, mock_pt_cls, mock_embed_cls):
        """Test document ingestion without metadata."""
        handler, mock_vs, mock_pt = _create_handler(mock_vs_cls, mock_pt_cls)

        mock_embedder = Mock()
        mock_embed_cls.return_value = mock_embedder
        mock_embedding_iterator = [("chunk 1", [0.1, 0.2, 0.3])]
        mock_embedder.generate_embeddings.return_value = mock_embedding_iterator

        result = handler._ingest_document(
            pipeline_id=PIPELINE_ID,
            source_id=456,
            document_id=789,
            document_uri="test_document.txt",
            table_name="test_table",
            embedding_model_params=DEFAULT_EMBEDDING_PARAMS
        )

        assert result is True
        # metadata defaults to {} when None
        call_kwargs = mock_vs.insert_embeddings.call_args[1]
        assert call_kwargs['metadata'] == {}

    @patch('rag_pipeline.rag_handler.EmbeddingsGenerator')
    @patch('rag_pipeline.rag_handler.PipelineTracking')
    @patch('rag_pipeline.rag_handler.YugabyteDBVectorStore')
    def test_ingest_document_no_chunk_kwargs(
        self, mock_vs_cls, mock_pt_cls, mock_embed_cls
    ):
        """Test document ingestion without chunk_kwargs."""
        handler, mock_vs, mock_pt = _create_handler(mock_vs_cls, mock_pt_cls)

        mock_embedder = Mock()
        mock_embed_cls.return_value = mock_embedder
        mock_embedding_iterator = [("chunk 1", [0.1, 0.2, 0.3])]
        mock_embedder.generate_embeddings.return_value = mock_embedding_iterator

        result = handler._ingest_document(
            pipeline_id=PIPELINE_ID,
            source_id=789,
            document_id=101,
            document_uri="test_document.txt",
            table_name="test_table",
            metadata={"source": "test"},
            embedding_model_params=DEFAULT_EMBEDDING_PARAMS
        )

        assert result is True
        mock_embedder.generate_embeddings.assert_called_once_with(
            pipeline_id=PIPELINE_ID,
            file_location="test_document.txt",
            chunk_args=None
        )

    @patch('rag_pipeline.rag_handler.EmbeddingsGenerator')
    @patch('rag_pipeline.rag_handler.PipelineTracking')
    @patch('rag_pipeline.rag_handler.YugabyteDBVectorStore')
    def test_ingest_document_with_embedding_model(
        self, mock_vs_cls, mock_pt_cls, mock_embed_cls
    ):
        """Test document ingestion with custom embedding model."""
        handler, mock_vs, mock_pt = _create_handler(mock_vs_cls, mock_pt_cls)

        mock_embedder = Mock()
        mock_embed_cls.return_value = mock_embedder
        mock_embedding_iterator = [("chunk 1", [0.1, 0.2, 0.3])]
        mock_embedder.generate_embeddings.return_value = mock_embedding_iterator

        custom_params = {"model": "custom_model", "dimensions": 1024}
        result = handler._ingest_document(
            pipeline_id=PIPELINE_ID,
            source_id=999,
            document_id=201,
            document_uri="test_document.txt",
            table_name="test_table",
            metadata={"source": "test"},
            chunk_kwargs={"chunk_size": 1000},
            embedding_model_params=custom_params
        )

        assert result is True
        mock_embed_cls.assert_called_once_with(
            embedding_model="custom_model",
            embedding_model_params=custom_params
        )

    @patch('rag_pipeline.rag_handler.PipelineTracking')
    @patch('rag_pipeline.rag_handler.YugabyteDBVectorStore')
    def test_ingest_document_missing_model(self, mock_vs_cls, mock_pt_cls):
        """Test document ingestion raises when embedding model is missing."""
        handler, mock_vs, mock_pt = _create_handler(mock_vs_cls, mock_pt_cls)

        with pytest.raises(ValueError, match="Embedding model not found"):
            handler._ingest_document(
                pipeline_id=PIPELINE_ID,
                source_id=123,
                document_id=456,
                document_uri="test_document.txt",
                table_name="test_table",
                embedding_model_params={"dimensions": 768}
            )

    @patch('rag_pipeline.rag_handler.PipelineTracking')
    @patch('rag_pipeline.rag_handler.YugabyteDBVectorStore')
    def test_ingest_document_missing_dimensions(self, mock_vs_cls, mock_pt_cls):
        """Test document ingestion raises when dimensions is missing."""
        handler, mock_vs, mock_pt = _create_handler(mock_vs_cls, mock_pt_cls)

        with pytest.raises(ValueError, match="Embedding dimension not found"):
            handler._ingest_document(
                pipeline_id=PIPELINE_ID,
                source_id=123,
                document_id=456,
                document_uri="test_document.txt",
                table_name="test_table",
                embedding_model_params={"model": "test-model"}
            )

    @patch('rag_pipeline.rag_handler.EmbeddingsGenerator')
    @patch('rag_pipeline.rag_handler.PipelineTracking')
    @patch('rag_pipeline.rag_handler.YugabyteDBVectorStore')
    def test_ingest_document_pipeline_exception(
        self, mock_vs_cls, mock_pt_cls, mock_embed_cls
    ):
        """Test document ingestion handles embedding generation exceptions."""
        handler, mock_vs, mock_pt = _create_handler(mock_vs_cls, mock_pt_cls)

        mock_embedder = Mock()
        mock_embed_cls.return_value = mock_embedder
        mock_embedder.generate_embeddings.side_effect = Exception("Embedding error")

        with pytest.raises(Exception, match="Embedding error"):
            handler._ingest_document(
                pipeline_id=PIPELINE_ID,
                source_id=123,
                document_id=456,
                document_uri="test_document.txt",
                table_name="test_table",
                embedding_model_params=DEFAULT_EMBEDDING_PARAMS
            )

        mock_vs.insert_embeddings.assert_not_called()

    @patch('rag_pipeline.rag_handler.EmbeddingsGenerator')
    @patch('rag_pipeline.rag_handler.PipelineTracking')
    @patch('rag_pipeline.rag_handler.YugabyteDBVectorStore')
    def test_ingest_document_embedding_exception(
        self, mock_vs_cls, mock_pt_cls, mock_embed_cls
    ):
        """Test document ingestion handles embedding generation exceptions."""
        handler, mock_vs, mock_pt = _create_handler(mock_vs_cls, mock_pt_cls)

        mock_embedder = Mock()
        mock_embed_cls.return_value = mock_embedder
        mock_embedder.generate_embeddings.side_effect = Exception("Embedding error")

        with pytest.raises(Exception, match="Embedding error"):
            handler._ingest_document(
                pipeline_id=PIPELINE_ID,
                source_id=123,
                document_id=456,
                document_uri="test_document.txt",
                table_name="test_table",
                embedding_model_params=DEFAULT_EMBEDDING_PARAMS
            )

        mock_vs.insert_embeddings.assert_not_called()

    @patch('rag_pipeline.rag_handler.EmbeddingsGenerator')
    @patch('rag_pipeline.rag_handler.PipelineTracking')
    @patch('rag_pipeline.rag_handler.YugabyteDBVectorStore')
    def test_ingest_document_vector_store_exception(
        self, mock_vs_cls, mock_pt_cls, mock_embed_cls
    ):
        """Test document ingestion handles vector store exceptions."""
        handler, mock_vs, mock_pt = _create_handler(mock_vs_cls, mock_pt_cls)

        mock_embedder = Mock()
        mock_embed_cls.return_value = mock_embedder
        mock_embedding_iterator = [("chunk 1", [0.1, 0.2, 0.3])]
        mock_embedder.generate_embeddings.return_value = mock_embedding_iterator
        mock_vs.insert_embeddings.side_effect = Exception("Vector store error")

        with pytest.raises(Exception, match="Vector store error"):
            handler._ingest_document(
                pipeline_id=PIPELINE_ID,
                source_id=123,
                document_id=456,
                document_uri="test_document.txt",
                table_name="test_table",
                embedding_model_params=DEFAULT_EMBEDDING_PARAMS
            )


if __name__ == "__main__":
    pytest.main([__file__])
