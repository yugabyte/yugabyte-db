-- Rollback of CREATE TABLE
BEGIN;
SAVEPOINT a;
CREATE TABLE test_table (a INT);
ROLLBACK TO a;
SELECT * FROM test_table;
ROLLBACK;
SELECT * FROM test_table;

-- Rollback with ALTER TABLE (ADD and DROP COLUMN).
BEGIN;
CREATE TABLE test_table (a INT, b INT);
SAVEPOINT a;
ALTER TABLE test_table ADD COLUMN c TEXT;
SAVEPOINT b;
ALTER TABLE test_table ADD COLUMN d TEXT;
ALTER TABLE test_table DROP COLUMN b;
ROLLBACK TO b;
\d test_table
ROLLBACK TO a;
\d test_table
ROLLBACK;
\d test_table

-- Drop Table DDL + DML Rollback
BEGIN;
CREATE TABLE test_table (a INT, b INT);
INSERT INTO test_table VALUES (1, 2);
SAVEPOINT a;
INSERT INTO test_table (a, b) VALUES (3, 4);
DROP TABLE test_table;
ROLLBACK TO a;
\d test_table
ROLLBACK;

-- Rollback of CREATE INDEX
BEGIN;
CREATE TABLE test_table (a INT, b INT);
SAVEPOINT a;
CREATE INDEX ON test_table(a);
ROLLBACK TO a;
\di
ROLLBACK;

-- Rollback of DROP + CREATE TABLE with the same name works.
BEGIN;
CREATE TABLE test_table (a INT, b INT);
SAVEPOINT svpt;
DROP TABLE test_table;
CREATE TABLE test_table (a INT, c INT, d INT);
ROLLBACK TO SAVEPOINT svpt;
SELECT * FROM test_table;
ROLLBACK;

-- Rollback of TRUNCATE
BEGIN;
CREATE TABLE test_table (a INT, b INT);
INSERT INTO test_table VALUES (1, 1);
INSERT INTO test_table VALUES (2, 2);
INSERT INTO test_table VALUES (3, 3);
SELECT * FROM test_table ORDER BY a;
SAVEPOINT svpt;
TRUNCATE test_table;
SELECT * FROM test_table ORDER BY a;
ROLLBACK TO SAVEPOINT svpt;
SELECT * FROM test_table ORDER BY a;
ROLLBACK;
