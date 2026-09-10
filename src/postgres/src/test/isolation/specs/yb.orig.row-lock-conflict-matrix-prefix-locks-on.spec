# Verify that explicit row-level lock levels conform to their documented
# conflict matrix under REPEATABLE READ and SERIALIZABLE isolation with prefix
# locks written (i.e., skip_prefix_locks=false) and wait-on-conflict
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
# This spec must run in a schedule whose cluster sets
# skip_prefix_locks=false (see yb_wait_queues_skip_prefix_locks_off_schedule).
#
# The matrix above holds for a target list of key columns only. A SERIALIZABLE
# read takes kStrongRead on what it reads: the liveness column for a key-only
# target list, but the row itself for SELECT *, which upgrades FOR KEY SHARE on
# the row from kWeakRead to kStrongRead and makes it conflict with FOR NO KEY
# UPDATE's kWeakWrite. Only FOR KEY SHARE is affected, since the other modes
# already take a strong intent on the row from their lock mode alone. The
# s*_star_keyshare steps cover this.

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
step s1_begin_sr { BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE; }
step s1_keyshare { SELECT k FROM conflict_matrix WHERE k = 1 FOR KEY SHARE; }
step s1_share    { SELECT k FROM conflict_matrix WHERE k = 1 FOR SHARE; }
step s1_nokeyupd { SELECT k FROM conflict_matrix WHERE k = 1 FOR NO KEY UPDATE; }
step s1_upd      { SELECT k FROM conflict_matrix WHERE k = 1 FOR UPDATE; }
step s1_cur_keyshare { DECLARE c CURSOR FOR SELECT k FROM conflict_matrix WHERE k = 1 FOR KEY SHARE; FETCH ALL c; }
step s1_cur_share    { DECLARE c CURSOR FOR SELECT k FROM conflict_matrix WHERE k = 1 FOR SHARE; FETCH ALL c; }
step s1_cur_nokeyupd { DECLARE c CURSOR FOR SELECT k FROM conflict_matrix WHERE k = 1 FOR NO KEY UPDATE; FETCH ALL c; }
step s1_cur_upd      { DECLARE c CURSOR FOR SELECT k FROM conflict_matrix WHERE k = 1 FOR UPDATE; FETCH ALL c; }
# FOR KEY SHARE whose target list reads a non-key column -- see the note above.
step s1_star_keyshare { SELECT * FROM conflict_matrix WHERE k = 1 FOR KEY SHARE; }
step s1_cur_star_keyshare { DECLARE c CURSOR FOR SELECT * FROM conflict_matrix WHERE k = 1 FOR KEY SHARE; FETCH ALL c; }
step s1_commit   { COMMIT; }

