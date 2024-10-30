setup
{
  CREATE TABLE test (
     k INT PRIMARY KEY,
     v INT
  );

  INSERT INTO test VALUES (1, 2);
}

teardown
{
  DROP TABLE test;
}

session "s1"

# Commands for 4 methods to start a transaction in a desired isolation level
step "s1_begin_rc_method1" { BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED; }
step "s1_begin_rr_method1" { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s1_begin_sr_method1" { BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE; }

step "s1_begin_rc_method2" { BEGIN; SET TRANSACTION ISOLATION LEVEL READ COMMITTED; }
step "s1_begin_rr_method2" { BEGIN; SET TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s1_begin_sr_method2" { BEGIN; SET TRANSACTION ISOLATION LEVEL SERIALIZABLE; }

step "s1_begin_rc_method3_part1" { SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL READ COMMITTED; }
step "s1_begin_rr_method3_part1" { SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s1_begin_sr_method3_part1" { SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL SERIALIZABLE; }
# "BEGIN;" is added as a separate step to ensure this picks the new default isolation level that is set.
# If it is run as a statement as part of the same client issued request, it doesn't pick the newly set isolation level.
step "s1_method3_part2" { BEGIN; }

step "s1_begin_rc_method4" { SET default_transaction_isolation='READ COMMITTED'; BEGIN; }
step "s1_begin_rr_method4" { SET default_transaction_isolation='REPEATABLE READ'; BEGIN; }
step "s1_begin_sr_method4" { SET default_transaction_isolation='SERIALIZABLE'; BEGIN; }

step "s1_switch_to_rc" { SET TRANSACTION ISOLATION LEVEL READ COMMITTED; }
step "s1_switch_to_rr" { SET TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s1_switch_to_sr" { SET TRANSACTION ISOLATION LEVEL SERIALIZABLE; }

step "s1_select_1"  { SELECT 1; }
step "s1_select"		{ SELECT * FROM test; }
step "s1_update"		{ UPDATE test SET v=v+1 WHERE k=1; }
step "s1_savepoint"		{ SAVEPOINT a; }
step "s1_read_only" { SET TRANSACTION READ ONLY; }
step "s1_read_write" { SET TRANSACTION READ WRITE; }
step "s1_commit"		{ COMMIT; }
step "s1_rollback"		{ ROLLBACK; }

session "s2"

# Commands for 4 methods to start a transaction in a desired isolation level
step "s2_begin_rc_method1" { BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED; }
step "s2_begin_rr_method1" { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s2_begin_sr_method1" { BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE; }

step "s2_begin_rc_method2" { BEGIN; SET TRANSACTION ISOLATION LEVEL READ COMMITTED; }
step "s2_begin_rr_method2" { BEGIN; SET TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s2_begin_sr_method2" { BEGIN; SET TRANSACTION ISOLATION LEVEL SERIALIZABLE; }

step "s2_begin_rc_method3_part1" { SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL READ COMMITTED; }
step "s2_begin_rr_method3_part1" { SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s2_begin_sr_method3_part1" { SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL SERIALIZABLE; }
step "s2_method3_part2" { BEGIN; }

step "s2_begin_rc_method4" { SET default_transaction_isolation='READ COMMITTED'; BEGIN; }
step "s2_begin_rr_method4" { SET default_transaction_isolation='REPEATABLE READ'; BEGIN; }
step "s2_begin_sr_method4" { SET default_transaction_isolation='SERIALIZABLE'; BEGIN; }

step "s2_switch_to_rc" { SET TRANSACTION ISOLATION LEVEL READ COMMITTED; }
step "s2_switch_to_rr" { SET TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s2_switch_to_sr" { SET TRANSACTION ISOLATION LEVEL SERIALIZABLE; }

step "s2_select"		{ SELECT * FROM test; }
step "s2_update"		{ UPDATE test SET v=v*2 WHERE k=1; }
step "s2_deferrable" { SET TRANSACTION DEFERRABLE; }
step "s2_read_only" { SET TRANSACTION READ ONLY; }
step "s2_commit"		{ COMMIT; }
step "s2_rollback"		{ ROLLBACK; }

# Test all possibilities of switching from RC/ SR to RR.
#
# A SELECT statement is to be executed at the start of an RR/ SR isolation transaction to ensure that transaction restarts don't occur on conflict errors in the UPDATE statements.
# This helps distinctly observe the different semantics in RC vs RR vs SR. Also note that SR transactions face a deadlock error because the SELECT statement also acquires locks. This differentiates SR semantics from RR.

permutation "s1_begin_rc_method1" "s2_begin_sr_method1" "s1_switch_to_rr" "s2_switch_to_rr" "s1_select" "s2_select" "s1_update" "s2_update" "s1_commit" "s2_rollback" "s2_select"
permutation "s1_begin_rc_method2" "s2_begin_sr_method2" "s1_switch_to_rr" "s2_switch_to_rr" "s1_select" "s2_select" "s1_update" "s2_update" "s1_commit" "s2_rollback" "s2_select"
permutation "s1_begin_rc_method3_part1" "s1_method3_part2" "s2_begin_sr_method3_part1" "s2_method3_part2" "s1_switch_to_rr" "s2_switch_to_rr" "s1_select" "s2_select" "s1_update" "s2_update" "s1_commit" "s2_rollback" "s2_select"
permutation "s1_begin_rc_method4" "s2_begin_sr_method4" "s1_switch_to_rr" "s2_switch_to_rr" "s1_select" "s2_select" "s1_update" "s2_update" "s1_commit" "s2_rollback" "s2_select"

# Test all possibilities of switching from RR/ SR to RC.
#
# A SELECT statement is to be executed at the start of an RR/ SR isolation transaction to ensure that transaction restarts don't occur on conflict errors in the UPDATE statements.
# This helps distinctly observe the different semantics in RC vs RR vs SR. Also note that SR transactions face a deadlock error because the SELECT statement also acquires locks. This differentiates SR semantics from RR.

permutation "s1_begin_rr_method1" "s2_begin_sr_method1" "s1_switch_to_rc" "s2_switch_to_rc" "s1_select" "s2_select" "s1_update" "s2_update" "s1_commit" "s2_commit" "s2_select"
permutation "s1_begin_rr_method2" "s2_begin_sr_method2" "s1_switch_to_rc" "s2_switch_to_rc" "s1_select" "s2_select" "s1_update" "s2_update" "s1_commit" "s2_commit" "s2_select"
permutation "s1_begin_rr_method3_part1" "s1_method3_part2" "s2_begin_sr_method3_part1" "s2_method3_part2" "s1_switch_to_rc" "s2_switch_to_rc" "s1_select" "s2_select" "s1_update" "s2_update" "s1_commit" "s2_commit" "s2_select"
permutation "s1_begin_rr_method4" "s2_begin_sr_method4" "s1_switch_to_rc" "s2_switch_to_rc" "s1_select" "s2_select" "s1_update" "s2_update" "s1_commit" "s2_commit" "s2_select"

# Test all possibilities of switching from RC/ RR to SR.
#
# A SELECT statement is to be executed at the start of an RR/ SR isolation transaction to ensure that transaction restarts don't occur on conflict errors in the UPDATE statements.
# This helps distinctly observe the different semantics in RC vs RR vs SR. Also note that SR transactions face a deadlock error because the SELECT statement also acquires locks. This differentiates SR semantics from RR.

permutation "s1_begin_rc_method1" "s2_begin_rr_method1" "s1_switch_to_sr" "s2_switch_to_sr" "s1_select" "s2_select" "s1_update" "s2_update" "s1_commit" "s2_rollback" "s2_select"
permutation "s1_begin_rc_method2" "s2_begin_rr_method2" "s1_switch_to_sr" "s2_switch_to_sr" "s1_select" "s2_select" "s1_update" "s2_update" "s1_commit" "s2_rollback" "s2_select"
permutation "s1_begin_rc_method3_part1" "s1_method3_part2" "s2_begin_rr_method3_part1" "s2_method3_part2" "s1_switch_to_sr" "s2_switch_to_sr" "s1_select" "s2_select" "s1_update" "s2_update" "s1_commit" "s2_rollback" "s2_select"
permutation "s1_begin_rc_method4" "s2_begin_rr_method4" "s1_switch_to_sr" "s2_switch_to_sr" "s1_select" "s2_select" "s1_update" "s2_update" "s1_commit" "s2_rollback" "s2_select"

# Setting isolation level is not allowed after a statement, other than the ones that change transaction characteristics, has been executed.
permutation "s1_begin_rc_method1" "s1_select_1" "s1_switch_to_rr" "s1_rollback"
permutation "s1_begin_rr_method1" "s1_select_1" "s1_switch_to_rc" "s1_rollback"
permutation "s1_begin_sr_method1" "s1_select_1" "s1_switch_to_rc" "s1_rollback"

permutation "s1_begin_rc_method2" "s1_select_1" "s1_switch_to_rr" "s1_rollback"
permutation "s1_begin_rr_method2" "s1_select_1" "s1_switch_to_rc" "s1_rollback"
permutation "s1_begin_sr_method2" "s1_select_1" "s1_switch_to_rc" "s1_rollback"

permutation "s1_begin_rc_method3_part1" "s1_method3_part2" "s1_select_1" "s1_switch_to_rr" "s1_rollback"
permutation "s1_begin_rr_method3_part1" "s1_method3_part2" "s1_select_1" "s1_switch_to_rc" "s1_rollback"
permutation "s1_begin_sr_method3_part1" "s1_method3_part2" "s1_select_1" "s1_switch_to_rc" "s1_rollback"

permutation "s1_begin_rc_method4" "s1_select_1" "s1_switch_to_rr" "s1_rollback"
permutation "s1_begin_rr_method4" "s1_select_1" "s1_switch_to_rc" "s1_rollback"
permutation "s1_begin_sr_method4" "s1_select_1" "s1_switch_to_rc" "s1_rollback"

# Setting isolation level is not allowed after a subtransaction has been created.
permutation "s1_begin_rc_method1" "s1_savepoint" "s1_switch_to_rr" "s1_rollback"
permutation "s1_begin_rr_method1" "s1_savepoint" "s1_switch_to_rc" "s1_rollback"
permutation "s1_begin_sr_method1" "s1_savepoint" "s1_switch_to_rc" "s1_rollback"

permutation "s1_begin_rc_method2" "s1_savepoint" "s1_switch_to_rr" "s1_rollback"
permutation "s1_begin_rr_method2" "s1_savepoint" "s1_switch_to_rc" "s1_rollback"
permutation "s1_begin_sr_method2" "s1_savepoint" "s1_switch_to_rc" "s1_rollback"

permutation "s1_begin_rc_method3_part1" "s1_method3_part2" "s1_savepoint" "s1_switch_to_rr" "s1_rollback"
permutation "s1_begin_rr_method3_part1" "s1_method3_part2" "s1_savepoint" "s1_switch_to_rc" "s1_rollback"
permutation "s1_begin_sr_method3_part1" "s1_method3_part2" "s1_savepoint" "s1_switch_to_rc" "s1_rollback"

permutation "s1_begin_rc_method4" "s1_savepoint" "s1_switch_to_rr" "s1_rollback"
permutation "s1_begin_rr_method4" "s1_savepoint" "s1_switch_to_rc" "s1_rollback"
permutation "s1_begin_sr_method4" "s1_savepoint" "s1_switch_to_rc" "s1_rollback"

# A change to DEFERRABLE characteristic is allowed only if no other statement, other than ones that that change transaction characteristics, has executed.
# DEFERRABLE doesn't make any sense for any mode other than SERIALIZABLE + READ ONLY, hence testing only for that (ref: https://www.postgresql.org/docs/current/sql-set-transaction.html)
#
# TODO: Due to GH issue #23120, the SELECT statement in session 2 waits for the concurrent transaction in session 1 to commit. Fix this once #23120 is resolved.

permutation "s2_begin_rc_method1" "s2_switch_to_sr" "s2_read_only" "s2_deferrable" "s1_begin_sr_method1" "s1_update" "s2_select" "s1_commit" "s2_commit"
permutation "s2_begin_rc_method2" "s2_switch_to_sr" "s2_read_only" "s2_deferrable" "s1_begin_sr_method1" "s1_update" "s2_select" "s1_commit" "s2_commit"
permutation "s2_begin_rc_method3_part1" "s2_method3_part2" "s2_switch_to_sr" "s2_read_only" "s2_deferrable" "s1_begin_sr_method1" "s1_update" "s2_select" "s1_commit" "s2_commit"
permutation "s2_begin_rc_method4" "s2_switch_to_sr" "s2_read_only" "s2_deferrable" "s1_begin_sr_method1" "s1_update" "s2_select" "s1_commit" "s2_commit"

permutation "s2_begin_rr_method1" "s2_switch_to_sr" "s2_read_only" "s2_deferrable" "s1_begin_sr_method1" "s1_update" "s2_select" "s1_commit" "s2_commit"
permutation "s2_begin_rr_method2" "s2_switch_to_sr" "s2_read_only" "s2_deferrable" "s1_begin_sr_method1" "s1_update" "s2_select" "s1_commit" "s2_commit"
permutation "s2_begin_rr_method3_part1" "s2_method3_part2" "s2_switch_to_sr" "s2_read_only" "s2_deferrable" "s1_begin_sr_method1" "s1_update" "s2_select" "s1_commit" "s2_commit"
permutation "s2_begin_rr_method4" "s2_switch_to_sr" "s2_read_only" "s2_deferrable" "s1_begin_sr_method1" "s1_update" "s2_select" "s1_commit" "s2_commit"

permutation "s2_begin_sr_method1" "s2_switch_to_sr" "s2_read_only" "s2_deferrable" "s1_begin_sr_method1" "s1_update" "s2_select" "s1_commit" "s2_commit"
permutation "s2_begin_sr_method2" "s2_switch_to_sr" "s2_read_only" "s2_deferrable" "s1_begin_sr_method1" "s1_update" "s2_select" "s1_commit" "s2_commit"
permutation "s2_begin_sr_method3_part1" "s2_method3_part2" "s2_switch_to_sr" "s2_read_only" "s2_deferrable" "s1_begin_sr_method1" "s1_update" "s2_select" "s1_commit" "s2_commit"
permutation "s2_begin_sr_method4" "s2_switch_to_sr" "s2_read_only" "s2_deferrable" "s1_begin_sr_method1" "s1_update" "s2_select" "s1_commit" "s2_commit"

# A change to READ-WRITE mode is allowed only if no statement, other than ones that that change transaction characteristics, has executed.
permutation "s1_begin_rc_method1" "s1_switch_to_sr" "s1_read_only" "s1_read_write" "s2_update" "s1_commit"

# A transaction can be made read-only even after executing some writes, but not vice-versa.
permutation "s1_begin_rc_method1" "s1_switch_to_rr" "s1_read_write" "s1_update" "s1_read_only" "s1_select" "s1_update" "s1_rollback"
permutation "s1_begin_rc_method1" "s1_switch_to_rr" "s1_read_write" "s1_update" "s1_read_only" "s1_select" "s1_read_write" "s1_rollback"