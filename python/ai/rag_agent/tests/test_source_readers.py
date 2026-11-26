#!/usr/bin/env python3
"""
Tests for rag_pipeline.source_readers.read_generic_column and its helpers.

Covers each filter-value source (literal, row-derived, resolver with a
literal WHERE value, resolver with a row-derived WHERE value) plus the
permanent-miss contract (a resolver or main-query miss returns None, never
raises) and the loud-failure contract for a genuinely missing row column
(a registration/reader mismatch, not a data miss).
"""

import pytest
from unittest.mock import Mock

from rag_pipeline.source_readers import (
    _build_filter_conditions,
    _filter_value,
    _resolve_filter_value,
    read_generic_column,
)


def _mock_pool(fetchone_return):
    pool = Mock()
    conn = Mock()
    cur = Mock()
    pool.get_connection.return_value = conn
    conn.cursor.return_value = cur
    cur.fetchone.return_value = fetchone_return
    return pool, conn, cur


@pytest.mark.unit
class TestFilterValue:
    def test_literal_value(self):
        f = {"source_filter_on_column": "trace_id", "value": "abc"}
        assert _filter_value(f, row=None) == "abc"

    def test_target_value_from_column(self):
        row = {"conversation_id": "conv-1"}
        f = {"source_filter_on_column": "trace_id", "target_value_from_column": "conversation_id"}
        assert _filter_value(f, row) == "conv-1"

    def test_target_value_from_column_missing_raises(self):
        f = {"source_filter_on_column": "trace_id", "target_value_from_column": "conversation_id"}
        with pytest.raises(ValueError, match="conversation_id"):
            _filter_value(f, row={})

    def test_target_value_from_column_no_row_raises(self):
        f = {"source_filter_on_column": "trace_id", "target_value_from_column": "conversation_id"}
        with pytest.raises(ValueError):
            _filter_value(f, row=None)


@pytest.mark.unit
class TestResolveFilterValue:
    def test_resolver_with_literal_where_value(self):
        pool, conn, cur = _mock_pool(("proj-123",))
        pools = {"system": pool}
        spec = {
            "schema": "meko_system", "table": "langfuse_project_mapping",
            "select_column": "langfuse_project_id", "where_column": "datapack_id",
            "where_value": "dp-1",
        }
        result = _resolve_filter_value(spec, pools, row=None)
        assert result == "proj-123"
        executed_sql, params = cur.execute.call_args[0]
        assert params == ("dp-1",)

    def test_resolver_with_row_derived_where_value(self):
        pool, conn, cur = _mock_pool(("proj-999",))
        pools = {"system": pool}
        spec = {
            "schema": "meko_system", "table": "langfuse_project_mapping",
            "select_column": "langfuse_project_id", "where_column": "datapack_id",
            "where_value_from_row_column": "tenant_id",
        }
        row = {"tenant_id": "dp-42"}
        result = _resolve_filter_value(spec, pools, row=row)
        assert result == "proj-999"
        _, params = cur.execute.call_args[0]
        assert params == ("dp-42",)

    def test_resolver_row_derived_missing_column_raises(self):
        pool, _, _ = _mock_pool(None)
        pools = {"system": pool}
        spec = {
            "schema": "s", "table": "t", "select_column": "c", "where_column": "w",
            "where_value_from_row_column": "tenant_id",
        }
        with pytest.raises(ValueError, match="tenant_id"):
            _resolve_filter_value(spec, pools, row={})

    def test_resolver_miss_returns_none_not_raise(self):
        pool, _, _ = _mock_pool(None)
        pools = {"system": pool}
        spec = {
            "schema": "s", "table": "t", "select_column": "c", "where_column": "w",
            "where_value": "x",
        }
        assert _resolve_filter_value(spec, pools, row=None) is None


@pytest.mark.unit
class TestBuildFilterConditions:
    def test_plain_equality(self):
        conditions, params = _build_filter_conditions(
            [{"source_filter_on_column": "trace_id", "value": "abc"}], pools={}, row=None
        )
        assert params == ["abc"]

    def test_json_path_filter(self):
        conditions, params = _build_filter_conditions(
            [{"source_filter_on_column": "metadata", "json_key": "message_id", "value": "m-1"}],
            pools={}, row=None,
        )
        assert params == ["m-1"]

    def test_resolver_miss_short_circuits_to_none(self):
        pool, _, _ = _mock_pool(None)
        conditions, params = _build_filter_conditions(
            [{"source_filter_on_column": "project_id", "resolve": {
                "schema": "s", "table": "t", "select_column": "c",
                "where_column": "w", "where_value": "x",
            }}],
            pools={"system": pool}, row=None,
        )
        assert conditions is None
        assert params is None


