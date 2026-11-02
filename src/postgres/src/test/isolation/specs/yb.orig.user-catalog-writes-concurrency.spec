# User-issued reads and writes of catalog rows, in REPEATABLE READ and SERIALIZABLE.
#
# TODO(#29275): YB checks write-write conflicts on catalog rows against the catalog snapshot, not
# the transaction snapshot as PG does, and doesn't keep SERIALIZABLE read intents on catalog rows
# (#34631). PG takes no SSI predicate locks on catalogs either.

setup
{
  CREATE TABLE a (k int);
  CREATE TABLE b (k int);
}

teardown
{
  DROP TABLE IF EXISTS a;
  DROP TABLE IF EXISTS b;
}

session s1
# The sys catalog tablet resolves these conflicts by priority. The NOTICE that priorities don't
# apply with wait queues is about user tables, so it is silenced.
setup
{
  SET yb_non_ddl_txn_for_sys_tables_allowed = on;
  SET client_min_messages = warning;
  SET yb_transaction_priority_lower_bound = 0.9;
  RESET client_min_messages;
}
step s1_begin_rr { BEGIN ISOLATION LEVEL REPEATABLE READ; }
step s1_begin_sr { BEGIN ISOLATION LEVEL SERIALIZABLE; }
step s1_read_a { SELECT relpages FROM pg_class WHERE relname = 'a'; }
step s1_increment_a { UPDATE pg_class SET relpages = relpages + 1 WHERE relname = 'a'; }
step s1_read_sum { SELECT sum(relpages) FROM pg_class WHERE relname IN ('a', 'b'); }
step s1_set_a_if_sum_zero
{
  UPDATE pg_class SET relpages = 1 WHERE relname = 'a' AND
    (SELECT sum(relpages) FROM pg_class WHERE relname IN ('a', 'b')) = 0;
}
step s1_commit { COMMIT; }

session s2
setup
{
  SET yb_non_ddl_txn_for_sys_tables_allowed = on;
  SET client_min_messages = warning;
  SET yb_transaction_priority_upper_bound = 0.1;
  RESET client_min_messages;
  -- Fail the conflicting first statement at once instead of retrying it.
  SET yb_max_query_layer_retries = 0;
}
step s2_begin_rr { BEGIN ISOLATION LEVEL REPEATABLE READ; }
step s2_begin_sr { BEGIN ISOLATION LEVEL SERIALIZABLE; }
step s2_read_a { SELECT relpages FROM pg_class WHERE relname = 'a'; }
step s2_increment_a { UPDATE pg_class SET relpages = relpages + 1 WHERE relname = 'a'; }
step s2_read_sum { SELECT sum(relpages) FROM pg_class WHERE relname IN ('a', 'b'); }
step s2_set_b_if_sum_zero
{
  UPDATE pg_class SET relpages = 1 WHERE relname = 'b' AND
    (SELECT sum(relpages) FROM pg_class WHERE relname IN ('a', 'b')) = 0;
}
step s2_commit { COMMIT; }
step s2_select_relpages { SELECT relname, relpages FROM pg_class WHERE relname IN ('a', 'b') ORDER BY relname; }

# Write-write conflict: the lower priority writer fails at once with "could not serialize access".
# PG blocks the second writer until the first commits, then fails it the same way.
permutation s1_begin_rr s2_begin_rr s1_increment_a s2_increment_a s1_commit s2_commit s2_select_relpages
permutation s1_begin_sr s2_begin_sr s1_increment_a s2_increment_a s1_commit s2_commit s2_select_relpages

# s2 writes after s1 commits. Both commit and a ends at 2, in RR and SERIALIZABLE alike: s2's UPDATE
# reads the row at the catalog snapshot, which sees s1's commit. In PG, s2's UPDATE fails with
# "could not serialize access", since its transaction snapshot predates s1's commit (#29275).
permutation s1_begin_rr s2_begin_rr s1_read_a s2_read_a s1_increment_a s1_commit s2_increment_a s2_commit s2_select_relpages
permutation s1_begin_sr s2_begin_sr s1_read_a s2_read_a s1_increment_a s1_commit s2_increment_a s2_commit s2_select_relpages

# Write skew: each transaction sets its own row only if a + b = 0, so a serial execution leaves the
# sum at 1. Both commit and the sum ends at 2, as in PG, which takes no SSI predicate locks on
# catalogs.
permutation s1_begin_rr s2_begin_rr s1_read_sum s2_read_sum s1_set_a_if_sum_zero s2_set_b_if_sum_zero s1_commit s2_commit s2_select_relpages
permutation s1_begin_sr s2_begin_sr s1_read_sum s2_read_sum s1_set_a_if_sum_zero s2_set_b_if_sum_zero s1_commit s2_commit s2_select_relpages
