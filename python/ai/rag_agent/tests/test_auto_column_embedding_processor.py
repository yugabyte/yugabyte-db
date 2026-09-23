#!/usr/bin/env python3
"""
Tests for rag_pipeline.auto_column_embedding_processor.AutoColumnEmbeddingProcessor.

Covers the buffer/flush trigger (volume vs. time), vector_index_id grouping
(two registrations on different models -> two embed_documents calls; same
model -> one call), per-tenant fairness capping, and the three finalize
paths (success, retryable failure re-queues and drops from the buffer,
permanent failure).

Note: this module transitively imports the PDF/HTML processing stack
(``embeddings`` -> ``pdf_processing``/``html_processing``) even though this
processor never uses either -- these tests patch EmbeddingsGenerator/
SourceConnectionPool/SystemConnectionPool/RegistrationPoller so no real
embedding or DB call ever happens, but a real interpreter that can import
``rag_pipeline.auto_column_embedding_processor`` at all (i.e. has the repo's
full requirements.txt installed, per tests/run_tests.sh) is still required.
"""

import time
from unittest.mock import MagicMock, Mock, patch
from uuid import uuid4

import pytest

from db.registration_cache import ColumnEmbeddingRegistration
from work_queue.registration_poller import ClaimedRow


def _registration(**overrides):
    defaults = dict(
        id=uuid4(),
        registration_name="conversation_history_search_langfuse",
        status="ACTIVE",
        vector_index_id=uuid4(),
        ai_provider="OPENAI",
        embedding_model_params={"model": "text-embedding-3-large", "dimensions": 1536},
        destination_schema="datapacks",
        destination_table="conversation_history_search",
        destination_pk_column="id",
        destination_embedding_column="embeddings",
        destination_tenant_column="tenant_id",
        destination_priority_column="priority",
        destination_stale_claim_seconds=900,
        destination_claim_batch_size=25,
        destination_reserved_backfill_slots=5,
        source_connection="generic",
        source_schema="clickhouse",
        source_table="observations",
        source_text_columns=["input", "output"],
        source_filter_by_columns=[
            {"source_filter_on_column": "trace_id", "target_value_from_column": "conversation_id"}
        ],
        source_order_by=None,
    )
    defaults.update(overrides)
    return ColumnEmbeddingRegistration(**defaults)


def _claimed_row(registration, row_id="row-1", tenant_id="tenant-a"):
    return ClaimedRow(
        registration=registration,
        row={"id": row_id, "tenant_id": tenant_id, "conversation_id": "conv-1"},
        claimed_at=None,
    )


@pytest.fixture
def processor_env():
    """Build an AutoColumnEmbeddingProcessor with every external dependency
    mocked -- no real DB connection, no real embedding call."""
    with patch(
        "rag_pipeline.auto_column_embedding_processor.SystemConnectionPool"
    ), patch(
        "rag_pipeline.auto_column_embedding_processor.SourceConnectionPool"
    ), patch(
        "rag_pipeline.auto_column_embedding_processor.RegistrationCache"
    ), patch(
        "rag_pipeline.auto_column_embedding_processor.RegistrationPoller"
    ) as mock_poller_cls:
        from rag_pipeline.auto_column_embedding_processor import AutoColumnEmbeddingProcessor

        mock_poller = Mock()
        mock_poller_cls.return_value = mock_poller
        processor = AutoColumnEmbeddingProcessor()
        processor.registration_poller = mock_poller
        yield processor, mock_poller


