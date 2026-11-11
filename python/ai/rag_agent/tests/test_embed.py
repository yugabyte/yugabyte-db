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
        """Embedding API failures must propagate so the pipeline can mark
        the task FAILED. Previously the text path swallowed the exception
        which caused docs to be reported as completed with zero embeddings
        persisted. Verify it is re-raised."""
        test_content = "Test content for embedding failure."

        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt') as f:
            f.write(test_content)
            temp_file = f.name

        try:
            gen, mock_embedder = _create_generator(mock_oai_cls)
            mock_embedder.embed_documents.side_effect = Exception("API Error")

            with pytest.raises(Exception, match="API Error"):
                list(gen.generate_embeddings(
                    pipeline_id=PIPELINE_ID, file_location=temp_file
                ))
        finally:
            os.unlink(temp_file)

    @patch('embeddings.embed.PipelineTracking')
    @patch('embeddings.embed.PDFProcessor')
    @patch('embeddings.embed.OpenAIEmbeddings')
    def test_generate_embeddings_zero_yielded_raises(
        self, mock_oai_cls, mock_pdf, mock_pt
    ):
        """If embed_documents returns an empty vector list for every batch,
        the post-condition guard must raise so a doc with non-empty chunks
        cannot be silently marked completed with zero embeddings."""
        test_content = "Test content with real text in it."

        with tempfile.NamedTemporaryFile(mode='w', delete=False, suffix='.txt') as f:
            f.write(test_content)
            temp_file = f.name

        try:
            gen, mock_embedder = _create_generator(mock_oai_cls)
            mock_embedder.embed_documents.return_value = []

            with pytest.raises(RuntimeError, match="0 vectors"):
                list(gen.generate_embeddings(
                    pipeline_id=PIPELINE_ID, file_location=temp_file
                ))
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


class TestEmbeddingsGeneratorProviderDispatch:
    """Tests covering ai_provider dispatch in EmbeddingsGenerator."""

    @patch('embeddings.embed.PipelineTracking')
    @patch('embeddings.embed.HTMLProcessor')
    @patch('embeddings.embed.PDFProcessor')
    @patch('embeddings.embed.BedrockEmbeddings')
    @patch('embeddings.embed.OpenAIEmbeddings')
    def test_aws_bedrock_provider_uses_bedrock_embeddings(
        self, mock_oai_cls, mock_brk_cls, mock_pdf, mock_html, mock_pt,
        monkeypatch
    ):
        """AWS_BEDROCK provider should construct BedrockEmbeddings, not OpenAIEmbeddings."""
        monkeypatch.setenv("AWS_REGION", "us-east-1")

        mock_brk_embedder = Mock()
        mock_brk_cls.return_value = mock_brk_embedder

        model_params = {
            "model": "amazon.titan-embed-text-v2:0",
            "dimensions": 1024,
        }
        gen = EmbeddingsGenerator(
            embedding_model="amazon.titan-embed-text-v2:0",
            embedding_model_params=model_params,
            ai_provider="AWS_BEDROCK",
        )

        mock_brk_cls.assert_called_once_with(
            model_id="amazon.titan-embed-text-v2:0",
            region_name="us-east-1",
        )
        mock_oai_cls.assert_not_called()
        assert gen.embedder is mock_brk_embedder
        assert gen.ai_provider == "AWS_BEDROCK"

    @patch('embeddings.embed.PipelineTracking')
    @patch('embeddings.embed.HTMLProcessor')
    @patch('embeddings.embed.PDFProcessor')
    @patch('embeddings.embed.BedrockEmbeddings')
    @patch('embeddings.embed.OpenAIEmbeddings')
    def test_default_provider_is_openai(
        self, mock_oai_cls, mock_brk_cls, mock_pdf, mock_html, mock_pt
    ):
        """When ai_provider is omitted, OpenAI should be the default."""
        mock_oai_embedder = Mock()
        mock_oai_cls.return_value = mock_oai_embedder

        gen = EmbeddingsGenerator(
            embedding_model="text-embedding-ada-002",
            embedding_model_params={"dimensions": 1536},
        )

        mock_oai_cls.assert_called_once()
        mock_brk_cls.assert_not_called()
        assert gen.ai_provider == "OPENAI"
        assert gen.embedder is mock_oai_embedder

    @patch('embeddings.embed.PipelineTracking')
    @patch('embeddings.embed.HTMLProcessor')
    @patch('embeddings.embed.PDFProcessor')
    @patch('embeddings.embed.BedrockEmbeddings')
    @patch('embeddings.embed.OpenAIEmbeddings')
    def test_unsupported_provider_raises(
        self, mock_oai_cls, mock_brk_cls, mock_pdf, mock_html, mock_pt
    ):
        """Unknown ai_provider values should raise ValueError."""
        with pytest.raises(ValueError, match="Unsupported ai_provider"):
            EmbeddingsGenerator(
                embedding_model="some-model",
                embedding_model_params={"dimensions": 1536},
                ai_provider="GOOGLE_VERTEX",
            )


