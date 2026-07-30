import os

import psycopg.errors
import pytest

from .motherduck_token_helper import create_test_user
from .utils import Duckdb, Postgres, make_new_duckdb_connection


@pytest.fixture(scope="session")
def shared_pg(tmp_path_factory):
    """Starts a new Postgres db that is shared for tests in this process"""
    pg = Postgres(tmp_path_factory.getbasetemp() / "pgdata")
    pg.initdb()

    pg.start()
    pg.sql("CREATE ROLE duckdb_group")

    yield pg

    pg.cleanup()


@pytest.fixture
def default_db_name(request):
    """Returns the name of the database used by the test"""
    yield request.node.name.removeprefix("test_")


def init_pg(pgobj, **kwargs):
    pgobj.sql("GRANT CREATE ON SCHEMA public TO duckdb_group", **kwargs)
    pgobj.sql("CREATE EXTENSION pg_duckdb", **kwargs)


def teardown_pg(pgobj, **kwargs):
    pgobj.sql("REVOKE ALL PRIVILEGES ON SCHEMA public FROM duckdb_group", **kwargs)
    pgobj.sql("DROP EXTENSION pg_duckdb CASCADE", **kwargs)


@pytest.fixture(scope="session")
def initialized_shared_pg(shared_pg):
    init_pg(shared_pg)
    yield shared_pg


@pytest.fixture
def pg(initialized_shared_pg, default_db_name):
    """
    Wraps the shared_pg fixture to reset the db after each test.

    It also creates a schema for the test to use. And logs the pg log to stdout
    for debugging failures.
    """
    shared_pg = initialized_shared_pg
    shared_pg.reset()

    with shared_pg.log_path.open() as f:
        f.seek(0, os.SEEK_END)
        try:
            shared_pg.create_schema(default_db_name)
            shared_pg.search_path = f"{default_db_name}, public"
            yield shared_pg
        finally:
            try:
                shared_pg.cleanup_test_leftovers()
            finally:
                print(f"\n\nPG_LOG {shared_pg.log_path}\n")
                logs = f.read()
                print(logs)
                # Usually when Postgres crashes we quickly notice due to a
                # query failing. Sometimes though it restarts fast enough that
                # there's no indictaion of the crash except for the logs. So
                # here we check that postgres did not crash during the last
                # test.
                assert "was terminated by signal 6" not in logs, (
                    "Postgres crashed! Check the logs above."
                )
                assert "was terminated by signal 11" not in logs, (
                    "Postgres crashed! Check the logs above."
                )


@pytest.fixture
def pg_two_dbs(shared_pg):
    """A cursor to a pg_duckdb enabled postgres"""

    for dbname in ["test_pg_db_1", "test_pg_db_2"]:
        try:
            shared_pg.dropdb(dbname)
        except psycopg.errors.InvalidCatalogName:
            pass  # ignore if it doesn't exist
        shared_pg.createdb(dbname)
        init_pg(shared_pg, dbname=dbname)

    shared_pg.reset()

    with shared_pg.log_path.open() as f:
        f.seek(0, os.SEEK_END)
        try:
            with shared_pg.cur(dbname="test_pg_db_1") as cur1:
                with shared_pg.cur(dbname="test_pg_db_2") as cur2:
                    assert cur1.sql(" SELECT current_database();") == "test_pg_db_1"
                    assert cur2.sql(" SELECT current_database();") == "test_pg_db_2"
                    yield cur1, cur2
        finally:
            try:
                for dbname in ["test_pg_db_1", "test_pg_db_2"]:
                    teardown_pg(shared_pg, dbname=dbname)
            finally:
                print("\n\nPG_LOG\n")
                print(f.read())

    shared_pg.cleanup_test_leftovers()


@pytest.fixture
def cur(pg):
    with pg.cur() as cur:
        yield cur


@pytest.fixture
def conn(pg):
    with pg.conn() as conn:
        yield conn


@pytest.fixture(scope="session")
def md_test_user():
    """Returns the test user token for MotherDuck.

    This makes sure that it's the same in all the places we use it
    """
    return create_test_user()


@pytest.fixture
def md_cur(pg, default_db_name, ddb, md_test_user):
    """A cursor to a MotherDuck enabled pg_duckdb"""
    # We don't actually need to use ddb connection, but we include the
    # fixture to make sure that the test database for the test is
    # dropped+created
    _ = ddb

    pg.sql(f"CALL duckdb.enable_motherduck('{md_test_user['token']}')")

    pg.search_path = f"ddb${default_db_name}, public"
    with pg.cur() as cur:
        cur.wait_until_schema_exists(f"ddb${default_db_name}", timeout=60)
        yield cur


@pytest.fixture
def ddb(default_db_name, md_test_user):
    """A DuckDB connection to MotherDuck

    This also creates a database for the test to use.
    """
    ddb_con = make_new_duckdb_connection(default_db_name, md_test_user["token"])

    try:
        yield Duckdb(ddb_con)
    finally:
        ddb_con.execute("USE my_db")
        ddb_con.execute(f"DROP DATABASE {default_db_name}")
