import datetime
import uuid

import psycopg.types.json
import pytest

from .utils import Connection, Cursor


def test_prepared(cur: Cursor):
    cur.sql("CREATE TABLE test_table (id int)")

    # Try prepared query without parameters
    q1 = "SELECT count(*) FROM test_table"
    assert cur.sql(q1, prepare=True) == 0
    assert cur.sql(q1) == 0
    assert cur.sql(q1) == 0

    cur.sql("INSERT INTO test_table VALUES (1), (2), (3)")
    assert cur.sql(q1) == 3

    # The following tests a prepared query that has parameters.
    # There are two ways in which prepared queries that have parameters can be
    # executed:
    # 1. With a custom plan, where the query is prepared with the exact values
    # 2. With a generic plan, where the query is planned without the values and
    #    the values get only substituted at execution time
    #
    # The below tests both of these cases, by setting the plan_cache_mode.
    q2 = "SELECT count(*) FROM test_table where id = %s"
    cur.sql("SET plan_cache_mode = 'force_custom_plan'")
    assert cur.sql(q2, (1,), prepare=True) == 1
    assert cur.sql(q2, (1,)) == 1
    assert cur.sql(q2, (1,)) == 1
    assert cur.sql(q2, (3,)) == 1
    assert cur.sql(q2, (4,)) == 0

    cur.sql("SET plan_cache_mode = 'force_generic_plan'")
    assert cur.sql(q2, (1,)) == 1  # creates generic plan
    assert cur.sql(q2, (1,)) == 1
    assert cur.sql(q2, (3,)) == 1
    assert cur.sql(q2, (4,)) == 0


def test_extended(cur: Cursor):
    cur.sql("""
        CREATE TABLE t(
            bool BOOLEAN,
            i2 SMALLINT,
            i4 INT,
            i8 BIGINT,
            fl4 REAL,
            fl8 DOUBLE PRECISION,
            t1 TEXT,
            t2 VARCHAR,
            t3 BPCHAR,
            ivl INTERVAL,
            time TIME,
            timetz TIMETZ,
            d DATE,
            ts TIMESTAMP,
            tstz TIMESTAMP WITH TIME ZONE,
            json_obj JSON,
            u UUID);
        """)

    row = (
        True,
        2,
        4,
        8,
        4.0,
        8.0,
        "t1",
        "t2",
        "t3",
        datetime.timedelta(days=5, hours=3, minutes=30),
        datetime.time(1, 2, 3),
        datetime.time(1, 2, 3, tzinfo=datetime.timezone(datetime.timedelta(hours=-5))),
        datetime.date(2024, 5, 4),
        datetime.datetime(2020, 1, 1, 1, 2, 3),
        datetime.datetime(2020, 1, 1, 1, 2, 3, tzinfo=datetime.timezone.utc),
        psycopg.types.json.Json({"a": 1}),
        uuid.UUID("12345678-1234-5678-1234-567812345678"),
    )
    cur.sql(
        "INSERT INTO t VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s)",
        row,
    )

    assert (True,) * len(row) == cur.sql(
        """
        SELECT
            bool = %s,
            i2 = %s,
            i4 = %s,
            i8 = %s,
            fl4 = %s,
            fl8 = %s,
            t1 = %s,
            t2 = %s,
            t3 = %s,
            ivl = %s,
            time = %s,
            timetz = %s,
            d = %s,
            ts = %s,
            tstz = %s,
            json_obj::text = %s::text,
            u = %s
        FROM t;
        """,
        row,
    )


def test_prepared_writes(cur: Cursor):
    cur.sql("CREATE TEMP TABLE test_table (id int)")
    cur.sql("INSERT INTO test_table VALUES (%s), (%s), (%s)", (1, 2, 3))
    assert cur.sql("SELECT * FROM test_table ORDER BY id") == [1, 2, 3]


def test_prepared_pipeline(conn: Connection):
    with conn.pipeline() as p, conn.cursor() as cur:
        cur = Cursor(cur)
        cur.execute("CREATE TEMP TABLE heapt (id int)")
        p.sync()
        cur.execute("CREATE TEMP TABLE duckt (id int) using duckdb")
        p.sync()

        # These all auto-commit, so they complete their pipeline immediately
        # and should succeed
        cur.execute("INSERT INTO duckt VALUES (%s), (%s), (%s)", (1, 2, 3))
        cur.execute("DELETE FROM duckt WHERE id = %s", (2,))
        cur.execute("INSERT INTO duckt VALUES (%s)", (4,))
        assert cur.sql("SELECT * FROM duckt ORDER BY id") == [1, 3, 4]
        p.sync()

        # But if we first insert into the heap table, then try to insert into
        # the duckdb table that should fail because the insert into the heap
        # table opens an implicit transaction.
        with pytest.raises(
            psycopg.errors.InternalError,
            match="Writing to DuckDB and Postgres tables in the same transaction block is not supported",
        ):
            cur.execute("INSERT INTO heapt VALUES (%s), (%s), (%s)", (1, 2, 3))
            cur.execute("INSERT INTO duckt VALUES (%s)", (5,))
            p.sync()
        assert cur.sql("SELECT * FROM duckt ORDER BY id") == [1, 3, 4]
        assert cur.sql("SELECT * FROM heapt ORDER BY id") == []


def test_prepared_ctas(cur: Cursor):
    cur.sql("CREATE TABLE heapt (id int, number int)")
    cur.sql("INSERT INTO heapt VALUES (1, 2), (2, 4), (3, 6)")
    cur.sql("CREATE TEMP TABLE t USING duckdb AS SELECT * FROM heapt")
    assert cur.sql("SELECT * FROM t ORDER BY id") == [(1, 2), (2, 4), (3, 6)]

    # We don't support CTAS with parameters yet. The error message and code
    # could be better, but this is what we have right now. At least we don't
    # crash.
    with pytest.raises(
        psycopg.errors.InternalError,
        match="Could not find parameter with identifier 1",
    ):
        cur.sql(
            "CREATE TEMP TABLE t2 USING duckdb AS SELECT * FROM heapt where id = %s",
            (2,),
        )

    prepared_query = "CREATE TEMP TABLE t3 USING duckdb AS SELECT * FROM heapt"
    cur.sql(prepared_query, prepare=True)
    assert cur.sql("SELECT count(*) FROM t3") == 3
    cur.sql("DROP TABLE t3")
    cur.sql(prepared_query)
    assert cur.sql("SELECT count(*) FROM t3") == 3


def test_prepared_change_type(cur: Cursor, tmp_path):
    tmp_path = tmp_path / "test.csv"
    tmp_path.write_text("123\n")
    prepared_query = f"SELECT * FROM read_csv('{tmp_path}')"
    cur.sql(prepared_query, prepare=True)

    tmp_path.write_text("abc\n")
    with pytest.raises(
        psycopg.errors.InternalError,
        match="Types returned by duckdb query changed between planning and execution",
    ):
        cur.sql(prepared_query)

    tmp_path.write_text("1,234\n")
    with pytest.raises(
        psycopg.errors.InternalError,
        match="Number of columns returned by DuckDB query changed between planning and execution",
    ):
        cur.sql(prepared_query)
