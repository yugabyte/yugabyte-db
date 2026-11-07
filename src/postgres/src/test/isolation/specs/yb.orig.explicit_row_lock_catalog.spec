# Explicit row locks on catalog rows, alone and joined with a user table (#36224).
#
# In the non-legacy catalog mode, locking a catalog row through the explicit row lock buffer used to
# recurse until the stack overflowed: the catalog read point switch flushed the buffer that was
# being flushed. Catalog and user rows also shared one batch, so user rows were locked at the
# catalog snapshot's read point. The join permutations check that each side's locks are honored
# with SKIP LOCKED (read-ahead on and off, both rowmark orders), NOWAIT and blocking waits.

setup
{
  CREATE TABLE t (k INT PRIMARY KEY, reloid OID);
  INSERT INTO t VALUES
    (1, 'pg_class'::regclass), (2, 'pg_type'::regclass), (3, 'pg_proc'::regclass);
  -- u holds the first sql_features row, so s1_dup_insert fails on a duplicate key.
  CREATE TABLE u (id TEXT PRIMARY KEY);
  INSERT INTO u SELECT feature_id FROM information_schema.sql_features
    ORDER BY feature_id, sub_feature_id LIMIT 1;
  CREATE FUNCTION lock_t_row(o OID) RETURNS INT LANGUAGE plpgsql AS
    $$ BEGIN PERFORM 1 FROM t WHERE reloid = o FOR UPDATE; RETURN 1; END $$;
}

teardown
{
  DROP FUNCTION lock_t_row(OID);
  DROP TABLE u;
  DROP TABLE t;
}

session "s1"
step "s1_rr"		{ BEGIN ISOLATION LEVEL REPEATABLE READ; }
step "s1_rc"		{ BEGIN ISOLATION LEVEL READ COMMITTED; }
step "s1_lock_t"	{ SELECT k FROM t WHERE k = 2 FOR UPDATE; }
step "s1_lock_c"	{ SELECT relname FROM pg_class WHERE oid = 'pg_type'::regclass FOR UPDATE; }
step "s1_lock_info"
{
  SELECT feature_id FROM information_schema.sql_features
  ORDER BY feature_id, sub_feature_id LIMIT 1 FOR UPDATE;
}
# The duplicate key on u is raised while the catalog row lock batch is being flushed.
step "s1_dup_insert"
{
  INSERT INTO u SELECT feature_id FROM (
    SELECT feature_id FROM information_schema.sql_features
    ORDER BY feature_id, sub_feature_id LIMIT 2 FOR UPDATE) f;
}
step "s1_dup_insert_in_block"
{
  DO $$ BEGIN
    INSERT INTO u SELECT feature_id FROM (
      SELECT feature_id FROM information_schema.sql_features
      ORDER BY feature_id, sub_feature_id LIMIT 2 FOR UPDATE) f;
  EXCEPTION WHEN unique_violation THEN NULL;
  END $$;
}
# Several LockRows nodes sharing the row lock buffer, mixing catalog and user rows. Each locks
# the pg_class row of pg_type and t's row k = 2.
step "s1_two_nodes"
{
  SELECT a.relname, b.k FROM
    (SELECT relname FROM pg_class WHERE oid = 'pg_type'::regclass FOR UPDATE) a,
    (SELECT k FROM t WHERE k = 2 FOR SHARE) b;
}
step "s1_lateral"
{
  SELECT c.relname, s.k FROM
    (SELECT oid, relname FROM pg_class
     WHERE oid IN ('pg_class'::regclass, 'pg_type'::regclass) FOR UPDATE) c,
    LATERAL (SELECT k FROM t WHERE t.reloid = c.oid FOR UPDATE) s
  ORDER BY s.k;
}
step "s1_nested"
{
  SELECT relname, lock_t_row(oid) FROM (
    SELECT oid, relname FROM pg_class
    WHERE oid IN ('pg_class'::regclass, 'pg_type'::regclass) FOR UPDATE) s
  ORDER BY relname;
}
step "s1_cte"
{
  WITH c AS MATERIALIZED (
    SELECT oid FROM pg_class WHERE oid = 'pg_type'::regclass FOR UPDATE)
  SELECT t.k FROM t, c WHERE t.reloid = c.oid FOR SHARE OF t;
}
step "s1_rollback"	{ ROLLBACK; }
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
step "s2_probe_c"	{ SELECT relname FROM pg_class WHERE oid = 'pg_type'::regclass FOR UPDATE NOWAIT; }
step "s2_probe_t"	{ SELECT k FROM t WHERE k = 2 FOR UPDATE NOWAIT; }
step "s2_probe_info"
{
  SELECT feature_id FROM information_schema.sql_features
  ORDER BY feature_id, sub_feature_id LIMIT 1 FOR UPDATE NOWAIT;
}
step "s2_wait_t"	{ SELECT k FROM t WHERE k = 2 FOR UPDATE; }
step "s2_update"	{ UPDATE t SET reloid = reloid WHERE k = 2; }
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
step "s3_read"		{ SELECT count(*) FROM t; }
step "s3_probe_t"	{ SELECT k FROM t WHERE k = 2 FOR UPDATE NOWAIT; }
step "s3_commit"	{ COMMIT; }
step "s3_rollback"	{ ROLLBACK; }

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

