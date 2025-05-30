setup
{
  CREATE TABLE test (k INT PRIMARY KEY, v INT);
  INSERT INTO test values (1, 1);
  CREATE TABLE test2 (k SERIAL PRIMARY KEY, v INT);
}

teardown
{
  DROP TABLE test, test2;
}

session "s1"
step "s1_begin" { BEGIN; }
step "s1_update" { UPDATE test SET v=0 where k=1; }
step "s1_commit" { COMMIT; }

session "s2"
step "s2_implicit_txn_a" { INSERT INTO test2 (v) values (1); UPDATE test SET v=v+1; }
step "s2_implicit_txn_b" { UPDATE test SET v=v+1; INSERT INTO test2 (v) values (1); }
step "s2_explicit_txn_block_a" { BEGIN; INSERT INTO test2 (v) values (1); UPDATE test SET v=v+1; COMMIT; }
step "s2_explicit_txn_block_b" { BEGIN; UPDATE test SET v=v+1; INSERT INTO test2 (v) values (1); COMMIT; }
step "s2_statement_after_commit_a" { BEGIN; INSERT INTO test2 (v) values (1); COMMIT; UPDATE test SET v=v+1; }
step "s2_statement_after_commit_b" { BEGIN; INSERT INTO test2 (v) values (1); COMMIT; INSERT INTO test2 (v) values (2); UPDATE test SET v=v+1; }
step "s2_commit" { COMMIT; }
step "s2_select_test" { SELECT * FROM test; }
step "s2_select_test2" { SELECT * FROM test2; }

# Test to ensure that statements in an implicit transaction are not retried.
permutation "s1_begin" "s1_update" "s2_implicit_txn_a" "s1_commit" "s2_select_test" "s2_select_test2"
permutation "s1_begin" "s1_update" "s2_implicit_txn_b" "s1_commit" "s2_select_test" "s2_select_test2"

# Test to ensure that a statement within an explicit transaction block isn't retried, be it the
# first statement, or a later one.
#
# Note that the error is thrown back to the external client immediately and later statements of the
# query are not executed. This leaves the transaction block in an open state requiring a closing
# statement before new statements. Without closing the block, we will receive a
# "current transaction is aborted, commands ignored until end of transaction block" error. Pg has
# this same quirk: it can't be seen with an RC transaction because a conflict will be resolved in Pg
# using READ COMMITTED update checking rules. So, to observe it in Pg, use REPEATABLE READ in s2.
permutation "s1_begin" "s1_update" "s2_explicit_txn_block_a" "s1_commit" "s2_select_test" "s2_commit" "s2_select_test" "s2_select_test2"
permutation "s1_begin" "s1_update" "s2_explicit_txn_block_b" "s1_commit" "s2_select_test" "s2_commit" "s2_select_test" "s2_select_test2"

# Test to ensure that a statement in an implicit transaction after an intervening "COMMIT;" isn't
# retried. The s2_statement_after_commit_b case is to ensure that the tablet test2 only has 1 row
# inserted (i.e., the whole implicit transaction after the "commit;" is aborted).
permutation "s1_begin" "s1_update" "s2_statement_after_commit_a" "s1_commit" "s2_select_test" "s2_select_test2"
permutation "s1_begin" "s1_update" "s2_statement_after_commit_b" "s1_commit" "s2_select_test" "s2_select_test2"
