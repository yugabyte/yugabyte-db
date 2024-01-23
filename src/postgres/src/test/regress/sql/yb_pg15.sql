--
-- Tests for pg15 branch stability.
--
-- Basics
create table t1 (id int, name text);

create table t2 (id int primary key, name text);

explain (COSTS OFF) insert into t2 values (1);
insert into t2 values (1);

explain (COSTS OFF) insert into t2 values (2), (3);
insert into t2 values (2), (3);

explain (COSTS OFF) select * from t2 where id = 1;
select * from t2 where id = 1;

explain (COSTS OFF) select * from t2 where id > 1;
select * from t2 where id > 1;

explain (COSTS OFF) update t2 set name = 'John' where id = 1;
update t2 set name = 'John' where id = 1;

explain (COSTS OFF) update t2 set name = 'John' where id > 1;
update t2 set name = 'John' where id > 1;

explain (COSTS OFF) update t2 set id = id + 4 where id = 1;
update t2 set id = id + 4 where id = 1;

explain (COSTS OFF) update t2 set id = id + 4 where id > 1;
update t2 set id = id + 4 where id > 1;

explain (COSTS OFF) delete from t2 where id = 1;
delete from t2 where id = 1;

explain (COSTS OFF) delete from t2 where id > 1;
delete from t2 where id > 1;

-- Before update trigger test.

alter table t2 add column count int;

insert into t2 values (1, 'John', 0);

CREATE OR REPLACE FUNCTION update_count() RETURNS trigger LANGUAGE plpgsql AS
$func$
BEGIN
   NEW.count := NEW.count+1;
   RETURN NEW;
END
$func$;

CREATE TRIGGER update_count_trig BEFORE UPDATE ON t2 FOR ROW EXECUTE PROCEDURE update_count();

update t2 set name = 'Jane' where id = 1;

select * from t2;

-- CREATE INDEX
CREATE INDEX myidx on t2(name);

-- Insert with on conflict
insert into t2 values (1, 'foo') on conflict ON CONSTRAINT t2_pkey do update set id = t2.id+1;

select * from t2;

-- Joins (YB_TODO: if I move it below pushdown test, the test fails)

CREATE TABLE p1 (a int, b int, c varchar, primary key(a,b));
INSERT INTO p1 SELECT i, i % 25, to_char(i, 'FM0000') FROM generate_series(0, 599) i WHERE i % 2 = 0;

CREATE TABLE p2 (a int, b int, c varchar, primary key(a,b));
INSERT INTO p2 SELECT i, i % 25, to_char(i, 'FM0000') FROM generate_series(0, 599) i WHERE i % 3 = 0;

-- Merge join
EXPLAIN (COSTS OFF) SELECT * FROM p1 t1 JOIN p2 t2 ON t1.a = t2.a WHERE t1.a <= 100 AND t2.a <= 100;
SELECT * FROM p1 t1 JOIN p2 t2 ON t1.a = t2.a WHERE t1.a <= 100 AND t2.a <= 100;

-- Hash join
SET enable_mergejoin = off;
EXPLAIN (COSTS OFF) SELECT * FROM p1 t1 JOIN p2 t2 ON t1.a = t2.a WHERE t1.a <= 100 AND t2.a <= 100;
SELECT * FROM p1 t1 JOIN p2 t2 ON t1.a = t2.a WHERE t1.a <= 100 AND t2.a <= 100;

-- Batched nested loop join
ANALYZE p1;
ANALYZE p2;
SET enable_hashjoin = off;
SET enable_seqscan = off;
SET enable_material = off;
SET yb_bnl_batch_size = 3;

EXPLAIN (COSTS OFF) SELECT * FROM p1 t1 JOIN p2 t2 ON t1.a = t2.a WHERE t1.a <= 100 AND t2.a <= 100;
SELECT * FROM p1 t1 JOIN p2 t2 ON t1.a = t2.a WHERE t1.a <= 100 AND t2.a <= 100;