if __name__ == "__main__":
    pytest.main([__file__, "-v"])


class TestEmbedBatchTiming:
    """EmbeddingsGenerator._embed_batch logs how long each provider call took."""

    @patch('embeddings.embed.PipelineTracking')
    @patch('embeddings.embed.PDFProcessor')
    @patch('embeddings.embed.OpenAIEmbeddings')
    def test_embed_batch_returns_vectors_and_logs_timing(
        self, mock_oai_cls, mock_pdf, mock_pt, caplog
    ):
        gen, mock_embedder = _create_generator(mock_oai_cls)
        mock_embedder.embed_documents.return_value = [[0.1], [0.2]]

        with caplog.at_level("INFO"):
            vectors = gen._embed_batch(["a", "b"])

        assert vectors == [[0.1], [0.2]]
        mock_embedder.embed_documents.assert_called_once_with(["a", "b"])
        lines = [r.getMessage() for r in caplog.records
                 if r.getMessage().startswith("Embedded batch of 2 chunks in ")]
        assert len(lines) == 1
        assert "ms per chunk" in lines[0]
        assert "model text-embedding-ada-002" in lines[0]

    @patch('embeddings.embed.PipelineTracking')
    @patch('embeddings.embed.PDFProcessor')
    @patch('embeddings.embed.OpenAIEmbeddings')
    def test_embed_batch_propagates_provider_errors(
        self, mock_oai_cls, mock_pdf, mock_pt
    ):
        gen, mock_embedder = _create_generator(mock_oai_cls)
        mock_embedder.embed_documents.side_effect = RuntimeError("throttled")

        with pytest.raises(RuntimeError, match="throttled"):
            gen._embed_batch(["a"])


