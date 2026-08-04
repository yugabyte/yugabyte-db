/* YB: workaround for lack of transactional DDL
BEGIN;
*/ -- YB

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

BEGIN; -- YB: Workaround for read time error, check #25665
SET yb_non_ddl_txn_for_sys_tables_allowed = true; -- YB: next statement updates pg_seclabel and is not a DDL
SELECT anon.init();
COMMIT; -- YB: Workaround for read time error, check #25665

--SELECT anon.start_dynamic_masking();

-- Table `people`
CREATE TABLE people (
  id SERIAL UNIQUE,
  name TEXT,
  "CreditCard" TEXT,
  fk_company INTEGER
);

CREATE OR REPLACE FUNCTION public.lower(TEXT)
RETURNS TEXT AS $$
  SELECT upper($1)
$$ LANGUAGE SQL;

SET anon.restrict_to_trusted_schemas = off;

-- TEST 1
SECURITY LABEL FOR anon ON COLUMN people.name
IS 'MASKED WITH FUNCTION lower(people.name) ';

SET anon.restrict_to_trusted_schemas = on;

-- TEST 2 generates an error
BEGIN; -- YB: workaround for lack of transactional DDL
SAVEPOINT before_test_2;
SECURITY LABEL FOR anon ON COLUMN people.name
IS 'MASKED WITH FUNCTION lower(people.name) ';
ROLLBACK TO before_test_2;

-- TEST 3 generates an error
SAVEPOINT before_test_3;
SECURITY LABEL FOR anon ON COLUMN people.name
IS 'MASKED WITH FUNCTION public.lower(people.name) ';
ROLLBACK TO before_test_3;

-- TEST 4
SECURITY LABEL FOR anon ON COLUMN people.name
IS 'MASKED WITH FUNCTION pg_catalog.lower(people.name) ';

SECURITY LABEL FOR anon ON SCHEMA public
IS 'TRUSTED';

-- TEST 5
SECURITY LABEL FOR anon ON COLUMN people.name
IS 'MASKED WITH FUNCTION public.lower(people.name) ';

-- TEST 6
SECURITY LABEL FOR anon ON COLUMN people.name
IS 'MASKED WITH VALUE NULL';

ROLLBACK;

DROP TABLE people CASCADE; -- YB: workaround for lack of transactional DDL
DROP EXTENSION anon CASCADE; -- YB: workaround for lack of transactional DDL
DROP FUNCTION public.lower(TEXT); -- YB: workaround for lack of transactional DDL
