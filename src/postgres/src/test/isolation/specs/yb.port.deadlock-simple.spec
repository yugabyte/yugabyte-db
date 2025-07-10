# The deadlock detector has a special case for "simple" deadlocks.  A simple
# deadlock occurs when we attempt a lock upgrade while another process waits
# for a lock upgrade on the same object; and the sought locks conflict with
# those already held, so that neither process can complete its upgrade until
# the other releases locks.  Test this scenario.

setup
{
  CREATE TABLE a1 ();
}

teardown
{
  DROP TABLE a1;
}

session s1
setup		{ BEGIN; }
step s1as	{ LOCK TABLE a1 IN ACCESS SHARE MODE; }
step s1ae	{ LOCK TABLE a1 IN ACCESS EXCLUSIVE MODE; }
step s1c	{ COMMIT; }

session s2
setup		{ BEGIN; }
step s2as	{ LOCK TABLE a1 IN ACCESS SHARE MODE; }
step s2ae	{ LOCK TABLE a1 IN ACCESS EXCLUSIVE MODE; }
step s2c	{ COMMIT; }

# TODO(#27819): In YB, the Object Lock Manager is indifferent to the status
# of the transaction and would treat the lock as active unless explicitly
# released. We change the permutation here so as to force finish of the
# aborted txn so as to allow the other txn to make progress.
permutation s1as s2as s1ae s2ae s2c s1c
