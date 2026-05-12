# Test to ensure that DMLs done after a DDL statement in a transaction use the transaction snapshot.
setup
{
  CREATE TABLE test (k int, v int);
  CREATE TABLE test2 (k int primary key, v int);
}

teardown
{
  DROP TABLE IF EXISTS test;
}

session s1
step s1_begin_rr { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step s1_drop_table_test2 { DROP TABLE test2; }
step s1_select { SELECT * FROM test; }
step s1_commit { COMMIT; }

session s2
step s2_insert { INSERT INTO test VALUES (1, 1); }

# TODO(#29328): Enable the test permutation with the 2nd s1_select
# permutation s1_begin_rr s1_select s2_insert s1_drop_table_test2 s1_select s1_commit
permutation s1_begin_rr s1_select s2_insert s1_drop_table_test2 s1_commit
