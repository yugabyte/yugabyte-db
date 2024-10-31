-- Drop AGGREGATE
DROP AGGREGATE IF EXISTS my_sum(integer);

-- Drop CAST
DROP CAST IF EXISTS (TEXT AS INTEGER);

-- Drop COLLATION
DROP COLLATION IF EXISTS my_collation;

-- Drop CONVERSION --- Not Supported on YB yet
-- DROP CONVERSION IF EXISTS my_conversion;

-- Drop DOMAIN
DROP DOMAIN IF EXISTS positive_int;

-- Drop FUNCTION
DROP FUNCTION IF EXISTS add_two_numbers(INTEGER, INTEGER);

-- Drop OPERATOR
DROP OPERATOR IF EXISTS === (INTEGER, INTEGER);

-- Drop OPERATOR CLASS
-- Drop the comparison function
DROP FUNCTION IF EXISTS text_length_cmp(text, text);
DROP OPERATOR CLASS IF EXISTS text_length_ops FOR TYPE text USING btree;

-- Drop OPERATOR FAMILY
DROP OPERATOR FAMILY IF EXISTS int_fam USING btree;

-- Drop POLICY
DROP POLICY IF EXISTS my_policy ON employees;

-- Drop PROCEDURE
DROP PROCEDURE IF EXISTS increment_salary(INTEGER, NUMERIC);

-- Drop RULE
DROP RULE IF EXISTS replace_insert ON employees;

-- Drop SCHEMA
DROP SCHEMA IF EXISTS company_schema CASCADE;

-- Drop STATISTICS
DROP STATISTICS IF EXISTS names_distinct;

-- Drop TRANSFORM --- Not Supported on YB yet

-- Drop TRIGGER
DROP TRIGGER IF EXISTS update_timestamp ON employees;

-- Drop FUNCTION for TRIGGER
DROP FUNCTION IF EXISTS update_modified_timestamp();

-- Drop VIEW
DROP VIEW IF EXISTS employee_salaries;

-- Drop TABLE
DROP TABLE IF EXISTS employees;
