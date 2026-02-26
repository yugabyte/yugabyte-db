# !/usr/bin/env python3
"""
Integration test suite for end-to-end RAG pipeline workflows.

This test file focuses on testing complete workflows that integrate multiple
components including document processing, chunking, embedding generation,
and vector storage.
"""

import pytest
import tempfile
import os
from unittest.mock import Mock, patch, MagicMock
import json

from rag_pipeline.rag_handler import RagPipelineHandler
from db.yugabytedb_vector_store import YugabyteDBVectorStore
from rag_pipeline.chunk import chunk
from embeddings.embed import EmbeddingsGenerator

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


class TestRAGPipelineIntegration:
    """Integration tests for complete RAG pipeline workflows."""

    @patch('rag_pipeline.rag_handler.EmbeddingsGenerator')
    @patch('rag_pipeline.rag_handler.PipelineTracking')
    @patch('rag_pipeline.rag_handler.YugabyteDBVectorStore')
    def test_end_to_end_document_ingestion(
        self, mock_vs_cls, mock_pt_cls, mock_embed_cls
    ):
        """Test complete end-to-end document ingestion workflow."""
        handler, mock_vs, mock_pt = _create_handler(mock_vs_cls, mock_pt_cls)

        mock_embedder = Mock()
        mock_embed_cls.return_value = mock_embedder
        mock_embedding_iterator = [
            ("This is chunk 1", [0.1, 0.2, 0.3, 0.4]),
            ("This is chunk 2", [0.5, 0.6, 0.7, 0.8]),
            ("This is chunk 3", [0.9, 1.0, 1.1, 1.2])
        ]
        mock_embedder.generate_embeddings.return_value = mock_embedding_iterator

        result = handler._ingest_document(
            pipeline_id=PIPELINE_ID,
            source_id=1,
            document_id=123,
            document_uri="test_document.txt",
            table_name="integration_test_table",
            metadata={"source": "integration_test", "type": "text"},
            chunk_kwargs={"chunk_size": 100, "chunk_overlap": 20},
            embedding_model_params=DEFAULT_EMBEDDING_PARAMS
        )

        assert result is True

        mock_embedder.generate_embeddings.assert_called_once_with(
            pipeline_id=PIPELINE_ID,
            file_location="test_document.txt",
            chunk_args={"chunk_size": 100, "chunk_overlap": 20}
        )

        mock_vs.insert_embeddings.assert_called_once()

    @patch('rag_pipeline.rag_handler.EmbeddingsGenerator')
    @patch('rag_pipeline.rag_handler.PipelineTracking')
    @patch('rag_pipeline.rag_handler.YugabyteDBVectorStore')
    def test_multiple_document_ingestion(
        self, mock_vs_cls, mock_pt_cls, mock_embed_cls
    ):
        """Test ingestion of multiple documents."""
        handler, mock_vs, mock_pt = _create_handler(mock_vs_cls, mock_pt_cls)

        documents = [
            ("doc1.txt", ["chunk1_doc1", "chunk2_doc1"]),
            ("doc2.txt", ["chunk1_doc2", "chunk2_doc2", "chunk3_doc2"]),
            ("doc3.txt", ["chunk1_doc3"])
        ]

        for i, (doc_uri, chunks) in enumerate(documents):
            mock_embedder = Mock()
            mock_embed_cls.return_value = mock_embedder
            mock_embedding_iterator = [(c, [0.1, 0.2, 0.3]) for c in chunks]
            mock_embedder.generate_embeddings.return_value = mock_embedding_iterator

            result = handler._ingest_document(
                pipeline_id=PIPELINE_ID + i,
                source_id=1,
                document_id=100 + i,
                document_uri=doc_uri,
                table_name="test_table",
                metadata={"source": f"doc_{i+1}", "batch": "integration_test"},
                embedding_model_params=DEFAULT_EMBEDDING_PARAMS
            )

            assert result is True
            mock_embedder.generate_embeddings.assert_called_with(
                pipeline_id=PIPELINE_ID + i,
                file_location=doc_uri,
                chunk_args=None
            )

        assert mock_vs.insert_embeddings.call_count == len(documents)

    @patch('rag_pipeline.rag_handler.EmbeddingsGenerator')
    @patch('rag_pipeline.rag_handler.PipelineTracking')
    @patch('rag_pipeline.rag_handler.YugabyteDBVectorStore')
    def test_document_ingestion_with_different_chunk_strategies(
        self, mock_vs_cls, mock_pt_cls, mock_embed_cls
    ):
        """Test document ingestion with different chunking strategies."""
        handler, mock_vs, mock_pt = _create_handler(mock_vs_cls, mock_pt_cls)

        chunk_strategies = [
            {"chunk_size": 50, "chunk_overlap": 10},
            {"chunk_size": 200, "chunk_overlap": 50},
            {"chunk_size": 1000, "chunk_overlap": 100}
        ]

        for i, chunk_kwargs in enumerate(chunk_strategies):
            mock_embedder = Mock()
            mock_embed_cls.return_value = mock_embedder
            mock_embedding_iterator = [(f"chunk_{j}", [0.1, 0.2, 0.3]) for j in range(3)]
            mock_embedder.generate_embeddings.return_value = mock_embedding_iterator

            result = handler._ingest_document(
                pipeline_id=PIPELINE_ID,
                source_id=1,
                document_id=200 + i,
                document_uri=f"test_doc_{i}.txt",
                table_name="test_table",
                metadata={"strategy": f"strategy_{i}"},
                chunk_kwargs=chunk_kwargs,
                embedding_model_params=DEFAULT_EMBEDDING_PARAMS
            )

            mock_embedder.generate_embeddings.assert_called_with(
                pipeline_id=PIPELINE_ID,
                file_location=f"test_doc_{i}.txt",
                chunk_args=chunk_kwargs
            )

            assert result is True

    @patch('rag_pipeline.rag_handler.EmbeddingsGenerator')
    @patch('rag_pipeline.rag_handler.PipelineTracking')
    @patch('rag_pipeline.rag_handler.YugabyteDBVectorStore')
    def test_error_handling_in_pipeline(
        self, mock_vs_cls, mock_pt_cls, mock_embed_cls
    ):
        """Test error handling throughout the pipeline."""
        handler, mock_vs, mock_pt = _create_handler(mock_vs_cls, mock_pt_cls)

        mock_embedder = Mock()
        mock_embed_cls.return_value = mock_embedder
        mock_embedder.generate_embeddings.side_effect = Exception("Embedding error")

        with pytest.raises(Exception, match="Embedding error"):
            handler._ingest_document(
                pipeline_id=PIPELINE_ID,
                source_id=1,
                document_id=301,
                document_uri="error_doc2.txt",
                table_name="test_table",
                embedding_model_params=DEFAULT_EMBEDDING_PARAMS
            )

        mock_vs.insert_embeddings.assert_not_called()

        # Test vector store error
        mock_embedder.generate_embeddings.side_effect = None
        mock_embedder.generate_embeddings.return_value = [("chunk1", [0.1, 0.2, 0.3])]
        mock_vs.insert_embeddings.side_effect = Exception("Vector store error")

        with pytest.raises(Exception, match="Vector store error"):
            handler._ingest_document(
                pipeline_id=PIPELINE_ID,
                source_id=1,
                document_id=302,
                document_uri="error_doc3.txt",
                table_name="test_table",
                embedding_model_params=DEFAULT_EMBEDDING_PARAMS
            )

    @patch('rag_pipeline.rag_handler.PipelineTracking')
    @patch('rag_pipeline.rag_handler.YugabyteDBVectorStore')
    def test_connection_cleanup(self, mock_vs_cls, mock_pt_cls):
        """Test proper connection cleanup in the pipeline."""
        handler, mock_vs, mock_pt = _create_handler(mock_vs_cls, mock_pt_cls)

        handler.vector_store.close()
        mock_vs.close.assert_called_once()

        handler.vector_store.close()
        assert mock_vs.close.call_count == 2

    def test_chunk_integration_with_different_splitters(self):
        """Test chunk function integration with different splitters."""
        test_text = """# Test Document
This is a test document with multiple paragraphs.

## Section 1
This is the first section with some content.

### Subsection 1.1
This is a subsection with more detailed information.

## Section 2
This is the second section with different content."""

        splitters = ["character", "recursive_character", "markdown"]

        for splitter in splitters:
            args = '{"chunk_size": 100, "chunk_overlap": 20}'
            result = chunk(splitter, test_text, args)

            assert isinstance(result, list)
            assert len(result) > 0
            for chunk_text in result:
                assert isinstance(chunk_text, str)
                assert len(chunk_text) > 0

    @patch('embeddings.embed.PipelineTracking')
    @patch('embeddings.embed.PDFProcessor')
    @patch('embeddings.embed.OpenAIEmbeddings')
    def test_embedding_generation_integration(
        self, mock_oai_cls, mock_pdf, mock_pt
    ):
        """Test embedding generation integration (mocked)."""
        mock_embedder = Mock()
        mock_oai_cls.return_value = mock_embedder

        def mock_embed_documents(docs):
            return [
                [0.1, 0.2, 0.3] if doc == "chunk 1" else [0.4, 0.5, 0.6]
                for doc in docs
            ]
        mock_embedder.embed_documents.side_effect = mock_embed_documents

        model_params = {"model": "text-embedding-ada-002", "dimensions": 3}
        gen = EmbeddingsGenerator(
            embedding_model="text-embedding-ada-002",
            embedding_model_params=model_params
        )

        test_content = "chunk 1\n\nchunk 2"
        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt') as f:
            f.write(test_content)
            temp_file = f.name

        try:
            result = list(gen.generate_embeddings(
                pipeline_id=PIPELINE_ID, file_location=temp_file
            ))

            assert len(result) > 0
            for chunk_text, embedding_vector in result:
                assert isinstance(chunk_text, str)
                assert isinstance(embedding_vector, list)
        finally:
            os.unlink(temp_file)

    @patch('rag_pipeline.rag_handler.EmbeddingsGenerator')
    @patch('rag_pipeline.rag_handler.PipelineTracking')
    @patch('rag_pipeline.rag_handler.YugabyteDBVectorStore')
    def test_large_document_processing(
        self, mock_vs_cls, mock_pt_cls, mock_embed_cls
    ):
        """Test processing of large documents with many chunks."""
        handler, mock_vs, mock_pt = _create_handler(mock_vs_cls, mock_pt_cls)

        num_chunks = 100
        chunks = [f"This is chunk number {i} with some content." for i in range(num_chunks)]

        mock_embedder = Mock()
        mock_embed_cls.return_value = mock_embedder
        mock_embedding_iterator = [(c, [0.1, 0.2, 0.3]) for c in chunks]
        mock_embedder.generate_embeddings.return_value = mock_embedding_iterator

        result = handler._ingest_document(
            pipeline_id=PIPELINE_ID,
            source_id=1,
            document_id=400,
            document_uri="large_document.txt",
            table_name="test_table",
            metadata={"size": "large", "chunks": num_chunks},
            chunk_kwargs={"chunk_size": 50, "chunk_overlap": 10},
            embedding_model_params=DEFAULT_EMBEDDING_PARAMS
        )

        assert result is True
        mock_embedder.generate_embeddings.assert_called_once()
        mock_vs.insert_embeddings.assert_called_once()

    @patch('rag_pipeline.rag_handler.EmbeddingsGenerator')
    @patch('rag_pipeline.rag_handler.PipelineTracking')
    @patch('rag_pipeline.rag_handler.YugabyteDBVectorStore')
    def test_concurrent_document_processing_simulation(
        self, mock_vs_cls, mock_pt_cls, mock_embed_cls
    ):
        """Test simulation of concurrent document processing."""
        handler, mock_vs, mock_pt = _create_handler(mock_vs_cls, mock_pt_cls)

        num_documents = 5
        for i in range(num_documents):
            mock_embedder = Mock()
            mock_embed_cls.return_value = mock_embedder
            chunks = [f"chunk_{j}_doc_{i}" for j in range(3)]
            mock_embedding_iterator = [(c, [0.1, 0.2, 0.3]) for c in chunks]
            mock_embedder.generate_embeddings.return_value = mock_embedding_iterator

            result = handler._ingest_document(
                pipeline_id=500 + i,
                source_id=1,
                document_id=500 + i,
                document_uri=f"concurrent_doc_{i}.txt",
                table_name="test_table",
                metadata={"batch": "concurrent_test", "doc_id": i},
                embedding_model_params=DEFAULT_EMBEDDING_PARAMS
            )

            assert result is True

        assert mock_vs.insert_embeddings.call_count == num_documents


if __name__ == "__main__":
    pytest.main([__file__])