class TestEmbedConcurrency:
    """EMBED_CONCURRENCY runs per-text Bedrock calls in parallel, in order."""

    @staticmethod
    def _bedrock_generator(mock_bedrock_cls, model="amazon.titan-embed-text-v1"):
        mock_embedder = Mock()
        mock_bedrock_cls.return_value = mock_embedder
        gen = EmbeddingsGenerator(
            embedding_model=model,
            embedding_model_params={"dimensions": 3},
            ai_provider="AWS_BEDROCK",
        )
        return gen, mock_embedder

    @patch('embeddings.embed.PipelineTracking')
    @patch('embeddings.embed.PDFProcessor')
    @patch('embeddings.embed.BedrockEmbeddings')
    def test_default_is_sequential_and_passes_no_config(
        self, mock_bedrock_cls, mock_pdf, mock_pt, monkeypatch
    ):
        monkeypatch.delenv("EMBED_CONCURRENCY", raising=False)
        gen, mock_embedder = self._bedrock_generator(mock_bedrock_cls)
        mock_embedder.embed_documents.return_value = [[1.0], [2.0]]

        assert gen.embed_concurrency == 1
        assert gen._embed_batch(["a", "b"]) == [[1.0], [2.0]]
        mock_embedder.embed_documents.assert_called_once_with(["a", "b"])
        mock_embedder.embed_query.assert_not_called()
        assert "config" not in mock_bedrock_cls.call_args.kwargs

    @patch('embeddings.embed.PipelineTracking')
    @patch('embeddings.embed.PDFProcessor')
    @patch('embeddings.embed.BedrockEmbeddings')
    def test_titan_runs_concurrently_and_keeps_input_order(
        self, mock_bedrock_cls, mock_pdf, mock_pt, monkeypatch
    ):
        import threading
        import time as _time
        monkeypatch.setenv("EMBED_CONCURRENCY", "4")
        gen, mock_embedder = self._bedrock_generator(mock_bedrock_cls)
        texts = [f"t{i}" for i in range(12)]
        active, peak, lock = [0], [0], threading.Lock()

        def fake_query(text):
            with lock:
                active[0] += 1
                peak[0] = max(peak[0], active[0])
            # Earlier texts finish later, so completion order is reversed.
            _time.sleep(0.002 * (12 - int(text[1:])))
            with lock:
                active[0] -= 1
            return [float(text[1:])]

        mock_embedder.embed_query.side_effect = fake_query
        vectors = gen._embed_batch(texts)

        assert vectors == [[float(i)] for i in range(12)]
        assert 1 < peak[0] <= 4
        mock_embedder.embed_documents.assert_not_called()
        config = mock_bedrock_cls.call_args.kwargs["config"]
        assert config.max_pool_connections >= 4
        assert config.retries["mode"] == "adaptive"

    @patch('embeddings.embed.PipelineTracking')
    @patch('embeddings.embed.PDFProcessor')
    @patch('embeddings.embed.BedrockEmbeddings')
    def test_cohere_keeps_its_multi_text_call(
        self, mock_bedrock_cls, mock_pdf, mock_pt, monkeypatch
    ):
        monkeypatch.setenv("EMBED_CONCURRENCY", "4")
        gen, mock_embedder = self._bedrock_generator(
            mock_bedrock_cls, model="cohere.embed-english-v3")
        mock_embedder.embed_documents.return_value = [[1.0], [2.0]]

        gen._embed_batch(["a", "b"])
        mock_embedder.embed_documents.assert_called_once_with(["a", "b"])
        mock_embedder.embed_query.assert_not_called()

    @patch('embeddings.embed.PipelineTracking')
    @patch('embeddings.embed.PDFProcessor')
    @patch('embeddings.embed.OpenAIEmbeddings')
    def test_openai_ignores_concurrency(
        self, mock_oai_cls, mock_pdf, mock_pt, monkeypatch
    ):
        monkeypatch.setenv("EMBED_CONCURRENCY", "8")
        gen, mock_embedder = _create_generator(mock_oai_cls)
        mock_embedder.embed_documents.return_value = [[1.0], [2.0]]

        gen._embed_batch(["a", "b"])
        mock_embedder.embed_documents.assert_called_once_with(["a", "b"])
        mock_embedder.embed_query.assert_not_called()

    @patch('embeddings.embed.PipelineTracking')
    @patch('embeddings.embed.PDFProcessor')
    @patch('embeddings.embed.BedrockEmbeddings')
    def test_provider_error_propagates(
        self, mock_bedrock_cls, mock_pdf, mock_pt, monkeypatch
    ):
        monkeypatch.setenv("EMBED_CONCURRENCY", "4")
        gen, mock_embedder = self._bedrock_generator(mock_bedrock_cls)
        mock_embedder.embed_query.side_effect = RuntimeError("ThrottlingException")

        with pytest.raises(RuntimeError, match="ThrottlingException"):
            gen._embed_batch(["a", "b", "c"])

    @pytest.mark.parametrize("raw,expected", [
        ("", 1), ("abc", 1), ("0", 1), ("-3", 1), ("3", 3), ("500", 32),
    ])
    def test_env_value_is_clamped(self, raw, expected, monkeypatch):
        from embeddings.embed import embed_concurrency_from_env
        monkeypatch.setenv("EMBED_CONCURRENCY", raw)
        assert embed_concurrency_from_env() == expected

    @pytest.mark.parametrize("concurrency,adaptive", [("1", False), ("4", True)])
    @patch('embeddings.embed.PipelineTracking')
    @patch('embeddings.embed.PDFProcessor')
    def test_real_bedrock_client_gets_the_retry_config(
        self, mock_pdf, mock_pt, concurrency, adaptive, monkeypatch
    ):
        # Builds the real langchain-aws BedrockEmbeddings, which makes no
        # network call, so a langchain-aws release that stops passing
        # `config` to the boto client fails here rather than in prod.
        monkeypatch.setenv("EMBED_CONCURRENCY", concurrency)
        monkeypatch.setenv("AWS_REGION", "us-east-1")
        monkeypatch.setenv("AWS_ACCESS_KEY_ID", "test")
        monkeypatch.setenv("AWS_SECRET_ACCESS_KEY", "test")
        gen = EmbeddingsGenerator(
            embedding_model="amazon.titan-embed-text-v1",
            embedding_model_params={"dimensions": 1536},
            ai_provider="AWS_BEDROCK",
        )
        client_config = gen.embedder.client.meta.config
        assert (client_config.retries.get("mode") == "adaptive") is adaptive
        if adaptive:
            assert client_config.max_pool_connections >= 4
