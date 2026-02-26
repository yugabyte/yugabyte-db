# !/usr/bin/env python3
"""
Test suite for embed.py functions.

This test file focuses on testing the core functionality of EmbeddingsGenerator
including the generate_embeddings method with proper mocking.
"""

import pytest
import tempfile
import os
from unittest.mock import Mock, patch, MagicMock
from embeddings.embed import EmbeddingsGenerator

PIPELINE_ID = 1
DEFAULT_MODEL_PARAMS = {"model": "text-embedding-ada-002", "dimensions": 3}


def _create_generator(mock_oai_cls):
    """Helper to create a properly mocked EmbeddingsGenerator."""
    mock_embedder = Mock()
    mock_oai_cls.return_value = mock_embedder
    gen = EmbeddingsGenerator(
        embedding_model="text-embedding-ada-002",
        embedding_model_params=DEFAULT_MODEL_PARAMS
    )
    return gen, mock_embedder


class TestGenerateEmbeddings:
    """Test cases for EmbeddingsGenerator.generate_embeddings method."""

    @patch('embeddings.embed.PipelineTracking')
    @patch('embeddings.embed.PDFProcessor')
    @patch('embeddings.embed.OpenAIEmbeddings')
    def test_generate_embeddings_basic_functionality(
        self, mock_oai_cls, mock_pdf, mock_pt
    ):
        """Test basic functionality of generate_embeddings."""
        test_content = ("First paragraph.\nWith multiple lines.\n\n"
                        "Second paragraph.\nAlso with content.")

        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt') as f:
            f.write(test_content)
            temp_file = f.name

        try:
            gen, mock_embedder = _create_generator(mock_oai_cls)
            mock_embedder.embed_documents.return_value = [[0.1, 0.2, 0.3]]

            results = list(gen.generate_embeddings(
                pipeline_id=PIPELINE_ID, file_location=temp_file
            ))

            assert len(results) > 0
            for chunk_text, embedding_vector in results:
                assert isinstance(chunk_text, str)
                assert isinstance(embedding_vector, list)
                assert len(embedding_vector) > 0
                assert all(isinstance(x, (int, float)) for x in embedding_vector)

            assert mock_embedder.embed_documents.call_count > 0
        finally:
            os.unlink(temp_file)

    @patch('embeddings.embed.PipelineTracking')
    @patch('embeddings.embed.PDFProcessor')
    @patch('embeddings.embed.OpenAIEmbeddings')
    def test_generate_embeddings_with_custom_chunk_args(
        self, mock_oai_cls, mock_pdf, mock_pt
    ):
        """Test generate_embeddings with custom chunk arguments."""
        test_content = "A single long paragraph that should be chunked into smaller pieces."

        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt') as f:
            f.write(test_content)
            temp_file = f.name

        try:
            custom_chunk_args = {
                'splitter': 'recursive_character',
                'args': '{"chunk_size": 50, "chunk_overlap": 10}'
            }

            gen, mock_embedder = _create_generator(mock_oai_cls)
            mock_embedder.embed_documents.return_value = [[0.1, 0.2, 0.3]]

            results = list(gen.generate_embeddings(
                pipeline_id=PIPELINE_ID,
                file_location=temp_file,
                chunk_args=custom_chunk_args
            ))

            assert len(results) > 0
            for chunk_text, embedding_vector in results:
                assert isinstance(chunk_text, str)
                assert isinstance(embedding_vector, list)
        finally:
            os.unlink(temp_file)

    @patch('embeddings.embed.PipelineTracking')
    @patch('embeddings.embed.PDFProcessor')
    @patch('embeddings.embed.OpenAIEmbeddings')
    def test_generate_embeddings_with_custom_model_and_api_key(
        self, mock_oai_cls, mock_pdf, mock_pt
    ):
        """Test generate_embeddings with custom model and API key."""
        test_content = "Test content for custom model."

        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt') as f:
            f.write(test_content)
            temp_file = f.name

        try:
            mock_embedder = Mock()
            mock_oai_cls.return_value = mock_embedder
            mock_embedder.embed_documents.return_value = [[0.1, 0.2, 0.3]]

            model_params = {"model": "text-embedding-3-large", "dimensions": 3}
            gen = EmbeddingsGenerator(
                embedding_model="text-embedding-3-large",
                llm_api_key="test-api-key",
                embedding_model_params=model_params
            )

            results = list(gen.generate_embeddings(
                pipeline_id=PIPELINE_ID, file_location=temp_file
            ))

            mock_oai_cls.assert_called_once_with(
                model="text-embedding-3-large",
                openai_api_key="test-api-key",
                dimensions=3
            )

            assert len(results) > 0
        finally:
            os.unlink(temp_file)

    @patch('embeddings.embed.PipelineTracking')
    @patch('embeddings.embed.PDFProcessor')
    @patch('embeddings.embed.OpenAIEmbeddings')
    def test_generate_embeddings_empty_file(
        self, mock_oai_cls, mock_pdf, mock_pt
    ):
        """Test generate_embeddings with empty file."""
        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt') as f:
            temp_file = f.name

        try:
            gen, mock_embedder = _create_generator(mock_oai_cls)
            mock_embedder.embed_documents.return_value = []

            results = list(gen.generate_embeddings(
                pipeline_id=PIPELINE_ID, file_location=temp_file
            ))

            assert len(results) == 0
            mock_embedder.embed_documents.assert_not_called()
        finally:
            os.unlink(temp_file)

    @patch('embeddings.embed.PipelineTracking')
    @patch('embeddings.embed.PDFProcessor')
    @patch('embeddings.embed.OpenAIEmbeddings')
    def test_generate_embeddings_whitespace_only_chunks(
        self, mock_oai_cls, mock_pdf, mock_pt
    ):
        """Test generate_embeddings with whitespace-only chunks."""
        test_content = "   \n\n   \n\nValid content.\n\n   \n\n"

        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt') as f:
            f.write(test_content)
            temp_file = f.name

        try:
            gen, mock_embedder = _create_generator(mock_oai_cls)
            mock_embedder.embed_documents.return_value = [[0.1, 0.2, 0.3]]

            results = list(gen.generate_embeddings(
                pipeline_id=PIPELINE_ID, file_location=temp_file
            ))

            for chunk_text, embedding_vector in results:
                assert chunk_text.strip() != ""
                assert isinstance(embedding_vector, list)
        finally:
            os.unlink(temp_file)

    @patch('embeddings.embed.PipelineTracking')
    @patch('embeddings.embed.PDFProcessor')
    @patch('embeddings.embed.OpenAIEmbeddings')
    def test_generate_embeddings_unsupported_file_type(
        self, mock_oai_cls, mock_pdf, mock_pt
    ):
        """Test generate_embeddings with unsupported file type."""
        with tempfile.NamedTemporaryFile(
            mode='w', delete=False, suffix='.xyz'
        ) as f:
            f.write("test content")
            temp_file = f.name

        try:
            gen, mock_embedder = _create_generator(mock_oai_cls)

            with pytest.raises(ValueError, match="Unsupported file type"):
                list(gen.generate_embeddings(
                    pipeline_id=PIPELINE_ID, file_location=temp_file
                ))
        finally:
            os.unlink(temp_file)

    @patch('embeddings.embed.PipelineTracking')
    @patch('embeddings.embed.PDFProcessor')
    @patch('embeddings.embed.OpenAIEmbeddings')
    def test_generate_embeddings_default_chunk_args(
        self, mock_oai_cls, mock_pdf, mock_pt
    ):
        """Test generate_embeddings with default chunk_args (None)."""
        test_content = "Test content for default chunk args."

        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt') as f:
            f.write(test_content)
            temp_file = f.name

        try:
            gen, mock_embedder = _create_generator(mock_oai_cls)
            mock_embedder.embed_documents.return_value = [[0.1, 0.2, 0.3]]

            results = list(gen.generate_embeddings(
                pipeline_id=PIPELINE_ID, file_location=temp_file, chunk_args=None
            ))

            assert len(results) > 0
            for chunk_text, embedding_vector in results:
                assert isinstance(chunk_text, str)
                assert isinstance(embedding_vector, list)
        finally:
            os.unlink(temp_file)

    @patch('embeddings.embed.PipelineTracking')
    @patch('embeddings.embed.PDFProcessor')
    @patch('embeddings.embed.OpenAIEmbeddings')
    def test_generate_embeddings_embedding_failure(
        self, mock_oai_cls, mock_pdf, mock_pt
    ):
        """Test generate_embeddings when embedding generation fails for a chunk.
        The current implementation catches the error per-chunk and continues."""
        test_content = "Test content for embedding failure."

        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt') as f:
            f.write(test_content)
            temp_file = f.name

        try:
            gen, mock_embedder = _create_generator(mock_oai_cls)
            mock_embedder.embed_documents.side_effect = Exception("API Error")

            # The current code catches per-chunk errors and continues,
            # so no exception is raised - we just get no results
            results = list(gen.generate_embeddings(
                pipeline_id=PIPELINE_ID, file_location=temp_file
            ))
            assert len(results) == 0
        finally:
            os.unlink(temp_file)

    @patch('embeddings.embed.PipelineTracking')
    @patch('embeddings.embed.PDFProcessor')
    @patch('embeddings.embed.OpenAIEmbeddings')
    def test_generate_embeddings_large_file(
        self, mock_oai_cls, mock_pdf, mock_pt
    ):
        """Test generate_embeddings with a large file."""
        paragraphs = []
        for i in range(50):
            paragraphs.append(f"Paragraph {i}.\nWith multiple lines.\nAnd content.")

        test_content = "\n\n".join(paragraphs)

        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt') as f:
            f.write(test_content)
            temp_file = f.name

        try:
            gen, mock_embedder = _create_generator(mock_oai_cls)
            mock_embedder.embed_documents.return_value = [[0.1, 0.2, 0.3]]

            results = list(gen.generate_embeddings(
                pipeline_id=PIPELINE_ID, file_location=temp_file
            ))

            assert len(results) > 0
            assert mock_embedder.embed_documents.call_count > 0
        finally:
            os.unlink(temp_file)

    @patch('embeddings.embed.PipelineTracking')
    @patch('embeddings.embed.PDFProcessor')
    @patch('embeddings.embed.OpenAIEmbeddings')
    def test_generate_embeddings_unicode_content(
        self, mock_oai_cls, mock_pdf, mock_pt
    ):
        """Test generate_embeddings with unicode content."""
        unicode_content = (
            "Hello \u4e16\u754c! \U0001F30D\n"
            "With emojis \U0001F680 and special chars "
            "\u00f1\u00e1\u00e9\u00ed\u00f3\u00fa\n\n"
            "Another paragraph with \u4e2d\u6587\u5185\u5bb9."
        )

        with tempfile.NamedTemporaryFile(
            mode='w', delete=False, suffix='.txt', encoding='utf-8'
        ) as f:
            f.write(unicode_content)
            temp_file = f.name

        try:
            gen, mock_embedder = _create_generator(mock_oai_cls)
            mock_embedder.embed_documents.return_value = [[0.1, 0.2, 0.3]]

            results = list(gen.generate_embeddings(
                pipeline_id=PIPELINE_ID, file_location=temp_file
            ))

            assert len(results) > 0
            for chunk_text, embedding_vector in results:
                assert isinstance(chunk_text, str)
                assert isinstance(embedding_vector, list)
        finally:
            os.unlink(temp_file)