@pytest.mark.unit
class TestBufferAndFlushTrigger:
    def test_buffer_below_batch_max_does_not_flush(self, processor_env):
        processor, _ = processor_env
        registration = _registration()
        with patch.object(processor, "_flush") as mock_flush:
            processor.buffer([_claimed_row(registration)])
            mock_flush.assert_not_called()

    def test_flush_fires_at_batch_max(self, processor_env):
        processor, _ = processor_env
        registration = _registration()
        import rag_pipeline.auto_column_embedding_processor as mod

        with patch.object(mod, "BATCH_MAX", 2), patch.object(processor, "_flush") as mock_flush:
            processor.buffer([_claimed_row(registration, row_id="r1")])
            mock_flush.assert_not_called()
            processor.buffer([_claimed_row(registration, row_id="r2")])
            mock_flush.assert_called_once()
            assert len(mock_flush.call_args[0][0]) == 2

    def test_maybe_flush_fires_on_time_trigger(self, processor_env):
        processor, _ = processor_env
        registration = _registration()
        import rag_pipeline.auto_column_embedding_processor as mod

        with patch.object(mod, "BATCH_MAX_WAIT_SECONDS", 0):
            with patch.object(processor, "_flush") as mock_flush:
                processor.buffer([_claimed_row(registration)])
                time.sleep(0.01)
                processor.maybe_flush()
                mock_flush.assert_called_once()

    def test_maybe_flush_noop_on_empty_buffer(self, processor_env):
        processor, _ = processor_env
        with patch.object(processor, "_flush") as mock_flush:
            processor.maybe_flush()
            mock_flush.assert_not_called()


@pytest.mark.unit
class TestVectorIndexGrouping:
    def test_two_registrations_same_vector_index_one_embed_call(self, processor_env):
        processor, poller = processor_env
        shared_vector_index_id = uuid4()
        reg_a = _registration(registration_name="reg-a", vector_index_id=shared_vector_index_id)
        reg_b = _registration(registration_name="reg-b", vector_index_id=shared_vector_index_id)

        from rag_pipeline.auto_column_embedding_processor import _BufferedItem

        mock_embedder = MagicMock()
        mock_embedder.embedder.embed_documents.return_value = [[0.1], [0.2]]
        with patch.object(processor, "_get_embedder", return_value=mock_embedder), \
                patch.object(processor, "_read_source_text", return_value="some text"):
            processor._flush([
                _BufferedItem(claimed_row=_claimed_row(reg_a, row_id="r1")),
                _BufferedItem(claimed_row=_claimed_row(reg_b, row_id="r2")),
            ])

        assert mock_embedder.embedder.embed_documents.call_count == 1
        assert poller.finalize_success.call_count == 2

    def test_two_registrations_different_vector_index_two_embed_calls(self, processor_env):
        processor, poller = processor_env
        reg_a = _registration(registration_name="reg-a", vector_index_id=uuid4())
        reg_b = _registration(registration_name="reg-b", vector_index_id=uuid4())

        embedders = {}

        def fake_get_embedder(registration):
            key = str(registration.vector_index_id)
            if key not in embedders:
                m = MagicMock()
                m.embedder.embed_documents.return_value = [[0.1]]
                embedders[key] = m
            return embedders[key]

        from rag_pipeline.auto_column_embedding_processor import _BufferedItem

        with patch.object(processor, "_get_embedder", side_effect=fake_get_embedder), \
                patch.object(processor, "_read_source_text", return_value="some text"):
            processor._flush([
                _BufferedItem(claimed_row=_claimed_row(reg_a, row_id="r1")),
                _BufferedItem(claimed_row=_claimed_row(reg_b, row_id="r2")),
            ])

        for embedder in embedders.values():
            assert embedder.embedder.embed_documents.call_count == 1
        assert poller.finalize_success.call_count == 2


@pytest.mark.unit
class TestPerTenantCap:
    def test_cap_defers_excess_rows_from_same_tenant(self, processor_env):
        processor, _ = processor_env
        registration = _registration()
        import rag_pipeline.auto_column_embedding_processor as mod
        from rag_pipeline.auto_column_embedding_processor import _BufferedItem

        with patch.object(mod, "PER_TENANT_FLUSH_CAP", 2):
            batch = [
                _BufferedItem(claimed_row=_claimed_row(registration, row_id=f"r{i}"))
                for i in range(5)
            ]
            capped = processor._apply_per_tenant_cap(batch)

        assert len(capped) == 2
        # The deferred 3 rows should be back at the front of the buffer.
        assert len(processor._buffer) == 3

    def test_no_cap_across_different_tenants(self, processor_env):
        processor, _ = processor_env
        registration = _registration()
        import rag_pipeline.auto_column_embedding_processor as mod
        from rag_pipeline.auto_column_embedding_processor import _BufferedItem

        with patch.object(mod, "PER_TENANT_FLUSH_CAP", 1):
            batch = [
                _BufferedItem(claimed_row=_claimed_row(registration, row_id="r1", tenant_id="t1")),
                _BufferedItem(claimed_row=_claimed_row(registration, row_id="r2", tenant_id="t2")),
            ]
            capped = processor._apply_per_tenant_cap(batch)

        assert len(capped) == 2


