setup
{
  CREATE TABLE t (key INT PRIMARY KEY, v INT);
}

teardown
{
  DROP TABLE t;
}

session "s1"
setup			{ BEGIN; }
step "s1_insert_1_1"		{ INSERT INTO t VALUES (1, 1); }
step "s1_savepoint_a"	{ SAVEPOINT a; }
step "s1_insert_2_1"		{ INSERT INTO t VALUES (2, 1); }
step "s1_rollback_to_a"		{ ROLLBACK TO a; }
step "s1_commit"		{ COMMIT; }
step "s1_select"		{ SELECT * FROM t; }
step "s1_sleep" { SELECT pg_sleep(2); }

session "s2"
setup			{ BEGIN; }
step "s2_insert_2_2"		{ INSERT INTO t VALUES (2, 2); }
step "s2_commit"		{ COMMIT; }

permutation "s1_insert_1_1" "s1_savepoint_a" "s1_insert_2_1" "s1_rollback_to_a" "s2_insert_2_2" "s1_commit" "s2_commit" "s1_select"
permutation "s1_insert_1_1" "s1_savepoint_a" "s1_insert_2_1" "s1_rollback_to_a" "s1_sleep" "s2_insert_2_2" "s1_commit" "s2_commit" "s1_select"