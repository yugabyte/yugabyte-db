-- This test cannot be run in a single transaction
-- This test must be run on a database named 'contrib_regression'
-- YB: the database doesn't need to be 'contrib_regression'

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

-- INIT

SET anon.maskschema TO 'foo';

BEGIN; -- YB: Workaround for read time error, check #25665
SET yb_non_ddl_txn_for_sys_tables_allowed = true; -- YB: next statement updates pg_seclabel and is not a DDL
SELECT anon.start_dynamic_masking();
COMMIT; -- YB: Workaround for read time error, check #25665

CREATE ROLE skynet LOGIN PASSWORD 'x';

SECURITY LABEL FOR anon ON ROLE skynet IS 'MASKED';

SELECT anon.mask_update();

-- search_path must be 'foo,public'
\! PGPASSWORD=x ${YB_BUILD_ROOT}/postgres/bin/ysqlsh -U skynet -c 'SHOW search_path;' # YB: Use ysqlsh and the default database


CREATE ROLE hal LOGIN PASSWORD 'x';

SECURITY LABEL FOR anon ON ROLE hal IS 'MASKED';

SELECT anon.mask_update();

-- search_path must be 'foo,public'
\! PGPASSWORD=x ${YB_BUILD_ROOT}/postgres/bin/ysqlsh -U hal -c 'SHOW search_path;' # YB: Use ysqlsh and the default database

-- STOP

SELECT anon.stop_dynamic_masking();

-- REMOVE MASKS
SELECT anon.remove_masks_for_all_roles();
SELECT COUNT(*)=0 FROM anon.pg_masked_roles WHERE hasmask;

--  CLEAN

DROP EXTENSION anon CASCADE;

REASSIGN OWNED BY skynet TO postgres;
DROP OWNED BY skynet CASCADE;
DROP ROLE skynet;

REASSIGN OWNED BY hal TO postgres;
DROP OWNED BY hal CASCADE;
DROP ROLE hal;
