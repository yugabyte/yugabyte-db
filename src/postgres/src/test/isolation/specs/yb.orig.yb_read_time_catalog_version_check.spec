# Time-travel reads (yb_read_time) across DDLs that commit after the read time.
#
# While yb_read_time is set, the backend keeps its catalog caches and its local
# catalog version as of yb_read_time: it neither refreshes the catalog nor
# applies invalidation messages until yb_read_time is reset. Its transactions
# take no object locks, so the tserver keeps the catalog version check for
# their requests, which it skips for backends that acquire object locks, and it
# rejects every read once a breaking catalog change has committed after
# yb_read_time. That check is the only guard against reading user data as of a
# time before a DDL, and it must behave the same whether or not the object
# locking feature (enable_object_locking_for_table_locks) is on.
#
# Permutations marked TODO record current behavior that is not intended.
#
# Not covered, because the errors name table ids or OIDs:
# - DDLs that replace the DocDB table after the read time (DROP TABLE,
#   TRUNCATE, ALTER TYPE rewrites). Without a snapshot schedule, which an
#   isolation test can't create, the old table is deleted and reads fail with
#   OBJECT_NOT_FOUND.
# - TODO(#37408): DROP COLUMN after the read time. The catalog as of the read
#   time still has the column but the current DocDB schema doesn't, so every
#   read fails with "Invalid column number". DROP COLUMN is non-breaking, so
#   the catalog version check doesn't catch it first.

setup
{
  CREATE TABLE t (k INT PRIMARY KEY, v INT, w INT);
  INSERT INTO t VALUES (1, 10, 100), (2, 20, 200);
  CREATE SEQUENCE s;
  SELECT nextval('s');
  CREATE ROLE yb_tt_user;
  GRANT SELECT ON t TO yb_tt_user;
  CREATE TABLE read_time (t BIGINT);
}

teardown
{
  DROP TABLE t;
  DROP SEQUENCE s;
  DROP TABLE read_time;
  DROP ROLE yb_tt_user;
}

session "tt"
step "tt_set"
{
  SELECT set_config('yb_read_time', t::text, false) IS NOT NULL AS set FROM read_time;
}
step "tt_sel"		{ SELECT * FROM t ORDER BY k; }
step "tt_seq"		{ SELECT last_value FROM s; }
step "tt_begin"		{ BEGIN ISOLATION LEVEL REPEATABLE READ; }
step "tt_rollback"	{ ROLLBACK; }
step "tt_reset"		{ RESET yb_read_time; }
step "tt_no_check"	{ SET yb_disable_catalog_version_check = on; }
step "tt_role"		{ SET ROLE yb_tt_user; }
step "tt_parallel"	{ SET force_parallel_mode = on; }
step "tt_explain"	{ EXPLAIN (COSTS OFF) SELECT * FROM t; }
step "tt_explain_analyze"
{
  EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF) SELECT * FROM t;
}
# The connection is reused by the next permutation, whose tables don't exist as
# of this permutation's read time.
teardown
{
  RESET ROLE;
  RESET yb_read_time;
  RESET yb_disable_catalog_version_check;
  RESET force_parallel_mode;
}

session "ddl"
step "ddl_read_time"
{
  INSERT INTO read_time
    SELECT (extract(epoch FROM clock_timestamp()) * 1000000)::BIGINT;
}
step "ddl_rename"		{ ALTER TABLE t RENAME COLUMN v TO v2; }
step "ddl_add_col"		{ ALTER TABLE t ADD COLUMN c INT; }
step "ddl_revoke"		{ REVOKE SELECT ON t FROM yb_tt_user; }
step "ddl_rls"			{ ALTER TABLE t ENABLE ROW LEVEL SECURITY; }

# A breaking DDL after the read time fails every read.
permutation "ddl_read_time" "ddl_rename" "tt_set" "tt_sel" "tt_sel"

# Same in a transaction block, and after it is rolled back.
permutation "ddl_read_time" "ddl_rename" "tt_set" "tt_begin" "tt_sel" "tt_sel" "tt_rollback" "tt_sel"

# A breaking DDL that commits after the session already read as of yb_read_time.
permutation "ddl_read_time" "tt_set" "tt_sel" "ddl_rename" "tt_sel"

# Sequence reads carry the catalog version too.
permutation "ddl_read_time" "ddl_rename" "tt_set" "tt_seq" "tt_seq"

# yb_disable_catalog_version_check (set by ysql_dump --read-time, the xCluster
# DDL queue handler and the CDC snapshot) opts out of the check.
permutation "ddl_read_time" "ddl_rename" "tt_no_check" "tt_set" "tt_sel"

# RESET yb_read_time reads the current data and schema again.
permutation "ddl_read_time" "ddl_rename" "tt_set" "tt_sel" "tt_reset" "tt_sel"

# TODO(#37408): non-breaking DDLs are never caught; after ADD COLUMN the read
# returns the schema and data as of yb_read_time.
permutation "ddl_read_time" "ddl_add_col" "tt_set" "tt_sel" "tt_sel"

# REVOKE is breaking, so the role can't read data as of yb_read_time with the
# privileges it had then.
permutation "ddl_read_time" "ddl_revoke" "tt_set" "tt_role" "tt_sel" "tt_sel"

# TODO(#37408): privileges are evaluated from the catalog as of yb_read_time,
# and ENABLE ROW LEVEL SECURITY is a non-breaking ALTER TABLE, so rows hidden
# from the role now are still returned.
permutation "ddl_read_time" "ddl_rls" "tt_set" "tt_role" "tt_sel"

# Parallel workers: the worker reads the catalog version itself at startup.
permutation "ddl_read_time" "ddl_add_col" "tt_set" "tt_parallel" "tt_explain_analyze" "tt_sel"
permutation "ddl_read_time" "ddl_rename" "tt_set" "tt_parallel" "tt_explain" "tt_sel" "tt_sel"
