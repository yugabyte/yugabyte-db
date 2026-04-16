# When the YSQL query layer issues multiple rpcs simultaneously to different tservers for performing
# operations in parallel, it can receive different errors from different nodes.
#
# In case a deadlock occurs, the transaction might be aborted to break the cycle. In this case, a
# few tservers might report a deadlock error, while others might report one of many other errors:
#   1. Transaction was recently aborted: <txn_id>
#   2. Unknown transaction, could be recently aborted: <txn_id>
#   3. Transaction <txn_id> expired or aborted by a conflict
#   4. Transaction metadata missing: <txn_id>, looks like it was just aborted
#   5. Transaction aborted: <txn_id>
#
# This test ensures that even if Pg receives different errors from various tservers due to a
# deadlock, the external client sees a "deadlock detected" error with 40P01 as the error code.
#
# NOTE: even without the fix that ensures this, we would only rarely see an error other than
# "deadlock detected" (the same test was run in java with 100s of iterations to confirm this). But
# we are still adding this test for completeness.

setup
{
  CREATE TABLE test (k INT PRIMARY KEY, v INT) SPLIT INTO 12 TABLETS;
  INSERT INTO test SELECT GENERATE_SERIES(1,20), 0;
}

teardown
{
  DROP TABLE test;
}

session "s1"
step "s1_begin" { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s1_acq_lock_k3+" { SELECT k FROM test WHERE k>=3 ORDER BY k FOR UPDATE; }
step "s1c"	{ COMMIT; }
step "s1_select" { SELECT * FROM test ORDER BY k; }

session "s2"
step "s2_begin" { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s2_upd_k1" { UPDATE test SET v=2 WHERE k=1; }
step "s2_upd_all_k_except_1" { UPDATE test SET v=2 where k!=1; }
step "s2c"	{ COMMIT; }

session "s3"
step "s3_begin" { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s3_upd_k2" { UPDATE test SET v=3 WHERE k=2; }
step "s3_upd_all_k_except_2" { UPDATE test SET v=3 where k!=2; }
step "s3c"	{ COMMIT; }

# S2 and S3 deadlock on each other but are also blocked on S1 on various tablets. So, it is
# possible for the tablet writes which are not involved in a deadlock to throw another error.
permutation "s1_begin" "s2_begin" "s3_begin" "s1_acq_lock_k3+" "s2_upd_k1" "s3_upd_k2" "s2_upd_all_k_except_1" "s3_upd_all_k_except_2" "s1c" "s2c" "s3c" "s1_select"

# The below test case is just extra for a sanity check, it doesn't really test the case mentioned in
# the comment blob above.
permutation "s2_begin" "s3_begin" "s2_upd_k1" "s3_upd_k2" "s2_upd_all_k_except_1" "s3_upd_all_k_except_2" "s2c" "s3c" "s1_select"
