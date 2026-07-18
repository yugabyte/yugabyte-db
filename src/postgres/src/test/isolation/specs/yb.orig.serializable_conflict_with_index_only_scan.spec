# Test to ensure that an index only scan in serializable isolation takes kStrongRead locks
# and conflicts with other writes.

setup
{
 CREATE TABLE test (k INT PRIMARY KEY, v1 INT, v2 INT);
 CREATE INDEX idx ON test (v1 ASC);
 CREATE UNIQUE INDEX unq_idx ON test (v2 ASC);
}

teardown
{
 DROP TABLE test;
}

session "s1"
step "s1_explain_idx_only_scan" { EXPLAIN (COSTS OFF) SELECT v1 FROM test WHERE v1 = 1; }
step "s1_explain_unq_idx_only_scan" { EXPLAIN (COSTS OFF) SELECT v2 FROM test WHERE v2 = 1; }
step "s1_high_pri" { SET yb_transaction_priority_lower_bound = 0.9; -- only for Fail-on-Conflict mode }
step "s1_low_pri" { SET yb_transaction_priority_upper_bound = 0.1; -- only for Fail-on-Conflict mode }
step "s1_begin_serializable" { BEGIN ISOLATION LEVEL SERIALIZABLE; }
step "s1_idx_only_scan" { SELECT v1 FROM test WHERE v1 = 1; }
step "s1_unq_idx_only_scan" { SELECT v2 FROM test WHERE v2 = 1; }
step "c1" { commit; }
teardown { RESET yb_transaction_priority_lower_bound; RESET yb_transaction_priority_upper_bound; }

session "s2"
step "s2_high_pri" { SET yb_transaction_priority_lower_bound = 0.9; -- only for Fail-on-Conflict mode }
step "s2_low_pri" { SET yb_transaction_priority_upper_bound = 0.1; -- only for Fail-on-Conflict mode }
step "s2_begin" { BEGIN; }
step "s2_insert"	{ INSERT INTO test VALUES (1, 1, 1); }
step "c2" { commit; }
teardown { RESET yb_transaction_priority_lower_bound; RESET yb_transaction_priority_upper_bound; }

# Test with non-unique index
permutation "s1_explain_idx_only_scan" "s1_high_pri" "s2_low_pri"
"s1_begin_serializable" "s2_begin" "s1_idx_only_scan" "s2_insert" "c1" "c2"

permutation "s1_explain_idx_only_scan" "s2_high_pri" "s1_low_pri"
"s1_begin_serializable" "s2_begin" "s2_insert" "s1_idx_only_scan" "c2" "c1"

# Test with unique index
permutation "s1_explain_unq_idx_only_scan" "s1_high_pri" "s2_low_pri"
"s1_begin_serializable" "s2_begin" "s1_unq_idx_only_scan" "s2_insert" "c1" "c2"

permutation "s1_explain_unq_idx_only_scan" "s2_high_pri" "s1_low_pri"
"s1_begin_serializable" "s2_begin" "s2_insert" "s1_unq_idx_only_scan" "c2" "c1"
