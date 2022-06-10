setup
{
  CREATE TABLE queue (
    id	int		PRIMARY KEY,
    data			text	NOT NULL,
    status			text	NOT NULL
  );
  INSERT INTO queue VALUES (1, 'foo', 'NEW'), (2, 'bar', 'NEW');
}

teardown
{
  DROP TABLE queue;
}

# This test asserts that a statement with SKIP LOCKED actually skips locking a tuple that was
# concurrently modified by another transaction. The test asserts the skip locked behaviour during
# three different states of the concurrent transaction that modified the tuple:
#   1. pending
#   2. committed but not applied
#   3. committed and applied.

session "s1"
setup		{ BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s1a"	{ SELECT * FROM queue; -- this is just to ensure we have picked the read point}
# UPDATE takes a kStrongRead + kStrongWrite intent on the sub doc key made of
# (hash code, pk, status col). Also the value portion will have 'OLD'.
step "s1b"	{ UPDATE queue set status='OLD' WHERE id=1; }
step "s1c"	{ COMMIT; }


session "s2"
setup		{ BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s2a"	{ SELECT * FROM queue; -- this is just to ensure we have picked the read point}
# FOR UPDATE attempts to take a kStrongRead + kStrongWrite on the sub doc key made of
# (hash code, pk, status col) with the value portion empty. But it will skip locking the first row
# with id=1 due to the above UPDATE (if that executes earlier).
step "s2b"	{ SELECT * FROM queue ORDER BY id FOR UPDATE SKIP LOCKED LIMIT 1; }
step "s2c"	{ COMMIT; }
step "s2_sleep" { SELECT pg_sleep(1); }

# (1) case of pending update:
#   Ensure that the SELECT skips locking the row that has been implicitly locked in a conflicting
#   mode due to the UPDATE by a yet to commit transaction.
permutation "s1a" "s2a" "s1b" "s2b" "s1c" "s2c"

# (2) case of "committed but not applied" update:
#   Ensure that the SELECT skips locking the row that has been modified by an UPDATE in a concurrent
#   transaction that recently committed but its intents have not yet moved to regular db. With "s2b"
#   executed right after "s1c", there are rare chances of triggering this scenario. But we don't
#   have to rely on the chance - this behaviour will surely be triggered in
#   TestPgIsolationRegress.testIsolationRegressWithDelayedTxnApply() since it tests all regress tests
#   with FLAGS_apply_intents_task_injected_delay_ms=100.
permutation "s1a" "s2a" "s1b" "s1c" "s2b" "s2c"

# (3) case of "committed and applied" update:
#   Ensure that the SELECT skips locking the row that has been modified by an UPDATE in a concurrent
#   transaction that has committed and applied. The sleep is added to ensure that the committed
#   transaction's intents have also been moved from intents db and regular db and the conflict is
#   hence detected in regular db.
permutation "s1a" "s2a" "s1b" "s1c" "s2_sleep" "s2b" "s2c"