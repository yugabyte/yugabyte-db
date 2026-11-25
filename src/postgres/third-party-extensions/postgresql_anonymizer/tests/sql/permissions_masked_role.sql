BEGIN;

CREATE EXTENSION anon CASCADE;
SELECT anon.init();

CREATE ROLE mallory_the_masked_user;
SECURITY LABEL FOR anon ON ROLE mallory_the_masked_user IS 'MASKED';

CREATE TABLE t1(i INT);
ALTER TABLE t1 ADD COLUMN t TEXT;

SECURITY LABEL FOR anon ON COLUMN t1.t
IS 'MASKED WITH VALUE NULL';

INSERT INTO t1 VALUES (1,'test');

--
-- We're checking the owner's permissions
--
-- see
-- https://postgresql-anonymizer.readthedocs.io/en/latest/SECURITY/#permissions
--

SET ROLE mallory_the_masked_user;

SELECT anon.pseudo_first_name(0) IS NOT NULL;

-- SHOULD FAIL
DO $$
BEGIN
  PERFORM anon.init();
  EXCEPTION WHEN insufficient_privilege
  THEN RAISE NOTICE 'insufficient_privilege';
END$$;


-- SHOULD FAIL
DO $$
BEGIN
  PERFORM anon.anonymize_table('t1');
  EXCEPTION WHEN insufficient_privilege
  THEN RAISE NOTICE 'insufficient_privilege';
END$$;

SET ROLE mallory_the_masked_user;

-- SHOULD FAIL
DO $$
BEGIN
  SELECT * FROM public.t1;
  EXCEPTION WHEN insufficient_privilege
  THEN RAISE NOTICE 'insufficient_privilege';
END$$;

SET ROLE mallory_the_masked_user;

SELECT COUNT(*)=1 FROM anon.pg_masking_rules;

-- SHOULD FAIL
SAVEPOINT fail_seclabel_on_role;
DO $$
BEGIN
  SECURITY LABEL FOR anon ON ROLE mallory_the_masked_user IS NULL;
  EXCEPTION WHEN insufficient_privilege
  THEN RAISE NOTICE 'insufficient_privilege';
END$$;
ROLLBACK TO fail_seclabel_on_role;


ROLLBACK;
