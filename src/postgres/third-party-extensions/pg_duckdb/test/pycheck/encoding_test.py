import psycopg
import pytest

from .utils import Cursor, Postgres


def test_non_utf8_encoding(pg: Postgres, cur: Cursor):
    cur.sql("CREATE DATABASE latin1_test ENCODING SQL_ASCII TEMPLATE=template0")
    with pg.cur(dbname="latin1_test") as latin1_cur:
        with pytest.raises(
            psycopg.errors.RaiseException,
            match="pg_duckdb can only be installed in a Postgres database with UTF8 encoding, this one is encoded using SQL_ASCII.",
        ):
            latin1_cur.sql("CREATE EXTENSION pg_duckdb")
    cur.sql("DROP DATABASE latin1_test")
