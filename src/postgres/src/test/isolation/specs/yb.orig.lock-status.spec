setup
{
  CREATE TABLE lock_status_test (
    k	int		PRIMARY KEY
  );
  INSERT INTO lock_status_test VALUES (1), (2);
}

teardown
{
  DROP TABLE lock_status_test;
}


session "s1"
setup				{ BEGIN; }
step "s1lock"		{ SELECT * FROM lock_status_test WHERE k = 1 FOR UPDATE;}
teardown			{ COMMIT; }


session "s2"
setup				{ BEGIN; }
step "s2lock"		{ SELECT * FROM lock_status_test WHERE k = 2 FOR UPDATE;}
teardown			{ COMMIT; }


session "status"
step "totalrows"	{ SELECT COUNT(*) FROM yb_lock_status(null, null); }
step "totaltxns"	{ SELECT COUNT(DISTINCT transaction_id) from yb_lock_status(null, null); }
step "0secold"		{ SET yb_locks_min_txn_age TO 0; }
step "7secold"		{ SET yb_locks_min_txn_age TO 7000; }
step "max1"			{ SET yb_locks_max_transactions TO 1; }
step "wait2s"		{ SELECT pg_sleep(2); }
step "wait5s"		{ SELECT pg_sleep(5); }
teardown
{
	SET yb_locks_max_transactions TO 16;
	SET yb_locks_min_txn_age TO 1000;
}


# (1) There should be no rows at all at the baseline.
permutation "totalrows"

# (2) Taking 1 lock, and waiting 2s >> default min age of 1s: Should see
#   2 total rows and 1 transaction.
permutation "s1lock" "wait2s" "totalrows" "totaltxns"

# (3) Now taking 2 locks and waiting 2s: Should see 4 total rows and 2
#   transactions.
permutation "s1lock" "s2lock" "wait2s" "totalrows" "totaltxns"

# (4) Take 2 locks. Set the limit to 1 transaction, and min txn age to 0.
#   Wait 5s and should get back 2 total rows and 1 transaction.
permutation "max1" "0secold" "s1lock" "s2lock" "wait5s" "totalrows" "totaltxns"

# (5) Take 2 locks. Here, set the min_age to 7s. Make sure nothing is
#   returned after 2s. But then after 5s more, we should see 4 total and 2
#   transactions.
permutation "7secold" "s1lock" "s2lock" "wait2s" "totalrows" "wait5s" "totalrows" "totaltxns"
