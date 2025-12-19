--
-- Part 2 of TRUNCATE_TABLE test
-- Performs TRUNCATE
--

-- Simple table
TRUNCATE TABLE tbl1;

-- Partitioned table and table with indexes
TRUNCATE TABLE tbl2, tbl3;

-- Tables with foreign keys and table with sequence
TRUNCATE TABLE tbl4, tbl5 RESTART IDENTITY CASCADE;

-- Inherited table
TRUNCATE TABLE cities, ONLY cities2;
