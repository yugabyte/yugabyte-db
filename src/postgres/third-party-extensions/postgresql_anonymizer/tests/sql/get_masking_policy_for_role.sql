BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

CREATE TABLE employee (
  "NAME" TEXT DEFAULT '<unkown>',
  job TEXT,
  salary int
);

CREATE ROLE batman;

SECURITY LABEL FOR anon ON ROLE batman is 'MASKED';


INSERT INTO employee
VALUES ('john', NULL, 100000);

--SELECT anon.masking_expressions('employee'::REGCLASS);

SECURITY LABEL FOR anon ON COLUMN employee.salary IS 'MASKED WITH VALUE 0';

--SET anon.transparent_dynamic_masking TO TRUE;

SET anon.masking_policies TO 'anon, rpgd';

SECURITY LABEL FOR rgpd ON COLUMN employee.salary IS 'MASKED WITH VALUE 0';

SAVEPOINT a;
SET ROLE batman;

SELECT * FROM employees;
ROLLBACK TO a;

ROLLBACK;