SET enable_mergejoin = on;
SET enable_hashjoin = on;
SET enable_seqscan = on;
SET enable_material = on;
-- Update pushdown test.

CREATE TABLE single_row_decimal (k int PRIMARY KEY, v1 decimal, v2 decimal(10,2), v3 int);
CREATE FUNCTION next_v3(int) returns int language sql as $$
  SELECT v3 + 1 FROM single_row_decimal WHERE k = $1;
$$;

INSERT INTO single_row_decimal(k, v1, v2, v3) values (1,1.5,1.5,1), (2,2.5,2.5,2), (3,null, null,null);
SELECT * FROM single_row_decimal ORDER BY k;
UPDATE single_row_decimal SET v1 = v1 + 1.555, v2 = v2 + 1.555, v3 = v3 + 1 WHERE k = 1;
-- v2 should be rounded to 2 decimals.
SELECT * FROM single_row_decimal ORDER BY k;

UPDATE single_row_decimal SET v1 = v1 + 1.555, v2 = v2 + 1.555, v3 = 3 WHERE k = 1;
SELECT * FROM single_row_decimal ORDER BY k;
UPDATE single_row_decimal SET v1 = v1 + 1.555, v2 = v2 + 1.555, v3 = next_v3(1) WHERE k = 1;
SELECT * FROM single_row_decimal ORDER BY k;

-- Delete with returning
insert into t2 values (4), (5), (6);
delete from t2 where id > 2 returning id, name;

-- COPY FROM
CREATE TABLE myemp (id int primary key, name text);
COPY myemp FROM stdin;
1	a
2	b
\.
SELECT * from myemp;

CREATE TABLE myemp2(id int primary key, name text) PARTITION BY range(id);
CREATE TABLE myemp2_1_100 PARTITION OF myemp2 FOR VALUES FROM (1) TO (100);
CREATE TABLE myemp2_101_200 PARTITION OF myemp2 FOR VALUES FROM (101) TO (200);
COPY myemp2 FROM stdin;
1	a
102	b
\.
SELECT * from myemp2_1_100;
SELECT * from myemp2_101_200;
-- Adding PK
create table test (id int);
insert into test values (1);
ALTER TABLE test ENABLE ROW LEVEL SECURITY;
CREATE POLICY test_policy ON test FOR SELECT USING (true);
alter table test add primary key (id);

create table test2 (id int);
insert into test2 values (1), (1);
alter table test2 add primary key (id);

-- Creating partitioned table
create table emp_par1(id int primary key, name text) partition by range(id);
CREATE TABLE emp_par1_1_100 PARTITION OF emp_par1 FOR VALUES FROM (1) TO (100);
create table emp_par2(id int primary key, name text) partition by list(id);
create table emp_par3(id int primary key, name text) partition by hash(id);

-- Adding FK
create table emp(id int unique);
create table address(emp_id int, addr text);
insert into address values (1, 'a');
ALTER TABLE address ADD FOREIGN KEY(emp_id) REFERENCES emp(id);
insert into emp values (1);
ALTER TABLE address ADD FOREIGN KEY(emp_id) REFERENCES emp(id);

-- Adding PK with pre-existing FK constraint
alter table emp add primary key (id);
alter table address add primary key (emp_id);

-- Add primary key with with pre-existing FK where confdelsetcols non nul
create table emp2 (id int, name text, primary key (id, name));
create table address2 (id int, name text, addr text,  FOREIGN KEY (id, name) REFERENCES emp2 ON DELETE SET NULL (name));
insert into emp2 values (1, 'a'), (2, 'b');
insert into address2 values (1, 'a', 'a'), (2, 'b', 'b');
delete from emp2 where id = 1;
select * from address2 order by id;
alter table address2 add primary key (id);
delete from emp2 where id = 2;
select * from address2 order by id;

-- create database
CREATE DATABASE mytest;

-- drop database
DROP DATABASE mytest;

