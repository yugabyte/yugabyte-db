# Test that DISCARD ALL does not preempt an ANALYZE run by auto-analyze, while a
# DDL that increments the catalog version still does. Requires transactional DDL
# and no object locking, so that the ANALYZE holds its catalog version row lock
# until COMMIT.

setup
{
  CREATE TABLE test (k INT PRIMARY KEY, v INT);
  INSERT INTO test SELECT i, i FROM generate_series(1, 10) AS i;
}

teardown
{
  DROP TABLE IF EXISTS test, test2;
}

session s1
step s1_begin { BEGIN; }
step s1_auto_analyze_conn { SET yb_use_internal_auto_analyze_service_conn = true; }
step s1_analyze { ANALYZE test; }
step s1_commit { COMMIT; }

session s2
step s2_discard_all { DISCARD ALL; }
step s2_create_table { CREATE TABLE test2 (k INT); }

permutation s1_begin s1_auto_analyze_conn s1_analyze s2_discard_all s1_commit

permutation s1_begin s1_auto_analyze_conn s1_analyze s2_create_table s1_commit
