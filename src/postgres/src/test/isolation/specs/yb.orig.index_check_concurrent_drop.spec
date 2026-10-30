# yb_index_check() racing a drop of the index it was asked to check (GH #33871).
#
# yb_index_check() takes its argument as an OID, resolved before any lock is
# held. A drop that commits while yb_index_check() waits for AccessShareLock
# leaves it holding an OID with no catalog row, which used to fail with an
# XX000 internal error: neither retryable nor distinguishable from a real
# problem with the index. It must report undefined_object instead.
#
# This permutation fails without the fix at has_subclass() inside
# find_all_inheritors(), with "cache lookup failed for relation <n>". The
# production report (GH #33871) instead got as far as relation_open() and
# "could not open relation with OID <n>" -- whichever cache misses first wins,
# so the check that guards both sits before find_all_inheritors(), not at the
# open. A recheck moved down to relation_open() would still pass this test.

setup
{
  CREATE TABLE t (k int PRIMARY KEY, v int);
  INSERT INTO t VALUES (1, 1), (2, 2);
  CREATE INDEX NONCONCURRENTLY t_v_idx ON t (v);

  -- Callers running DDL alongside the check match on the SQLSTATE and the
  -- message, so pin both. The OID is masked because it is not stable across
  -- runs.
  CREATE FUNCTION check_t_v_idx() RETURNS text LANGUAGE plpgsql AS $$
  BEGIN
    PERFORM yb_index_check('t_v_idx'::regclass);
    RETURN 'checked';
  EXCEPTION WHEN undefined_object THEN
    RETURN SQLSTATE || ': ' || regexp_replace(SQLERRM, '\d+', 'N');
  END $$;
}

teardown
{
  DROP TABLE IF EXISTS t;
  DROP FUNCTION check_t_v_idx();
}

session s1
step s1_begin		{ BEGIN ISOLATION LEVEL READ COMMITTED; }
# Cascades to t_v_idx, taking AccessExclusiveLock on it.
step s1_drop_column	{ ALTER TABLE t DROP COLUMN v; }
step s1_commit		{ COMMIT; }
step s1_rollback	{ ROLLBACK; }

session s2
step s2_index_check	{ SELECT check_t_v_idx(); }

# The drop commits while yb_index_check() is waiting, so the index is gone by
# the time the lock is granted.
permutation s1_begin s1_drop_column s2_index_check s1_commit

# Same wait, but the drop rolls back: the index survives and the check runs.
# Guards against the recheck rejecting an index that was never dropped.
permutation s1_begin s1_drop_column s2_index_check s1_rollback
