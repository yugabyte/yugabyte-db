CREATE TABLE test1 (id int PRIMARY KEY);

-- Test rollback of DDL+DML transaction block.
BEGIN ISOLATION LEVEL REPEATABLE READ;
CREATE TABLE test2 (id int);
INSERT INTO test1 VALUES (2);
ROLLBACK;

-- No rows in test1.
SELECT * FROM test1;
-- test2 does not exist.
SELECT * FROM test2;

-- Test commit of DDL+DML transaction block.
BEGIN ISOLATION LEVEL REPEATABLE READ;
CREATE TABLE test2 (id int);
INSERT INTO test1 VALUES (2);
COMMIT;

SELECT * FROM test1;
SELECT * FROM test2;

-- Test rollback of a block with multiple DDLs
BEGIN ISOLATION LEVEL REPEATABLE READ;
CREATE TABLE test3 (id int);
CREATE TABLE test4 (id int PRIMARY KEY, b int);
ALTER TABLE test1 ADD COLUMN value text;
INSERT INTO test1 VALUES (3, 'text');
ROLLBACK;

SELECT * FROM test3;
SELECT * FROM test4;
SELECT * FROM test1;

-- Test commit of a block with multiple DDLs
BEGIN ISOLATION LEVEL REPEATABLE READ;
CREATE TABLE test3 (id int);
CREATE TABLE test4 (id int);
ALTER TABLE test1 ADD COLUMN value text;
INSERT INTO test1 VALUES (3, 'text');
COMMIT;

SELECT * FROM test3;
SELECT * FROM test4;
SELECT * FROM test1;

-- Same test as above but the first statement is a DML
BEGIN ISOLATION LEVEL REPEATABLE READ;
INSERT INTO test1 VALUES (5, 'text');
CREATE TABLE test5 (id int);
CREATE TABLE test6 (id int);
ALTER TABLE test1 ADD COLUMN value1 text;
INSERT INTO test1 VALUES (4, 'text', 'text2');
ROLLBACK;

SELECT * FROM test5;
SELECT * FROM test6;
SELECT * FROM test1;

BEGIN ISOLATION LEVEL REPEATABLE READ;
INSERT INTO test1 VALUES (5, 'text');
CREATE TABLE test5 (id int);
CREATE TABLE test6 (id int);
ALTER TABLE test1 ADD COLUMN value1 text;
INSERT INTO test1 VALUES (4, 'text', 'text2');
COMMIT;

SELECT * FROM test5;
SELECT * FROM test6;
SELECT * FROM test1;

CREATE INDEX ON test1(value);
SELECT value FROM test1 WHERE value='text';

-- Test that schema version bump in case of alter table rollback is handled.
CREATE TABLE test7 (a int primary key, b int);
BEGIN ISOLATION LEVEL REPEATABLE READ;
INSERT INTO test7 VALUES (1, 1);
INSERT INTO test7 VALUES (2, 2);
ALTER TABLE test7 ADD COLUMN c int;
INSERT INTO test7 VALUES (3, 3, 3);
ROLLBACK;
BEGIN ISOLATION LEVEL REPEATABLE READ;
INSERT INTO test7 VALUES (1, 1);
COMMIT;
SELECT * FROM test7;

SET allow_system_table_mods = on;
BEGIN ISOLATION LEVEL REPEATABLE READ;
-- Truncate system table inside a transaction block.
TRUNCATE pg_extension;
ROLLBACK;
RESET allow_system_table_mods;

SET yb_enable_alter_table_rewrite = off;
BEGIN ISOLATION LEVEL REPEATABLE READ;
-- Truncate user table inside a transaction block with table rewrite disabled.
TRUNCATE test7;
ROLLBACK;
RESET yb_enable_alter_table_rewrite;

-- Rollback CREATE, DROP and CREATE TABLE with same name in a transaction block.
BEGIN ISOLATION LEVEL REPEATABLE READ;
CREATE TABLE test8 (a int primary key, b int);
INSERT INTO test8 VALUES (1, 1);
SELECT * FROM test8;
DROP TABLE test8;
CREATE TABLE test8 (c int primary key, d int);
INSERT INTO test8 VALUES (10, 10);
ROLLBACK;
SELECT * FROM test8;

