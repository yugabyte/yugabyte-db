# Test SKIP LOCKED with single shard transactions

setup
{
  CREATE TABLE test (k int PRIMARY KEY, v int);
  INSERT INTO test VALUES (1, 1);
}

teardown
{
  DROP TABLE test;
}

session "s1"
setup { BEGIN; }
step "s1a"	{ SELECT * FROM test where k=1 FOR UPDATE; }
step "s1c"	{ COMMIT; }

session "s2"
step "s2a"	{ SELECT * FROM test WHERE k=1 FOR UPDATE SKIP LOCKED; }
# Below is to ensure this was a single shard transaction, this "COMMIT" should throw a warning
step "s2c"	{ COMMIT; }

permutation "s1a" "s2a" "s1c" "s2c"