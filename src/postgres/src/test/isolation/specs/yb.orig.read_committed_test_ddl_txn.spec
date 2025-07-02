# Tests for the DDLs executed in Read Committed isolation with Transactional DDL enabled.

setup
{
 create table test (k int primary key, v int);
}

teardown
{
 DROP TABLE test;
}

session "s1"
step "b1" { BEGIN ISOLATION LEVEL READ COMMITTED; }
step "insert_11_row_in_test" { insert into test values (1, 1); }
step "update_11_row_in_s1" { update test set v = 100 where k = 1; }
step "s1_add_column_a" { alter table test add column a int; }
step "c1" { commit; }

session "s2"
step "b2" { BEGIN ISOLATION LEVEL READ COMMITTED; }
step "insert_22_row_in_test" { insert into test values (2, 2); }
step "update_11_row_in_s2" { update test set v = 150 where k = 1; }
step "s2_add_column_b" { alter table test add column b int; }
step "c2" { commit; }

session "s3"
step "select_test" { select * from test; }

# Query layer retry should not happen for the two add column DDLs.
permutation "b1" "b2" "insert_11_row_in_test" "insert_22_row_in_test" "s1_add_column_a" "s2_add_column_b" "c2" "c1" "select_test"

# Query layer retry should not happen even if 's2_add_column_b' is the first statement of the txn.
permutation "b1" "b2" "insert_11_row_in_test" "s1_add_column_a" "s2_add_column_b" "c1" "c2" "select_test"

# Query layer retry should not happen for DDLs without txn block
permutation "b1" "insert_11_row_in_test" "s1_add_column_a" "s2_add_column_b" "c1" "select_test"

# DMLs executed after DDLs should be retried as usual
# "update_11_row_in_s1" step will face conflict and get retried
permutation "insert_11_row_in_test" "b1" "b2" "s1_add_column_a" "update_11_row_in_s2" "update_11_row_in_s1" "c2" "c1" "select_test"