# Both rows of k = 2 held by s1: s2 skips only k = 2.
permutation "s1_rc" "s1_lock_t" "s1_lock_c" "s2_ra_on" "s2_rc" "s2_skip_tc" "s2_skip_ct" "s2_commit" "s1_commit"
permutation "s1_rr" "s1_lock_t" "s1_lock_c" "s2_ra_off" "s2_rr" "s2_skip_tc" "s2_skip_ct" "s2_commit" "s1_commit"

# A user row lock taken next to catalog locks blocks a conflicting lock until the holder commits.
# There is no blocking variant for catalog rows: the sys catalog tablet resolves conflicts by txn
# priority, not wait queues, so either side can lose. The NOWAIT probes cover those locks.
permutation "s1_rc" "s1_two_nodes" "s2_rc" "s2_wait_t"("s1_commit") "s1_commit" "s2_commit"

# A lock on an information_schema row (catalog path) is held.
permutation "s1_rc" "s1_lock_info" "s2_rc" "s2_probe_info" "s2_rollback" "s1_commit"

# The user row is locked at the txn read point, not the catalog snapshot's, so a concurrent
# update after the read point is a serialization error.
permutation "s3_rr" "s3_read" "s2_update" "s3_join" "s3_rollback"

# An error raised while a row lock batch is flushing must not leave the buffer unusable: a later
# row lock is still taken and the commit succeeds.
permutation "s1_rc" "s1_dup_insert" "s1_rollback" "s1_rc" "s1_lock_t" "s2_rc" "s2_probe_t" "s2_rollback" "s1_commit"
# Same, with the error caught by a subtransaction, so the txn goes on without a buffer Clear().
permutation "s1_rc" "s1_dup_insert_in_block" "s1_lock_t" "s2_rc" "s2_probe_t" "s2_rollback" "s1_commit"

# Every lock taken by several LockRows nodes in one statement is held.
permutation "s1_rc" "s1_two_nodes" "s2_rc" "s2_probe_c" "s2_rollback" "s3_rc" "s3_probe_t" "s3_rollback" "s1_commit"
permutation "s1_rc" "s1_lateral" "s2_rc" "s2_probe_c" "s2_rollback" "s3_rc" "s3_probe_t" "s3_rollback" "s1_commit"
permutation "s1_rc" "s1_nested" "s2_rc" "s2_probe_c" "s2_rollback" "s3_rc" "s3_probe_t" "s3_rollback" "s1_commit"
permutation "s1_rc" "s1_cte" "s2_rc" "s2_probe_c" "s2_rollback" "s3_rc" "s3_probe_t" "s3_rollback" "s1_commit"
