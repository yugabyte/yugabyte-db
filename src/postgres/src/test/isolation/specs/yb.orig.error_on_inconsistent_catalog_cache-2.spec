# The Java test (TestPgRegressDDLIsolationNoTxnDDLNoObjectLocking#testWithHighHeartbeatDelay)
# restarts the cluster with heartbeat_interval_ms=2000, so give this spec's async DDL
# verification and catalog version propagation that much time to settle before the next
# spec in the schedule starts, so it doesn't inherit a stale catalog snapshot from this one.
teardown
{
  SELECT pg_sleep(3);
}

session s1
step s1_create_table { create table parent(c1 int) partition by range(c1); }
step s1_alter_table_add_col { alter table parent add column c2 int; }

session s2
step s2_select_parent { SELECT * from parent; }
step s2_create_child_table_via_partioning { create table child partition of parent default; }

session s3
step s3_select_child { SELECT * from child; }

permutation s1_create_table s2_select_parent s1_alter_table_add_col s2_create_child_table_via_partioning s3_select_child
