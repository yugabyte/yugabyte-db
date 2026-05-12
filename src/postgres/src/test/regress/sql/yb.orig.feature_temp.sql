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

-- Test ON COMMIT DROP

BEGIN;

CREATE TEMP TABLE temptest(col int) ON COMMIT DROP;

INSERT INTO temptest VALUES (1);
INSERT INTO temptest VALUES (2);

SELECT * FROM temptest;
COMMIT;

SELECT * FROM temptest;

BEGIN;
CREATE TEMP TABLE temptest(col) ON COMMIT DROP AS SELECT 1;

SELECT * FROM temptest;
COMMIT;

SELECT * FROM temptest;

-- Test it with a CHECK condition that produces a toasted pg_constraint entry
BEGIN;
do $$
begin
  execute format($cmd$
    CREATE TEMP TABLE temptest (col text CHECK (col < %L)) ON COMMIT DROP
  $cmd$,
    (SELECT string_agg(g.i::text || ':' || random()::text, '|')
     FROM generate_series(1, 100) g(i)));
end$$;

SELECT * FROM temptest;
COMMIT;

SELECT * FROM temptest;

-- ON COMMIT is only allowed for TEMP

CREATE TABLE temptest(col int) ON COMMIT DELETE ROWS;
CREATE TABLE temptest(col) ON COMMIT DELETE ROWS AS SELECT 1;

-- test on commit drop on a temp table with an index and pk
BEGIN;

CREATE TEMP TABLE temptest(col int primary key) ON COMMIT DROP;
CREATE INDEX NONCONCURRENTLY temptest_idx ON temptest(col);

COMMIT;
SELECT * FROM temptest;

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

-- table rewrite on an empty temp table
create temp table p(f1 int, f2 int);
alter table p alter column f2 type bigint;
select * from p;
-- tests for row locking on temp tables
CREATE TEMP TABLE locktest(f1 int, f2 text);
INSERT INTO locktest VALUES (1, 'one'), (2, 'two'), (3, 'three');
SELECT * FROM locktest ORDER BY f1 FOR UPDATE;
SELECT * FROM locktest WHERE f1 = 2 FOR UPDATE;
SELECT * FROM locktest WHERE f1 IN (SELECT f1 FROM locktest WHERE f1 < 3) FOR UPDATE;
SELECT a.f1, a.f2 FROM locktest a JOIN locktest b ON a.f1 = b.f1 WHERE a.f1 = 1 FOR UPDATE;
BEGIN;
SELECT * FROM locktest WHERE f1 = 3 FOR UPDATE;
ROLLBACK;
CREATE TEMP TABLE locktest_join(id int, note text);
INSERT INTO locktest_join VALUES (2, 'x'), (3, 'y'), (4, 'z');
SELECT l.f1, l.f2, j.note FROM locktest l JOIN locktest_join j ON l.f1 = j.id FOR UPDATE;
CREATE TABLE regtable(id int, name text);
INSERT INTO regtable VALUES (1, 'a'), (2, 'b'), (3, 'c');
CREATE TEMP TABLE temptable(id int, note text);
INSERT INTO temptable VALUES (1, 'x'), (2, 'y'), (4, 'z');
SELECT r.id, r.name FROM regtable r WHERE r.id IN
    (SELECT id FROM temptable FOR UPDATE) ORDER BY r.id;


-- Original test from insert.sql
-- tuple larger than fillfactor - temp table
--
CREATE TEMP TABLE large_tuple_test (a int, b text) WITH (fillfactor = 10);
ALTER TABLE large_tuple_test ALTER COLUMN b SET STORAGE plain;

-- create page w/ free space in range [nearlyEmptyFreeSpace, MaxHeapTupleSize)
INSERT INTO large_tuple_test (select 1, NULL);

-- should still fit on the page
INSERT INTO large_tuple_test (select 2, repeat('a', 1000));
SELECT pg_size_pretty(pg_relation_size('large_tuple_test'::regclass, 'main'));

