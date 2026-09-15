# In YSQL, without object locking, we can have scenarios where a stale catalog cache is used
# during statement execution in PG backends. This can have bad outcomes. One such outcome is a
# corrupted catalog which happens as follows:
#
# Consider a partitioned table prnt1 with partition chld1, and a second partitioned table
# prnt2.
# (1) S1 loads chld1's pg_inherits entry into its cache (an INSERT into chld1 triggers a
#     partition-constraint check that populates the by-child, i.e. inhrelid, pg_inherits cache
#     entry; a plain SELECT does not).
# (2) Before the heartbeat arrives to S1's backend, S2 moves chld1 from prnt1 to prnt2:
#     DETACH PARTITION chld1 from prnt1, then ATTACH PARTITION chld1 to prnt2.
# (3) S1, still relying on its stale cache, tries to DETACH chld1 from prnt1 again. Since
#     chld1 is no longer a partition of prnt1, this should fail with "chld1 is not a
#     partition of prnt1". If an inconsistent catalog cache is used to successfully execute
#     this DDL instead (the previous buggy behaviour), the pg_inherits row that now belongs to
#     (chld1, prnt2) gets deleted by its PK ybctid while only the prnt1 index entry is
#     cleaned up, orphaning the pg_inherits_parent_index entry for prnt2. Every subsequent
#     scan through that index -- including the catalog preload done by every new connection --
#     then fails with "DocKey(...) not found in indexed table".
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
step s1_setup {
  CREATE TABLE prnt1 (k int, v int) PARTITION BY RANGE (k);
  CREATE TABLE prnt2 (k int, v int) PARTITION BY RANGE (k);
  CREATE TABLE chld1 PARTITION OF prnt1 FOR VALUES FROM (1) TO (100);
}
step s1_move_chld1_to_prnt2 {
  ALTER TABLE prnt1 DETACH PARTITION chld1;
  ALTER TABLE prnt2 ATTACH PARTITION chld1 FOR VALUES FROM (1) TO (100);
}

session s2
step s2_load_chld1_inherits_cache { INSERT INTO chld1 VALUES (5, 5); }
step s2_stale_detach_chld1_from_prnt1 { ALTER TABLE prnt1 DETACH PARTITION chld1; }

session s3
step s3_select_prnt2 { SELECT * from prnt2; }

# The INSERT in S2 is required to load the by-child pg_inherits entry for chld1 before it is
# re-parented to prnt2 by S1.
#
# If an inconsistent catalog cache is used to successfully execute the DETACH in S2 (which was
# the previous buggy behaviour), the pg_inherits_parent_index entry for prnt2 would be
# orphaned, and the SELECT in S3 (which scans that index while expanding prnt2's partitions)
# would then fail with "DocKey(...) not found in indexed table" instead of returning normally.
permutation s1_setup(yb_never_waits) s2_load_chld1_inherits_cache s1_move_chld1_to_prnt2 s2_stale_detach_chld1_from_prnt1 s3_select_prnt2
