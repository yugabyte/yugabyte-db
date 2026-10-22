# A primary-key-changing UPDATE run from a cached generic plan must maintain
# secondary indexes even when the table's relcache entry is rebuilt between
# planning and execution.
#
# The cached plan locks only the table, so the first lock on t_a is taken while
# processing the first row, and that applies the GRANT's invalidation. The
# rebuilt entry has rd_pkindex reset until RelationGetIndexList() runs again,
# which used to hide the PK change from the skip-list logic: t_a stayed on the
# skip list, the row moved to a new ybctid, and t_a kept only the old entry.

setup
{
  CREATE TABLE t (id int PRIMARY KEY, a int, b int);
  CREATE INDEX t_a ON t (a);
  INSERT INTO t VALUES (1, 1, 1);
}

teardown
{
  DROP TABLE t;
}

session s1
setup
{
  SET plan_cache_mode = force_generic_plan;
  -- a = a lists t_a's column in SET, making t_a skippable when unchanged.
  PREPARE u(int) AS UPDATE t SET id = id + 100, a = a WHERE b = $1;
}
step s1_cache_plan	{ EXECUTE u(0); }
step s1_sleep		{ SET yb_test_sleep_before_executor_start_ms = 10000; }
step s1_update		{ EXECUTE u(1); }
# Issued after s2_grant so the tester waits for s1_update before the checks.
step s1_reset		{ RESET yb_test_sleep_before_executor_start_ms; }

session s2
setup				{ SET enable_seqscan = off; }
step s2_grant		{ GRANT SELECT ON t TO PUBLIC; }
step s2_index_scan	{ SELECT * FROM t WHERE a = 1; }
step s2_index_check	{ SELECT yb_index_check('t_a'::regclass); }

permutation s1_cache_plan s1_sleep s1_update s2_grant s1_reset s2_index_scan s2_index_check
