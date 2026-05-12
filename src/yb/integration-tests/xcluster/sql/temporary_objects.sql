--
-- Creating temporary objects then dropping them.
--
-- (The dropping has to be done in this file because all temporary
-- objects are automatically dropped at/before the end of a session.)
--

-- Basic tables
CREATE TEMP TABLE temp_table (id INT, value TEXT);
DROP TABLE temp_table;

-- Views
CREATE TEMP TABLE temp_table_for_view (id INT, data TEXT);
CREATE TEMP VIEW temp_view AS SELECT * FROM temp_table_for_view;
DROP VIEW temp_view;
DROP TABLE temp_table_for_view;

-- Standalone sequences
CREATE TEMP SEQUENCE temp_seq START 1;
DROP SEQUENCE temp_seq;

-- Create a temp table with an temporary index
CREATE TEMP TABLE temp_table3 (id INT, data TEXT);
CREATE INDEX temp_index ON temp_table3 (data);
DROP INDEX temp_index;
DROP TABLE temp_table3;


-- Sequences via SERIAL columns, defaults
CREATE TEMP SEQUENCE myseq2;
CREATE TEMP SEQUENCE myseq3;
CREATE TEMP TABLE t1 (
  f1 serial,
  f2 int DEFAULT nextval('myseq2'),
  f3 int DEFAULT nextval('myseq3'::text)
);
DROP SEQUENCE myseq3;
DROP TABLE t1;
DROP SEQUENCE myseq2;


-- Create a temporary table with a column default
CREATE TEMP TABLE temp_table_for_default (
    id INT PRIMARY KEY,
    value TEXT DEFAULT 'default_value'
);
-- Drop the default value from the column
ALTER TABLE temp_table_for_default ALTER COLUMN value DROP DEFAULT;

-- Create a temporary table with multiple columns
CREATE TEMP TABLE temp_table_for_column_drop (
    id INT PRIMARY KEY,
    name TEXT,
    age INT
);
-- Drop the 'age' column
ALTER TABLE temp_table_for_column_drop DROP COLUMN age;

-- Create a temporary table with a primary key
CREATE TEMP TABLE temp_table_for_pk (
    id INT PRIMARY KEY,
    name TEXT
);
-- Drop the primary key constraint
ALTER TABLE temp_table_for_pk DROP CONSTRAINT temp_table_for_pk_pkey;

-- Create two related temporary tables
CREATE TEMP TABLE temp_parent_table (
    id INT PRIMARY KEY
);
CREATE TEMP TABLE temp_table_for_fk (
    id INT PRIMARY KEY,
    parent_id INT,
    CONSTRAINT temp_fk_parent FOREIGN KEY (parent_id) REFERENCES temp_parent_table(id)
);
-- Drop the foreign key constraint
ALTER TABLE temp_table_for_fk DROP CONSTRAINT temp_fk_parent;

-- Create a temporary table with a unique constraint
CREATE TEMP TABLE temp_table_for_unique (
    id INT PRIMARY KEY,
    email TEXT UNIQUE
);
-- Drop the unique constraint
ALTER TABLE temp_table_for_unique DROP CONSTRAINT temp_table_for_unique_email_key;

-- Create a temporary table with a CHECK constraint
CREATE TEMP TABLE temp_table_for_check (
    id INT PRIMARY KEY,
    age INT CHECK (age >= 18)
);
-- Drop the check constraint
ALTER TABLE temp_table_for_check DROP CONSTRAINT temp_table_for_check_age_check;

-- Create a temporary table and enable RLS
CREATE TEMP TABLE temp_table_for_rls (
    id INT PRIMARY KEY,
    sensitive_data TEXT
);
ALTER TABLE temp_table_for_rls ENABLE ROW LEVEL SECURITY;
-- Create a policy
CREATE POLICY temp_my_policy ON temp_table_for_rls USING (id > 1);
-- Drop the policy
DROP POLICY temp_my_policy ON temp_table_for_rls;


--
-- Testing a DROP with all the cases
--

CREATE TEMP TABLE temp_complex_table (
    id INT PRIMARY KEY,          -- Primary Key
    parent_id INT,               -- Foreign Key (added later)
    value TEXT DEFAULT 'default', -- Default Value
    email TEXT UNIQUE,           -- Unique Constraint
    age INT CHECK (age >= 18)    -- Check Constraint
);

-- Add a foreign key constraint
ALTER TABLE temp_complex_table 
    ADD CONSTRAINT fk_parent FOREIGN KEY (parent_id) 
    REFERENCES temp_complex_table(id);

-- Create an index on the "value" column
CREATE INDEX temp_index_on_value ON temp_complex_table (value);

-- Enable Row-Level Security
ALTER TABLE temp_complex_table ENABLE ROW LEVEL SECURITY;

-- Add a Row-Level Security Policy
CREATE POLICY temp_policy ON temp_complex_table 
    FOR SELECT USING (id > 0);

-- Attach a trigger to the table; function here is a temporary object as well.
CREATE FUNCTION pg_temp.temp_trigger_function() RETURNS trigger AS $$
BEGIN
    NEW.value := UPPER(NEW.value);
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;
CREATE TRIGGER temp_trigger
    BEFORE INSERT ON temp_complex_table
    FOR EACH ROW EXECUTE FUNCTION pg_temp.temp_trigger_function();

-- Create a sequence and attach it to a column
CREATE TEMP SEQUENCE temp_sequence START 1;
ALTER TABLE temp_complex_table ALTER COLUMN id SET DEFAULT nextval('temp_sequence');

-- Finally, DROP everything using CASCADE
DROP TABLE temp_complex_table CASCADE;
DROP FUNCTION pg_temp.temp_trigger_function;


-- Temporary rule case:
CREATE TEMP TABLE temp_table_for_rule (
    id INT PRIMARY KEY
);
-- Create a rule
CREATE RULE _test_temp_rule AS
    ON UPDATE TO temp_table_for_rule
    DO INSTEAD NOTHING;
-- Drop the rule
DROP RULE _test_temp_rule ON temp_table_for_rule;
