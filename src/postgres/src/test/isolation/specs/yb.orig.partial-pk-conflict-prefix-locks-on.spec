# Explicit row-level locking when the locked row is identified by a prefix of
# the primary key instead of the full primary key: one session locks the row by
# its full key (h = 1 AND r = 2), the other by the (h = 1) prefix only. A
# partial-key request cannot be resolved to a single row key, so what it locks
# depends on the isolation level and on whether prefix locks are written.
#
# Whatever is locked, the two requests must still see each other. In REPEATABLE
# READ they follow the documented lock conflict matrix exactly:
#
#   held \ requested  | FOR KEY SHARE | FOR SHARE | FOR NO KEY UPDATE | FOR UPDATE
#   ------------------+---------------+-----------+-------------------+-----------
#   FOR KEY SHARE     |       O       |     O     |         O         |     X
#   FOR SHARE         |       O       |     O     |         X         |     X
#   FOR NO KEY UPDATE |       O       |     X     |         X         |     X
#   FOR UPDATE        |       X       |     X     |         X         |     X
#
# SERIALIZABLE is different in two cells -- see "SERIALIZABLE conflict matrix"
# below. That is a property of locking a prefix rather than a row, not of this
# test.
#
# Both directions of the matrix are exercised: s1 holding the full-key lock and
# s2 requesting the partial-key lock, and vice versa.
#
# In REPEATABLE READ (and READ COMMITTED) a partial-key request locks only the
# rows matching the predicate that are visible in the requester's MVCC snapshot,
# so rows outside that snapshot stay writable and lockable by other
# transactions. SERIALIZABLE must not allow that: the lock has to cover rows
# that satisfy the predicate but do not exist yet.
#
# The last two groups of permutations cover what the partial-key lock covers,
# rather than which modes conflict:
#   - s2 inserts a row that satisfies the (h = 1) prefix predicate but did not
#     exist when s1 locked, plus (under SERIALIZABLE only, where it is the cell
#     that tells the lock granularities apart) a control row under a different
#     hash key.
#   - s3 commits such a row after s1 took its lock, and s2 then locks, updates
#     or deletes it (under REPEATABLE READ only -- see the note at the end).
#
# This variant must run in a schedule whose cluster sets skip_prefix_locks=false
# (see yb_wait_queues_skip_prefix_locks_off_schedule), i.e. prefix locks are
# written. Under SERIALIZABLE, a partial-key request takes weak locks on the
# ancestors of the key and a strong lock on the (h = 1) prefix itself, so the
# lock is confined to that prefix: rows that satisfy the predicate but do not
# exist yet are still blocked, while writes under a different hash key are not
# (unlike the tablet-wide escalation with skip_prefix_locks=true).
#
# SERIALIZABLE conflict matrix
# ----------------------------
# Under SERIALIZABLE the two requests do not meet at the row: the partial-key
# request locks the (h = 1) prefix, and the full-key request only leaves *weak*
# intents on that prefix (the strong ones sit on the row below it). Conflicts
# are therefore decided between a strong intent set and a weak intent set at
# (h = 1), and weak intents only record read-vs-write, not which of the four
# row-mark modes produced them. On top of that, a SERIALIZABLE read takes
# kStrongRead on its request key -- so the partial-key side holds a strong read
# on the prefix whatever its row mark is, even FOR KEY SHARE. At the prefix, the
# modes collapse to:
#
#   FOR KEY SHARE / FOR SHARE     full key -> {weak read}
#                                 prefix   -> {strong read}
#   FOR NO KEY UPDATE             full key -> {weak read, weak write}
#                                 prefix   -> {strong read, weak write}
#   FOR UPDATE                    full key -> {weak read, weak write}
#                                 prefix   -> {strong read, strong write}
#
# This results in a deviation from the matrix above in exactly two cells. Only
# the prefix side contributes strong intents, so each cell is decided by which
# mode sits on the prefix, not by which session acquires its lock first: both
# deviations hold in either acquisition order (so each shows up twice below,
# once per permutation group), and both disappear once the two modes swap
# granularities.
#
#   - FOR SHARE on the full key vs FOR NO KEY UPDATE on the prefix does NOT
#     conflict (the matrix says it should).
#   - FOR NO KEY UPDATE on the full key vs FOR KEY SHARE on the prefix does
#     conflict (the matrix says it should not). This is not the FOR KEY SHARE
#     row mark conflicting: it is the SERIALIZABLE read's strong read on the
#     prefix meeting the weak write left by FOR NO KEY UPDATE. A plain SELECT
#     WHERE h = 1 with no FOR clause conflicts with the same FOR NO KEY UPDATE,
#     for the same reason. Swapped -- FOR KEY SHARE on the full key, FOR NO KEY
#     UPDATE on the prefix -- the full key leaves only a weak read, which
#     nothing on the prefix side conflicts with, again as the matrix says.
#
# The default skip_prefix_locks=true behaviour is covered by
# yb.orig.partial-pk-conflict.

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
step s1_begin_sr { BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE; }
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
step s2_begin_sr { BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE; }
step s2_full_keyshare { SELECT h, r FROM partial_key WHERE h = 1 AND r = 2 FOR KEY SHARE; }
step s2_full_share    { SELECT h, r FROM partial_key WHERE h = 1 AND r = 2 FOR SHARE; }
step s2_full_nokeyupd { SELECT h, r FROM partial_key WHERE h = 1 AND r = 2 FOR NO KEY UPDATE; }
step s2_full_upd      { SELECT h, r FROM partial_key WHERE h = 1 AND r = 2 FOR UPDATE; }
step s2_part_keyshare { SELECT h, r FROM partial_key WHERE h = 1 FOR KEY SHARE; }
step s2_part_share    { SELECT h, r FROM partial_key WHERE h = 1 FOR SHARE; }
step s2_part_nokeyupd { SELECT h, r FROM partial_key WHERE h = 1 FOR NO KEY UPDATE; }
step s2_part_upd      { SELECT h, r FROM partial_key WHERE h = 1 FOR UPDATE; }
# Writes of rows that satisfy the (h = 1) prefix predicate but were not part of
# the snapshot in which s1 took its lock, plus a control write under a
# different hash key.
step s2_ins_prefix { INSERT INTO partial_key VALUES (1, 9, 9); }
step s2_ins_other  { INSERT INTO partial_key VALUES (2, 9, 9); }
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

