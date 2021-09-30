-- Tests for yb_obj_properties_functions: Verify new YB functions to get table
-- or database properties.
--
-- Test NON-COLOCATED database.
--
-- Test yb_is_database_colocated.
SELECT yb_is_database_colocated();

-- Test yb_table_properties.
CREATE TABLE temp_tbl (h1 INT, h2 INT, PRIMARY KEY((h1, h2))) SPLIT INTO 7 TABLETS;

SELECT yb_table_properties('temp_tbl'::regclass);

-- Cleanup.
DROP TABLE temp_tbl;

--
-- Test COLOCATED database.
--
CREATE DATABASE colocated_db colocated = true;

\c colocated_db
--
-- Test yb_is_database_colocated.
SELECT yb_is_database_colocated();
--
-- Test yb_table_properties.
CREATE TABLE col_temp_tbl (h INT PRIMARY KEY) WITH (colocated = true) SPLIT INTO 5 TABLETS;

SELECT yb_table_properties('col_temp_tbl'::regclass);

-- Cleanup.
DROP TABLE col_temp_tbl;
