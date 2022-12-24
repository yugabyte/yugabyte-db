
CREATE DATABASE colocated_db WITH COLOCATION = true;
\c colocated_db

-- Colocated Tables and Indexes
CREATE TABLE tbl (k INT PRIMARY KEY, v INT);
CREATE TABLE tbl2 (k INT PRIMARY KEY, v INT, v2 TEXT);
CREATE UNIQUE INDEX ON tbl (v DESC);
CREATE INDEX ON tbl2 (v2);

-- Table and Index opt out colocation
CREATE TABLE tbl3 (k INT PRIMARY KEY, v INT) WITH (COLOCATION = false);
CREATE UNIQUE INDEX ON tbl3 (v HASH);

-- Create a table using old 'colocated' syntax,
-- ysql_dump should dump new 'colocaiton' syntax.
CREATE TABLE tbl4 (k INT PRIMARY KEY, v INT, v2 TEXT) WITH (COLOCATED = false);
