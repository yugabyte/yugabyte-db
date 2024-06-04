# Tests for the Read Committed isolation

setup
{
 create table test (k int primary key, v int);
 insert into test values (0, 5), (1, 5), (2, 5), (3, 5), (4, 1);
}

teardown
{
 DROP TABLE test;
}

# The test checks semantics of UPDATE, SELECT FOR UPDATE, SELECT FOR NO KEY UPDATE,
# SELECT FOR KEY SHARE, and SELECT FOR SHARE with the following predicate - where v>=5;
# One of these statements will be run from session 1.

# We check the semantics of the statement in session 1 against 6 cases -
#   1. Concurrent insertion of a new row that satisfies the predicate.
#   2. Concurrent deletion of an existing row that satisfies the predicate.
#   3. Concurrent update that changes the predicate from -
# 		 i) true to false
# 		 ii) true to true
# 		 iii) false to true
#   4. Concurrent pk update of row that satifies predicate

session "s1"
setup { BEGIN ISOLATION LEVEL READ COMMITTED; }
step "update" { update test set v=100 where v>=5; }
step "select_for_update" { select * from test where v>=5 for update; }
step "select_for_no_key_update" { select * from test where v>=5 for no key update; }
step "select_for_key_share" { select * from test where v>=5 for key share; }
step "select_for_share" { select * from test where v>=5 for share; }
step "select" { select * from test; }
step "c1" { commit; }

session "s2"
setup	{ BEGIN ISOLATION LEVEL READ COMMITTED; }
step "insert_new_satisfying_row_k_5"	{ insert into test values (5, 5); }
step "delete_satisfying_row" { delete from test where k=3; }
step "update_true_to_false" { update test set v=1 where k=1; }
step "update_true_to_true" { update test set v=10 where k=2; }
step "update_false_to_true" { update test set v=10 where k=4; }
step "pk_update" { update test set k=10 where k=0; }
step "c2" { commit; }

permutation "insert_new_satisfying_row_k_5" "delete_satisfying_row" "update_true_to_false"
  "update_true_to_true" "update_false_to_true" "pk_update" "update" "c2" "select" "c1"