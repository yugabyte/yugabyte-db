# The Java test (TestPgRegressDDLIsolationNoTxnDDLNoObjectLocking#testWithHighHeartbeatDelay)
# restarts the cluster with heartbeat_interval_ms=2000, so give this spec's async DDL
# verification and catalog version propagation that much time to settle before the next
# spec in the schedule starts, so it doesn't inherit a stale catalog snapshot from this one.
teardown
{
  SELECT pg_sleep(3);
}

session s1
step s1_create_table { create table part1(id int) partition by range(id); }
step s1_create_default_partition { create table part1def partition of part1 default; }

session s2
step s2_select_table { select * from part1; }
step s2_create_another_default_partition { create table part2def partition of part1 default; }

session s3
step s3_select_table { SELECT * from part1; }

permutation s1_create_table s2_select_table s1_create_default_partition s2_create_another_default_partition s3_select_table
