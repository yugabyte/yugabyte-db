--
-- A query-layer retry of a whole transaction must leave the backend as an
-- abort followed by a replay of the same command would.  Each case below
-- raises a retriable kAborted (YB001) on its first attempt only, keyed on a
-- sequence, so the retry always runs once and then succeeds.
--

--
-- #30540: the retry released PL/pgSQL's simple-expression plan refcounts but
-- kept the local transaction id, so a plan invalidated before the retry made
-- it fail with "plancache reference ... is not owned by resource owner
-- PL/pgSQL simple expressions".  Scenario: the first attempt evaluates a
-- simple expression, runs DISCARD PLANS, then raises YB001; the retry must
-- return 1.
--
CREATE SEQUENCE attempt;
CREATE FUNCTION f() RETURNS int LANGUAGE plpgsql AS $$
BEGIN
  IF nextval('attempt') = 1 THEN
    EXECUTE 'DISCARD PLANS';
    RAISE EXCEPTION 'injected kAborted' USING ERRCODE = 'YB001';
  END IF;
  RETURN 1;
END $$;
SELECT f();

--
-- The same retry inside an explicit SERIALIZABLE block: the new local
-- transaction id is assigned mid-block, and the block must keep its
-- isolation level.
--
ALTER SEQUENCE attempt RESTART;
BEGIN ISOLATION LEVEL SERIALIZABLE;
SELECT f();
SHOW transaction_isolation;
COMMIT;

--
-- The procedure shape #30540 was reported on: CALL of a procedure that
-- invalidates plans and raises YB001 before its COMMIT; the retried CALL
-- must commit exactly one row.
--
CREATE TABLE retry_rows (k int);
CREATE SEQUENCE p_attempt;
CREATE PROCEDURE p() LANGUAGE plpgsql AS $$
BEGIN
  INSERT INTO retry_rows VALUES (1);
  IF nextval('p_attempt') = 1 THEN
    EXECUTE 'DISCARD PLANS';
    RAISE EXCEPTION 'injected kAborted' USING ERRCODE = 'YB001';
  END IF;
  COMMIT;
END $$;
-- READ COMMITTED retries this CALL on its own; opt it in so REPEATABLE READ
-- (the debug-build default) retries it too.
SET yb_extra_commands_to_retry = 'CALL';
SET yb_extra_commands_to_retry_in_proc = 'DISCARD PLANS';
CALL p();
SELECT count(*) FROM retry_rows;
RESET yb_extra_commands_to_retry;
RESET yb_extra_commands_to_retry_in_proc;

DROP PROCEDURE p();
DROP SEQUENCE p_attempt;
DROP TABLE retry_rows;
DROP FUNCTION f();
DROP SEQUENCE attempt;
