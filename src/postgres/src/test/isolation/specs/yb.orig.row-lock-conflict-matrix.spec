# Verify that explicit row-level lock levels conform to their documented
# conflict matrix under REPEATABLE READ isolation with wait-on-conflict
# concurrency control (conflicting lock requests block until the holder
# commits):
#
#   held \ requested  | FOR KEY SHARE | FOR SHARE | FOR NO KEY UPDATE | FOR UPDATE
#   ------------------+---------------+-----------+-------------------+-----------
#   FOR KEY SHARE     |       O       |     O     |         O         |     X
#   FOR SHARE         |       O       |     O     |         X         |     X
#   FOR NO KEY UPDATE |       O       |     X     |         X         |     X
#   FOR UPDATE        |       X       |     X     |         X         |     X
#
# This variant runs with the default skip_prefix_locks=true, i.e. prefix locks
# are not written, and covers REPEATABLE READ only. SERIALIZABLE is not meant to
# be run in that mode due to high contention from tablet level locks. The SERIALIZABLE
# matrix is covered by yb.orig.row-lock-conflict-matrix-prefix-locks-on instead.
#
# s1 takes its lock both ways: through a plain SELECT, and through a cursor (the
# s1_cur_* steps).
#
# The default-isolation (READ COMMITTED) matrix is covered by the ported
# upstream test tuplelock-conflict in yb_pg_isolation_schedule.

setup
{
  DROP TABLE IF EXISTS conflict_matrix;
  CREATE TABLE conflict_matrix (k INT PRIMARY KEY, v INT);
  INSERT INTO conflict_matrix VALUES (1, 1);
}

teardown
{
  DROP TABLE conflict_matrix;
}

session s1
step s1_begin_rr { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step s1_keyshare { SELECT k FROM conflict_matrix WHERE k = 1 FOR KEY SHARE; }
step s1_share    { SELECT k FROM conflict_matrix WHERE k = 1 FOR SHARE; }
step s1_nokeyupd { SELECT k FROM conflict_matrix WHERE k = 1 FOR NO KEY UPDATE; }
step s1_upd      { SELECT k FROM conflict_matrix WHERE k = 1 FOR UPDATE; }
step s1_cur_keyshare { DECLARE c CURSOR FOR SELECT k FROM conflict_matrix WHERE k = 1 FOR KEY SHARE; FETCH ALL c; }
step s1_cur_share    { DECLARE c CURSOR FOR SELECT k FROM conflict_matrix WHERE k = 1 FOR SHARE; FETCH ALL c; }
step s1_cur_nokeyupd { DECLARE c CURSOR FOR SELECT k FROM conflict_matrix WHERE k = 1 FOR NO KEY UPDATE; FETCH ALL c; }
step s1_cur_upd      { DECLARE c CURSOR FOR SELECT k FROM conflict_matrix WHERE k = 1 FOR UPDATE; FETCH ALL c; }
step s1_commit   { COMMIT; }

session s2
step s2_begin_rr { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step s2_keyshare { SELECT k FROM conflict_matrix WHERE k = 1 FOR KEY SHARE; }
step s2_share    { SELECT k FROM conflict_matrix WHERE k = 1 FOR SHARE; }
step s2_nokeyupd { SELECT k FROM conflict_matrix WHERE k = 1 FOR NO KEY UPDATE; }
step s2_upd      { SELECT k FROM conflict_matrix WHERE k = 1 FOR UPDATE; }
step s2_commit   { COMMIT; }

# REPEATABLE READ
permutation s1_begin_rr s1_keyshare s2_begin_rr s2_keyshare s1_commit s2_commit
permutation s1_begin_rr s1_keyshare s2_begin_rr s2_share    s1_commit s2_commit
permutation s1_begin_rr s1_keyshare s2_begin_rr s2_nokeyupd s1_commit s2_commit
permutation s1_begin_rr s1_keyshare s2_begin_rr s2_upd      s1_commit s2_commit
permutation s1_begin_rr s1_share    s2_begin_rr s2_keyshare s1_commit s2_commit
permutation s1_begin_rr s1_share    s2_begin_rr s2_share    s1_commit s2_commit
permutation s1_begin_rr s1_share    s2_begin_rr s2_nokeyupd s1_commit s2_commit
permutation s1_begin_rr s1_share    s2_begin_rr s2_upd      s1_commit s2_commit
permutation s1_begin_rr s1_nokeyupd s2_begin_rr s2_keyshare s1_commit s2_commit
permutation s1_begin_rr s1_nokeyupd s2_begin_rr s2_share    s1_commit s2_commit
permutation s1_begin_rr s1_nokeyupd s2_begin_rr s2_nokeyupd s1_commit s2_commit
permutation s1_begin_rr s1_nokeyupd s2_begin_rr s2_upd      s1_commit s2_commit
permutation s1_begin_rr s1_upd      s2_begin_rr s2_keyshare s1_commit s2_commit
permutation s1_begin_rr s1_upd      s2_begin_rr s2_share    s1_commit s2_commit
permutation s1_begin_rr s1_upd      s2_begin_rr s2_nokeyupd s1_commit s2_commit
permutation s1_begin_rr s1_upd      s2_begin_rr s2_upd      s1_commit s2_commit

# REPEATABLE READ, s1 takes its lock through a cursor
permutation s1_begin_rr s1_cur_keyshare s2_begin_rr s2_keyshare s1_commit s2_commit
permutation s1_begin_rr s1_cur_keyshare s2_begin_rr s2_share    s1_commit s2_commit
permutation s1_begin_rr s1_cur_keyshare s2_begin_rr s2_nokeyupd s1_commit s2_commit
permutation s1_begin_rr s1_cur_keyshare s2_begin_rr s2_upd      s1_commit s2_commit
permutation s1_begin_rr s1_cur_share    s2_begin_rr s2_keyshare s1_commit s2_commit
permutation s1_begin_rr s1_cur_share    s2_begin_rr s2_share    s1_commit s2_commit
permutation s1_begin_rr s1_cur_share    s2_begin_rr s2_nokeyupd s1_commit s2_commit
permutation s1_begin_rr s1_cur_share    s2_begin_rr s2_upd      s1_commit s2_commit
permutation s1_begin_rr s1_cur_nokeyupd s2_begin_rr s2_keyshare s1_commit s2_commit
permutation s1_begin_rr s1_cur_nokeyupd s2_begin_rr s2_share    s1_commit s2_commit
permutation s1_begin_rr s1_cur_nokeyupd s2_begin_rr s2_nokeyupd s1_commit s2_commit
permutation s1_begin_rr s1_cur_nokeyupd s2_begin_rr s2_upd      s1_commit s2_commit
permutation s1_begin_rr s1_cur_upd      s2_begin_rr s2_keyshare s1_commit s2_commit
permutation s1_begin_rr s1_cur_upd      s2_begin_rr s2_share    s1_commit s2_commit
permutation s1_begin_rr s1_cur_upd      s2_begin_rr s2_nokeyupd s1_commit s2_commit
permutation s1_begin_rr s1_cur_upd      s2_begin_rr s2_upd      s1_commit s2_commit

