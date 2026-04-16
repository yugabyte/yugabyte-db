BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

SELECT anon.masking_expressions_for_table(NULL, 'anon') IS NULL;

SELECT anon.masking_expressions_for_table('pg_roles'::REGCLASS,NULL) IS NULL;

SELECT anon.masking_expressions_for_table('pg_roles'::REGCLASS,'does_not_exists') IS NOT NULL;

CREATE TABLE employees (
  "NAME" TEXT DEFAULT '<unkown>',
  job TEXT,
  salary int
);

INSERT INTO employees
VALUES ('john', NULL, 100000);

SELECT anon.masking_expressions_for_table('employees'::REGCLASS,'anon')
     = ' "NAME" AS "NAME",job AS job,salary AS salary';
;--     = '"NAME" AS "NAME",job AS job,salary AS salary';

SELECT anon.masking_value_for_column('employees'::REGCLASS,1,'anon')
     = '"NAME"';

SET anon.privacy_by_default TO FALSE;

SECURITY LABEL FOR anon ON COLUMN employees.salary IS 'MASKED WITH VALUE 0';

SECURITY LABEL FOR anon ON COLUMN employees.job
IS 'MASKED WITH FUNCTION pg_catalog.md5($$ t e s t $$)';

SELECT anon.masking_expressions_for_table('employees'::REGCLASS,'anon') IS NOT NULL;

SELECT anon.masking_value_for_column('employees'::REGCLASS,1,'anon')
     = '"NAME"';
;

SELECT anon.masking_value_for_column('employees'::REGCLASS,2,'anon')
     = 'CAST(pg_catalog.md5($$ t e s t $$) AS text)';


SELECT anon.masking_value_for_column('employees'::REGCLASS,3,'anon')
    = 'CAST(0 AS integer)';

SET anon.strict_mode TO FALSE;

SELECT anon.masking_value_for_column('employees'::REGCLASS,1,'anon')
     = '"NAME"';

SELECT anon.masking_value_for_column('employees'::REGCLASS,2,'anon')
     = 'pg_catalog.md5($$ t e s t $$)';

SELECT anon.masking_value_for_column('employees'::REGCLASS,3,'anon')
     = '0';

SET anon.privacy_by_default TO TRUE;


SELECT anon.masking_value_for_column('employees'::REGCLASS,1,'anon')
     =  $$'<unkown>'::text$$;

SELECT anon.masking_value_for_column('employees'::REGCLASS,2,'anon')
     = 'pg_catalog.md5($$ t e s t $$)';

SELECT anon.masking_value_for_column('employees'::REGCLASS,3,'anon')
    = '0';

SELECT anon.masking_expressions_for_table('employees'::REGCLASS,'anon') IS NOT NULL;

ROLLBACK;