-- add small record to the second page
INSERT INTO large_tuple_test (select 3, NULL);

-- now this tuple won't fit on the second page, but the insert should
-- still succeed by extending the relation
INSERT INTO large_tuple_test (select 4, repeat('a', 8126));

DROP TABLE large_tuple_test;

-- ported from strings.sql specifically for temp tables
-- test substr with toasted text values
--
SET bytea_output TO escape;
CREATE TEMP TABLE toasttest(f1 text);

insert into toasttest values(repeat('1234567890',10000));
insert into toasttest values(repeat('1234567890',10000));

--
-- Ensure that some values are uncompressed, to test the faster substring
-- operation used in that case
--
alter table toasttest alter column f1 set storage external;
insert into toasttest values(repeat('1234567890',10000));
insert into toasttest values(repeat('1234567890',10000));

-- If the starting position is zero or less, then return from the start of the string
-- adjusting the length to be consistent with the "negative start" per SQL.
SELECT substr(f1, -1, 5) from toasttest;

-- If the length is less than zero, an ERROR is thrown.
SELECT substr(f1, 5, -1) from toasttest;

-- If no third argument (length) is provided, the length to the end of the
-- string is assumed.
SELECT substr(f1, 99995) from toasttest;

-- If start plus length is > string length, the result is truncated to
-- string length
SELECT substr(f1, 99995, 10) from toasttest;

TRUNCATE TABLE toasttest;
INSERT INTO toasttest values (repeat('1234567890',300));
INSERT INTO toasttest values (repeat('1234567890',300));
INSERT INTO toasttest values (repeat('1234567890',300));
INSERT INTO toasttest values (repeat('1234567890',300));
-- expect >0 blocks
SELECT pg_relation_size(reltoastrelid) = 0 AS is_empty
  FROM pg_class where relname = 'toasttest';

TRUNCATE TABLE toasttest;
ALTER TABLE toasttest set (toast_tuple_target = 4080);
INSERT INTO toasttest values (repeat('1234567890',300));
INSERT INTO toasttest values (repeat('1234567890',300));
INSERT INTO toasttest values (repeat('1234567890',300));
INSERT INTO toasttest values (repeat('1234567890',300));
-- expect 0 blocks
SELECT pg_relation_size(reltoastrelid) = 0 AS is_empty
  FROM pg_class where relname = 'toasttest';

DROP TABLE toasttest;

--
-- test substr with toasted bytea values
--
CREATE TEMP TABLE toasttest(f1 bytea);

insert into toasttest values(decode(repeat('1234567890',10000),'escape'));
insert into toasttest values(decode(repeat('1234567890',10000),'escape'));

--
-- Ensure that some values are uncompressed, to test the faster substring
-- operation used in that case
--
alter table toasttest alter column f1 set storage external;
insert into toasttest values(decode(repeat('1234567890',10000),'escape'));
insert into toasttest values(decode(repeat('1234567890',10000),'escape'));

-- If the starting position is zero or less, then return from the start of the string
-- adjusting the length to be consistent with the "negative start" per SQL.
SELECT substr(f1, -1, 5) from toasttest;

-- If the length is less than zero, an ERROR is thrown.
SELECT substr(f1, 5, -1) from toasttest;

-- If no third argument (length) is provided, the length to the end of the
-- string is assumed.
SELECT substr(f1, 99995) from toasttest;

-- If start plus length is > string length, the result is truncated to
-- string length
SELECT substr(f1, 99995, 10) from toasttest;

DROP TABLE toasttest;

-- test internally compressing datums

-- this tests compressing a datum to a very small size which exercises a
-- corner case in packed-varlena handling: even though small, the compressed
-- datum must be given a 4-byte header because there are no bits to indicate
-- compression in a 1-byte header

CREATE TEMP TABLE toasttest (c char(4096));
INSERT INTO toasttest VALUES('x');
SELECT length(c), c::text FROM toasttest;
SELECT c FROM toasttest;
DROP TABLE toasttest;

RESET bytea_output;
