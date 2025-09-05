# In YSQL, without commit 9891ccbf, we would not process invalidation messages after acquiring table
# locks. This can result in the usage of a catalog cache which has some (or) all stale data during
# statement execution. This can in turn have bad outcomes. Note that having some but not all stale
# data also means that the view of the catalog is inconsistent.
#
# This spec tests one such scenario:
#
# Consider a table with 2 columns.
# (1) A session S1 alters a table to add a column with att num 3.
# (2) Another session S2 tries to concurrently alter the type of an existing column. It blocks
#     on S1 to acquire a table lock.
# (2) After S1 commits, S2 acquires a lock on the table. S2's cache still has the old value of 2 for
#     relnatts in pg_class since it has not been invalidated after lock acquisition.
#
#     When modifying some data for the table in pg_class, S2 writes the whole row again, resulting
#     in an overwrite to relnatts to 2 again. This results in a catalog corruption where relnatts is
#     2 but there are 3 columns in the table.
#
# Post this, any backend that fetches the full catalog will see the following error:
# ERROR:  invalid attribute number 3 for test

setup
{
  CREATE TABLE test (k int primary key, v int);
  INSERT INTO test SELECT i, i FROM generate_series(1, 10) AS i;
}

teardown
{
  DROP TABLE IF EXISTS test;
}

session s1
step s1_begin_rr { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step s1_alter_table_test_add_col_a { ALTER TABLE test ADD COLUMN a INT; }
step s1_commit { COMMIT; }

session s2
step s2_begin_rr { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step s2_alter_table_test_alter_col_v { ALTER TABLE test ALTER COLUMN v TYPE SMALLINT; }
step s2_alter_table_test_alter_col_a { ALTER TABLE test ALTER COLUMN a TYPE SMALLINT; }
step s2_commit { COMMIT; }

session s3
step s3_select_test_atts { SELECT attname FROM pg_attribute WHERE attrelid = 'test'::regclass::oid and attnum > 0; }

# If invalidation messages are not processed after lock acquisition in S2, an inconsistent catalog
# cache would be used to successfully execute the DDL. Hence, we would see 2 columns in the select
# done by S3 because relnatts would be overwritten to 2.

permutation s1_begin_rr s2_begin_rr s1_alter_table_test_add_col_a s2_alter_table_test_alter_col_v s1_commit s2_commit s3_select_test_atts
# Test ALTER outside a transaction block in S2.
permutation s1_begin_rr s1_alter_table_test_add_col_a s2_alter_table_test_alter_col_v s1_commit s3_select_test_atts

# Tests with an alter of the newly added column.
permutation s1_begin_rr s2_begin_rr s1_alter_table_test_add_col_a s2_alter_table_test_alter_col_a s1_commit s2_commit s3_select_test_atts
permutation s1_begin_rr s1_alter_table_test_add_col_a s2_alter_table_test_alter_col_a s1_commit s3_select_test_atts
