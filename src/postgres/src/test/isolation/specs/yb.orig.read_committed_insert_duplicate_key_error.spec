# Tests for the Read Committed isolation

setup
{
 CREATE TABLE t (k INT PRIMARY KEY, v1 INT, v2 int);
 CREATE INDEX ON t(v1);
 CREATE UNIQUE INDEX ON t(v2);
}

teardown
{
 DROP TABLE t;
}

session "s1"
setup { BEGIN ISOLATION LEVEL READ COMMITTED; }
step "s1_insert_1_1_1" { INSERT INTO t VALUES(1, 1, 1); }
step "s1_insert_2_1_1" { INSERT INTO t VALUES(2, 1, 1); }
step "s1_commit" { COMMIT; }
step "s1_rollback" { ROLLBACK; }
step "s1_select" { SELECT * FROM T; }

session "s2"
setup	{ BEGIN ISOLATION LEVEL READ COMMITTED; }
step "s2_insert_1_1_2" { INSERT INTO t VALUES(1, 1, 2); }
step "s2_insert_2_1_2" { INSERT INTO t VALUES(2, 1, 2); }
step "s1_insert_3_1_1" { INSERT INTO t VALUES(3, 1, 1); }
step "s2_commit" { COMMIT; }
step "s2_rollback" { ROLLBACK; }

# Duplicate key error due to pk
permutation "s1_insert_1_1_1" "s2_insert_1_1_2" "s1_commit" "s2_rollback" "s1_select"
# Duplicate key error due to unique index
permutation "s1_insert_2_1_1" "s1_insert_3_1_1" "s1_commit" "s2_rollback" "s1_select"
# Duplicate non unique index value, but no error
permutation "s1_insert_1_1_1" "s2_insert_2_1_2" "s1_commit" "s2_commit" "s1_select"
