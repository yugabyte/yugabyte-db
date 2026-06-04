"""Tests for EXPLAIN

These tests are using Python mainly because the output of EXPLAIN ANALYZE
contains timings, so the output is not deterministic.
"""

import json

import psycopg.errors
import pytest

from .utils import Cursor


def test_explain(cur: Cursor):
    cur.sql("CREATE TABLE test_table (id int, name text)")
    result = cur.sql("EXPLAIN SELECT count(*) FROM test_table")
    plan = "\n".join(result)
    assert "UNGROUPED_AGGREGATE" in plan
    assert "Total Time:" not in plan
    assert "Output:" not in plan

    result = cur.sql("EXPLAIN ANALYZE SELECT count(*) FROM test_table")
    plan = "\n".join(result)
    assert "Query Profiling Information" in plan
    assert "UNGROUPED_AGGREGATE" in plan
    assert "Total Time:" in plan
    assert "Output:" not in plan

    result = cur.sql("EXPLAIN SELECT count(*) FROM test_table where id = %s", (1,))
    plan = "\n".join(result)
    assert "UNGROUPED_AGGREGATE" in plan
    assert "id=1" in plan
    assert "Total Time:" not in plan
    assert "Output:" not in plan

    result = cur.sql(
        "EXPLAIN ANALYZE SELECT count(*) FROM test_table where id = %s", (1,)
    )
    plan = "\n".join(result)
    assert "UNGROUPED_AGGREGATE" in plan
    assert "id=1" in plan
    assert "Total Time:" in plan
    assert "Output:" not in plan

    result = cur.sql("EXPLAIN VERBOSE SELECT count(*) FROM test_table")
    plan = "\n".join(result)
    assert "UNGROUPED_AGGREGATE" in plan
    assert "Total Time:" not in plan
    assert "Output:" in plan

    result = cur.sql("EXPLAIN (VERBOSE, ANALYZE) SELECT count(*) FROM test_table")
    plan = "\n".join(result)
    assert "Query Profiling Information" in plan
    assert "UNGROUPED_AGGREGATE" in plan
    assert "Total Time:" in plan
    assert "Output:" in plan

    # Test for Json Output Format , psycopg internal convert json to dict
    result = cur.sql("EXPLAIN (FORMAT JSON) SELECT count(*) FROM test_table")
    assert len(result) == 1
    assert type(result[0]) is dict
    assert type(result[0]["Plan"]["DuckDB Execution Plan"]) is list
    assert result[0]["Plan"]["Custom Plan Provider"] == "DuckDBScan"
    assert type(result[0]["Plan"]["DuckDB Execution Plan"][0]["extra_info"]) is dict

    result = cur.sql("EXPLAIN (VERBOSE, FORMAT JSON) SELECT count(*) FROM test_table")
    assert len(result) == 1
    assert type(result[0]) is dict
    assert type(result[0]["Plan"]["DuckDB Execution Plan"]) is list
    assert result[0]["Plan"]["Custom Plan Provider"] == "DuckDBScan"
    assert type(result[0]["Plan"]["DuckDB Execution Plan"][0]["extra_info"]) is dict
    assert type(result[0]["Plan"]["Output"]) is list

    result = cur.sql(
        "EXPLAIN (VERBOSE, ANALYZE, FORMAT JSON) SELECT count(*) FROM test_table"
    )
    assert len(result) == 1
    assert type(result[0]) is dict
    assert (
        result[0]["Plan"]["DuckDB Execution Plan"]["children"][0]["operator_name"]
        == "EXPLAIN_ANALYZE"
    )
    assert type(result[0]["Plan"]["Output"]) is list
    assert "Planning Time" in (result[0]).keys()
    assert "Execution Time" in (result[0]).keys()


