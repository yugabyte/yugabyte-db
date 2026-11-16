# ROLLBACK TO SAVEPOINT must only release the table locks acquired after the
# savepoint. s1's locks taken before the savepoint must keep blocking s2 until
# s1 commits.
#
# GH #34734: the savepoint holds only DML, and the tserver's per-subtransaction
# release must not drop s1's earlier ROW EXCLUSIVE lock on t.
#
# GH #32980, #36887: the rolled-back work holds all of s1's DDLs (an ALTER,
# CREATE or DROP in the savepoint, or a DDL in a PL/pgSQL EXCEPTION block).
# Undoing them must not make the master release all of s1's locks, whether
# they come from DML, LOCK TABLE, or a DDL that leaves t's DocDB schema
# unchanged. s1_wait gives that asynchronous release time to land before s2
# runs, so a regression shows up as s2 not waiting.

setup
{
  CREATE TABLE t (k int PRIMARY KEY, v int, note text);
  INSERT INTO t VALUES (1, 1, 'n');
  CREATE TABLE u (k int PRIMARY KEY);
  CREATE FUNCTION tf() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN RETURN NEW; END $$;
}

teardown
{
  DROP TABLE IF EXISTS t, u, w;
  DROP FUNCTION tf();
}

session s1
step s1_begin_rr	{ BEGIN ISOLATION LEVEL REPEATABLE READ; }
step s1_begin_rc	{ BEGIN ISOLATION LEVEL READ COMMITTED; }
step s1_insert_t	{ INSERT INTO t VALUES (2, 20); }
step s1_lock_t		{ LOCK TABLE t IN ACCESS EXCLUSIVE MODE; }
step s1_storage_t	{ ALTER TABLE t ALTER COLUMN note SET STORAGE EXTERNAL; }
step s1_trigger_t	{ CREATE TRIGGER trg BEFORE INSERT ON t FOR EACH ROW EXECUTE FUNCTION tf(); }
step s1_select_t	{ SELECT v FROM t WHERE k = 1; }
step s1_savepoint	{ SAVEPOINT a; }
step s1_insert_u	{ INSERT INTO u VALUES (1); }
step s1_alter_u		{ ALTER TABLE u ADD COLUMN c int; }
step s1_create_w	{ CREATE TABLE w (k int PRIMARY KEY); }
step s1_drop_u		{ DROP TABLE u; }
step s1_rollback_to	{ ROLLBACK TO SAVEPOINT a; }
step s1_ddl_in_exception_block
{
  DO $$
  BEGIN
    ALTER TABLE u ADD COLUMN c int;
    RAISE EXCEPTION 'undo';
  EXCEPTION WHEN raise_exception THEN
    NULL;
  END $$;
}
step s1_wait		{ SELECT pg_sleep(2); }
step s1_update_t	{ UPDATE t SET v = 1 + 10 WHERE k = 1; }
step s1_commit		{ COMMIT; }

session s2
step s2_add_check	{ ALTER TABLE t ADD CONSTRAINT tc CHECK (v < 10); }
step s2_update_t	{ UPDATE t SET v = v + 5 WHERE k = 1; }
step s2_drop_t		{ DROP TABLE t; }
step s2_select		{ SELECT k, v FROM t ORDER BY k; }
step s2_triggers	{ SELECT tgname FROM pg_trigger WHERE tgname = 'trg'; }

# DML in the savepoint.
permutation s1_begin_rr s1_insert_t s1_savepoint s1_insert_u s1_rollback_to s2_add_check s1_commit s2_select
permutation s1_begin_rc s1_insert_t s1_savepoint s1_insert_u s1_rollback_to s2_add_check s1_commit s2_select

# DDL in the savepoint, with a DML lock taken before it.
permutation s1_begin_rr s1_insert_t s1_savepoint s1_alter_u s1_rollback_to s1_wait s2_add_check s1_commit s2_select
permutation s1_begin_rc s1_insert_t s1_savepoint s1_alter_u s1_rollback_to s1_wait s2_add_check s1_commit s2_select

# ALTER, CREATE and DROP in the savepoint, with a LOCK TABLE taken before it.
permutation s1_begin_rr s1_lock_t s1_savepoint s1_alter_u s1_rollback_to s1_wait s2_update_t s1_commit s2_select
permutation s1_begin_rr s1_lock_t s1_savepoint s1_create_w s1_rollback_to s1_wait s2_update_t s1_commit s2_select
permutation s1_begin_rr s1_lock_t s1_savepoint s1_drop_u s1_rollback_to s1_wait s2_update_t s1_commit s2_select

# The lock before the savepoint comes from a DDL that leaves t's DocDB schema unchanged, so t is
# not in s1's DDL verification state. Losing that lock would lose s2's update.
permutation s1_begin_rr s1_storage_t s1_select_t s1_savepoint s1_alter_u s1_rollback_to s1_wait s2_update_t s1_update_t s1_commit s2_select

# Same, with the lock from CREATE TRIGGER. Losing it would let s2 drop t under the new trigger.
permutation s1_begin_rr s1_trigger_t s1_savepoint s1_alter_u s1_rollback_to s1_wait s2_drop_t s1_commit s2_triggers

# DDL undone by a PL/pgSQL EXCEPTION block.
permutation s1_begin_rr s1_insert_t s1_ddl_in_exception_block s1_wait s2_add_check s1_commit s2_select
