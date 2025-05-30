/* YB: workaround for lack of transactional DDL
BEGIN;
*/ -- YB

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

BEGIN; -- YB: Workaround for read time error, check #25665
SET yb_non_ddl_txn_for_sys_tables_allowed = true; -- YB: next statement updates pg_seclabel and is not a DDL
SELECT anon.start_dynamic_masking();
COMMIT; -- YB: Workaround for read time error, check #25665

CREATE ROLE "WOPR";
SECURITY LABEL FOR anon ON ROLE "WOPR" IS 'MASKED';

CREATE ROLE hal LOGIN;
SECURITY LABEL FOR anon ON ROLE hal IS 'MASKED';

CREATE ROLE jarvis;

-- FORCE update because COMMENT doesn't trigger the Event Trigger
SELECT anon.mask_update();

SELECT anon.hasmask('"WOPR"') IS TRUE;

SELECT anon.hasmask('hal') IS TRUE;

SELECT anon.hasmask('jarvis') IS FALSE;

SELECT anon.hasmask('postgres') IS FALSE;

SELECT anon.hasmask(NULL) IS FALSE;

-- Must return an error
SELECT anon.hasmask('does_not_exist');

/* YB: workaround for lack of transactional DDL
ROLLBACK;
*/ -- YB

SELECT anon.remove_masks_for_all_roles(); -- YB: workaround for lack of transactional DDL
SELECT anon.stop_dynamic_masking(); -- YB: workaround for lack of transactional DDL
DROP EXTENSION anon; -- YB: workaround for lack of transactional DDL
DROP OWNED BY "WOPR", hal, jarvis CASCADE; -- YB: workaround for lack of transactional DDL
DROP ROLE "WOPR", hal, jarvis; -- YB: workaround for lack of transactional DDL
