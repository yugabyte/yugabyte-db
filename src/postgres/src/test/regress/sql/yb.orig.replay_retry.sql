--
-- Whole-transaction retry by replay: a retryable error on the first statement
-- that reads the snapshot is retried in a fresh transaction after replaying
-- the snapshot-free statements before it (BEGIN, SET, ...).  Once a statement
-- that reads the snapshot has completed, later statements are not retried,
-- because a replay could see different data than the client already got.
--
-- inj(seq) raises a retryable YB error the first time it runs for a given
-- sequence; sequences survive abort, so the retried attempt succeeds.
--
SET yb_enable_replay_retry = on;
CREATE TABLE replay_t (k int);
CREATE SEQUENCE replay_s1;
CREATE SEQUENCE replay_s2;
CREATE SEQUENCE replay_s3;
CREATE SEQUENCE replay_s4;
CREATE FUNCTION inj(seq regclass, code text) RETURNS int LANGUAGE plpgsql AS $$
BEGIN
  IF nextval(seq) = 1 THEN
    RAISE EXCEPTION 'injected %', code USING ERRCODE = code;
  END IF;
  RETURN 1;
END $$;

-- RR: SET LOCAL before the failing SELECT is replayed; its value survives.
BEGIN ISOLATION LEVEL REPEATABLE READ;
SET LOCAL work_mem = '8MB';
SELECT inj('replay_s1', 'YB003');
SHOW work_mem;
COMMIT;
SHOW work_mem;

-- SERIALIZABLE, kAborted, failing statement writes: the row lands once.
BEGIN ISOLATION LEVEL SERIALIZABLE;
SET LOCAL statement_timeout = '60s';
SET LOCAL lock_timeout = '30s';
INSERT INTO replay_t SELECT inj('replay_s2', 'YB001');
SHOW lock_timeout;
COMMIT;
SELECT * FROM replay_t;

-- A completed snapshot read closes the log: the next error reaches the client.
BEGIN ISOLATION LEVEL REPEATABLE READ;
SELECT count(*) FROM replay_t;
SELECT inj('replay_s3', 'YB003');
ROLLBACK;

-- READ COMMITTED cases are in pg_txn-test (PgReplayRetryReadCommittedTest):
-- debug builds turn READ COMMITTED off, which would change this file's output.

-- Off: today's behavior, the error reaches the client.
SET yb_enable_replay_retry = off;
BEGIN ISOLATION LEVEL REPEATABLE READ;
SET LOCAL work_mem = '8MB';
SELECT inj('replay_s4', 'YB003');
ROLLBACK;

-- statement_timeout bounds the statement across retries, as it does for the
-- surgical retry: two 1.5s attempts exceed the 2s budget.  The sleep is the
-- point of the test: it is what spends the timeout.
CREATE SEQUENCE replay_s5;
CREATE FUNCTION slow_inj(seq regclass) RETURNS int LANGUAGE plpgsql AS $$
BEGIN
  PERFORM pg_sleep(1.5);
  RETURN inj(seq, 'YB003');
END $$;
SET yb_enable_replay_retry = on;
SET statement_timeout = '2s';
BEGIN ISOLATION LEVEL REPEATABLE READ;
SET LOCAL work_mem = '8MB';
SELECT slow_inj('replay_s5');
ROLLBACK;
RESET statement_timeout;

-- A log over yb_replay_retry_log_limit gives up on the replay: the error
-- reaches the client.
CREATE SEQUENCE replay_s9;
SET yb_replay_retry_log_limit = 0;
BEGIN ISOLATION LEVEL REPEATABLE READ;
SET LOCAL work_mem = '8MB';
SELECT inj('replay_s9', 'YB003');
ROLLBACK;
RESET yb_replay_retry_log_limit;

-- A change of yb_enable_replay_retry inside a transaction applies from the
-- next transaction, so a block never switches between the in-place restart and
-- the replay: this block started with it on and is replayed.
CREATE SEQUENCE replay_s14;
BEGIN ISOLATION LEVEL REPEATABLE READ;
SET LOCAL yb_enable_replay_retry = off;
SELECT inj('replay_s14', 'YB003');
COMMIT;
SHOW yb_enable_replay_retry;

-- The replayed transaction keeps the original transaction start, which
-- deadlock-victim choice and wait-queue order go by: now() on the retried
-- attempt equals now() on the failed one (recorded in a sequence, which
-- outlives the abort).
CREATE SEQUENCE replay_s12;
CREATE SEQUENCE now_seen MINVALUE 0;
CREATE FUNCTION inj_now(seq regclass) RETURNS boolean LANGUAGE plpgsql AS $$
DECLARE t bigint := (extract(epoch FROM now()) * 1000000)::bigint;
BEGIN
  IF nextval(seq) = 1 THEN
    PERFORM setval('now_seen', t);
    RAISE EXCEPTION 'injected YB003' USING ERRCODE = 'YB003';
  END IF;
  RETURN t = (SELECT last_value FROM now_seen);
END $$;
BEGIN ISOLATION LEVEL REPEATABLE READ;
SET LOCAL work_mem = '8MB';
SELECT inj_now('replay_s12') AS same_start;
COMMIT;

-- pg_stat_statements counts a replayed statement once: the replay is part of
-- the retry, not a new execution.
CREATE SEQUENCE replay_s11;
SELECT pg_stat_statements_reset() IS NOT NULL AS reset;
BEGIN ISOLATION LEVEL REPEATABLE READ;
SET LOCAL enable_seqscan = off;
SELECT inj('replay_s11', 'YB003');
COMMIT;
SELECT calls FROM pg_stat_statements WHERE query = 'SET LOCAL enable_seqscan = off';

DROP TABLE replay_t;
DROP FUNCTION slow_inj;
DROP FUNCTION inj_now;
DROP FUNCTION inj;
