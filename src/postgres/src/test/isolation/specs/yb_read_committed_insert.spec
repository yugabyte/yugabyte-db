# Tests for the Read Committed isolation

setup
{
 create table test (k int primary key, v int);
 insert into test values (1, 1);
}

teardown
{
 DROP TABLE test;
}

session "s1"
setup { BEGIN ISOLATION LEVEL READ COMMITTED; }
step "insert_k1" { insert into test values (1, 1); }
step "insert_k2" { insert into test values (2, 1); }
step "insert_k1_on_conflict" { insert into test values (1, 1) on conflict (k) do update set v=100; }
step "insert_k2_on_conflict" { insert into test values (2, 1) on conflict (k) do update set v=100; }
step "select" { select * from test; }
step "r1" { rollback; }
step "c1" { commit; }

session "s2"
setup	{ BEGIN ISOLATION LEVEL READ COMMITTED; }
step "update_k1_to_2"	{ update test set k=2 where k=1; }
step "c2" { commit; }

permutation "update_k1_to_2" "insert_k1" "c2" "select" "c1"
permutation "update_k1_to_2" "insert_k2" "c2" "r1" "select"
permutation "update_k1_to_2" "insert_k1_on_conflict" "c2" "select" "c1"
permutation "update_k1_to_2" "insert_k2_on_conflict" "c2" "select" "c1"