# Tests for the Read Committed isolation

setup
{
 create table test (k int primary key, v int);
 insert into test values (1, 1), (2, 2);
}

teardown
{
 DROP TABLE test;
}

session "s1"
setup { BEGIN ISOLATION LEVEL READ COMMITTED; }
step "update_k1_in_s1"	{ update test set v=10 where k=1; }
step "update_k2_in_s1"	{ update test set v=20 where k=2; }
step "select" { select * from test; }
step "c1" { commit; }

session "s2"
setup	{ BEGIN ISOLATION LEVEL READ COMMITTED; }
step "update_k2_in_s2"	{ update test set v=40 where k=2; }
step "c2" { commit; }



# When session 1 faces a conflict while writing k=2, it will wait for session 2 to complete and then
# retry the write for k=2. Once the write for k=2 is done and session 1 commits, we should be able
# to see the update to k=1 as well. It should not be lost due to any reason.

# Motivation for below test:
# --------------------------
# There was an existing bug with the automatic per-statement retries for kReadRestart errors in
# READ COMMITTED isolation. A retry would restart the whole txn and then re-run the statement. But
# that results in removal of previous writes. Instead of restarting the whole txn, we should have
# set an internal savepoint before running the statement and rolled back to it. To ensure we are
# not restarting the txn, we are testing with kConflict errors instead of kReadRestart.

permutation "update_k2_in_s2" "update_k1_in_s1" "update_k2_in_s1" "c2" "select" "c1"
