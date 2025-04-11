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
