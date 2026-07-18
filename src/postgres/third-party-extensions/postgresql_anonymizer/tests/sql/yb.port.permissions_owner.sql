/* YB: workaround for lack of transactional DDL
BEGIN;
*/ -- YB

CREATE EXTENSION anon CASCADE;
BEGIN; -- YB: Workaround for read time error, check #25665
SET yb_non_ddl_txn_for_sys_tables_allowed = true; -- YB: next statement updates pg_seclabel and is not a DDL
SELECT anon.init();
COMMIT; -- YB: Workaround for read time error, check #25665

CREATE ROLE oscar_the_owner;
ALTER DATABASE :DBNAME OWNER TO oscar_the_owner;

CREATE ROLE mallory_the_masked_user;
SECURITY LABEL FOR anon ON ROLE mallory_the_masked_user IS 'MASKED';

--
-- We're checking the owner's permissions
--
-- see
-- https://postgresql-anonymizer.readthedocs.io/en/latest/SECURITY/#permissions
--

SET ROLE oscar_the_owner;

SELECT anon.pseudo_first_name(0) IS NOT NULL;


-- SHOULD FAIL
DO $$
BEGIN
  PERFORM anon.init();
  EXCEPTION WHEN insufficient_privilege
  THEN RAISE NOTICE 'insufficient_privilege';
END$$;

CREATE TABLE t1(i INT);
ALTER TABLE t1 ADD COLUMN t TEXT;

SECURITY LABEL FOR anon ON COLUMN t1.t
IS 'MASKED WITH VALUE NULL';

INSERT INTO t1 VALUES (1,'test');

SELECT anon.anonymize_table('t1');

SELECT * FROM t1;

UPDATE t1 SET t='test' WHERE i=1;

-- SHOULD FAIL
BEGIN; -- YB: workaround for lack of transactional DDL
SAVEPOINT fail_start_engine;
SELECT anon.start_dynamic_masking();
ROLLBACK TO fail_start_engine;

RESET ROLE;
SELECT anon.start_dynamic_masking();
SET ROLE oscar_the_owner;

SELECT * FROM t1;

--SELECT * FROM mask.t1;

-- SHOULD FAIL
SAVEPOINT fail_stop_engine;
SELECT anon.stop_dynamic_masking();
ROLLBACK TO fail_stop_engine;

RESET ROLE;
SELECT anon.stop_dynamic_masking();
SET ROLE oscar_the_owner;

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

RESET ROLE; -- YB: workaround for lack of transactional DDL
ALTER DATABASE :DBNAME OWNER TO yugabyte; -- YB: workaround for lack of transactional DDL
DROP OWNED BY oscar_the_owner CASCADE; -- YB: workaround for lack of transactional DDL
DROP OWNED BY mallory_the_masked_user CASCADE; -- YB: workaround for lack of transactional DDL
DROP ROLE oscar_the_owner; -- YB: workaround for lack of transactional DDL
DROP ROLE mallory_the_masked_user; -- YB: workaround for lack of transactional DDL
DROP EXTENSION anon CASCADE; -- YB: workaround for lack of transactional DDL
