# Four-process deadlock with two hard edges and two soft edges.
# d2 waits for e1 (soft edge), e1 waits for d1 (hard edge),
# d1 waits for e2 (soft edge), e2 waits for d2 (hard edge).
# The deadlock detector resolves the deadlock by reversing the d1-e2 edge,
# unblocking d1.

setup
{
  CREATE TABLE a1 ();
  CREATE TABLE a2 ();
}

teardown
{
  DROP TABLE a1, a2;
}

session d1
setup		{ BEGIN; SET deadlock_timeout = '10s'; }
step d1a1	{ LOCK TABLE a1 IN ACCESS SHARE MODE; }
step d1a2	{ LOCK TABLE a2 IN ACCESS SHARE MODE; }
step d1c	{ COMMIT; }

session d2
setup		{ BEGIN; SET deadlock_timeout = '10ms'; }
step d2a2	{ LOCK TABLE a2 IN ACCESS SHARE MODE; }
step d2a1	{ LOCK TABLE a1 IN ACCESS SHARE MODE; }
step d2c	{ COMMIT; }

session e1
setup		{ BEGIN; SET deadlock_timeout = '10s'; }
step e1l	{ LOCK TABLE a1 IN ACCESS EXCLUSIVE MODE; }
step e1c	{ COMMIT; }

session e2
setup		{ BEGIN; SET deadlock_timeout = '10s'; }
step e2l	{ LOCK TABLE a2 IN ACCESS EXCLUSIVE MODE; }
step e2c	{ COMMIT; }

# Note: YB differs from PG here. PG tries to avoid aborting the txn(s) in
# the deadlock cycle by letting the latter ACCESS_SHARE lock requests of
# d1/d2 through despite an earlier waiting ACCESS_EXCLUSIVE lock request
# from e1/e2. YB takes a different route to avoid starvation and breaks
# the deadlock cycle by aborting e1/e2.
#
# TODO(#27819): We change the permutation here so as to force finish of the
# aborted txn so as to allow the other txn to make progress. In YB, the
# Object Lock Manager is indifferent to the status of the transaction and
# would treat the lock as active unless explicitly released.
permutation d1a1 d2a2 e1l e2l d1a2 d2a1 e2c d1c e1c d2c
