# This test ensures that ANALYZE on system catalog tables (like pg_yb_invalidation_messages)
# does not cause a deadlock with concurrent DDL operations.

setup
{
  CREATE TABLE test (k INT PRIMARY KEY, v INT);
  INSERT INTO test SELECT i, i FROM generate_series(1, 10) AS i;
}

teardown
{
  DROP TABLE IF EXISTS test;
}

session s1
step s1_begin { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step s1_alter_table_add_col_a { ALTER TABLE test ADD COLUMN a INT; }
step s1_alter_table_add_col_b { ALTER TABLE test ADD COLUMN b INT; }
step s1_alter_table_add_col_c { ALTER TABLE test ADD COLUMN c INT; }
step s1_commit { COMMIT; }

session s2
step s2_begin { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step s2_analyze_inval { ANALYZE pg_catalog.pg_yb_invalidation_messages; }
step s2_analyze_cat_version { ANALYZE pg_catalog.pg_yb_catalog_version; }
step s2_commit { COMMIT; }

permutation s1_begin s2_begin s1_alter_table_add_col_a s2_analyze_inval s1_commit s2_commit

permutation s1_begin s2_begin s1_alter_table_add_col_b s2_analyze_cat_version s1_commit s2_commit

permutation s1_begin s2_begin s2_analyze_cat_version s1_alter_table_add_col_c s1_commit s2_commit
