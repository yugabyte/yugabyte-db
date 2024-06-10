-- Tests for yb_obj_properties_functions: Verify new YB functions to get table
-- or database properties.
--
-- Test NON-COLOCATED database.
--
-- Test yb_is_database_colocated.
SELECT yb_is_database_colocated();

-- Test yb_table_properties.
CREATE TABLE temp_tbl (h1 INT, h2 INT, PRIMARY KEY((h1, h2))) SPLIT INTO 7 TABLETS;

SELECT * FROM yb_table_properties('temp_tbl'::regclass);

-- Cleanup.
DROP TABLE temp_tbl;

--
-- Test COLOCATED database.
--
CREATE DATABASE test_yb_obj_props_clc COLOCATION = true;

\c test_yb_obj_props_clc
--
-- Test yb_is_database_colocated.
SELECT yb_is_database_colocated();
--
-- Test yb_table_properties.
CREATE TABLE clc_temp_tbl (h INT PRIMARY KEY) WITH (colocation=true);
CREATE TABLE clc_temp_tbl_2 (h INT PRIMARY KEY) WITH (colocation=true, colocation_id=100500);

SELECT
    c.relname,
    props.num_tablets,
    props.num_hash_key_columns,
    props.is_colocated,
    grp.grpname,
    props.colocation_id
FROM pg_class c, yb_table_properties(c.oid) props
JOIN pg_yb_tablegroup grp ON grp.oid = props.tablegroup_oid
WHERE c.relname LIKE 'clc_%' AND c.relkind <> 'i' ORDER BY c.oid;

-- Cleanup.
DROP TABLE clc_temp_tbl;
\c yugabyte
DROP DATABASE test_yb_obj_props_clc;
