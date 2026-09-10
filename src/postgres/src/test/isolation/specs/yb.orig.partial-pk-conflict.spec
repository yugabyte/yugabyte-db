# Explicit row-level locking when the locked row is identified by a prefix of
# the primary key instead of the full primary key: one session locks the row by
# its full key (h = 1 AND r = 2), the other by the (h = 1) prefix only. A
# partial-key request cannot be resolved to a single row key, so what it locks
# depends on the isolation level and on whether prefix locks are written.
#
# Whatever is locked, the two requests must still see each other and follow the
# documented lock conflict matrix:
#
#   held \ requested  | FOR KEY SHARE | FOR SHARE | FOR NO KEY UPDATE | FOR UPDATE
#   ------------------+---------------+-----------+-------------------+-----------
#   FOR KEY SHARE     |       O       |     O     |         O         |     X
#   FOR SHARE         |       O       |     O     |         X         |     X
#   FOR NO KEY UPDATE |       O       |     X     |         X         |     X
#   FOR UPDATE        |       X       |     X     |         X         |     X
#
# Both directions of the matrix are exercised: s1 holding the full-key lock and
# s2 requesting the partial-key lock, and vice versa.
#
# The last two groups of permutations cover what the partial-key lock covers,
# rather than which modes conflict. In REPEATABLE READ (and READ COMMITTED) a
# partial-key request locks only the rows matching the predicate that are visible
# in the requester's MVCC snapshot, so rows outside that snapshot stay writable
# and lockable by other transactions:
#   - s2 inserts a row that satisfies the (h = 1) prefix predicate but did not
#     exist when s1 locked. It must not block.
#   - s3 commits such a row after s1 took its lock, and s2 then locks, updates
#     or deletes it. It must not block.
#
# This variant runs with the default skip_prefix_locks=true, i.e. prefix locks
# are not written, and covers REPEATABLE READ only. This is because SERIALIZABLE is not
# meant to be run in this mode due to high contention from the tablet level locks it takes
# in this mode. See yb.orig.partial-pk-conflict-prefix-locks-on for serializable isolation
# level.

setup
{
  DROP TABLE IF EXISTS partial_key;
  CREATE TABLE partial_key (h INT, r INT, v INT, PRIMARY KEY (h, r));
  INSERT INTO partial_key VALUES (1, 2, 3);
}

teardown
{
  DROP TABLE partial_key;
}

