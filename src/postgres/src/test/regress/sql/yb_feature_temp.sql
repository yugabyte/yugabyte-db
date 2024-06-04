--
-- TEMP
-- Test temp relations and indexes
--

-- test temp table/index masking

CREATE TABLE temptest(col int);

CREATE INDEX i_temptest ON temptest(col);

CREATE TEMP TABLE temptest(tcol int);

CREATE INDEX i_temptest ON temptest(tcol);

SELECT * FROM temptest;

DROP TABLE temptest;

SELECT * FROM temptest;

DROP TABLE temptest;

-- test different syntaxes for temp table creation

CREATE TEMPORARY TABLE a (z int);

DROP TABLE a;

CREATE LOCAL TEMPORARY TABLE b (z int);

DROP TABLE b;

CREATE LOCAL TEMP TABLE c (z int);

DROP TABLE c;

-- GLOBAL temp table is not supported.

CREATE GLOBAL TEMP TABLE d (z int);

-- test temp table selects

CREATE TABLE temptest(col int);

INSERT INTO temptest VALUES (1);

CREATE TEMP TABLE temptest(tcol float);

INSERT INTO temptest VALUES (2.1);

SELECT * FROM temptest;

-- test temp table truncate

TRUNCATE temptest;

SELECT * FROM temptest;

DROP TABLE temptest;

SELECT * FROM temptest;

DROP TABLE temptest;

-- test temp table CREATE .. AS

CREATE TABLE test (a int);

INSERT INTO test VALUES (1);

CREATE TEMP TABLE temp AS SELECT * FROM test;

SELECT * FROM test;

-- test ON COMMIT DELETE ROWS

CREATE TEMP TABLE temptest(col int) ON COMMIT DELETE ROWS;

BEGIN;
INSERT INTO temptest VALUES (1);
INSERT INTO temptest VALUES (2);

SELECT * FROM temptest;
COMMIT;

SELECT * FROM temptest;

DROP TABLE temptest;

BEGIN;
CREATE TEMP TABLE temptest(col) ON COMMIT DELETE ROWS AS SELECT 1;

SELECT * FROM temptest;
COMMIT;

SELECT * FROM temptest;

DROP TABLE temptest;

-- test ON COMMIT DROP
-- TODO(dmitry) ON COMMIT DROP should be fixed in context of #7926

-- BEGIN;

-- CREATE TEMP TABLE temptest(col int) ON COMMIT DROP;

-- INSERT INTO temptest VALUES (1);
-- INSERT INTO temptest VALUES (2);

-- SELECT * FROM temptest;
-- COMMIT;

-- SELECT * FROM temptest;

-- BEGIN;
-- CREATE TEMP TABLE temptest(col) ON COMMIT DROP AS SELECT 1;

-- SELECT * FROM temptest;
-- COMMIT;

-- SELECT * FROM temptest;

-- ON COMMIT is only allowed for TEMP

CREATE TABLE temptest(col int) ON COMMIT DELETE ROWS;
CREATE TABLE temptest(col) ON COMMIT DELETE ROWS AS SELECT 1;

-- test temp table updation

CREATE TEMP TABLE test (a int);

INSERT INTO test VALUES (1);

INSERT INTO test VALUES (2);

INSERT INTO test VALUES (1);

UPDATE test SET a=3 WHERE a=1;

SELECT * FROM test;

-- test temp table row deletion

DELETE FROM test;

SELECT * FROM test;

-- test ALTER TABLE on a temp table

ALTER TABLE test ADD COLUMN d int;

ALTER TABLE test ADD CONSTRAINT uniq UNIQUE(d);

INSERT INTO test (d) VALUES (1);

INSERT INTO test (d) VALUES (1); -- should fail

SELECT * FROM test;

ALTER TABLE test DROP COLUMN d;

DELETE FROM test;

ALTER TABLE test ADD COLUMN b int;

SELECT * FROM test;

ALTER TABLE test RENAME COLUMN b to c;

SELECT * FROM test;

ALTER TABLE test ADD CONSTRAINT checkc CHECK (c >= 0);

INSERT INTO test (c) VALUES (-1); -- should fail

INSERT INTO test (c) VALUES (1);

ALTER TABLE test DROP COLUMN c;

SELECT * FROM test;

-- test COPY on a temp table

COPY test (a) FROM stdin;
1
\.

SELECT * FROM test;

COPY test (a) TO stdout;

-- test transactions with temp tables

BEGIN;

INSERT INTO x VALUES (1); -- should fail

INSERT INTO test VALUES (2);

END;

SELECT * FROM test;

BEGIN;

INSERT INTO test VALUES (2);

INSERT INTO x VALUES (1); -- should fail

END;

SELECT * FROM test;

BEGIN;

