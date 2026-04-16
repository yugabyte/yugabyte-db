setup
{
  CREATE TABLE ref_tb(k1 INT, k2 INT, PRIMARY KEY(k1, k2));
  CREATE TABLE tb(k INT PRIMARY KEY, fk1 INT, fk2 INT, FOREIGN KEY(fk1, fk2) REFERENCES ref_tb(k1, k2));

  INSERT INTO ref_tb VALUES(1, 1), (10, 1), (1, 10);
  INSERT INTO tb VALUES(1, 1, 1);
}
teardown
{
  DROP TABLE ref_tb CASCADE;
  DROP TABLE tb;
}

session "s1"
step "s1_serializable_txn"    { BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE; }
step "s1_repeatable_read_txn" { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s1_read_committed_txn"  { BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED; }
step "s1_update_fk1"          { UPDATE tb SET fk1 = 10 WHERE k = 1; }
step "s1_commit"              { COMMIT; }
step "s1_select_tb"           { SELECT * FROM tb; }
step "s1_select_ref_tb"       { SELECT * FROM ref_tb; }
# Note that priorities will only take effect/make sense when this test is run in Fail-on-Conflict
# concurrency control mode and the isolation is not read committed.
step "s1_priority"            {  SET yb_transaction_priority_lower_bound = .9; }


session "s2"
step "s2_serializable_txn"    { BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE; }
step "s2_repeatable_read_txn" { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s2_read_committed_txn"  { BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED; }
step "s2_update_fk2"          { UPDATE tb SET fk2 = 10 WHERE k = 1; }
step "s2_commit"              { COMMIT; }
step "s2_priority"            { SET yb_transaction_priority_upper_bound= .1; }
# TestPgWaitQueuesRegress sets yb_max_query_layer_retries to 0 so that the wait queues mechanism is tested in isolation
# without query layer retrying giving the fake notion of waiting to the external client. However, read committed
# isolation, requires sufficient number of retries for the right semantics. In essence, query layer retries are an
# optimization/ added benefit for repeatable read and serializable isolation as compared to PostgreSQL and might result
# in better but different behaviour. But for RC, the query layer retries are a necessary requirement for achieve the
# bare minimum correct semantics.
#
# Also, these retries apply to all statements in an RC transaction but only the first statement in RR/ SR transactions.
step "s2_enable_retries" { SET yb_max_query_layer_retries = 60; }

# Transactions
permutation "s1_priority" "s2_priority" "s1_serializable_txn" "s2_serializable_txn" "s1_update_fk1" "s2_update_fk2" "s1_commit" "s2_commit" "s1_select_tb" "s1_select_ref_tb"
permutation "s1_priority" "s2_priority" "s1_repeatable_read_txn" "s2_repeatable_read_txn" "s1_update_fk1" "s2_update_fk2" "s1_commit" "s2_commit" "s1_select_tb" "s1_select_ref_tb"
permutation "s1_priority" "s2_priority" "s2_enable_retries" "s1_read_committed_txn" "s2_read_committed_txn" "s1_update_fk1" "s2_update_fk2" "s1_commit" "s2_commit" "s1_select_tb" "s1_select_ref_tb"

# Fastpath
permutation "s1_serializable_txn" "s1_update_fk1" "s2_update_fk2" "s1_commit" "s1_select_tb" "s1_select_ref_tb"
permutation "s1_read_committed_txn" "s1_update_fk1" "s2_update_fk2" "s1_commit" "s1_select_tb" "s1_select_ref_tb"
permutation "s1_repeatable_read_txn" "s1_update_fk1" "s2_update_fk2" "s1_commit" "s1_select_tb" "s1_select_ref_tb"
