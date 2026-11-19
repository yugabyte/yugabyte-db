# Catalog reads of a SERIALIZABLE transaction are snapshot reads that leave no read intents
# (#34631). Object locks still order DDL against DML on the same table.

setup
{
  CREATE TABLE test (k int primary key, v int);
  INSERT INTO test SELECT i, i FROM generate_series(1, 10) AS i;
}

teardown
{
  DROP TABLE IF EXISTS test;
  DROP FUNCTION IF EXISTS f;
}

session s1
step s1_begin_sr { BEGIN ISOLATION LEVEL SERIALIZABLE; }
step s1_select_now { SELECT now() IS NOT NULL; }
step s1_select_test { SELECT * FROM test WHERE k = 1; }
step s1_alter_table_test_add_col_a { ALTER TABLE test ADD COLUMN a INT; }
step s1_commit { COMMIT; }

session s2
step s2_create_function { CREATE FUNCTION f() RETURNS int LANGUAGE sql AS 'SELECT 1'; }
step s2_alter_table_test_add_col_b { ALTER TABLE test ADD COLUMN b INT; }
step s2_select_test_atts { SELECT attname FROM pg_attribute WHERE attrelid = 'test'::regclass::oid and attnum > 0; }

# Catalog reads don't conflict with a concurrent DDL.
permutation s1_begin_sr s1_select_now s2_create_function s1_select_test s1_commit
# A DDL reads its own catalog writes.
permutation s1_begin_sr s1_alter_table_test_add_col_a s1_select_test s1_commit
# Object locks still order DML against DDL on the same table: the ALTER waits for s1 to commit.
permutation s1_begin_sr s1_select_test s2_alter_table_test_add_col_b s1_select_test s1_commit s2_select_test_atts