def test_explain_execute_statement(cur: Cursor):
    """
    EXPLAIN EXECUTE is known to be broken, because we don't have the right
    hooks in Postgres 17 and below. We should fix this for PG18.

    For new we at least run it to make sure that it doesn't crash (which it did
    in the past).
    """
    cur.sql("CREATE TABLE test_table (id int, name text)")
    cur.sql("EXPLAIN (ANALYZE, FORMAT JSON) SELECT * FROM test_table t")
    cur.sql("PREPARE test_query AS SELECT * FROM test_table t")
    result = cur.sql("EXPLAIN EXECUTE test_query")
    plan = "\n".join(result)
    print(plan)
    result2 = cur.sql("EXPLAIN EXECUTE test_query")
    plan2 = "\n".join(result2)
    print(plan2)
    # Due to bugs the second output will be in different format, the first one
    # will still be using JSON formatting as well as using EXPLAIN ANALYZE.
    assert result[3] != result2[3]
    assert "EXPLAIN_ANALYZE" in plan
    assert "EXPLAIN_ANALYZE" not in plan2
    # Similarly the first run like this will result in invalid JSON
    with pytest.raises(
        json.decoder.JSONDecodeError,
    ):
        cur.sql("EXPLAIN (FORMAT JSON) EXECUTE test_query")

    # The second query correctly creates JSON output though.
    cur.sql("EXPLAIN (FORMAT JSON) EXECUTE test_query")


def test_explain_dml(cur: Cursor):
    cur.sql("CREATE TEMP TABLE test_table (id int) USING duckdb")
    result = cur.sql(
        "EXPLAIN INSERT INTO test_table SELECT * FROM generate_series(1, 5)"
    )
    plan = "\n".join(result)
    print(plan)
    assert "GENERATE_SERIES" in plan
    assert "Total Time:" not in plan

    # The insert should not have been executed
    assert cur.sql("SELECT * FROM test_table") == []

    result = cur.sql(
        "EXPLAIN ANALYZE INSERT INTO test_table SELECT * FROM generate_series(1, 5)"
    )
    plan = "\n".join(result)
    print(plan)
    assert "GENERATE_SERIES" in plan
    assert "Total Time:" in plan
    # The insert should have been executed exactly once
    assert cur.sql("SELECT * FROM test_table ORDER BY id") == [1, 2, 3, 4, 5]


def test_explain_ctas(cur: Cursor):
    cur.sql("CREATE TEMP TABLE heap1(id) AS SELECT 1")
    result = cur.sql("EXPLAIN CREATE TEMP TABLE heap2(id) AS SELECT * from heap1")
    plan = "\n".join(result)
    assert "POSTGRES_SCAN" in plan
    assert "Total Time:" not in plan

    # EXPLAIN ANALYZE of a CTAS doesn't work if we use DuckDB execution
    with pytest.raises(
        psycopg.errors.InternalError,
        match="Not implemented Error: Cannot use EXPLAIN ANALYZE with CREATE TABLE ... AS when using DuckDB execution",
    ):
        cur.sql("EXPLAIN ANALYZE CREATE TEMP TABLE heap2(id) AS SELECT * from heap1")

    # But it continues to work fine when we PG execution is used
    cur.sql("SET duckdb.force_execution = false")
    cur.sql("EXPLAIN ANALYZE CREATE TEMP TABLE heap2(id) AS SELECT * from heap1")
    cur.sql("SET duckdb.force_execution = true")

    result = cur.sql(
        "EXPLAIN CREATE TEMP TABLE duckdb1(id) USING duckdb AS SELECT * from heap1"
    )
    plan = "\n".join(result)
    assert "POSTGRES_SCAN" in plan
    assert "Total Time:" not in plan

    # EXPLAIN ANALYZE is not supported for DuckDB CTAS (yet)
    with pytest.raises(
        psycopg.errors.InternalError,
        match="Not implemented Error: Cannot use EXPLAIN ANALYZE with CREATE TABLE ... AS when using DuckDB execution",
    ):
        cur.sql(
            "EXPLAIN ANALYZE CREATE TEMP TABLE duckdb2(id) USING duckdb AS SELECT * from heap1"
        )