# SERIALIZABLE: s1 locks the full key, s2 locks the partial key.
permutation s1_begin_sr s1_full_keyshare s2_begin_sr s2_part_keyshare s1_commit s2_commit
permutation s1_begin_sr s1_full_keyshare s2_begin_sr s2_part_share    s1_commit s2_commit
permutation s1_begin_sr s1_full_keyshare s2_begin_sr s2_part_nokeyupd s1_commit s2_commit
permutation s1_begin_sr s1_full_keyshare s2_begin_sr s2_part_upd      s1_commit s2_commit
permutation s1_begin_sr s1_full_share    s2_begin_sr s2_part_keyshare s1_commit s2_commit
permutation s1_begin_sr s1_full_share    s2_begin_sr s2_part_share    s1_commit s2_commit
permutation s1_begin_sr s1_full_share    s2_begin_sr s2_part_nokeyupd s1_commit s2_commit
permutation s1_begin_sr s1_full_share    s2_begin_sr s2_part_upd      s1_commit s2_commit
permutation s1_begin_sr s1_full_nokeyupd s2_begin_sr s2_part_keyshare s1_commit s2_commit
permutation s1_begin_sr s1_full_nokeyupd s2_begin_sr s2_part_share    s1_commit s2_commit
permutation s1_begin_sr s1_full_nokeyupd s2_begin_sr s2_part_nokeyupd s1_commit s2_commit
permutation s1_begin_sr s1_full_nokeyupd s2_begin_sr s2_part_upd      s1_commit s2_commit
permutation s1_begin_sr s1_full_upd      s2_begin_sr s2_part_keyshare s1_commit s2_commit
permutation s1_begin_sr s1_full_upd      s2_begin_sr s2_part_share    s1_commit s2_commit
permutation s1_begin_sr s1_full_upd      s2_begin_sr s2_part_nokeyupd s1_commit s2_commit
permutation s1_begin_sr s1_full_upd      s2_begin_sr s2_part_upd      s1_commit s2_commit