-- Same test as above but with COMMIT.
BEGIN ISOLATION LEVEL REPEATABLE READ;
CREATE TABLE test8 (a int primary key, b int);
INSERT INTO test8 VALUES (1, 1);
SELECT * FROM test8;
DROP TABLE test8;
CREATE TABLE test8 (c int primary key, d int);
INSERT INTO test8 VALUES (10, 10);
COMMIT;
SELECT * FROM test8;

-- Rollback of DROP TABLE.
CREATE TABLE test9 (a int primary key, b int);
INSERT INTO test9 VALUES (1, 1);
BEGIN ISOLATION LEVEL REPEATABLE READ;
INSERT INTO test9 VALUES (2, 2);
SELECT * FROM test9;
DROP TABLE test9;
ROLLBACK;
SELECT * FROM test9;

-- Rollback of CREATE INDEX should work.
CREATE TABLE test10(id INT PRIMARY KEY, val TEXT);
BEGIN ISOLATION LEVEL REPEATABLE READ;
CREATE INDEX test10_idx ON test10(val);
\d+ test10;
ROLLBACK;
\d+ test10;

-- TODO(#3109): CREATE and DROP database are already being tested in various
-- other regress tests. This is being tested here since
-- FLAGS_TEST_yb_ddl_transaction_block_enabled is false for all of them.
-- Remove this once FLAGS_TEST_yb_ddl_transaction_block_enabled is true by
-- default.
create database k1;
drop database k1;

CREATE SEQUENCE regtest_seq;
BEGIN ISOLATION LEVEL REPEATABLE READ;
DROP SEQUENCE regtest_seq;
COMMIT;

CREATE TABLE test11(id INT PRIMARY KEY, val TEXT);
INSERT INTO test11 VALUES (1, 'text');
BEGIN ISOLATION LEVEL REPEATABLE READ;
TRUNCATE test11;
TRUNCATE test11;
SELECT * FROM test11;
ROLLBACK;
SELECT * FROM test11;

-- Savepoint allowed without any DDL.
CREATE TABLE test12 (a int primary key, b int);
BEGIN ISOLATION LEVEL REPEATABLE READ;
INSERT INTO test12 VALUES (1, 1);
SAVEPOINT test12_sp;
INSERT INTO test12 VALUES (2, 2);
SELECT * FROM test12;
ROLLBACK TO SAVEPOINT test12_sp;
COMMIT;
SELECT * FROM test12;

-- DDL after Savepoint disallowed.
BEGIN ISOLATION LEVEL REPEATABLE READ;
INSERT INTO test12 VALUES (3, 3);
SAVEPOINT test12_sp;
CREATE TABLE test13 (a int primary key, b int);
ROLLBACK;

-- Savepoint after DDL disallowed.
BEGIN ISOLATION LEVEL REPEATABLE READ;
CREATE TABLE test13 (a int primary key, b int);
SAVEPOINT test13_sp;
ROLLBACK;

BEGIN ISOLATION LEVEL REPEATABLE READ;
CREATE TEMPORARY TABLE temp_table (
    a INT PRIMARY KEY
) ON COMMIT DELETE ROWS;
INSERT INTO temp_table VALUES (1);
INSERT INTO temp_table VALUES (2);
SELECT * FROM temp_table;
COMMIT;
SELECT * FROM temp_table;

BEGIN ISOLATION LEVEL REPEATABLE READ;
CREATE TEMP TABLE temp_table_commit_drop (
    id INT PRIMARY KEY
)
ON COMMIT DROP;
INSERT INTO temp_table_commit_drop VALUES (1);
SELECT * FROM temp_table_commit_drop;
COMMIT;
SELECT * FROM temp_table_commit_drop;
ANALYZE test1, test2, test3;

CREATE TABLE sales_data (
    sale_id INT,
    sale_date DATE,
    amount DECIMAL(10, 2)
) PARTITION BY RANGE (sale_date);
CREATE TABLE sales_data_202401 PARTITION OF sales_data
    FOR VALUES FROM ('2024-01-01') TO ('2024-02-01');
INSERT INTO sales_data (sale_id, sale_date, amount) VALUES
    (1, '2024-01-10', 100.50),
    (2, '2024-01-25', 75.20);
CREATE TABLE sales_data_202402 PARTITION OF sales_data
    FOR VALUES FROM ('2024-02-01') TO ('2024-03-01');
INSERT INTO sales_data (sale_id, sale_date, amount) VALUES
    (3, '2024-02-05', 120.00);
ALTER TABLE sales_data DETACH PARTITION sales_data_202401 CONCURRENTLY;
