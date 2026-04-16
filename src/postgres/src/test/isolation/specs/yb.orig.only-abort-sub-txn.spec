setup
{
  CREATE TABLE tbl (k INT PRIMARY KEY, v INT);
  INSERT INTO tbl SELECT i, i FROM GENERATE_SERIES(1, 10) i;
}

teardown
{
  DROP TABLE tbl;
}

session "s1"
setup			{ BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s1_pick_read_time"		{ SELECT * FROM tbl; }
step "s1_savepoint_a"	{ SAVEPOINT a; }
step "s1_update_conflicting_key"		{ UPDATE tbl SET v = 3 WHERE k = 1; }
step "s1_rollback_to_a"		{ ROLLBACK TO a; }
step "s1_update_non_conflicting_key"		{ UPDATE tbl SET v = 3 WHERE k = 2; }
step "s1_commit"		{ COMMIT; }

session "s2"
step "s2_update_conflicting_key"		{ UPDATE tbl SET v = 2 WHERE k = 1; }

permutation "s1_pick_read_time" "s2_update_conflicting_key" "s1_savepoint_a" "s1_update_conflicting_key" "s1_rollback_to_a" "s1_update_non_conflicting_key" "s1_commit"
