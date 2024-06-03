# This tests ensures the following guarantee:
#
#  1. Txn 1 registers a savepoint and modifies a row
#  2. Txn 1 rolls back to the savepoint, undoing the modification
#  3. Txn 1 then locks the same row
#  4. Txn 1 commits but is yet to apply the intents on the txn participant.
#  5. Another transaction tries to modify the same row. This new transaction sees 2 conflicts - a
#     conflict with the modification from Txn 1 and a conflict with the lock-only intent from Txn 1.
#     The conflict with the modification should be ignored since it is part of an aborted sub-txn.
#     The conflict with the lock-only intent should also be ignored since the Txn 1 has committed
#     and no longer holds the lock.
#
# This scenario is only tested with "org.yb.pgsql.TestPgIsolationRegress#withDelayedTxnApply" which
# uses TEST_inject_sleep_before_applying_intents_ms to induce a delay before applying intents. This
# is needed for step (4) above. Without this, the lock-only intent would be removed and the second
# transaction wouldn't see it (and we want to test the fact that the second transaction should
# ignore it after seeing it in intents db because it is a lock-only intent).
#
# This test was introduced due to a review comment in 4fb1676a785319fa35e2d75241a870e3115f1637. The
# comment was due to the following issue in the work in progress code: a modification intent that
# was part of an aborted sub-txn was causing a non-aborted lock-only intent of an aborted txn to
# conflict with other transactions.

setup
{
  CREATE TABLE t (k INT PRIMARY KEY, v INT);
  INSERT INTO t SELECT generate_series(1, 10), 0;
}

teardown
{
  DROP TABLE t;
}

session "s1"
setup			{ BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s1_select"		{ SELECT * FROM t; }
step "s1_savepoint_a"	{ SAVEPOINT a; }
step "s1_update_k_1" { UPDATE t SET v=1 WHERE k=1; }
step "s1_rollback_to_a" { ROLLBACK TO a; }
step "s1_lock_k_1" { SELECT * FROM t WHERE k=1 FOR UPDATE; }
step "s1_update_k_3" { UPDATE t SET v=1 WHERE k=3; }
step "s1_commit" { COMMIT; }

session "s2"
setup			{ BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s2_select"		{ SELECT * FROM t; }
step "s2_update_k_1"		{ UPDATE t SET v=2 WHERE k=1; }
step "s2_commit"		{ COMMIT; }

permutation "s1_select" "s2_select" "s1_savepoint_a" "s1_update_k_1" "s1_rollback_to_a" "s1_lock_k_1" "s1_update_k_3" "s1_commit" "s2_update_k_1" "s2_commit" "s1_select"
