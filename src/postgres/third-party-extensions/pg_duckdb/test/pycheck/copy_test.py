import json
from pathlib import Path

import psycopg.errors
import pytest

from .utils import Cursor


def test_copy_to_local(cur: Cursor, tmp_path: Path):
    # Create a sample table and insert data
    cur.sql("CREATE TEMP TABLE test_table (id INT, name TEXT)")
    cur.sql("INSERT INTO test_table (id, name) VALUES (1, 'Alice'), (2, 'Bob')")

    # Define the local file path
    csv_path = tmp_path / "test_copy.csv"

    # Use COPY command to copy data to a local file
    cur.sql(f"COPY test_table TO '{csv_path}' WITH (FORMAT CSV)")

    # Verify the local file content
    with open(csv_path, "r") as file:
        content = file.read()
        expected_content = "id,name\n1,Alice\n2,Bob\n"
        assert content == expected_content, (
            f"Expected: {expected_content}, but got: {content}"
        )

    # The above was using duckdb exection because duckdb.force_execution is
    # true by default in our tests. We can validate that by looking at the
    # error message.
    with pytest.raises(
        psycopg.errors.InternalError,
        match='Not implemented Error: Unrecognized option "unknown_option" for csv',
    ):
        cur.sql(
            f"COPY test_table TO '{csv_path}' WITH (FORMAT CSV, UNKNOWN_OPTION true)"
        )

    # copying to relative paths is not allowed though, in accordance with
    # Postgres behaviour to avoid overwriting datababase file accidentally.
    with pytest.raises(
        psycopg.errors.InvalidName, match="relative path not allowed for COPY to file"
    ):
        cur.sql("COPY test_table TO 'test_copy.csv' WITH (FORMAT CSV)")

    # Disabling duckdb.force_execution makes the query fail with a different
    # error.
    cur.sql("SET duckdb.force_execution = false")
    with pytest.raises(
        psycopg.errors.SyntaxError,
        match='option "unknown_option" not recognized',
    ):
        cur.sql(
            f"COPY test_table TO '{csv_path}' WITH (FORMAT CSV, UNKNOWN_OPTION true)"
        )

    parquet_path = tmp_path / "test_copy.parquet"

    # Use COPY command to copy data to a local file
    cur.sql(f"COPY test_table TO '{parquet_path}' WITH (FORMAT PARQUET)")
    assert cur.sql(f"select * from read_parquet('{parquet_path}')") == [
        (1, "Alice"),
        (2, "Bob"),
    ]

    # Again relative paths are not allowed for COPY TO, this becomes an
    # internal error though, due to our failure to propagate error codes
    # correctly.
    with pytest.raises(
        psycopg.errors.InternalError, match="relative path not allowed for COPY to file"
    ):
        cur.sql("COPY test_table TO 'test_copy.parquet' WITH (FORMAT PARQUET)")

    # We can copy the result of a DuckDB query using Postgres its COPY logic
    cur.sql(
        f"COPY (select * from duckdb.query($$ select 123 as xyz $$)) TO '{csv_path}'"
    )
    assert cur.sql(f"select * from read_csv('{csv_path}')") == 123

    # And we can do the same with the duckdb writer
    cur.sql(
        f"COPY (select * from duckdb.query($$ select 123 as xyz $$)) TO '{parquet_path}' (FORMAT PARQUET)"
    )
    assert cur.sql(f"select * from read_parquet('{parquet_path}')") == 123

    # We don't even need to specify the format, just the file extension will work
    cur.sql(
        f"COPY (select * from duckdb.query($$ select 123 as xyz $$)) TO '{parquet_path}'"
    )
    assert cur.sql(f"select * from read_parquet('{parquet_path}')") == 123

    # We don't even need to specify the format, just the file extension will work
    cur.sql(
        f"COPY (select * from duckdb.query($$ select 123 as xyz $$)) TO '{parquet_path}'"
    )
    assert cur.sql(f"select * from read_parquet('{parquet_path}')") == 123

    json_path = tmp_path / "test_copy.jsonl"
    cur.sql(
        f"COPY (select * from duckdb.query($$ select 123 as xyz $$)) TO '{json_path}'"
    )
    assert cur.sql(f"select * from read_json('{json_path}')") == 123
    assert json.loads(json_path.read_text()) == {"xyz": 123}

    json_path = tmp_path / "test_copy.json"
    cur.sql(
        f"COPY (select * from duckdb.query($$ select 123 as xyz $$)) TO '{json_path}' (ARRAY true)"
    )
    assert cur.sql(f"select * from read_json('{json_path}')") == 123
    assert json.loads(json_path.read_text()) == [{"xyz": 123}]

    # We cannot read postgres system catalogs though
    with pytest.raises(
        psycopg.errors.InternalError,
        match="DuckDB does not support querying PG catalog tables",
    ):
        cur.sql(f"COPY (SELECT * FROM pg_proc) TO '{json_path}'")

    # Also not directly
    with pytest.raises(
        psycopg.errors.InternalError,
        match="DuckDB does not support querying PG catalog tables",
    ):
        cur.sql(f"COPY pg_proc TO '{json_path}'")


