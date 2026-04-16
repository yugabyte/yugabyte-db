setup
{
 CREATE TABLE t(k INT PRIMARY KEY, v1 INT, v2 INT);
 INSERT INTO t VALUES(1, 10, 20);

 CREATE OR REPLACE FUNCTION add_with_limit(cur_value INT, delta INT, sum_limit INT) RETURNS INT AS $$
 BEGIN
     IF cur_value + delta < sum_limit THEN
         RETURN cur_value + delta;
     END IF;
     RETURN 0;
 END;
 $$ LANGUAGE plpgsql IMMUTABLE;
}

teardown
{
  DROP TABLE t;
  DROP function add_with_limit(INT, INT, INT);
}

session "s1"
# Note that priorities will only take effect/make sense when this test is run in Fail-on-Conflict
# concurrency control mode and the isolation is not read committed.
setup {
  SET yb_transaction_priority_lower_bound = .9;
}

step "s1_serializable_txn"    { BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE; }
step "s1_repeatable_read_txn" { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s1_read_committed_txn"  { BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED; }
step "s1_update"              { UPDATE t SET v1 = add_with_limit(v1, v2, 35) WHERE k = 1; }
step "s1_update_case" {
  UPDATE t SET v1 = CASE WHEN v1 + v2 < 35 THEN v1 + v2 ELSE 0 END WHERE k = 1;
}
step "s1_commit"   { COMMIT; }
step "s1_select"   { select *, v1 + v2 from t; }


session "s2"
setup {
  SET yb_transaction_priority_upper_bound= .1;
}

step "s2_serializable_txn"    { BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE; }
step "s2_repeatable_read_txn" { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s2_read_committed_txn"  { BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED; }
step "s2_update"              { UPDATE t SET v2 = add_with_limit(v2, v1, 35) WHERE k = 1; }
step "s2_update_case" {
  UPDATE t SET v2 = CASE WHEN v1 + v2 < 35 THEN v1 + v2 ELSE 0 END WHERE k = 1;
}
step "s2_commit"   { COMMIT; }
# TestPgWaitQueuesRegress sets yb_max_query_layer_retries to 0 so that the wait queues mechanism is tested in isolation
# without query layer retrying giving the fake notion of waiting to the external client. However, read committed
# isolation, requires sufficient number of retries for the right semantics. In essence, query layer retries are an
# optimization/ added benefit for repeatable read and serializable isolation as compared to PostgreSQL and might result
# in better but different behaviour. But for RC, the query layer retries are a necessary requirement for achieve the
# bare minimum correct semantics.
#
# Also, these retries apply to all statements in an RC transaction but only the first statement in RR/ SR transactions.
step "s2_enable_retries" { SET yb_max_query_layer_retries = 60; }

permutation "s1_serializable_txn" "s2_serializable_txn" "s1_update" "s2_update" "s1_commit" "s2_commit" "s1_select"
permutation "s1_repeatable_read_txn" "s2_repeatable_read_txn" "s1_update" "s2_update" "s1_commit" "s2_commit" "s1_select"
permutation "s2_enable_retries" "s1_read_committed_txn" "s2_read_committed_txn" "s1_update" "s2_update" "s1_commit" "s2_commit" "s1_select"

permutation "s1_serializable_txn" "s1_update" "s2_update" "s1_commit" "s1_select"
permutation "s1_repeatable_read_txn" "s1_update" "s2_update" "s1_commit" "s1_select"
permutation "s2_enable_retries" "s1_read_committed_txn" "s1_update" "s2_update" "s1_commit" "s1_select"

permutation "s1_serializable_txn" "s2_serializable_txn" "s1_update_case" "s2_update_case" "s1_commit" "s2_commit" "s1_select"
permutation "s1_repeatable_read_txn" "s2_repeatable_read_txn" "s1_update_case" "s2_update_case" "s1_commit" "s2_commit" "s1_select"
permutation "s2_enable_retries" "s1_read_committed_txn" "s2_read_committed_txn" "s1_update_case" "s2_update_case" "s1_commit" "s2_commit" "s1_select"

permutation "s1_serializable_txn" "s1_update_case" "s2_update_case" "s1_commit" "s1_select"
permutation "s1_repeatable_read_txn" "s1_update_case" "s2_update_case" "s1_commit" "s1_select"
permutation "s2_enable_retries" "s1_read_committed_txn" "s1_update_case" "s2_update_case" "s1_commit" "s1_select"
