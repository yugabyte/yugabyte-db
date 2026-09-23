#!/usr/bin/env python3
"""
Tests for work_queue.registration_poller.RegistrationPoller.

Covers claim behavior (priority split vs. no-priority-column shape, limits
respected, empty-candidate short circuits) and each finalize path
(success deletes the progress row + writes the embedding; retry increments
attempts with backoff or flips to FAILED once max_attempts is hit; failure
sets FAILED without incrementing).
"""

import pytest
from unittest.mock import Mock, patch
from uuid import uuid4

from db.registration_cache import ColumnEmbeddingRegistration
from work_queue.registration_poller import ClaimedRow, RegistrationPoller


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


def _create_poller(mock_pool_cls):
    mock_pool = Mock()
    mock_pool_cls.return_value = mock_pool
    mock_conn = Mock()
    mock_cur = Mock()
    mock_pool.get_connection.return_value = mock_conn
    mock_conn.cursor.return_value = mock_cur
    poller = RegistrationPoller()
    return poller, mock_pool, mock_conn, mock_cur


@pytest.mark.unit
class TestClaimBatch:
    @patch('work_queue.registration_poller.TargetConnectionPool')
    def test_no_candidates_returns_empty_without_claim_query(self, mock_pool_cls):
        poller, mock_pool, mock_conn, mock_cur = _create_poller(mock_pool_cls)
        registration = _registration()
        # Both sub-claim SELECTs return nothing.
        mock_cur.fetchall.return_value = []

        result = poller.claim_batch(registration, worker_id="w1")

        assert result == []
        # Only the two candidate SELECTs ran -- no UPSERT, no fetch-rows.
        assert mock_cur.execute.call_count == 2

    @patch('work_queue.registration_poller.TargetConnectionPool')
    def test_priority_split_issues_two_sub_claim_queries(self, mock_pool_cls):
        poller, mock_pool, mock_conn, mock_cur = _create_poller(mock_pool_cls)
        registration = _registration(
            destination_claim_batch_size=25, destination_reserved_backfill_slots=5,
        )
        mock_cur.fetchall.return_value = []

        poller._find_candidates(mock_cur, registration)

        assert mock_cur.execute.call_count == 2
        query_a, params_a = mock_cur.execute.call_args_list[0][0]
        query_b, params_b = mock_cur.execute.call_args_list[1][0]
        assert params_a["limit"] == 20  # batch_size - reserved_backfill_slots
        assert params_b["limit"] == 5

    @patch('work_queue.registration_poller.TargetConnectionPool')
    def test_no_priority_column_issues_single_query(self, mock_pool_cls):
        poller, mock_pool, mock_conn, mock_cur = _create_poller(mock_pool_cls)
        registration = _registration(destination_priority_column=None)
        mock_cur.fetchall.return_value = []

        poller._find_candidates(mock_cur, registration)

        assert mock_cur.execute.call_count == 1
        _, params = mock_cur.execute.call_args[0]
        assert params["limit"] == registration.destination_claim_batch_size

    @patch('work_queue.registration_poller.TargetConnectionPool')
    def test_reserved_slots_ge_batch_size_skips_live_subclaim(self, mock_pool_cls):
        poller, mock_pool, mock_conn, mock_cur = _create_poller(mock_pool_cls)
        registration = _registration(
            destination_claim_batch_size=5, destination_reserved_backfill_slots=5,
        )
        mock_cur.fetchall.return_value = []

        poller._find_candidates(mock_cur, registration)

        # live_limit == 0 -> sub-claim A skipped, only sub-claim B runs.
        assert mock_cur.execute.call_count == 1

    @patch('work_queue.registration_poller.TargetConnectionPool')
    def test_full_claim_batch_flow_returns_claimed_rows(self, mock_pool_cls):
        poller, mock_pool, mock_conn, mock_cur = _create_poller(mock_pool_cls)
        registration = _registration()

        # candidates (sub-claim A, sub-claim B), then claim UPSERT, then fetch.
        mock_cur.fetchall.side_effect = [
            [("row-1",)],  # sub-claim A candidates
            [],  # sub-claim B candidates
            [("row-1",)],  # claim UPSERT RETURNING
            [("row-1", "tenant-a", None)],  # fetched full row
        ]
        mock_cur.description = [("id",), ("tenant_id",), ("embeddings",)]

        result = poller.claim_batch(registration, worker_id="w1")

        assert len(result) == 1
        assert isinstance(result[0], ClaimedRow)
        assert result[0].row == {"id": "row-1", "tenant_id": "tenant-a", "embeddings": None}
        assert result[0].registration is registration

    @patch('work_queue.registration_poller.TargetConnectionPool')
    def test_claim_upsert_returning_nothing_returns_empty(self, mock_pool_cls):
        poller, mock_pool, mock_conn, mock_cur = _create_poller(mock_pool_cls)
        registration = _registration()
        mock_cur.fetchall.side_effect = [
            [("row-1",)],
            [],
            [],  # another worker won the race -- nothing RETURNING
        ]

        result = poller.claim_batch(registration, worker_id="w1")

        assert result == []

    @patch('work_queue.registration_poller.TargetConnectionPool')
    def test_claim_error_rolls_back_and_raises(self, mock_pool_cls):
        poller, mock_pool, mock_conn, mock_cur = _create_poller(mock_pool_cls)
        registration = _registration()
        mock_cur.execute.side_effect = Exception("db boom")

        with pytest.raises(Exception, match="db boom"):
            poller.claim_batch(registration, worker_id="w1")

        mock_conn.rollback.assert_called_once()
        mock_pool.return_connection.assert_called_once_with(mock_conn)