class TestGenerateEmbeddingsIntegration:
    """Integration tests for generate_embeddings function."""

    @patch('embeddings.embed.PipelineTracking')
    @patch('embeddings.embed.PDFProcessor')
    @patch('embeddings.embed.OpenAIEmbeddings')
    def test_generate_embeddings_with_real_pipeline(
        self, mock_oai_cls, mock_pdf, mock_pt
    ):
        """Test generate_embeddings integration with real pipeline."""
        test_content = """First paragraph with some content.
This is a longer paragraph.

Second paragraph.
Also with content.

Third paragraph with more content."""

        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt') as f:
            f.write(test_content)
            temp_file = f.name

        try:
            gen, mock_embedder = _create_generator(mock_oai_cls)
            mock_embedder.embed_documents.return_value = [[0.1, 0.2, 0.3]]

            results = list(gen.generate_embeddings(
                pipeline_id=PIPELINE_ID, file_location=temp_file
            ))

            assert len(results) > 0
            for chunk_text, embedding_vector in results:
                assert isinstance(chunk_text, str)
                assert isinstance(embedding_vector, list)
                assert len(embedding_vector) == 3
                assert all(isinstance(x, (int, float)) for x in embedding_vector)

            assert mock_embedder.embed_documents.call_count == len(results)
        finally:
            os.unlink(temp_file)

    @patch('embeddings.embed.PipelineTracking')
    @patch('embeddings.embed.PDFProcessor')
    @patch('embeddings.embed.OpenAIEmbeddings')
    def test_generate_embeddings_different_models(
        self, mock_oai_cls, mock_pdf, mock_pt
    ):
        """Test generate_embeddings with different embedding models."""
        test_content = "Test content for different models."

        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt') as f:
            f.write(test_content)
            temp_file = f.name

        try:
            models_to_test = [
                "text-embedding-ada-002",
                "text-embedding-3-small",
                "text-embedding-3-large"
            ]

            for model in models_to_test:
                mock_oai_cls.reset_mock()
                mock_embedder = Mock()
                mock_oai_cls.return_value = mock_embedder
                mock_embedder.embed_documents.return_value = [[0.1, 0.2, 0.3]]

                model_params = {"model": model, "dimensions": 3}
                gen = EmbeddingsGenerator(
                    embedding_model=model,
                    embedding_model_params=model_params
                )

                results = list(gen.generate_embeddings(
                    pipeline_id=PIPELINE_ID, file_location=temp_file
                ))

                mock_oai_cls.assert_called_once_with(
                    model=model,
                    openai_api_key="test-api-key",
                    dimensions=3
                )

                assert len(results) > 0
        finally:
            os.unlink(temp_file)


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
