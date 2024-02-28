setup
{
  DROP TABLE IF EXISTS foo;
  CREATE TABLE foo (
    k	int		PRIMARY KEY,
    v	int 	NOT NULL
  );

  INSERT INTO foo SELECT generate_series(0, 100), 0;
}

teardown
{
  DROP TABLE foo;
}

session "s1"
setup {
  BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;
  UPDATE foo SET v=100 WHERE k=0;
  SAVEPOINT a;
  UPDATE foo SET v=1 WHERE k=1;
  SAVEPOINT b;
  UPDATE foo SET v=2 WHERE k=2;
}
step "s1_rollback_b"    { ROLLBACK TO b; }
step "s1_rollback_a"    { ROLLBACK TO a; }
step "s1_commit"	      { COMMIT; }

session "s2"
step "s2_update_1"		  { UPDATE foo SET v=10 WHERE k=1; }
step "s2_select"        { SELECT * FROM foo WHERE k<3 ORDER BY k; }

session "s3"
step "s3_update_2"		  { UPDATE foo SET v=20 WHERE k=2; }

session "s4"
step "s4_begin"		      { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s4_update_2"      { UPDATE foo SET v=20 WHERE k=2; }
step "s4_update_1"      { UPDATE foo SET v=10 WHERE k=1; }
step "s4_commit"	      { COMMIT; }

permutation "s2_update_1" "s3_update_2" "s1_rollback_b" "s1_rollback_a" "s1_commit" "s2_select"
permutation "s4_begin" "s4_update_2" "s1_rollback_b" "s4_update_1" "s1_rollback_a" "s4_commit" "s1_commit" "s2_select"