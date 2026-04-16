setup
{
  CREATE TABLE test (k int PRIMARY KEY, v int);
}

teardown
{
  DROP TABLE test;
}

session "s1"
# TestPgWaitQueuesRegress sets yb_max_query_layer_retries to 0 so that the wait queues mechanism is tested in isolation
# without query layer retrying giving the fake notion of waiting to the external client. However, read committed
# isolation, requires sufficient number of retries for the right semantics. In essence, query layer retries are an
# optimization/ added benefit for repeatable read and serializable isolation as compared to PostgreSQL and might result
# in better but different behaviour. But for RC, the query layer retries are a necessary requirement for achieve the
# bare minimum correct semantics.
#
# Also, these retries apply to all statements in an RC transaction but only the first statement in RR/ SR transactions.
setup {
  SET yb_max_query_layer_retries = 60;
  BEGIN;
}
step "s1_ins"	{ INSERT INTO test VALUES (2, 2); }
step "s1_commit"	{ COMMIT; }
step "s1_select"	{ SELECT * FROM test; }

session "s2"
setup {
  SET yb_max_query_layer_retries = 60;
}
step "s2_ins"	{ INSERT INTO test VALUES (2, 4); }
step "s2_multi_insert" { INSERT INTO test VALUES (1, 1), (2, 4), (3, 3); }

session "s3"
step "s3_del"	{ DELETE FROM test WHERE k=2; }

permutation "s1_ins" "s2_ins" "s1_commit" "s1_select"
permutation "s1_ins" "s3_del" "s2_ins" "s1_commit" "s1_select"
permutation "s1_ins" "s2_multi_insert" "s1_commit" "s1_select"
permutation "s1_ins" "s3_del" "s2_multi_insert" "s1_commit" "s1_select"