create table fastpath (a int, b text, c numeric);
insert into fastpath select y.x, 'b' || (y.x/10)::text, 100 from (select generate_series(1,10000) as x) y;
select md5(string_agg(a::text, b order by a, b asc)) from fastpath
	where a >= 1000 and a < 2000 and b > 'b1' and b < 'b3';

-- Index scan test row comparison expressions
CREATE TABLE pk_range_int_asc (r1 INT, r2 INT, r3 INT, v INT, PRIMARY KEY(r1 asc, r2 asc, r3 asc));
INSERT INTO pk_range_int_asc SELECT i/25, (i/5) % 5, i % 5, i FROM generate_series(1, 125) AS i;
EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM pk_range_int_asc WHERE (r1, r2, r3) <= (2,3,2);
SELECT * FROM pk_range_int_asc WHERE (r1, r2, r3) <= (2,3,2);

-- SERIAL type
CREATE TABLE serial_test (k int, v SERIAL);
INSERT INTO serial_test VALUES (1), (1), (1);
SELECT * FROM serial_test ORDER BY v;
SELECT last_value, is_called FROM public.serial_test_v_seq;

-- lateral join
CREATE TABLE tlateral1 (a int, b int, c varchar);
INSERT INTO tlateral1 SELECT i, i % 25, to_char(i % 4, 'FM0000') FROM generate_series(0, 599, 2) i;
CREATE TABLE tlateral2 (a int, b int, c varchar);
INSERT INTO tlateral2 SELECT i % 25, i, to_char(i % 4, 'FM0000') FROM generate_series(0, 599, 3) i;
ANALYZE tlateral1, tlateral2;
-- YB_TODO: pg15 used merge join, whereas hash join is expected.
-- EXPLAIN (COSTS FALSE) SELECT * FROM tlateral1 t1 LEFT JOIN LATERAL (SELECT t2.a AS t2a, t2.c AS t2c, t2.b AS t2b, t3.b AS t3b, least(t1.a,t2.a,t3.b) FROM tlateral1 t2 JOIN tlateral2 t3 ON (t2.a = t3.b AND t2.c = t3.c)) ss ON t1.a = ss.t2a WHERE t1.b = 0 ORDER BY t1.a;
SELECT * FROM tlateral1 t1 LEFT JOIN LATERAL (SELECT t2.a AS t2a, t2.c AS t2c, t2.b AS t2b, t3.b AS t3b, least(t1.a,t2.a,t3.b) FROM tlateral1 t2 JOIN tlateral2 t3 ON (t2.a = t3.b AND t2.c = t3.c)) ss ON t1.a = ss.t2a WHERE t1.b = 0 ORDER BY t1.a;