INSERT INTO test VALUES (2);

ROLLBACK;

SELECT * FROM test;

BEGIN;

INSERT INTO test VALUES (2);

COMMIT;

SELECT * FROM test;

CREATE TABLE x (y int check (y >= 0));

BEGIN;

INSERT INTO test VALUES (2);

INSERT INTO x VALUES (1);

COMMIT;

SELECT * FROM x;

SELECT * FROM test;

DELETE FROM x;

DELETE FROM test;

BEGIN;

INSERT INTO test VALUES (1);

INSERT INTO x VALUES (1);

INSERT INTO x VALUES (-1); -- constraint violation, should fail

COMMIT;

SELECT * FROM x;

SELECT * FROM test;

DROP TABLE test;

DROP TABLE x;

-- test temp table deletion

CREATE TEMP TABLE temptest (col int);

\c

SELECT * FROM temptest;

-- Sleep a second as a workaround due to bug github #1469.

SELECT pg_sleep(10);

-- test temp table deletion

CREATE TEMP TABLE temptest (col int);

CREATE VIEW tempview AS SELECT * FROM temptest;

\c

SELECT * FROM tempview;

SELECT * FROM temptest;

-- Sleep a second as a workaround due to bug github #1469.

SELECT pg_sleep(10);

-- test temp table with indexes
CREATE TEMP TABLE temptest (k int PRIMARY KEY, v1 int, v2 int);
CREATE UNIQUE INDEX ON temptest (v1);
CREATE INDEX ON temptest USING hash (v2);

-- \d temptest has unstable output as the temporary schemaname contains
-- the tserver uuid. Use regexp_replace to change it to pg_temp_x so that the
-- result is stable.
select current_setting('data_directory') || 'describe.out' as desc_output_file
\gset
\o :desc_output_file
\d temptest
\o
select regexp_replace(pg_read_file(:'desc_output_file'), 'pg_temp_.{32}_\d+', 'pg_temp_x', 'g');

INSERT INTO temptest VALUES (1, 2, 3), (4, 5, 6);
INSERT INTO temptest VALUES (2, 2, 3);

SELECT * FROM temptest WHERE k IN (1, 4) ORDER BY k;

UPDATE temptest SET v1 = 0 WHERE k = 1;

SELECT * FROM temptest ORDER BY k;

DELETE FROM temptest WHERE k = 4;

SELECT * FROM temptest WHERE k IN (1, 4) ORDER BY k;

-- test temp table primary key duplicate error.
INSERT INTO temptest VALUES (100, 200, 300);
INSERT INTO temptest VALUES (100, 200, 300);
INSERT INTO temptest VALUES (101, 201, 301), (101, 201, 301);
DROP TABLE temptest;

-- test temp table with primary index scan.
CREATE TEMP TABLE temptest (a INT PRIMARY KEY);
EXPLAIN (COSTS OFF) SELECT a FROM temptest WHERE a = 1;

-- test temp table being used to update YB table.
CREATE TABLE test1 (x int, y int, z int);
INSERT INTO test1 VALUES (1, 2, 3);
CREATE TEMP TABLE test2 as table test1;
UPDATE test1 SET z = 2 FROM test2 WHERE test1.x = test2.x;
SELECT * FROM test1;

-- test temp table ORDER BY and WHERE clause after failed insertion into unique indexed column
CREATE TEMP TABLE IF NOT EXISTS t1(c0 TEXT  DEFAULT '3.61.4.60' PRIMARY KEY NOT NULL, c1 DECIMAL);;
INSERT INTO t1(c1) VALUES(0.835), (0.703);
SELECT t1.c0 FROM t1 ORDER BY t1.c0;
SELECT t1.c0 FROM t1 where c0 in ('3.61.4.60');

-- test temp table UPDATE on unique indexed column
CREATE temp TABLE t4(c0 DECIMAL NULL, UNIQUE(c0));
INSERT INTO t4(c0) VALUES(0.03);
UPDATE t4 SET c0 = (0.05) WHERE t4.c0 = 0.03;
SELECT ALL t4.c0 FROM t4 ORDER BY t4.c0 ASC;

CREATE TEMP TABLE tempt (k int PRIMARY KEY, v1 int, v2 int);
CREATE UNIQUE INDEX ON tempt (v1);
INSERT INTO tempt VALUES (1, 2, 3), (4, 5, 6);
INSERT INTO tempt VALUES (2, 2, 3);
SELECT * FROM tempt ORDER BY k;

-- types in temp schema
set search_path = pg_temp, public;
create domain pg_temp.nonempty as text check (value <> '');
-- function-syntax invocation of types matches rules for functions
select nonempty('');
select pg_temp.nonempty('');
-- other syntax matches rules for tables
select ''::nonempty;

reset search_path;