def test_copy_to_stdout(cur: Cursor):
    # We can COPY the result from a DuckDB query to STDOUT
    with cur.copy(
        "COPY (select * from duckdb.query($$ select 123 as xyz $$)) TO STDOUT"
    ) as copy:
        for row in copy.rows():
            assert row == ("123",)

    # ... but only in formats that Postgres natively supports
    with pytest.raises(
        psycopg.errors.InternalError,
        match="COPY ... TO STDOUT/FROM STDIN is not supported by DuckDB",
    ):
        with cur.copy(
            "COPY (select * from duckdb.query($$ select 123 as xyz $$)) TO STDOUT (FORMAT PARQUET)"
        ) as copy:
            for row in copy.rows():
                assert row == ("123",)

    cur.sql("CREATE TEMP TABLE pg_table (id INT, name TEXT)")
    cur.sql("INSERT INTO pg_table (id, name) VALUES (1, 'Alice'), (2, 'Bob')")

    # We can COPY the result from a postgres table to STDOUT (i.e. we don't
    # break normal Postgres functionality)
    with cur.copy("COPY pg_table TO STDOUT") as copy:
        rows = list(copy.rows())
        assert rows == [("1", "Alice"), ("2", "Bob")]

    # ... but only in formats that Postgres natively supports
    with pytest.raises(
        psycopg.errors.InternalError,
        match="COPY ... TO STDOUT/FROM STDIN is not supported by DuckDB",
    ):
        with cur.sql("COPY pg_table TO STDOUT (FORMAT PARQUET)"):
            pass

    cur.sql("CREATE TEMP TABLE duck_table (id INT, name TEXT) USING duckdb")
    cur.sql("INSERT INTO duck_table (id, name) VALUES (1, 'Alice'), (2, 'Bob')")

    # We can COPY a DuckDB table to STDOUT
    with cur.copy("COPY duck_table TO STDOUT") as copy:
        rows = list(copy.rows())
        assert rows == [("1", "Alice"), ("2", "Bob")]

    # ... but only in formats that Postgres natively supports
    with pytest.raises(
        psycopg.errors.InternalError,
        match="COPY ... TO STDOUT/FROM STDIN is not supported by DuckDB",
    ):
        with cur.copy("COPY duck_table TO STDOUT (FORMAT PARQUET)") as copy:
            pass


def test_copy_from_local(cur: Cursor, tmp_path: Path):
    cur.sql("CREATE TEMP TABLE pg_table (id INT, name TEXT)")
    cur.sql("INSERT INTO pg_table (id, name) VALUES (1, 'Alice'), (2, 'Bob')")

    # Create two files that we can load data from
    csv_path = tmp_path / "test_copy.csv"
    parquet_path = tmp_path / "test_copy.parquet"
    cur.sql(f"COPY pg_table TO '{csv_path}' WITH (FORMAT CSV)")
    cur.sql(f"COPY pg_table TO '{parquet_path}' WITH (FORMAT PARQUET)")
    cur.sql("TRUNCATE pg_table")

    cur.sql("CREATE TEMP TABLE duck_table (id INT, name TEXT) USING duckdb")

    # We can copy into a DuckDB table from a local csv file
    cur.sql(f"COPY duck_table FROM '{csv_path}'")
    assert cur.sql("SELECT * FROM duck_table") == [(1, "Alice"), (2, "Bob")]
    cur.sql("TRUNCATE duck_table")

    # We can copy into a DuckDB table from a local csv file
    cur.sql(f"COPY duck_table FROM '{parquet_path}'")
    assert cur.sql("SELECT * FROM duck_table") == [(1, "Alice"), (2, "Bob")]
    cur.sql("TRUNCATE duck_table")

    # We can copy into a Postgres table from a local csv file
    cur.sql(f"COPY pg_table FROM '{csv_path}' WITH (FORMAT CSV, HEADER true)")
    assert cur.sql("SELECT * FROM pg_table") == [(1, "Alice"), (2, "Bob")]
    cur.sql("TRUNCATE pg_table")

    # ... but parquet files are not supported
    with pytest.raises(
        psycopg.errors.InternalError,
        match="pg_duckdb does not support COPY ... FROM ... yet for Postgres tables",
    ):
        cur.sql(f"COPY pg_table FROM '{parquet_path}' WITH (FORMAT PARQUET)")

    # Non-SELECT queries fall back to the Postgres COPY logic if we're not
    # using DuckDB features.
    cur.sql(f"COPY (INSERT INTO pg_table VALUES (42) RETURNING (id)) TO '{csv_path}'")
    assert cur.sql(f"select * from read_csv('{csv_path}')") == 42
    cur.sql(f"COPY (INSERT INTO duck_table VALUES (43) RETURNING (id)) TO '{csv_path}'")
    assert cur.sql(f"select * from read_csv('{csv_path}')") == 43

    # If that's not possible (e.g. due to parquet), then we throw a clear error
    with pytest.raises(
        psycopg.errors.InternalError,
        match="DuckDB does not support modififying Postgres tables",
    ):
        cur.sql(
            f"COPY (INSERT INTO pg_table VALUES (1) RETURNING (id)) TO '{parquet_path}'"
        )

    with pytest.raises(
        psycopg.errors.InternalError,
        match="DuckDB COPY only supports SELECT statement",
    ):
        cur.sql(
            f"COPY (INSERT INTO duck_table VALUES (1) RETURNING (id)) TO '{parquet_path}'"
        )
