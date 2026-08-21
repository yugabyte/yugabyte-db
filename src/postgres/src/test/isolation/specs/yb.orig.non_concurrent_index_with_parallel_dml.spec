setup
{
  DROP TABLE IF EXISTS test;
  CREATE TABLE test (
    k	int		PRIMARY KEY,
    v   int
  );
  INSERT INTO test SELECT i,i from generate_series(1, 10) as i;
}

teardown
{
  DROP TABLE test;
}

session "s1"
step "s1begin"          { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s1update"	        { UPDATE test SET v=100 WHERE k=4; }
step "s1commit"	        { COMMIT; }


session "s2"
step "s2begin"          { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s2indexcreate"    { CREATE INDEX idx_test ON test(v); }
step "s2select"	        { SELECT * FROM test ORDER BY k; }
step "s2update"	        { UPDATE test SET v=200 WHERE k=5; }
step "s2commit"	        { COMMIT; }
step "s2indexcheck"     { SELECT yb_index_check('idx_test'::regclass); }

# This test is to ensure that the CREATE INDEX NON CONCURRENTLY sees the changes
# from the CONCURRENT UPDATE.
permutation "s1begin" "s1update" "s2begin" "s2indexcreate" "s1commit" "s2select" "s2commit" "s2indexcheck"
permutation "s1begin" "s1update" "s2begin" "s2select" "s2indexcreate" "s1commit" "s2select" "s2commit" "s2indexcheck"
permutation "s1begin" "s1update" "s2begin" "s2update" "s2indexcreate" "s1commit" "s2select" "s2commit" "s2indexcheck"
