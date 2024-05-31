BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

CREATE TABLE employee (
  firstname TEXT,
  lastname TEXT,
  phone TEXT,
  zipcode INTEGER
);
INSERT INTO employee VALUES ('Sarah','Connor','0609110911');
SELECT * FROM employee;


SECURITY LABEL FOR anon ON COLUMN employee.firstname
IS 'MASKED WITH VALUE ''XXXX'' ';

SECURITY LABEL FOR anon ON COLUMN employee.lastname
IS 'MASKED WITH VALUE $$REDACTED$$ ';

SECURITY LABEL FOR anon ON COLUMN employee.phone
IS 'MASKED WITH VALUE NULL ';

SECURITY LABEL FOR anon ON COLUMN employee.zipcode
IS 'MASKED WITH VALUE 100';

SELECT anon.anonymize_table('employee');

SELECT count(*)=1
FROM anon.pg_masking_rules
WHERE masking_filter = '$$REDACTED$$';

SELECT count(*)=1
FROM anon.pg_masking_rules
WHERE masking_filter = 'NULL';

SELECT count(*)=1
FROM anon.pg_masking_rules
WHERE masking_filter = '100';




ROLLBACK;
