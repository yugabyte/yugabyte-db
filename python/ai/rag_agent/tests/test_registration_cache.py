#!/usr/bin/env python3
"""
Tests for db.registration_cache.RegistrationCache.

Covers the TTL-refresh contract: a refresh only hits the DB once per
refresh_interval_seconds, and a failed refresh keeps serving the last known
good snapshot rather than raising or clearing it.
"""

import pytest
from unittest.mock import Mock, patch
from uuid import uuid4

from db.registration_cache import ColumnEmbeddingRegistration, RegistrationCache


def _row(name="reg-1", reg_id=None, vector_index_id=None):
    return (
        reg_id or uuid4(), name, "ACTIVE", vector_index_id or uuid4(),
        "OPENAI", {"model": "text-embedding-3-large", "dimensions": 1536},
        "datapacks", "conversation_history_search", "id", "embeddings",
        "tenant_id", "priority", 900, 25, 5,
        "generic", "clickhouse", "observations", ["input", "output"],
        [{"source_filter_on_column": "trace_id", "target_value_from_column": "conversation_id"}],
        {"column": "start_time", "direction": "ASC"},
    )


def _create_cache(mock_pool_cls, refresh_interval_seconds=30):
    mock_pool = Mock()
    mock_pool_cls.return_value = mock_pool
    mock_conn = Mock()
    mock_cur = Mock()
    mock_pool.get_connection.return_value = mock_conn
    mock_conn.cursor.return_value = mock_cur
    cache = RegistrationCache(refresh_interval_seconds=refresh_interval_seconds)
    return cache, mock_pool, mock_conn, mock_cur


@pytest.mark.unit
class TestRegistrationCache:
    @patch('db.registration_cache.ConnectionPool')
    def test_from_db_row_maps_all_fields(self, mock_pool_cls):
        row = _row()
        reg = ColumnEmbeddingRegistration.from_db_row(row)
        assert reg.registration_name == "reg-1"
        assert reg.status == "ACTIVE"
        assert reg.ai_provider == "OPENAI"
        assert reg.destination_tenant_column == "tenant_id"
        assert reg.destination_priority_column == "priority"
        assert reg.source_text_columns == ["input", "output"]
        assert reg.source_filter_by_columns[0]["target_value_from_column"] == "conversation_id"
        assert reg.source_order_by["column"] == "start_time"

    @patch('db.registration_cache.ConnectionPool')
    def test_get_active_queries_db_on_first_call(self, mock_pool_cls):
        cache, mock_pool, mock_conn, mock_cur = _create_cache(mock_pool_cls)
        mock_cur.fetchall.return_value = [_row()]

        result = cache.get_active()

        assert len(result) == 1
        mock_cur.execute.assert_called_once()
        assert "column_embedding_registrations" in mock_cur.execute.call_args[0][0]
        assert "status = 'ACTIVE'" in mock_cur.execute.call_args[0][0]

    @patch('db.registration_cache.ConnectionPool')
    def test_get_active_does_not_requery_within_refresh_interval(self, mock_pool_cls):
        cache, mock_pool, mock_conn, mock_cur = _create_cache(
            mock_pool_cls, refresh_interval_seconds=3600
        )
        mock_cur.fetchall.return_value = [_row()]

        cache.get_active()
        cache.get_active()
        cache.get_active()

        assert mock_cur.execute.call_count == 1

    @patch('db.registration_cache.ConnectionPool')
    def test_get_active_requeries_after_interval_elapses(self, mock_pool_cls):
        cache, mock_pool, mock_conn, mock_cur = _create_cache(
            mock_pool_cls, refresh_interval_seconds=0
        )
        mock_cur.fetchall.return_value = [_row()]

        cache.get_active()
        cache.get_active()

        assert mock_cur.execute.call_count == 2

    @patch('db.registration_cache.ConnectionPool')
    def test_refresh_failure_keeps_serving_stale_snapshot(self, mock_pool_cls):
        cache, mock_pool, mock_conn, mock_cur = _create_cache(
            mock_pool_cls, refresh_interval_seconds=3600
        )
        mock_cur.fetchall.return_value = [_row(name="good-reg")]
        first = cache.get_active()
        assert len(first) == 1

        # Force an immediate re-attempt by resetting the refresh clock, then
        # make the DB call fail -- get_active() should not raise, and
        # should keep returning the last known good snapshot.
        cache._last_refreshed_at = 0
        mock_cur.execute.side_effect = Exception("db blip")

        second = cache.get_active()
        assert len(second) == 1
        assert second[0].registration_name == "good-reg"

    @patch('db.registration_cache.ConnectionPool')
    def test_get_active_returns_empty_before_any_successful_refresh(self, mock_pool_cls):
        cache, mock_pool, mock_conn, mock_cur = _create_cache(mock_pool_cls)
        mock_cur.execute.side_effect = Exception("db down")

        assert cache.get_active() == []
