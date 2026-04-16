setup
{
  CREATE TABLE test (k int primary key, v int);
  CREATE TABLE test2 (k int primary key, v int);
  INSERT INTO test SELECT i, i FROM generate_series(1, 10) AS i;
  INSERT INTO test2 SELECT i, i FROM generate_series(1, 10) AS i;
}

teardown
{
  DROP TABLE IF EXISTS test;
  DROP TABLE IF EXISTS test2;
}

session s1
step s1_begin_rc { BEGIN ISOLATION LEVEL READ COMMITTED; }
step s1_begin_rr { BEGIN ISOLATION LEVEL REPEATABLE READ; }
step s1_begin_sr { BEGIN ISOLATION LEVEL SERIALIZABLE; }
step s1_drop_table_test { DROP TABLE test; }
step s1_alter_table_test_add_col_a { ALTER TABLE test ADD COLUMN a INT; }
step s1_commit { COMMIT; }
step s1_rollback { ROLLBACK; }

session s2
step s2_begin_rc { BEGIN ISOLATION LEVEL READ COMMITTED; }
step s2_begin_rr { BEGIN ISOLATION LEVEL REPEATABLE READ; }
step s2_begin_sr { BEGIN ISOLATION LEVEL SERIALIZABLE; }
step s2_drop_table_test { DROP TABLE test; }
step s2_drop_table_test_if_exists { DROP TABLE IF EXISTS test; }
step s2_drop_table_test2 { DROP TABLE test2; }
step s2_alter_table_test_add_col_b { ALTER TABLE test ADD COLUMN b INT; }
step s2_alter_table_test_add_col_a { ALTER TABLE test ADD COLUMN a INT; }
step s2_alter_table_test2_add_col_a { ALTER TABLE test2 ADD COLUMN a INT; }
step s2_commit { COMMIT; }
step s2_select_test_atts { SELECT attname FROM pg_attribute WHERE attrelid = 'test'::regclass::oid and attnum > 0; }

# Concurrent DDLs that work on the different objects.
permutation s1_begin_rr s2_begin_rr s1_drop_table_test s2_drop_table_test2 s1_commit s2_commit
permutation s1_begin_rr s2_begin_rr s1_alter_table_test_add_col_a s2_alter_table_test2_add_col_a s1_commit s2_commit

# Concurrent DDLs that work on the same object.
permutation s1_begin_rr s2_begin_rr s1_alter_table_test_add_col_a s2_alter_table_test_add_col_b s1_commit s2_commit s2_select_test_atts
permutation s1_begin_rr s2_begin_rr s1_alter_table_test_add_col_a s2_alter_table_test_add_col_a s1_commit s2_commit s2_select_test_atts
permutation s1_begin_rr s2_begin_rr s1_alter_table_test_add_col_a s2_alter_table_test_add_col_b s1_rollback s2_commit s2_select_test_atts

# TODO: The following test cases give different error messages, fix it and enable the test cases.
# permutation s1_begin_rr s2_begin_rr s1_drop_table_test s2_drop_table_test s1_commit s2_commit
# permutation s1_begin_rr s2_begin_rr s1_drop_table_test s2_drop_table_test_if_exists s1_commit s2_commit
