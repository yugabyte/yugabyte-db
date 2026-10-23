# With object locking, the tserver no longer rejects requests with a stale catalog version, so a
# REVOKE must be honored the way PG does it: relation_openrv() calls AcceptInvalidationMessages()
# on every statement, even when the relation lock is already held, because GRANT/REVOKE take no
# lock on the relation. In YB that refreshes the backend's catalog cache when its tserver has a
# newer catalog version, so the next statement of an open transaction block fails the permission
# check. With object locking, the REVOKE's commit pushes the new catalog version to all tservers
# before returning, so no wait for a heartbeat is needed.

setup
{
  CREATE ROLE regress_revoke_reader;
  CREATE TABLE test (k int primary key, v int);
  INSERT INTO test SELECT i, i FROM generate_series(1, 10) AS i;
  GRANT SELECT ON test TO regress_revoke_reader;
}

teardown
{
  DROP TABLE IF EXISTS test;
  DROP ROLE IF EXISTS regress_revoke_reader;
}

session s1
setup { SET ROLE regress_revoke_reader; }
step s1_begin_rr { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step s1_begin_rc { BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED; }
step s1_select { SELECT COUNT(*) FROM test; }
step s1_rollback { ROLLBACK; }
# Teardown drops the role.
teardown { RESET ROLE; }

session s2
step s2_revoke { REVOKE SELECT ON test FROM regress_revoke_reader; }

permutation s1_begin_rr s1_select s2_revoke s1_select s1_rollback
permutation s1_begin_rc s1_select s2_revoke s1_select s1_rollback