@pytest.mark.unit
class TestFinalizePaths:
    def test_source_read_failure_is_retryable(self, processor_env):
        processor, poller = processor_env
        registration = _registration()
        poller.finalize_retry.return_value = "QUEUED"
        from rag_pipeline.auto_column_embedding_processor import _BufferedItem

        with patch.object(processor, "_get_embedder", return_value=MagicMock()), \
                patch.object(processor, "_read_source_text", side_effect=RuntimeError("db down")):
            processor._flush([_BufferedItem(claimed_row=_claimed_row(registration))])

        poller.finalize_retry.assert_called_once()
        poller.finalize_failure.assert_not_called()

    def test_missing_source_text_is_permanent_failure(self, processor_env):
        processor, poller = processor_env
        registration = _registration()
        from rag_pipeline.auto_column_embedding_processor import _BufferedItem

        with patch.object(processor, "_get_embedder", return_value=MagicMock()), \
                patch.object(processor, "_read_source_text", return_value=None):
            processor._flush([_BufferedItem(claimed_row=_claimed_row(registration))])

        poller.finalize_failure.assert_called_once()
        poller.finalize_retry.assert_not_called()

    def test_embed_call_failure_retries_every_item(self, processor_env):
        processor, poller = processor_env
        registration = _registration()
        poller.finalize_retry.return_value = "QUEUED"
        mock_embedder = MagicMock()
        mock_embedder.embedder.embed_documents.side_effect = RuntimeError("rate limited")
        from rag_pipeline.auto_column_embedding_processor import _BufferedItem

        with patch.object(processor, "_get_embedder", return_value=mock_embedder), \
                patch.object(processor, "_read_source_text", return_value="text"):
            processor._flush([
                _BufferedItem(claimed_row=_claimed_row(registration, row_id="r1")),
                _BufferedItem(claimed_row=_claimed_row(registration, row_id="r2")),
            ])

        assert poller.finalize_retry.call_count == 2

    def test_successful_flush_calls_finalize_success_per_item(self, processor_env):
        processor, poller = processor_env
        registration = _registration()
        mock_embedder = MagicMock()
        mock_embedder.embedder.embed_documents.return_value = [[0.1], [0.2]]
        from rag_pipeline.auto_column_embedding_processor import _BufferedItem

        with patch.object(processor, "_get_embedder", return_value=mock_embedder), \
                patch.object(processor, "_read_source_text", return_value="text"):
            items = [
                _BufferedItem(claimed_row=_claimed_row(registration, row_id="r1")),
                _BufferedItem(claimed_row=_claimed_row(registration, row_id="r2")),
            ]
            processor._flush(items)

        assert poller.finalize_success.call_count == 2

    def test_terminal_failure_callback_invoked_when_retries_exhausted(self, processor_env):
        processor, poller = processor_env
        registration = _registration()
        poller.finalize_retry.return_value = "FAILED"
        callback = Mock()
        processor._on_terminal_failure = callback
        from rag_pipeline.auto_column_embedding_processor import _BufferedItem

        with patch.object(processor, "_get_embedder", return_value=MagicMock()), \
                patch.object(processor, "_read_source_text", side_effect=RuntimeError("boom")):
            processor._flush([_BufferedItem(claimed_row=_claimed_row(registration))])

        callback.assert_called_once()
