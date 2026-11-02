--
-- A query-layer retry restarts the transaction in place.  It must leave the
-- session as an abort and a replay of the same statement would.  Each
-- function below raises a retriable kAborted (YB001) on its first call only,
-- keyed on a sequence.  After the retry, current_user must be the caller.
-- First case: retry_alice calls sd(), a SECURITY DEFINER function owned by
-- the test user.
--
CREATE ROLE retry_alice;
CREATE SEQUENCE sd_attempt;
GRANT USAGE ON SEQUENCE sd_attempt TO retry_alice;
CREATE FUNCTION sd() RETURNS int LANGUAGE plpgsql SECURITY DEFINER AS $$
BEGIN
  IF nextval('sd_attempt') = 1 THEN
    RAISE EXCEPTION 'injected kAborted' USING ERRCODE = 'YB001';
  END IF;
  RETURN 1;
END $$;
GRANT EXECUTE ON FUNCTION sd() TO retry_alice;
SET ROLE retry_alice;
SELECT sd();
SELECT current_user;
RESET ROLE;

--
-- sr() sets the role with a SET clause instead of SECURITY DEFINER.  After
-- the retry, current_user must be the caller, not retry_bob.  The check runs
-- in the same REPEATABLE READ transaction, because COMMIT would undo the
-- clause first and hide the bug.
--
CREATE ROLE retry_bob;
CREATE SEQUENCE sr_attempt;
GRANT USAGE ON SEQUENCE sr_attempt TO retry_bob;
CREATE FUNCTION sr() RETURNS int LANGUAGE plpgsql SET role = retry_bob AS $$
BEGIN
  IF nextval('sr_attempt') = 1 THEN
    RAISE EXCEPTION 'injected kAborted' USING ERRCODE = 'YB001';
  END IF;
  RETURN 1;
END $$;
BEGIN ISOLATION LEVEL REPEATABLE READ;
SELECT sr();
SELECT current_user;
COMMIT;

--
-- sde() and sre() are sd() and sr() with an exception block that does not
-- catch YB001.  plpgsql runs the block in a subtransaction and rolls it back
-- before the error leaves the function.  The retry must still restore the
-- caller.
--
CREATE SEQUENCE sde_attempt;
GRANT USAGE ON SEQUENCE sde_attempt TO retry_alice;
CREATE FUNCTION sde() RETURNS int LANGUAGE plpgsql SECURITY DEFINER AS $$
BEGIN
  IF nextval('sde_attempt') = 1 THEN
    RAISE EXCEPTION 'injected kAborted' USING ERRCODE = 'YB001';
  END IF;
  RETURN 1;
EXCEPTION WHEN division_by_zero THEN
  RETURN -1;
END $$;
GRANT EXECUTE ON FUNCTION sde() TO retry_alice;
SET ROLE retry_alice;
SELECT sde();
SELECT current_user;
RESET ROLE;

CREATE SEQUENCE sre_attempt;
GRANT USAGE ON SEQUENCE sre_attempt TO retry_bob;
CREATE FUNCTION sre() RETURNS int LANGUAGE plpgsql SET role = retry_bob AS $$
BEGIN
  IF nextval('sre_attempt') = 1 THEN
    RAISE EXCEPTION 'injected kAborted' USING ERRCODE = 'YB001';
  END IF;
  RETURN 1;
EXCEPTION WHEN division_by_zero THEN
  RETURN -1;
END $$;
BEGIN ISOLATION LEVEL REPEATABLE READ;
SELECT sre();
SELECT current_user;
COMMIT;

DROP FUNCTION sd();
DROP FUNCTION sr();
DROP FUNCTION sde();
DROP FUNCTION sre();
DROP SEQUENCE sd_attempt;
DROP SEQUENCE sr_attempt;
DROP SEQUENCE sde_attempt;
DROP SEQUENCE sre_attempt;
DROP ROLE retry_alice;
DROP ROLE retry_bob;
