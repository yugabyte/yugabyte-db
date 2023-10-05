setup
{
  CREATE TABLE test (k int PRIMARY KEY, v int);
}

teardown
{
  DROP TABLE test;
}

session "s1"
setup		{ BEGIN; }
step "s1_ins"	{ INSERT INTO test VALUES (2, 2); }
step "s1_commit"	{ COMMIT; }
step "s1_select"	{ SELECT * FROM test; }

session "s2"
step "s2_ins"	{ INSERT INTO test VALUES (2, 4); }
step "s2_multi_insert" { INSERT INTO test VALUES (1, 1), (2, 4), (3, 3); }

session "s3"
step "s3_del"	{ DELETE FROM test WHERE k=2; }

permutation "s1_ins" "s2_ins" "s1_commit" "s1_select"
permutation "s1_ins" "s3_del" "s2_ins" "s1_commit" "s1_select"
permutation "s1_ins" "s2_multi_insert" "s1_commit" "s1_select"
permutation "s1_ins" "s3_del" "s2_multi_insert" "s1_commit" "s1_select"
