setup
{
  CREATE TABLE test (
     k INT PRIMARY KEY,
     v INT
  );

  INSERT INTO test VALUES (1, 1);
}

teardown
{
  DROP TABLE test;
}

session "s1"
setup			{ BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s1_priority"		{ SET yb_transaction_priority_lower_bound = 0.6; }
step "s1_update"		{ UPDATE test SET v=5 where k=1; }
step "s1_commit"		{ COMMIT; }

session "s2"
setup			{ BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s2_priority"		{ SET yb_transaction_priority_upper_bound = 0.4; }
step "s2_update"		{ UPDATE test SET v=6 where k=1; }
step "s2_rollback"		{ ROLLBACK; }

permutation "s1_priority" "s2_priority" "s1_update" "s2_update" "s1_commit" "s2_rollback"
