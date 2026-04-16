# Test to ensure that the transaction read point is reset for each DML statement even after a DDL
# has executed in a READ COMMITTED transaction block.

setup
{
  CREATE TABLE test (k INT PRIMARY KEY, v INT);
  CREATE TABLE test2 (k INT PRIMARY KEY);
}

teardown
{
  DROP TABLE test, test2;
}

session "s1"
step "s1_begin" { BEGIN ISOLATION LEVEL READ COMMITTED; }
step "s1_select" { SELECT * FROM test; }
step "s1_alter" { ALTER TABLE test2 ADD COLUMN v int; }
step "s1_sleep" { SELECT pg_sleep(1); }
step "s1_commit" { commit; }

session "s2"
step "s2_insert" { insert into test values (1, 1); }

# Without the sleep before s2_insert, the test would pass even if the transaction read point wasn't
# reset in the second invcation of the select because a read restart would occur due to the insert
# happening within the clock skew ambiguity window and the inserted row would be visible on the
# statement retry.
permutation "s1_begin" "s1_select" "s1_alter" "s1_sleep" "s2_insert" "s1_select" "s1_commit"