session s1
step s1_begin_rr { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step s1_full_keyshare { SELECT h, r FROM partial_key WHERE h = 1 AND r = 2 FOR KEY SHARE; }
step s1_full_share    { SELECT h, r FROM partial_key WHERE h = 1 AND r = 2 FOR SHARE; }
step s1_full_nokeyupd { SELECT h, r FROM partial_key WHERE h = 1 AND r = 2 FOR NO KEY UPDATE; }
step s1_full_upd      { SELECT h, r FROM partial_key WHERE h = 1 AND r = 2 FOR UPDATE; }
step s1_part_keyshare { SELECT h, r FROM partial_key WHERE h = 1 FOR KEY SHARE; }
step s1_part_share    { SELECT h, r FROM partial_key WHERE h = 1 FOR SHARE; }
step s1_part_nokeyupd { SELECT h, r FROM partial_key WHERE h = 1 FOR NO KEY UPDATE; }
step s1_part_upd      { SELECT h, r FROM partial_key WHERE h = 1 FOR UPDATE; }
step s1_commit   { COMMIT; }

session s2
step s2_begin_rr { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step s2_full_keyshare { SELECT h, r FROM partial_key WHERE h = 1 AND r = 2 FOR KEY SHARE; }
step s2_full_share    { SELECT h, r FROM partial_key WHERE h = 1 AND r = 2 FOR SHARE; }
step s2_full_nokeyupd { SELECT h, r FROM partial_key WHERE h = 1 AND r = 2 FOR NO KEY UPDATE; }
step s2_full_upd      { SELECT h, r FROM partial_key WHERE h = 1 AND r = 2 FOR UPDATE; }
step s2_part_keyshare { SELECT h, r FROM partial_key WHERE h = 1 FOR KEY SHARE; }
step s2_part_share    { SELECT h, r FROM partial_key WHERE h = 1 FOR SHARE; }
step s2_part_nokeyupd { SELECT h, r FROM partial_key WHERE h = 1 FOR NO KEY UPDATE; }
step s2_part_upd      { SELECT h, r FROM partial_key WHERE h = 1 FOR UPDATE; }
# A write of a row that satisfies the (h = 1) prefix predicate but was not part
# of the snapshot in which s1 took its lock.
step s2_ins_prefix { INSERT INTO partial_key VALUES (1, 9, 9); }
# Operations on the row inserted (and committed) by s3 after s1 took its lock.
step s2_new_upd    { SELECT h, r FROM partial_key WHERE h = 1 AND r = 9 FOR UPDATE; }
step s2_new_update { UPDATE partial_key SET v = 10 WHERE h = 1 AND r = 9; }
step s2_new_delete { DELETE FROM partial_key WHERE h = 1 AND r = 9; }
step s2_commit   { COMMIT; }

# s3 runs in autocommit: it publishes a new row satisfying the (h = 1) prefix
# predicate after s1 has already taken its lock.
session s3
step s3_ins_prefix { INSERT INTO partial_key VALUES (1, 9, 9); }

# REPEATABLE READ: s1 locks the full key, s2 locks the partial key
permutation s1_begin_rr s1_full_keyshare s2_begin_rr s2_part_keyshare s1_commit s2_commit
permutation s1_begin_rr s1_full_keyshare s2_begin_rr s2_part_share    s1_commit s2_commit
permutation s1_begin_rr s1_full_keyshare s2_begin_rr s2_part_nokeyupd s1_commit s2_commit
permutation s1_begin_rr s1_full_keyshare s2_begin_rr s2_part_upd      s1_commit s2_commit
permutation s1_begin_rr s1_full_share    s2_begin_rr s2_part_keyshare s1_commit s2_commit
permutation s1_begin_rr s1_full_share    s2_begin_rr s2_part_share    s1_commit s2_commit
permutation s1_begin_rr s1_full_share    s2_begin_rr s2_part_nokeyupd s1_commit s2_commit
permutation s1_begin_rr s1_full_share    s2_begin_rr s2_part_upd      s1_commit s2_commit
permutation s1_begin_rr s1_full_nokeyupd s2_begin_rr s2_part_keyshare s1_commit s2_commit
permutation s1_begin_rr s1_full_nokeyupd s2_begin_rr s2_part_share    s1_commit s2_commit
permutation s1_begin_rr s1_full_nokeyupd s2_begin_rr s2_part_nokeyupd s1_commit s2_commit
permutation s1_begin_rr s1_full_nokeyupd s2_begin_rr s2_part_upd      s1_commit s2_commit
permutation s1_begin_rr s1_full_upd      s2_begin_rr s2_part_keyshare s1_commit s2_commit
permutation s1_begin_rr s1_full_upd      s2_begin_rr s2_part_share    s1_commit s2_commit
permutation s1_begin_rr s1_full_upd      s2_begin_rr s2_part_nokeyupd s1_commit s2_commit
permutation s1_begin_rr s1_full_upd      s2_begin_rr s2_part_upd      s1_commit s2_commit

# REPEATABLE READ: s1 locks the partial key, s2 locks the full key
permutation s1_begin_rr s1_part_keyshare s2_begin_rr s2_full_keyshare s1_commit s2_commit
permutation s1_begin_rr s1_part_keyshare s2_begin_rr s2_full_share    s1_commit s2_commit
permutation s1_begin_rr s1_part_keyshare s2_begin_rr s2_full_nokeyupd s1_commit s2_commit
permutation s1_begin_rr s1_part_keyshare s2_begin_rr s2_full_upd      s1_commit s2_commit
permutation s1_begin_rr s1_part_share    s2_begin_rr s2_full_keyshare s1_commit s2_commit
permutation s1_begin_rr s1_part_share    s2_begin_rr s2_full_share    s1_commit s2_commit
permutation s1_begin_rr s1_part_share    s2_begin_rr s2_full_nokeyupd s1_commit s2_commit
permutation s1_begin_rr s1_part_share    s2_begin_rr s2_full_upd      s1_commit s2_commit
permutation s1_begin_rr s1_part_nokeyupd s2_begin_rr s2_full_keyshare s1_commit s2_commit
permutation s1_begin_rr s1_part_nokeyupd s2_begin_rr s2_full_share    s1_commit s2_commit
permutation s1_begin_rr s1_part_nokeyupd s2_begin_rr s2_full_nokeyupd s1_commit s2_commit
permutation s1_begin_rr s1_part_nokeyupd s2_begin_rr s2_full_upd      s1_commit s2_commit
permutation s1_begin_rr s1_part_upd      s2_begin_rr s2_full_keyshare s1_commit s2_commit
permutation s1_begin_rr s1_part_upd      s2_begin_rr s2_full_share    s1_commit s2_commit
permutation s1_begin_rr s1_part_upd      s2_begin_rr s2_full_nokeyupd s1_commit s2_commit
permutation s1_begin_rr s1_part_upd      s2_begin_rr s2_full_upd      s1_commit s2_commit

# REPEATABLE READ: s1 holds the partial-key lock while s2 inserts a row that
# satisfies the (h = 1) prefix predicate but was not in the snapshot in which
# s1 locked. None of these may block.
permutation s1_begin_rr s1_part_keyshare s2_begin_rr s2_ins_prefix s1_commit s2_commit
permutation s1_begin_rr s1_part_share    s2_begin_rr s2_ins_prefix s1_commit s2_commit
permutation s1_begin_rr s1_part_nokeyupd s2_begin_rr s2_ins_prefix s1_commit s2_commit
permutation s1_begin_rr s1_part_upd      s2_begin_rr s2_ins_prefix s1_commit s2_commit

# REPEATABLE READ: a row satisfying the (h = 1) prefix predicate is committed by
# s3 after s1 took its partial-key lock; s2 must be free to lock, update and
# delete it, since it was never in s1's snapshot. Only FOR UPDATE is exercised
# for s1: it is the strongest partial-key lock, so if it does not cover the new
# row, no weaker lock mode does.
permutation s1_begin_rr s1_part_upd s3_ins_prefix s2_begin_rr s2_new_upd    s1_commit s2_commit
permutation s1_begin_rr s1_part_upd s3_ins_prefix s2_begin_rr s2_new_update s1_commit s2_commit
permutation s1_begin_rr s1_part_upd s3_ins_prefix s2_begin_rr s2_new_delete s1_commit s2_commit
