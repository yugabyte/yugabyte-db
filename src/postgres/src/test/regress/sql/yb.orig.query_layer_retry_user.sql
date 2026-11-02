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

--
-- srs() sets search_path with a SET clause.  After the retry, an unqualified
-- name in the same transaction must resolve through the caller's
-- search_path: rt is public.rt, not retry_s.rt.
--
CREATE SCHEMA retry_s;
CREATE TABLE rt (v text);
INSERT INTO rt VALUES ('public');
CREATE TABLE retry_s.rt (v text);
INSERT INTO retry_s.rt VALUES ('retry_s');
CREATE SEQUENCE srs_attempt;
CREATE FUNCTION srs() RETURNS int LANGUAGE plpgsql SET search_path = retry_s AS $$
BEGIN
  IF nextval('public.srs_attempt') = 1 THEN
    RAISE EXCEPTION 'injected kAborted' USING ERRCODE = 'YB001';
  END IF;
  RETURN 1;
END $$;
BEGIN ISOLATION LEVEL REPEATABLE READ;
SELECT srs();
SELECT v FROM rt;
COMMIT;

--
-- sdp() is SECURITY DEFINER and also sets search_path with a SET clause, so
-- the failed attempt leaks both the user switch and a GUC level.  After the
-- retry, current_user must be retry_alice and rt must be public.rt.
--
CREATE SEQUENCE sdp_attempt;
GRANT SELECT ON rt TO retry_alice;
CREATE FUNCTION sdp() RETURNS int LANGUAGE plpgsql SECURITY DEFINER
  SET search_path = retry_s AS $$
BEGIN
  IF nextval('public.sdp_attempt') = 1 THEN
    RAISE EXCEPTION 'injected kAborted' USING ERRCODE = 'YB001';
  END IF;
  RETURN 1;
END $$;
GRANT EXECUTE ON FUNCTION sdp() TO retry_alice;
SET ROLE retry_alice;
BEGIN ISOLATION LEVEL REPEATABLE READ;
SELECT sdp();
SELECT current_user;
SELECT v FROM rt;
COMMIT;
RESET ROLE;

DROP FUNCTION sd();
DROP FUNCTION sr();
DROP FUNCTION sde();
DROP FUNCTION sre();
DROP FUNCTION srs();
DROP FUNCTION sdp();
DROP SEQUENCE sd_attempt;
DROP SEQUENCE sr_attempt;
DROP SEQUENCE sde_attempt;
DROP SEQUENCE sre_attempt;
DROP SEQUENCE srs_attempt;
DROP SEQUENCE sdp_attempt;
DROP TABLE rt;
DROP TABLE retry_s.rt;
DROP SCHEMA retry_s;
DROP ROLE retry_alice;
DROP ROLE retry_bob;
