# Test to ensure that user table reads done as part of a DDL statement use the transaction
# snapshot.

setup
{
  CREATE TABLE test (k int primary key, v int);
  INSERT INTO test VALUES (1, 1);
}

teardown
{
  DROP TABLE IF EXISTS test CASCADE;
}

session s1
step s1_begin_rr { BEGIN ISOLATION LEVEL REPEATABLE READ; }
step s1_select { SELECT * FROM test; }
step s1_sleep { SELECT pg_sleep(1); -- to avoid a read restart error }
step s1_create_materialized_view { CREATE MATERIALIZED VIEW test_v AS SELECT * FROM test; }
step s1_select_view { SELECT * FROM test_v; }
step s1_commit { COMMIT; }

session s2
step s2_insert { INSERT INTO test VALUES (2, 2); }

permutation s1_begin_rr s1_select s1_sleep s2_insert s1_create_materialized_view s1_commit s1_select_view
