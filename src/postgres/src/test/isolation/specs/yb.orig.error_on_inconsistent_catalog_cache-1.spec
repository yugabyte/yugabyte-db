# In YSQL, without object locking, we can have scenarios where a stale catalog cache is used
# during statement execution in PG backends. This can have bad outcomes. One such outcome is a
# corrupted catalog which happens as follows:
#
# Consider a table with 2 columns.
# (1) S1 alters a table to add a column with att num 3.
# (2) After S1 commits, but before the heartbeat arrives to the backend of S2, S2 executes an
#     alter type on the newly added column. Since this attribute is not in cache, S2 fetches this
#     info from master. S2's cache still has the old value of 2 for relnatts in pg_class. When
#     modifying some data for the table in pg_class, S2 writes the whole row again, resulting in a
#     overwrite to relnatts to 2 again. This results in a catalog corruption where relnatts is 2
#     but there are 3 columns in the table.
#
# Post this, any backend that fetches the full catalog will see the following error:
# ERROR:  invalid attribute number 3 for test
#
# This test is part of a schedule that runs with high heartbeat delays to create the required
# conditions for the issue to occur.

# The Java test (TestPgRegressDDLIsolationNoTxnDDLNoObjectLocking#testWithHighHeartbeatDelay)
# restarts the cluster with heartbeat_interval_ms=2000, so give this spec's async DDL
# verification and catalog version propagation that much time to settle before the next
# spec in the schedule starts, so it doesn't inherit a stale catalog snapshot from this one.
teardown
{
  SELECT pg_sleep(3);
}

session s1
step s1_create_table_test { CREATE TABLE test (k int primary key, v int); }
step s1_alter_table_test_add_col_a { ALTER TABLE test ADD COLUMN a INT; }

session s2
step s2_select_test { SELECT * from test; }
step s2_alter_table_test_alter_col_a { ALTER TABLE test ALTER COLUMN v TYPE SMALLINT; }

session s3
step s3_select_test { SELECT * from test; }

# The SELECT in S2 is required to load the pg_class entry for the table before relnatts is
# increased to 3 by S1.
#
# If an inconsistent catalog cache is used to successfully execute the DDL in S2 (which was the previous
# buggy behaviour), we would see 2 columns in the select done by S3. This is because S2 would overwrite
# the pg_class row and hence the old cached value of relnatts 2 replaces 3.
permutation s1_create_table_test(yb_never_waits) s2_select_test s1_alter_table_test_add_col_a s2_alter_table_test_alter_col_a s3_select_test
