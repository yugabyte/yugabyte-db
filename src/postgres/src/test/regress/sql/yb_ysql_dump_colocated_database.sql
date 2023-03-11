
CREATE DATABASE colocated_db WITH COLOCATION = true;
\c colocated_db

-- Colocated Tables and Indexes
CREATE TABLE tbl (k INT PRIMARY KEY, v INT);
CREATE TABLE tbl2 (k INT PRIMARY KEY, v INT, v2 TEXT);
CREATE UNIQUE INDEX ON tbl (v DESC);
CREATE INDEX ON tbl2 (v2);

-- Colocated Partial Indexes
CREATE UNIQUE INDEX partial_unique_idx ON tbl (v DESC) WITH (colocation_id = 40000) WHERE v >= 100 AND v <= 200;
CREATE INDEX partial_idx ON tbl2 (k ASC, v DESC) WITH (colocation_id = 40001) WHERE k > 10 AND k < 20 AND v > 200;

-- Table and Index opt out colocation
CREATE TABLE tbl3 (k INT PRIMARY KEY, v INT) WITH (COLOCATION = false);
CREATE UNIQUE INDEX ON tbl3 (v HASH);

-- Create a table using old 'colocated' syntax,
-- ysql_dump should dump new 'colocaiton' syntax.
CREATE TABLE tbl4 (k INT PRIMARY KEY, v INT, v2 TEXT) WITH (COLOCATED = false);

-- Partitioned table and its partitions
CREATE TABLE htest (
    k1 INT,
    k2 TEXT,
    k3 INT,
    v1 INT,
    v2 TEXT
)
PARTITION BY HASH (k1)
WITH (colocation_id='123456');

CREATE TABLE htest_1 PARTITION OF htest
FOR VALUES WITH (modulus 2, remainder 0)
WITH (colocation_id='234567');
