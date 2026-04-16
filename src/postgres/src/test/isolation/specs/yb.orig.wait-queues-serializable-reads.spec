setup
{
  DROP TABLE IF EXISTS test;
  CREATE TABLE test (k	int	PRIMARY KEY, v	int);
  INSERT INTO test VALUES (1, 1);
}

teardown
{
  DROP TABLE test;
}

session "s1"
setup		{ BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE; }
step "s1_update" { UPDATE test SET v=2 WHERE k=1; }
step "s1_commit" { COMMIT; }

session "s2"
setup		{ BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE; }
step "s2_select" { SELECT * FROM test WHERE k=1; }
step "s2_commit" { COMMIT; }

permutation "s1_update" "s2_select" "s1_commit" "s2_commit"