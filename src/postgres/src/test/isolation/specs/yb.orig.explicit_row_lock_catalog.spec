# Explicit row locks on catalog rows, alone and joined with a user table (#36224).
#
# In the non-legacy catalog mode, locking a catalog row through the explicit row lock buffer used to
# recurse until the stack overflowed: the catalog read point switch flushed the buffer that was
# being flushed. Catalog and user rows also shared one batch, so user rows were locked at the
# catalog snapshot's read point. The join permutations check that each side's locks are honored
# with SKIP LOCKED (read-ahead on and off, both rowmark orders) and NOWAIT.

setup
{
  CREATE TABLE t (k INT PRIMARY KEY, reloid OID);
  INSERT INTO t VALUES
    (1, 'pg_class'::regclass), (2, 'pg_type'::regclass), (3, 'pg_proc'::regclass);
}

teardown
{
  DROP TABLE t;
}

session "s1"
step "s1_rr"		{ BEGIN ISOLATION LEVEL REPEATABLE READ; }
step "s1_rc"		{ BEGIN ISOLATION LEVEL READ COMMITTED; }
step "s1_lock_t"	{ SELECT k FROM t WHERE k = 2 FOR UPDATE; }
step "s1_lock_c"	{ SELECT relname FROM pg_class WHERE oid = 'pg_type'::regclass FOR UPDATE; }
step "s1_commit"	{ COMMIT; }

session "s2"
step "s2_rr"		{ BEGIN ISOLATION LEVEL REPEATABLE READ; }
step "s2_rc"		{ BEGIN ISOLATION LEVEL READ COMMITTED; }
step "s2_ra_on"		{ SET yb_explicit_row_lock_skip_locked_max_read_ahead = 10; }
step "s2_ra_off"	{ SET yb_explicit_row_lock_skip_locked_max_read_ahead = 1; }
step "s2_skip_tc"
{
  SELECT k, relname FROM (
    SELECT t.k, c.relname FROM t JOIN pg_class c ON c.oid = t.reloid
    FOR UPDATE OF t SKIP LOCKED FOR UPDATE OF c SKIP LOCKED) s
  ORDER BY k;
}
step "s2_skip_ct"
{
  SELECT k, relname FROM (
    SELECT t.k, c.relname FROM t JOIN pg_class c ON c.oid = t.reloid
    FOR UPDATE OF c SKIP LOCKED FOR UPDATE OF t SKIP LOCKED) s
  ORDER BY k;
}
step "s2_nowait"
{
  SELECT k, relname FROM (
    SELECT t.k, c.relname FROM t JOIN pg_class c ON c.oid = t.reloid FOR UPDATE NOWAIT) s
  ORDER BY k;
}
step "s2_commit"	{ COMMIT; }
step "s2_rollback"	{ ROLLBACK; }

# Single-session catalog row locks: every rowmark, a multi-row catalog batch, an
# information_schema table (in the sys catalog tablet but not IsCatalogRelation), a catalog and
# user table join, and the unbatched path.
session "s3"
step "s3_rr"		{ BEGIN ISOLATION LEVEL REPEATABLE READ; }
step "s3_rc"		{ BEGIN ISOLATION LEVEL READ COMMITTED; }
step "s3_ser"		{ BEGIN ISOLATION LEVEL SERIALIZABLE; }
step "s3_modes"
{
  SELECT relname FROM pg_class WHERE oid = 'pg_class'::regclass FOR UPDATE;
  SELECT relname FROM pg_class WHERE oid = 'pg_class'::regclass FOR NO KEY UPDATE;
  SELECT relname FROM pg_class WHERE oid = 'pg_class'::regclass FOR SHARE;
  SELECT relname FROM pg_class WHERE oid = 'pg_class'::regclass FOR KEY SHARE;
}
step "s3_multi"
{
  SELECT count(*) > 50 AS many FROM (
    SELECT oid FROM pg_class WHERE relnamespace = 'pg_catalog'::regnamespace FOR SHARE) s;
}
step "s3_info_schema"
{
  SELECT feature_id FROM information_schema.sql_features
  ORDER BY feature_id, sub_feature_id LIMIT 1 FOR UPDATE;
}
step "s3_join"
{
  SELECT k, relname FROM (
    SELECT t.k, c.relname FROM t JOIN pg_class c ON c.oid = t.reloid FOR UPDATE) s
  ORDER BY k;
}
step "s3_unbatched"
{
  SET LOCAL yb_explicit_row_locking_batch_size = 1;
  SELECT relname FROM pg_class WHERE oid = 'pg_class'::regclass FOR UPDATE;
}
step "s3_commit"	{ COMMIT; }

permutation "s3_rr" "s3_modes" "s3_multi" "s3_info_schema" "s3_join" "s3_unbatched" "s3_commit"
permutation "s3_rc" "s3_modes" "s3_multi" "s3_info_schema" "s3_join" "s3_unbatched" "s3_commit"
permutation "s3_ser" "s3_modes" "s3_multi" "s3_info_schema" "s3_join" "s3_unbatched" "s3_commit"

# User row locked by s1: s2 skips k = 2.
permutation "s1_rr" "s1_lock_t" "s2_ra_on" "s2_rr" "s2_skip_tc" "s2_skip_ct" "s2_commit" "s1_commit"
permutation "s1_rr" "s1_lock_t" "s2_ra_off" "s2_rr" "s2_skip_tc" "s2_skip_ct" "s2_commit" "s1_commit"
permutation "s1_rc" "s1_lock_t" "s2_ra_on" "s2_rc" "s2_skip_tc" "s2_skip_ct" "s2_commit" "s1_commit"

# Catalog row locked by s1: s2 skips k = 2.
permutation "s1_rr" "s1_lock_c" "s2_ra_on" "s2_rr" "s2_skip_tc" "s2_skip_ct" "s2_commit" "s1_commit"
permutation "s1_rr" "s1_lock_c" "s2_ra_off" "s2_rr" "s2_skip_tc" "s2_skip_ct" "s2_commit" "s1_commit"
permutation "s1_rc" "s1_lock_c" "s2_ra_on" "s2_rc" "s2_skip_tc" "s2_skip_ct" "s2_commit" "s1_commit"

# NOWAIT errors out on whichever side s1 holds. READ COMMITTED only: in REPEATABLE READ, NOWAIT is
# fail-on-conflict resolved by random txn priority, so either session can lose.
permutation "s1_rc" "s1_lock_t" "s2_rc" "s2_nowait" "s2_rollback" "s1_commit"
permutation "s1_rc" "s1_lock_c" "s2_rc" "s2_nowait" "s2_rollback" "s1_commit"