@pytest.mark.unit
class TestReadGenericColumn:
    def test_returns_joined_text_columns(self):
        pool, conn, cur = _mock_pool(None)
        cur.fetchone.return_value = ("question text", "answer text")
        filter_entry = {
            "source_filter_on_column": "trace_id",
            "target_value_from_column": "conversation_id",
        }
        source = {
            "schema": "clickhouse", "table": "observations",
            "text_columns": ["input", "output"],
            "filters": [filter_entry],
        }
        row = {"conversation_id": "conv-1"}
        result = read_generic_column(source, pools={"source": pool}, row=row)
        assert result == "question text\n\nanswer text"

    def test_no_matching_row_returns_none(self):
        pool, conn, cur = _mock_pool(None)
        cur.fetchone.return_value = None
        source = {
            "schema": "s", "table": "t", "text_columns": ["input"],
            "filters": [{"source_filter_on_column": "id", "value": "missing"}],
        }
        assert read_generic_column(source, pools={"source": pool}) is None

    def test_resolver_miss_returns_none(self):
        pool, _, _ = _mock_pool(None)
        source = {
            "schema": "s", "table": "t", "text_columns": ["input"],
            "filters": [{"source_filter_on_column": "project_id", "resolve": {
                "schema": "meko_system", "table": "langfuse_project_mapping",
                "select_column": "langfuse_project_id", "where_column": "datapack_id",
                "where_value_from_row_column": "tenant_id",
            }}],
        }
        row = {"tenant_id": "dp-1"}
        assert read_generic_column(source, pools={"source": pool, "system": pool}, row=row) is None

    def test_requires_non_empty_filters(self):
        with pytest.raises(ValueError):
            read_generic_column({"schema": "s", "table": "t", "text_columns": ["input"]}, pools={})

    def test_requires_text_columns(self):
        with pytest.raises(ValueError):
            read_generic_column(
                {
                    "schema": "s", "table": "t",
                    "filters": [{"source_filter_on_column": "id", "value": "x"}],
                },
                pools={},
            )

    def test_applies_order_by(self):
        pool, conn, cur = _mock_pool(None)
        cur.fetchone.return_value = ("text",)
        source = {
            "schema": "clickhouse", "table": "observations", "text_columns": ["input"],
            "filters": [{"source_filter_on_column": "trace_id", "value": "t-1"}],
            "order_by": {"column": "start_time", "direction": "ASC"},
        }
        read_generic_column(source, pools={"source": pool})
        executed_sql = cur.execute.call_args[0][0]
        assert "ORDER BY" in executed_sql.as_string(None)

    def test_order_by_direction_is_case_insensitive(self):
        pool, conn, cur = _mock_pool(None)
        cur.fetchone.return_value = ("text",)
        source = {
            "schema": "clickhouse", "table": "observations", "text_columns": ["input"],
            "filters": [{"source_filter_on_column": "trace_id", "value": "t-1"}],
            "order_by": {"column": "start_time", "direction": "desc"},
        }
        read_generic_column(source, pools={"source": pool})
        executed_sql = cur.execute.call_args[0][0].as_string(None)
        assert "ORDER BY" in executed_sql and "DESC" in executed_sql

    def test_order_by_direction_rejects_sql_injection(self):
        """Regression: order_by comes from a registration's r_source_order_by
        (create_column_embedding_mapping is PUBLIC-executable, no validation
        of its own) and is composed into the query via sql.SQL(), which does
        no escaping. A direction outside ASC/DESC must be rejected before it
        ever reaches query composition -- otherwise a registration can inject
        arbitrary SQL (e.g. a subquery) into the ORDER BY clause, running
        with this worker's DB privileges."""
        pool, conn, cur = _mock_pool(None)
        source = {
            "schema": "clickhouse", "table": "observations", "text_columns": ["input"],
            "filters": [{"source_filter_on_column": "trace_id", "value": "t-1"}],
            "order_by": {
                "column": "start_time",
                "direction": "ASC, (SELECT 1 FROM pg_sleep(3600))",
            },
        }
        with pytest.raises(ValueError, match="invalid order_by direction"):
            read_generic_column(source, pools={"source": pool})
        cur.execute.assert_not_called()
