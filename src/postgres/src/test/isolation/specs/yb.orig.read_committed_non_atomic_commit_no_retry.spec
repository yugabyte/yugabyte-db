# Test to ensure that CALL/DO statements with in-procedure COMMIT are NOT
# retried after a conflict error.
#
# When a stored procedure or DO block performs a COMMIT, that work is
# permanently committed. If a subsequent statement in the same procedure hits
# a kConflict error, the retry logic must NOT re-execute the entire CALL/DO
# from scratch, as that would re-execute already-committed work (potentially
# causing duplicate inserts or other incorrect behavior).
#
# The test uses a side table (without a primary key) to track how many times
# the procedure body executes. The key invariant is: the side table must
# contain exactly 1 row, proving the INSERT+COMMIT ran only once and the
# CALL/DO was not retried.
#
# The test works as follows:
# 1. Session 1 begins a READ COMMITTED transaction and updates row k=1,
#    holding a row lock.
# 2. Session 2 calls a procedure (or DO block) that:
#    a. Inserts a row into the side table (no PK, so duplicates are allowed).
#    b. COMMITs (the insert is now permanently committed).
#    c. Updates row k=1 -- this blocks waiting for session 1's lock.
# 3. Session 1 commits, releasing the lock.
# 4. Session 2 wakes up and receives a kConflict error (Read Committed
#    requires a statement retry with a new snapshot). The guard prevents the
#    retry and the error is surfaced to the user.
# 5. We verify that the side table has exactly 1 row (not 2), proving the
#    procedure was not re-executed from the beginning.
#
# The last permutation tests the GUC yb_enable_retry_after_non_atomic_commit
# which restores the old (unsafe) retry behavior. When enabled, the retry
# happens and the side table ends up with 2 rows (the committed INSERT is
# re-executed).

setup
{
  CREATE TABLE test (k INT PRIMARY KEY, v INT);
  INSERT INTO test VALUES (1, 1);
  CREATE TABLE side_table (v INT);
  CREATE PROCEDURE proc_with_commit() AS $$
  BEGIN
    INSERT INTO side_table VALUES (1);
    COMMIT;
    UPDATE test SET v = v + 10;
  END $$ LANGUAGE PLPGSQL;
}

teardown
{
  DROP PROCEDURE proc_with_commit;
  DROP TABLE test;
  DROP TABLE side_table;
}

session "s1"
setup		{ BEGIN ISOLATION LEVEL READ COMMITTED; }
step "s1_update"	{ UPDATE test SET v = 100 WHERE k = 1; }
step "s1_commit"	{ COMMIT; }

session "s2"
setup		{ SET default_transaction_isolation = 'read committed'; }
step "s2_call"			{ CALL proc_with_commit(); }
step "s2_do"			{ DO $$ BEGIN INSERT INTO side_table VALUES (1); COMMIT; UPDATE test SET v = v + 10; END $$; }
step "s2_enable_guc"		{ SET yb_enable_retry_after_non_atomic_commit = true; }
step "s2_select_test"		{ SELECT * FROM test ORDER BY k; }
step "s2_select_side_table"	{ SELECT COUNT(*) AS side_table_updates FROM side_table; }

# Test CALL with in-procedure COMMIT: should error, not retry.
# The side table must have exactly 1 row.
permutation "s1_update" "s2_call" "s1_commit" "s2_select_test" "s2_select_side_table"

# Test DO block with in-procedure COMMIT: should error, not retry.
# The side table must have exactly 1 row.
permutation "s1_update" "s2_do" "s1_commit" "s2_select_test" "s2_select_side_table"

# Test with the GUC override: the retry IS attempted, and the side table ends
# up with 2 rows because the INSERT+COMMIT is re-executed on retry.
permutation "s2_enable_guc" "s1_update" "s2_call" "s1_commit" "s2_select_test" "s2_select_side_table"
