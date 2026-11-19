BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

SELECT anon.init();

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
IS 'MASKED WITH FUNCTION anon.lower(people.name) ';

-- TEST 5
SECURITY LABEL FOR anon ON SCHEMA public
IS 'TRUSTED';

SECURITY LABEL FOR anon ON COLUMN people.name
IS 'MASKED WITH FUNCTION public.lower(people.name) ';

-- TEST 6
SECURITY LABEL FOR anon ON COLUMN people.name
IS 'MASKED WITH VALUE NULL';

ROLLBACK;