session s2
step s2_begin_rr { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step s2_begin_sr { BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE; }
step s2_keyshare { SELECT k FROM conflict_matrix WHERE k = 1 FOR KEY SHARE; }
step s2_share    { SELECT k FROM conflict_matrix WHERE k = 1 FOR SHARE; }
step s2_nokeyupd { SELECT k FROM conflict_matrix WHERE k = 1 FOR NO KEY UPDATE; }
step s2_upd      { SELECT k FROM conflict_matrix WHERE k = 1 FOR UPDATE; }
step s2_star_keyshare { SELECT * FROM conflict_matrix WHERE k = 1 FOR KEY SHARE; }
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

# SERIALIZABLE
permutation s1_begin_sr s1_keyshare s2_begin_sr s2_keyshare s1_commit s2_commit
permutation s1_begin_sr s1_keyshare s2_begin_sr s2_share    s1_commit s2_commit
permutation s1_begin_sr s1_keyshare s2_begin_sr s2_nokeyupd s1_commit s2_commit
permutation s1_begin_sr s1_keyshare s2_begin_sr s2_upd      s1_commit s2_commit
permutation s1_begin_sr s1_share    s2_begin_sr s2_keyshare s1_commit s2_commit
permutation s1_begin_sr s1_share    s2_begin_sr s2_share    s1_commit s2_commit
permutation s1_begin_sr s1_share    s2_begin_sr s2_nokeyupd s1_commit s2_commit
permutation s1_begin_sr s1_share    s2_begin_sr s2_upd      s1_commit s2_commit
permutation s1_begin_sr s1_nokeyupd s2_begin_sr s2_keyshare s1_commit s2_commit
permutation s1_begin_sr s1_nokeyupd s2_begin_sr s2_share    s1_commit s2_commit
permutation s1_begin_sr s1_nokeyupd s2_begin_sr s2_nokeyupd s1_commit s2_commit
permutation s1_begin_sr s1_nokeyupd s2_begin_sr s2_upd      s1_commit s2_commit
permutation s1_begin_sr s1_upd      s2_begin_sr s2_keyshare s1_commit s2_commit
permutation s1_begin_sr s1_upd      s2_begin_sr s2_share    s1_commit s2_commit
permutation s1_begin_sr s1_upd      s2_begin_sr s2_nokeyupd s1_commit s2_commit
permutation s1_begin_sr s1_upd      s2_begin_sr s2_upd      s1_commit s2_commit

# SERIALIZABLE, FOR KEY SHARE reading a non-key column
permutation s1_begin_sr s1_star_keyshare s2_begin_sr s2_keyshare      s1_commit s2_commit
permutation s1_begin_sr s1_star_keyshare s2_begin_sr s2_share         s1_commit s2_commit
permutation s1_begin_sr s1_star_keyshare s2_begin_sr s2_nokeyupd      s1_commit s2_commit
permutation s1_begin_sr s1_star_keyshare s2_begin_sr s2_upd           s1_commit s2_commit
permutation s1_begin_sr s1_keyshare      s2_begin_sr s2_star_keyshare s1_commit s2_commit
permutation s1_begin_sr s1_share         s2_begin_sr s2_star_keyshare s1_commit s2_commit
permutation s1_begin_sr s1_nokeyupd      s2_begin_sr s2_star_keyshare s1_commit s2_commit
permutation s1_begin_sr s1_upd           s2_begin_sr s2_star_keyshare s1_commit s2_commit

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

# SERIALIZABLE, s1 takes its lock through a cursor
permutation s1_begin_sr s1_cur_keyshare s2_begin_sr s2_keyshare s1_commit s2_commit
permutation s1_begin_sr s1_cur_keyshare s2_begin_sr s2_share    s1_commit s2_commit
permutation s1_begin_sr s1_cur_keyshare s2_begin_sr s2_nokeyupd s1_commit s2_commit
permutation s1_begin_sr s1_cur_keyshare s2_begin_sr s2_upd      s1_commit s2_commit
permutation s1_begin_sr s1_cur_share    s2_begin_sr s2_keyshare s1_commit s2_commit
permutation s1_begin_sr s1_cur_share    s2_begin_sr s2_share    s1_commit s2_commit
permutation s1_begin_sr s1_cur_share    s2_begin_sr s2_nokeyupd s1_commit s2_commit
permutation s1_begin_sr s1_cur_share    s2_begin_sr s2_upd      s1_commit s2_commit
permutation s1_begin_sr s1_cur_nokeyupd s2_begin_sr s2_keyshare s1_commit s2_commit
permutation s1_begin_sr s1_cur_nokeyupd s2_begin_sr s2_share    s1_commit s2_commit
permutation s1_begin_sr s1_cur_nokeyupd s2_begin_sr s2_nokeyupd s1_commit s2_commit
permutation s1_begin_sr s1_cur_nokeyupd s2_begin_sr s2_upd      s1_commit s2_commit
permutation s1_begin_sr s1_cur_upd      s2_begin_sr s2_keyshare s1_commit s2_commit
permutation s1_begin_sr s1_cur_upd      s2_begin_sr s2_share    s1_commit s2_commit
permutation s1_begin_sr s1_cur_upd      s2_begin_sr s2_nokeyupd s1_commit s2_commit
permutation s1_begin_sr s1_cur_upd      s2_begin_sr s2_upd      s1_commit s2_commit

# SERIALIZABLE, FOR KEY SHARE reading a non-key column, through a cursor
permutation s1_begin_sr s1_cur_star_keyshare s2_begin_sr s2_keyshare      s1_commit s2_commit
permutation s1_begin_sr s1_cur_star_keyshare s2_begin_sr s2_share         s1_commit s2_commit
permutation s1_begin_sr s1_cur_star_keyshare s2_begin_sr s2_nokeyupd      s1_commit s2_commit
permutation s1_begin_sr s1_cur_star_keyshare s2_begin_sr s2_upd           s1_commit s2_commit
permutation s1_begin_sr s1_cur_keyshare      s2_begin_sr s2_star_keyshare s1_commit s2_commit
permutation s1_begin_sr s1_cur_share         s2_begin_sr s2_star_keyshare s1_commit s2_commit
permutation s1_begin_sr s1_cur_nokeyupd      s2_begin_sr s2_star_keyshare s1_commit s2_commit
permutation s1_begin_sr s1_cur_upd           s2_begin_sr s2_star_keyshare s1_commit s2_commit
