setup
{
  create table demo (id bigint primary key, value int);
  insert into demo values (123, 103);
}

teardown
{
  DROP TABLE demo;
}

session "s1"

step "s1_txn_pri_lower" { set yb_transaction_priority_lower_bound to 0.1; }
step "s1_txn_pri_upper" { set yb_transaction_priority_upper_bound to 0.4; }
step "s1_begin" { begin transaction isolation level read committed; }
step "s1_update" { update demo set value=1 where id=123; }
step "s1_commit" { commit; }
step "s1_select" { select * from demo; }

session "s2"

step "s2_txn_pri_lower" { set yb_transaction_priority_lower_bound to 0.6; }
step "s2_txn_pri_upper" { set yb_transaction_priority_upper_bound to 0.9; }
step "s2_begin" { begin transaction isolation level read committed; }
step "s2_update" { update demo set value=2 where id=123; }
step "s2_commit" { commit; }
step "s2_select" { select * from demo; }

session "s3_fast_path"

step "s3_fast_pathtxn_pri_lower" { set yb_transaction_priority_lower_bound to 0.45; }
step "s3_fast_pathtxn_pri_upper" { set yb_transaction_priority_upper_bound to 0.55; }
step "s3_fast_path_update" { update demo set value=3 where id=123; }

permutation "s1_txn_pri_lower" "s1_txn_pri_upper" "s2_txn_pri_lower" "s2_txn_pri_upper"
"s1_begin" "s2_begin" "s1_update" "s2_update" "s1_commit" "s2_commit" "s1_select"

# Tests to check interaction of fast path with distributed transatcions
permutation "s1_txn_pri_lower" "s1_txn_pri_upper" "s3_fast_pathtxn_pri_lower"
"s3_fast_pathtxn_pri_upper" "s1_begin" "s1_update" "s3_fast_path_update" "s1_commit"
"s1_select"

permutation "s2_txn_pri_lower" "s2_txn_pri_upper" "s3_fast_pathtxn_pri_lower"
"s3_fast_pathtxn_pri_upper" "s2_begin" "s2_update" "s3_fast_path_update" "s2_commit"
"s2_select"