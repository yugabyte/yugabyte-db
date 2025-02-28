/* YB: workaround for lack of transactional DDL
BEGIN;
*/ -- YB

CREATE SCHEMA dbo;

CREATE TABLE dbo.tbl1 AS
SELECT
    1234::INTEGER AS staff_id,
    'John'::TEXT AS firstname,
    'Doe'::TEXT  AS lastname,
    'john@doe.com'::TEXT AS email
;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

SECURITY LABEL FOR anon ON COLUMN dbo.tbl1.lastname
IS 'MASKED WITH FUNCTION anon.fake_last_name()';

BEGIN; -- YB: Workaround for read time error, check #25665
SET yb_non_ddl_txn_for_sys_tables_allowed = true; -- YB: next statement updates pg_seclabel and is not a DDL
SELECT anon.init();
COMMIT; -- YB: Workaround for read time error, check #25665

SELECT lastname = 'Doe' FROM dbo.tbl1;

-- Test with the masked schema in the search path

SET search_path=dbo,public;

SET anon.sourceschema TO 'dbo';
SET anon.maskschema TO 'dbo_mask';
SELECT anon.start_dynamic_masking();

SELECT lastname != 'Doe' FROM dbo_mask.tbl1;

SELECT anon.stop_dynamic_masking();

SELECT COUNT(*)=0 FROM pg_namespace WHERE nspname='dbo_mask';

-- Test WITHOUT the masked schema in the search path

SET search_path=public;

SET anon.sourceschema TO 'dbo';
SET anon.maskschema TO 'dbo_MASK_2';
SELECT anon.start_dynamic_masking();

SELECT lastname != 'Doe' FROM "dbo_MASK_2".tbl1;

SELECT anon.stop_dynamic_masking();

SELECT COUNT(*)=0 FROM pg_namespace WHERE nspname='dbo_MASK_2';

/* YB: workaround for lack of transactional DDL
ROLLBACK;
*/ -- YB


DROP TABLE dbo.tbl1 CASCADE; -- YB: workaround for lack of transactional DDL
DROP SCHEMA dbo CASCADE; -- YB: workaround for lack of transactional DDL
DROP EXTENSION anon CASCADE; -- YB: workaround for lack of transactional DDL
