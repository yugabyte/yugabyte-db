# Test for #30693
# Event sequence without the fix:
# 1. s1_select_anchor picks a read time.
# 2. s2_update_child updates the key at a later time.
# 3. s1_update_child reads at txn snapshot.
# 4. s1_update_child buffers the update.
# 5. s1_update_child issues a catalog read that flushes with catalog read time.
# This results in the write running conflict resolution at catalog read time
# and not txn snapshot as it is supposed to.

setup
{
  CREATE TABLE snapshot_anchor (k int primary key);
  INSERT INTO snapshot_anchor VALUES (1);
  CREATE TABLE fk_parent (k int primary key);
  -- Without the fix s1 computes v = 1 + 10 from the stale value, so 11 must be a valid
  -- fk_parent key -- otherwise this fails on the FK check instead of showing the lost write.
  INSERT INTO fk_parent SELECT generate_series(1, 20);
  -- The FK is what makes the UPDATE issue a catalog read after buffering its write.
  CREATE TABLE fk_child (k int primary key, v int REFERENCES fk_parent(k));
  INSERT INTO fk_child VALUES (1, 1);
}

teardown
{
  DROP TABLE IF EXISTS snapshot_anchor;
  DROP TABLE IF EXISTS fk_child;
  DROP TABLE IF EXISTS fk_parent;
}

session s1
step s1_begin_rr { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step s1_select_anchor { SELECT * FROM snapshot_anchor; }
step s1_wait_out_uncertainty_window { SELECT pg_sleep(1); }
step s1_update_child { UPDATE fk_child SET v = v + 10 WHERE k = 1; }
step s1_commit { COMMIT; }
step s1_select_child { SELECT v FROM fk_child WHERE k = 1; }

session s2
step s2_update_child { UPDATE fk_child SET v = v + 1 WHERE k = 1; }

permutation s1_begin_rr s1_select_anchor s1_wait_out_uncertainty_window s2_update_child
	s1_update_child s1_commit s1_select_child
