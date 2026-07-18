# Tests to ensure that DO and CALL are retried for ensuring Read Committed semantics

setup
{
 CREATE TABLE test (k INT PRIMARY KEY, v INT);
 INSERT INTO test VALUES (1, 1);
 CREATE PROCEDURE update_k1_in_s1() AS $$
  BEGIN
  UPDATE test SET v=v*5 WHERE k=1;
  END $$ LANGUAGE PLPGSQL;
 CREATE PROCEDURE update_k1_in_s2() AS $$
  BEGIN
  UPDATE test SET v=v+2 WHERE k=1;
  END $$ LANGUAGE PLPGSQL;
}

teardown
{
 DROP PROCEDURE update_k1_in_s1;
 DROP PROCEDURE update_k1_in_s2;
 DROP TABLE test;
}

session "s1"
setup { BEGIN ISOLATION LEVEL READ COMMITTED; }
step "update_k1_in_s1_do" { DO $$ BEGIN UPDATE test SET v=v*5 WHERE k=1; END $$; }
step "update_k1_in_s1_call" { CALL update_k1_in_s1(); }
step "select" { SELECT * FROM test; }
step "c1" { COMMIT; }

session "s2"
setup	{ BEGIN ISOLATION LEVEL READ COMMITTED; }
step "update_k1_in_s2_do"	{ DO $$ BEGIN UPDATE test SET v=v+2 WHERE k=1; END $$; }
step "update_k1_in_s2_call" { CALL update_k1_in_s2(); }
step "c2" { COMMIT; }

permutation "update_k1_in_s2_do" "update_k1_in_s1_do" "c2" "select" "c1"
permutation "update_k1_in_s2_call" "update_k1_in_s1_call" "c2" "select" "c1"