-- Test FailedAssertion("BufferIsValid(bsrcslot->buffer) failure from ExecCopySlot in ExecMergeJoin.
CREATE TABLE mytest1(h int, r int, v1 int, v2 int, v3 int, primary key(h HASH, r ASC));
INSERT INTO mytest1 VALUES (1,2,4,9,2), (2,3,2,4,6);

CREATE TABLE mytest2(h int, r int, v1 int, v2 int, v3 int, primary key(h ASC, r ASC));
INSERT INTO mytest2 VALUES (1,2,4,5,7), (1,3,8,6,1), (4,3,7,3,2);

SET enable_hashjoin = off;
SET enable_nestloop = off;
explain SELECT * FROM mytest1 t1 JOIN mytest2 t2 on t1.h = t2.h WHERE t2.r = 2;
SELECT * FROM mytest1 t1 JOIN mytest2 t2 on t1.h = t2.h WHERE t2.r = 2;
SET enable_hashjoin = on;
SET enable_nestloop = on;
-- Insert with on conflict on temp table
create temporary table mytmp (id int primary key, name text, count int);
insert into mytmp values (1, 'foo', 0);
insert into mytmp values (1, 'foo') on conflict ON CONSTRAINT mytmp_pkey do update set id = mytmp.id+1;
select * from mytmp;

CREATE OR REPLACE FUNCTION update_count() RETURNS trigger LANGUAGE plpgsql AS
$func$
BEGIN
   NEW.count := NEW.count+1;
   RETURN NEW;
END
$func$;

CREATE TRIGGER update_count_trig BEFORE UPDATE ON mytmp FOR ROW EXECUTE PROCEDURE update_count();
insert into mytmp values (2, 'foo') on conflict ON CONSTRAINT mytmp_pkey do update set id = mytmp.id+1;
select * from mytmp;

create view myview as  select * from mytmp;
insert into myview values (3, 'foo') on conflict (id) do update set id = myview.id + 1;
select * from myview;

-- YB batched nested loop join
CREATE TABLE p3 (a int, b int, c varchar, primary key(a,b));
INSERT INTO p3 SELECT i, i % 25, to_char(i, 'FM0000') FROM generate_series(0, 599) i WHERE i % 5 = 0;
ANALYZE p3;

CREATE INDEX p1_b_idx ON p1 (b ASC);
SET enable_hashjoin = off;
SET enable_mergejoin = off;
SET enable_seqscan = off;
SET enable_material = off;
SET yb_bnl_batch_size = 3;

SELECT * FROM p1 JOIN p2 ON p1.a = p2.b AND p2.a = p1.b;

SELECT * FROM p3 t3 RIGHT OUTER JOIN (SELECT t1.a as a FROM p1 t1 JOIN p2 t2 ON t1.a = t2.b WHERE t1.b <= 10 AND t2.b <= 15) s ON t3.a = s.a;

CREATE TABLE m1 (a money, primary key(a asc));
INSERT INTO m1 SELECT i*2 FROM generate_series(1, 2000) i;

CREATE TABLE m2 (a money, primary key(a asc));
INSERT INTO m2 SELECT i*5 FROM generate_series(1, 2000) i;
SELECT * FROM m1 t1 JOIN m2 t2 ON t1.a = t2.a WHERE t1.a <= 50::money;
-- Index on tmp table
create temp table prtx2 (a integer, b integer, c integer);
insert into prtx2 select 1 + i%10, i, i from generate_series(1,5000) i, generate_series(1,10) j;
create index on prtx2 (c);

-- testing yb_hash_code pushdown on a secondary index with a text hash column
CREATE TABLE text_table (hr text, ti text, tj text, i int, j int, primary key (hr));
INSERT INTO text_table SELECT i::TEXT, i::TEXT, i::TEXT, i, i FROM generate_series(1,10000) i;
CREATE INDEX textidx ON text_table (tj);
SELECT tj FROM text_table WHERE yb_hash_code(tj) <= 63;

-- Row locking
CREATE TABLE t(h INT, r INT, PRIMARY KEY(h, r));
INSERT INTO t VALUES(1, 1), (1, 3);
SELECT * FROM t WHERE h = 1 AND r in(1, 3) FOR KEY SHARE;
DROP TABLE t;

-- Test for ItemPointerIsValid assertion failure
CREATE TYPE rainbow AS ENUM ('red', 'orange', 'yellow', 'green', 'blue', 'purple');
-- Aggregate pushdown
SELECT COUNT(*) FROM pg_enum WHERE enumtypid = 'rainbow'::regtype;
-- IndexOnlyScan
SELECT enumlabel FROM pg_enum WHERE enumtypid = 'rainbow'::regtype;

-- Cleanup
DROP TABLE IF EXISTS address, address2, emp, emp2, emp_par1, emp_par1_1_100, emp_par2, emp_par3,
  fastpath, myemp, myemp2, myemp2_101_200, myemp2_1_100, p1, p2, pk_range_int_asc,
  single_row_decimal, t1, t2, test, test2, serial_test, tlateral1, tlateral2, mytest1, mytest2 CASCADE;

-- insert into temp table in function body
create temp table compos (f1 int, f2 text);
create function fcompos1(v compos) returns void as $$
insert into compos values (v.*);
$$ language sql;
select fcompos1(row(1,'one'));

-- very basic REINDEX
CREATE TABLE yb (i int PRIMARY KEY, j int);
CREATE INDEX NONCONCURRENTLY ON yb (j);
UPDATE pg_index SET indisvalid = false
    WHERE indexrelid = 'yb_j_idx'::regclass;
\c
REINDEX INDEX yb_j_idx;
UPDATE pg_index SET indisvalid = false
    WHERE indexrelid = 'yb_j_idx'::regclass;
\c
\set VERBOSITY terse
REINDEX(verbose) INDEX yb_j_idx;
\set VERBOSITY default

-- internal collation
create table texttab (t text);
insert into texttab values ('a');
select count(*) from texttab group by t;

-- ALTER TABLE ADD COLUMN DEFAULT with pre-existing rows
CREATE TABLE mytable (pk INT NOT NULL PRIMARY KEY);
INSERT INTO mytable SELECT * FROM generate_series(1, 10) a;
ALTER TABLE mytable ADD COLUMN c_bigint BIGINT NOT NULL DEFAULT -1;
SELECT c_bigint FROM mytable WHERE c_bigint = -1 LIMIT 1;
DROP TABLE mytable;

-- Test ON CONFLICT DO UPDATE with partitioned table and non-identical children

CREATE TABLE upsert_test (
    a   INT PRIMARY KEY,
    b   TEXT
) PARTITION BY LIST (a);

CREATE TABLE upsert_test_1 PARTITION OF upsert_test FOR VALUES IN (1);
CREATE TABLE upsert_test_2 (b TEXT, a INT PRIMARY KEY);
ALTER TABLE upsert_test ATTACH PARTITION upsert_test_2 FOR VALUES IN (2);

INSERT INTO upsert_test VALUES(1, 'Boo'), (2, 'Zoo');
-- uncorrelated sub-select:
WITH aaa AS (SELECT 1 AS a, 'Foo' AS b) INSERT INTO upsert_test
  VALUES (1, 'Bar') ON CONFLICT(a)
  DO UPDATE SET (b, a) = (SELECT b, a FROM aaa) RETURNING *;
-- correlated sub-select:
WITH aaa AS (SELECT 1 AS ctea, ' Foo' AS cteb) INSERT INTO upsert_test
  VALUES (1, 'Bar'), (2, 'Baz') ON CONFLICT(a)
  DO UPDATE SET (b, a) = (SELECT upsert_test.b||cteb, upsert_test.a FROM aaa) RETURNING *;

DROP TABLE upsert_test;

-- Update partitioned table with multiple partitions
CREATE TABLE t(id int) PARTITION BY range(id);
CREATE TABLE t_1_100 PARTITION OF t FOR VALUES FROM (1) TO (100);
CREATE TABLE t_101_200 PARTITION OF t FOR VALUES FROM (101) TO (200);
INSERT INTO t VALUES (1);
UPDATE t SET id = 2;
SELECT * FROM t;
DROP TABLE t;

-- Update partitioned table with multiple partitions and secondary index
CREATE TABLE t3(id int primary key, name int, add int, unique(id, name)) PARTITION BY range(id);
CREATE TABLE t3_1_100 partition of t3 FOR VALUES FROM (1) TO (100);
CREATE TABLE t3_101_200 partition of t3 FOR VALUES FROM (101) TO (200);
INSERT INTO t3 VALUES (1, 1, 1);
UPDATE t3 SET ADD = 2;
SELECT * from t3;
DROP TABLE t3;

-- Test no segmentation fault in YbSeqscan with row marks
CREATE TABLE main_table (a int) partition by range(a);
CREATE TABLE main_table_1_100 partition of main_table FOR VALUES FROM (1) TO (100);
INSERT INTO main_table VALUES (1);
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
SELECT * FROM main_table;
SELECT * FROM main_table FOR KEY SHARE;
COMMIT;
