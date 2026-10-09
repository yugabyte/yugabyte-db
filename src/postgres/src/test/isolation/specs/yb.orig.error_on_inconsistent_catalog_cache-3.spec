# The Java test (TestPgRegressDDLIsolationNoTxnDDLNoObjectLocking#testWithHighHeartbeatDelay)
# restarts the cluster with heartbeat_interval_ms=2000, so give this spec's async DDL
# verification and catalog version propagation that much time to settle before the next
# spec in the schedule starts, so it doesn't inherit a stale catalog snapshot from this one.
teardown
{
  SELECT pg_sleep(3);
}

session s1
step s1_create_table { create table parent2(c1 int); }
step s1_alter_table_add_col { alter table parent2 add column c2 int; }

session s2
step s2_select_parent { SELECT * from parent2; }
step s2_create_child_table_via_inheritance { create table child2(chc1 int) inherits(parent2); }

session s3
step s3_select_child { SELECT * from child2; }

permutation s1_create_table(yb_never_waits) s2_select_parent s1_alter_table_add_col s2_create_child_table_via_inheritance s3_select_child
