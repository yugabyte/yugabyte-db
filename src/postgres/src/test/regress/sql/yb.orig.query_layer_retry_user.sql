--
-- A query-layer retry of a whole transaction must leave the backend as an
-- abort followed by a replay of the same command would.  AbortTransaction
-- restores the user id that a SECURITY DEFINER function switched to; the
-- retry did not, so the caller kept the definer's privileges.  Scenario:
-- retry_alice calls a superuser-owned SECURITY DEFINER function that raises
-- a retriable kAborted (YB001) on its first attempt only, keyed on a
-- sequence; after the retry, current_user must be retry_alice.
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
-- The same for a function that changes the role through its SET clause
-- rather than SECURITY DEFINER: the abort undoes the clause, so after the
-- retry current_user must be the caller, not retry_bob.  The check runs in
-- the same REPEATABLE READ transaction, which the retry restarts in place;
-- the commit would otherwise undo the clause before a later statement.
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

DROP FUNCTION sd();
DROP FUNCTION sr();
DROP SEQUENCE sd_attempt;
DROP SEQUENCE sr_attempt;
DROP ROLE retry_alice;
DROP ROLE retry_bob;