# SERIALIZABLE: s1 locks the partial key, s2 locks the full key.
permutation s1_begin_sr s1_part_keyshare s2_begin_sr s2_full_keyshare s1_commit s2_commit
permutation s1_begin_sr s1_part_keyshare s2_begin_sr s2_full_share    s1_commit s2_commit
permutation s1_begin_sr s1_part_keyshare s2_begin_sr s2_full_nokeyupd s1_commit s2_commit
permutation s1_begin_sr s1_part_keyshare s2_begin_sr s2_full_upd      s1_commit s2_commit
permutation s1_begin_sr s1_part_share    s2_begin_sr s2_full_keyshare s1_commit s2_commit
permutation s1_begin_sr s1_part_share    s2_begin_sr s2_full_share    s1_commit s2_commit
permutation s1_begin_sr s1_part_share    s2_begin_sr s2_full_nokeyupd s1_commit s2_commit
permutation s1_begin_sr s1_part_share    s2_begin_sr s2_full_upd      s1_commit s2_commit
permutation s1_begin_sr s1_part_nokeyupd s2_begin_sr s2_full_keyshare s1_commit s2_commit
permutation s1_begin_sr s1_part_nokeyupd s2_begin_sr s2_full_share    s1_commit s2_commit
permutation s1_begin_sr s1_part_nokeyupd s2_begin_sr s2_full_nokeyupd s1_commit s2_commit
permutation s1_begin_sr s1_part_nokeyupd s2_begin_sr s2_full_upd      s1_commit s2_commit
permutation s1_begin_sr s1_part_upd      s2_begin_sr s2_full_keyshare s1_commit s2_commit
permutation s1_begin_sr s1_part_upd      s2_begin_sr s2_full_share    s1_commit s2_commit
permutation s1_begin_sr s1_part_upd      s2_begin_sr s2_full_nokeyupd s1_commit s2_commit
permutation s1_begin_sr s1_part_upd      s2_begin_sr s2_full_upd      s1_commit s2_commit

# REPEATABLE READ: s1 holds the partial-key lock while s2 inserts a row that
# satisfies the (h = 1) prefix predicate but was not in the snapshot in which
# s1 locked. None of these may block. The s2_ins_other control is only
# exercised under SERIALIZABLE below: a row that does not even satisfy the
# predicate cannot be blocked if s2_ins_prefix is not.
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

# SERIALIZABLE: s1 holds the partial-key lock while s2 inserts. s2_ins_prefix
# must be blocked -- the lock has to cover rows that satisfy the predicate but
# do not exist yet. s2_ins_other tells the two lock granularities apart: it is
# blocked only if the partial-key request escalated to the tablet's top level
# key, and not if the lock is confined to the (h = 1) prefix.
permutation s1_begin_sr s1_part_keyshare s2_begin_sr s2_ins_prefix s1_commit s2_commit
permutation s1_begin_sr s1_part_keyshare s2_begin_sr s2_ins_other  s1_commit s2_commit
permutation s1_begin_sr s1_part_share    s2_begin_sr s2_ins_prefix s1_commit s2_commit
permutation s1_begin_sr s1_part_share    s2_begin_sr s2_ins_other  s1_commit s2_commit
permutation s1_begin_sr s1_part_nokeyupd s2_begin_sr s2_ins_prefix s1_commit s2_commit
permutation s1_begin_sr s1_part_nokeyupd s2_begin_sr s2_ins_other  s1_commit s2_commit
permutation s1_begin_sr s1_part_upd      s2_begin_sr s2_ins_prefix s1_commit s2_commit
permutation s1_begin_sr s1_part_upd      s2_begin_sr s2_ins_other  s1_commit s2_commit

# There is no SERIALIZABLE counterpart of the s3 permutations above: s3 runs in
# autocommit, so its insert is itself blocked by s1's lock, and s2's operation on
# the same not-yet-visible row blocks on s1's lock too. Both merely re-test the
# s2_ins_prefix permutations above.
