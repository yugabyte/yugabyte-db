BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

CREATE TABLE employee AS SELECT 'john' as "NAME", 100000 AS salary;

SECURITY LABEL FOR anon ON COLUMN employee.salary IS 'MASKED WITH VALUE 0';

\timing

SELECT bool_and(anon.masking_expressions_for_table('employee'::REGCLASS) IS NOT NULL)
AS "anon.masking_expressions"
FROM generate_series(1,1000);

SELECT bool_and(anon.mask_filters('employee'::REGCLASS) IS NOT NULL)
AS "anon.mask_filters"
FROM generate_series(1,1000);

\timing off

ROLLBACK;
