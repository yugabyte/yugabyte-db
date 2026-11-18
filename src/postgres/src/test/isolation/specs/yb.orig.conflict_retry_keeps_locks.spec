# A statement retried after a transaction conflict must still hold its locks afterwards
# (GH #35007, #36262). The writer's UPDATE of hot waits on the blocker's row lock. Its read time was
# already picked by the read of gate, a 3-tablet table read first by the uncorrelated subquery, so
# once the blocker commits, the write conflicts and the statement is transparently retried: the
# whole transaction for REPEATABLE READ and autocommit, just the statement for READ COMMITTED in a
# transaction block. The retry must re-acquire its table and advisory locks, so a DDL on t2 (or a
# read of t3) waits for the writer.
#
# Outside a transaction block the statement holds its locks only while it runs, so it ends by
# waiting on advisory lock 1, which the gate session holds while the DDL is tried.

setup
{
  CREATE TABLE hot (k int PRIMARY KEY, v int);
  INSERT INTO hot VALUES (1, 0);
  CREATE TABLE t2 (id int PRIMARY KEY, v int);
  CREATE TABLE t3 (k int PRIMARY KEY);
  CREATE TABLE gate (k int, PRIMARY KEY (k HASH)) SPLIT INTO 3 TABLETS;
  INSERT INTO gate SELECT generate_series(1, 10);
  CREATE FUNCTION wait_for_gate() RETURNS bool LANGUAGE plpgsql AS $$
  BEGIN
    PERFORM pg_advisory_xact_lock(1);
    RETURN true;
  END $$;
  CREATE FUNCTION xact_lock_and_update() RETURNS void LANGUAGE plpgsql AS $$
  BEGIN
    PERFORM pg_advisory_xact_lock(42);
    WITH u AS (UPDATE hot SET v = v + (SELECT COUNT(*) FROM gate) WHERE k = 1 RETURNING v)
    INSERT INTO t2 SELECT 1, v FROM u;
  END $$;
  CREATE FUNCTION lock_and_update() RETURNS void LANGUAGE plpgsql AS $$
  BEGIN
    LOCK TABLE t3 IN ACCESS EXCLUSIVE MODE;
    WITH u AS (UPDATE hot SET v = v + (SELECT COUNT(*) FROM gate) WHERE k = 1 RETURNING v)
    INSERT INTO t2 SELECT 1, v FROM u;
  END $$;
}

teardown
{
  DROP TABLE hot, t2, t3, gate;
  DROP FUNCTION wait_for_gate(), xact_lock_and_update(), lock_and_update();
}

session blocker
step b_begin	{ BEGIN ISOLATION LEVEL REPEATABLE READ; }
step b_update	{ UPDATE hot SET v = v + 1 WHERE k = 1; }
step b_commit	{ COMMIT; }

session writer
# QPM explains a newly seen plan at ExecutorEnd, which re-opens its tables with ACCESS SHARE after
# the retry and would block the DDL on its own, hiding a lost lock.
setup		{ SET yb_pg_stat_plans_track = 'none'; }
step w_begin_rr	{ BEGIN ISOLATION LEVEL REPEATABLE READ; }
step w_begin_rc	{ BEGIN ISOLATION LEVEL READ COMMITTED; }
step w_autocommit_rr	{ SET default_transaction_isolation = 'repeatable read'; }
step w_autocommit_rc	{ SET default_transaction_isolation = 'read committed'; }
step w_read_hot	{ SELECT v FROM hot WHERE k = 1; }
step w_write
{
  WITH u AS (UPDATE hot SET v = v + (SELECT COUNT(*) FROM gate) WHERE k = 1 RETURNING v)
  INSERT INTO t2 SELECT 1, v FROM u;
}
step w_write_then_wait
{
  WITH u AS (UPDATE hot SET v = v + (SELECT COUNT(*) FROM gate) WHERE k = 1 RETURNING v)
  INSERT INTO t2 SELECT 1, v FROM u WHERE wait_for_gate();
}
step w_advisory_write	{ SELECT xact_lock_and_update(); }
step w_exclusive_write	{ SELECT lock_and_update(); }
step w_commit	{ COMMIT; }

session gate
step g_lock	{ SELECT pg_advisory_lock(1); }
step g_unlock	{ SELECT pg_advisory_unlock(1); }

session ddl
step d_add_check	{ ALTER TABLE t2 ADD CONSTRAINT t2_nonneg CHECK (v >= 0) NOT VALID; }
step d_try_advisory	{ SELECT pg_try_advisory_xact_lock(42); }

session reader
step r_read_t3	{ SELECT * FROM t3; }

# REPEATABLE READ in a transaction block.
permutation w_begin_rr b_begin b_update w_write b_commit d_add_check w_commit
# REPEATABLE READ autocommit.
permutation g_lock w_autocommit_rr b_begin b_update w_write_then_wait b_commit d_add_check g_unlock
# READ COMMITTED in a transaction block, as the first statement and after another one.
permutation w_begin_rc b_begin b_update w_write b_commit d_add_check w_commit
permutation w_begin_rc w_read_hot b_begin b_update w_write b_commit d_add_check w_commit
# READ COMMITTED autocommit.
permutation g_lock w_autocommit_rc b_begin b_update w_write_then_wait b_commit d_add_check g_unlock
# A transaction-level advisory lock taken before the conflict.
permutation w_begin_rr b_begin b_update w_advisory_write b_commit d_try_advisory w_commit d_try_advisory
# A global ACCESS EXCLUSIVE lock taken before the conflict, released and re-acquired by the retry.
permutation w_begin_rr b_begin b_update w_exclusive_write b_commit d_add_check r_read_t3 w_commit