@pytest.mark.unit
class TestFinalize:
    @patch('work_queue.registration_poller.TargetConnectionPool')
    def test_finalize_success_writes_embedding_and_deletes_progress(self, mock_pool_cls):
        poller, mock_pool, mock_conn, mock_cur = _create_poller(mock_pool_cls)
        registration = _registration()
        claimed_row = ClaimedRow(
            registration=registration, row={"id": "row-1"}, claimed_at=None
        )

        poller.finalize_success(claimed_row, embedding=[0.1, 0.2])

        update_sql, update_params = mock_cur.execute.call_args_list[0][0]
        assert "UPDATE" in update_sql.as_string(None)
        assert update_params == ([0.1, 0.2], "row-1")
        delete_sql, delete_params = mock_cur.execute.call_args_list[1][0]
        assert "DELETE FROM dist_rag.column_embedding_progress" in delete_sql
        assert delete_params == (str(registration.id), "row-1")
        mock_conn.commit.assert_called_once()

    @patch('work_queue.registration_poller.TargetConnectionPool')
    def test_finalize_retry_returns_queued_under_max_attempts(self, mock_pool_cls):
        poller, mock_pool, mock_conn, mock_cur = _create_poller(mock_pool_cls)
        registration = _registration()
        claimed_row = ClaimedRow(registration=registration, row={"id": "row-1"}, claimed_at=None)
        mock_cur.fetchone.return_value = ("QUEUED",)

        status = poller.finalize_retry(
            claimed_row, "transient error", max_attempts=5, backoff_base_seconds=2.0
        )

        assert status == "QUEUED"
        _, params = mock_cur.execute.call_args[0]
        assert params["max_attempts"] == 5
        assert params["error"] == "transient error"

    @patch('work_queue.registration_poller.TargetConnectionPool')
    def test_finalize_retry_returns_failed_when_no_row_matched(self, mock_pool_cls):
        poller, mock_pool, mock_conn, mock_cur = _create_poller(mock_pool_cls)
        registration = _registration()
        claimed_row = ClaimedRow(registration=registration, row={"id": "row-1"}, claimed_at=None)
        mock_cur.fetchone.return_value = None

        status = poller.finalize_retry(
            claimed_row, "err", max_attempts=5, backoff_base_seconds=2.0
        )

        assert status == "FAILED"

    @patch('work_queue.registration_poller.TargetConnectionPool')
    def test_finalize_failure_sets_failed_status(self, mock_pool_cls):
        poller, mock_pool, mock_conn, mock_cur = _create_poller(mock_pool_cls)
        registration = _registration()
        claimed_row = ClaimedRow(registration=registration, row={"id": "row-1"}, claimed_at=None)

        poller.finalize_failure(claimed_row, "permanent miss")

        sql_text, params = mock_cur.execute.call_args[0]
        assert "status = 'FAILED'" in sql_text
        assert params == ("permanent miss", str(registration.id), "row-1")
        mock_conn.commit.assert_called_once()

    @patch('work_queue.registration_poller.TargetConnectionPool')
    def test_finalize_success_rolls_back_and_raises_on_error(self, mock_pool_cls):
        poller, mock_pool, mock_conn, mock_cur = _create_poller(mock_pool_cls)
        registration = _registration()
        claimed_row = ClaimedRow(registration=registration, row={"id": "row-1"}, claimed_at=None)
        mock_cur.execute.side_effect = Exception("write failed")

        with pytest.raises(Exception, match="write failed"):
            poller.finalize_success(claimed_row, embedding=[0.1])

        mock_conn.rollback.assert_called_once()
