# Table-lock interaction between concurrent DDLs (GH #29273).
#
# Pairs covered (upstream PG lock semantics):
#   - ANALYZE vs ANALYZE on the same table: ShareUpdateExclusiveLock
#     self-conflicts, so the second ANALYZE must block.
#   - CREATE TRIGGER vs CREATE INDEX on the same table:
#     ShareRowExclusiveLock conflicts with ShareLock, so the CREATE INDEX
#     must block.
#   - CREATE INDEX vs CREATE INDEX (both non-concurrent) on the same table:
#     ShareLock is self-compatible, so the second CREATE INDEX must NOT
#     block.
#
# CREATE TRIGGER vs UPDATE is covered by the ported create-trigger spec in
# yb_pg_isolation_lock_schedule.

setup
{
  CREATE TABLE t (k int PRIMARY KEY, v int);
  INSERT INTO t VALUES (1, 1), (2, 2);
  CREATE FUNCTION trig_fn() RETURNS trigger LANGUAGE plpgsql AS
    $$ BEGIN RETURN NEW; END; $$;
}

teardown
{
  DROP TABLE t;
  DROP FUNCTION trig_fn();
}

session s1
step s1_begin		{ BEGIN ISOLATION LEVEL READ COMMITTED; }
step s1_analyze		{ ANALYZE t; }
step s1_create_trigger	{ CREATE TRIGGER trig AFTER UPDATE ON t FOR EACH ROW EXECUTE FUNCTION trig_fn(); }
# step s1_create_index_i1	{ CREATE INDEX NONCONCURRENTLY i1 ON t (v); }
step s1_commit		{ COMMIT; }

session s2
step s2_analyze		{ ANALYZE t; }
step s2_create_index_i2	{ CREATE INDEX NONCONCURRENTLY i2 ON t (v); }

# ANALYZE blocks a concurrent ANALYZE of the same table.
permutation s1_begin s1_analyze s2_analyze s1_commit

# An uncommitted CREATE TRIGGER blocks CREATE INDEX on the same table.
permutation s1_begin s1_create_trigger s2_create_index_i2 s1_commit

# An uncommitted CREATE INDEX does not block another non-concurrent
# CREATE INDEX on the same table (ShareLock is self-compatible).
# TODO(#29638): YSQL does not support inplace catalog updates, which this
# needs (both index builds update the same pg_class row), so the two
# CREATE INDEX NONCONCURRENTLY statements conflict instead of coexisting.
# Enable the permutation once inplace catalog updates are supported.
# permutation s1_begin s1_create_index_i1 s2_create_index_i2 s1_commit
