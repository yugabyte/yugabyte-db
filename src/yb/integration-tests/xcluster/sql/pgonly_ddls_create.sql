-- Create AGGREGATE
CREATE AGGREGATE my_sum(integer) (
    SFUNC = int4pl,
    STYPE = integer,
    INITCOND = '0'
);

-- Create CAST
CREATE CAST (TEXT AS INTEGER)
    WITH INOUT
    AS IMPLICIT;

-- Create COLLATION
CREATE COLLATION my_collation (provider = 'icu', locale='');

-- COMMENT
COMMENT ON AGGREGATE my_sum(integer) IS 'Custom aggregate function to calculate sum of integers';

-- Create CONVERSION  --- Not Supported on YB yet
-- CREATE CONVERSION my_conversion
-- FOR 'UTF8' TO 'LATIN1'
-- FROM utf8_to_iso8859_1;

-- Create DOMAIN
CREATE DOMAIN positive_int AS INTEGER
    CHECK (VALUE > 0);

-- Create FUNCTION
CREATE FUNCTION add_two_numbers(a INTEGER, b INTEGER)
RETURNS INTEGER AS $$
BEGIN
    RETURN a + b;
END;
$$ LANGUAGE plpgsql;

-- Create OPERATOR
CREATE OPERATOR === (
    FUNCTION = int4eq,
    LEFTARG = INTEGER,
    RIGHTARG = INTEGER
);

-- Create OPERATOR CLASS
CREATE FUNCTION text_length_cmp(text, text) RETURNS INTEGER AS $$
    SELECT CASE
        WHEN length($1) < length($2) THEN -1
        WHEN length($1) > length($2) THEN 1
        ELSE 0
    END;
$$ LANGUAGE SQL;

-- Create the operator class for length-based ordering on text
CREATE OPERATOR CLASS text_length_ops
    FOR TYPE text USING btree AS
    OPERATOR 1 < (text, text),
    OPERATOR 2 <= (text, text),
    OPERATOR 3 = (text, text),
    OPERATOR 4 >= (text, text),
    OPERATOR 5 > (text, text),
    FUNCTION 1 text_length_cmp(text, text);

-- Create OPERATOR FAMILY
CREATE OPERATOR FAMILY int_fam USING btree;

-- Create a sample table for testing CREATE POLICY, CREATE TRIGGER, etc.
CREATE TABLE employees (
    first_name VARCHAR(50),
    last_name VARCHAR(50),
    department VARCHAR(50),
    salary NUMERIC(8, 2),
    hire_date DATE,
    last_modified TIMESTAMP
);

-- Create POLICY
CREATE POLICY my_policy ON employees
FOR SELECT
USING (department = 'HR');

-- Create PROCEDURE
CREATE PROCEDURE increment_salary(first_name VARCHAR(50), increment NUMERIC)
LANGUAGE plpgsql
AS $$
BEGIN
    UPDATE employees SET salary = salary + increment WHERE first_name = 'sandeep';
END;
$$;

-- Create RULE
CREATE RULE replace_insert AS
ON INSERT TO employees
DO INSTEAD
    INSERT INTO employees (first_name, last_name, department, salary, hire_date)
    VALUES ('Default', 'User', 'Default Dept', 50000, NOW());

-- Create SCHEMA
CREATE SCHEMA company_schema;

-- Create STATISTICS
CREATE STATISTICS names_distinct(ndistinct)
ON first_name, last_name
FROM employees;

-- Create TRANSFORM --- Not Supported on YB yet
-- CREATE TRANSFORM FOR hstore LANGUAGE plpgsql (
--    FROM SQL WITH FUNCTION hstore_in(plpgsql),
--    TO SQL WITH FUNCTION hstore_out(plpgsql)
-- );

-- Create TRIGGER
CREATE OR REPLACE FUNCTION update_modified_timestamp()
RETURNS TRIGGER AS $$
BEGIN
    NEW.last_modified = NOW();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER update_timestamp
BEFORE UPDATE ON employees
FOR EACH ROW
EXECUTE FUNCTION update_modified_timestamp();

-- Create VIEW
CREATE VIEW employee_salaries AS
SELECT first_name, last_name, salary FROM employees;

-------------- Foreign Objects---------------------------

-- Create Foreign Data Wrapper
CREATE FOREIGN DATA WRAPPER postgres_fdw;

-- Create Foreign Server (The server does not need to exist unless we want to query it)
CREATE SERVER foreign_server
FOREIGN DATA WRAPPER postgres_fdw
OPTIONS (host 'dummy_server', port '1111', dbname 'dummy_db');

-- Create Foreign table
CREATE FOREIGN TABLE foreign_table_name (
    id INT,
    name TEXT,
    created_at TIMESTAMP
)
SERVER foreign_server
OPTIONS (table_name 'dummy_table_name');

-- Create User Mapping for current user
CREATE USER MAPPING FOR current_user
SERVER foreign_server
OPTIONS (user 'dummy_remote_user', password 'dummy_remote_password');

--------------Text Search DDLs --------------------------

CREATE TEXT SEARCH TEMPLATE simple_template (
    init = dsimple_init,
    lexize = dsimple_lexize
);

CREATE TEXT SEARCH PARSER simple_parser (
    start = prsd_start,
    gettoken = prsd_nexttoken,
    end = prsd_end,
    lextypes = prsd_lextype
);

CREATE TEXT SEARCH DICTIONARY simple_dict (
    template = simple
);

CREATE TEXT SEARCH CONFIGURATION simple_config (parser = default);
