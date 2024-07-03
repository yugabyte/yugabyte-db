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

-- Test whether single row optimization is invoked when
-- only one partition is being updated.
CREATE TABLE list_parted (a int, b int, c int, primary key(a,b)) PARTITION BY list (a);
CREATE TABLE sub_parted PARTITION OF list_parted for VALUES in (1) PARTITION BY list (b);
CREATE TABLE sub_part1 PARTITION OF sub_parted for VALUES in (1);
INSERT INTO list_parted VALUES (1, 1, 1);
EXPLAIN (COSTS OFF) UPDATE list_parted SET c = 2 WHERE a = 1 and b = 1;
UPDATE list_parted SET c = 2 WHERE a = 1 and b = 1;
SELECT * FROM list_parted;

EXPLAIN (COSTS OFF) DELETE FROM list_parted WHERE a = 1 and b = 1;
DELETE FROM list_parted WHERE a = 1 and b = 1;
SELECT * FROM list_parted;

DROP TABLE list_parted;
-- Cross partition UPDATE with nested loop join (multiple matches)
CREATE TABLE list_parted (a int, b int) PARTITION BY list (a);
CREATE TABLE sub_part1 PARTITION OF list_parted for VALUES in (1);
CREATE TABLE sub_part2 PARTITION OF list_parted for VALUES in (2);
INSERT into list_parted VALUES (1, 2);

CREATE TABLE non_parted (id int);
INSERT into non_parted VALUES (1), (1), (1);
UPDATE list_parted t1 set a = 2 FROM non_parted t2 WHERE t1.a = t2.id and a = 1;
SELECT * FROM list_parted;
DROP TABLE list_parted;
DROP TABLE non_parted;
-- Test no segmentation fault in YbSeqscan with row marks
CREATE TABLE main_table (a int) partition by range(a);
CREATE TABLE main_table_1_100 partition of main_table FOR VALUES FROM (1) TO (100);
INSERT INTO main_table VALUES (1);
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
SELECT * FROM main_table;
SELECT * FROM main_table FOR KEY SHARE;
COMMIT;

-- YB_TODO: begin: remove this after tracking yb_pg_partition_prune
-- Test partition pruning
create table rlp (a int, b varchar) partition by range (a);
create table rlp_default partition of rlp default partition by list (a);
create table rlp_default_default partition of rlp_default default;
create table rlp_default_10 partition of rlp_default for values in (10);
create table rlp_default_30 partition of rlp_default for values in (30);
create table rlp_default_null partition of rlp_default for values in (null);
create table rlp1 partition of rlp for values from (minvalue) to (1);
create table rlp2 partition of rlp for values from (1) to (10);

create table rlp3 (b varchar, a int) partition by list (b varchar_ops);
create table rlp3_default partition of rlp3 default;
create table rlp3abcd partition of rlp3 for values in ('ab', 'cd');
create table rlp3efgh partition of rlp3 for values in ('ef', 'gh');
create table rlp3nullxy partition of rlp3 for values in (null, 'xy');
alter table rlp attach partition rlp3 for values from (15) to (20);

create table rlp4 partition of rlp for values from (20) to (30) partition by range (a);
create table rlp4_default partition of rlp4 default;
create table rlp4_1 partition of rlp4 for values from (20) to (25);
create table rlp4_2 partition of rlp4 for values from (25) to (29);

create table rlp5 partition of rlp for values from (31) to (maxvalue) partition by range (a);
create table rlp5_default partition of rlp5 default;
create table rlp5_1 partition of rlp5 for values from (31) to (40);

explain (costs off) select * from rlp where a = 1 or b = 'ab';
-- YB_TODO: end

-- suppress warning that depends on wal_level
SET client_min_messages = 'ERROR';
CREATE PUBLICATION p;
RESET client_min_messages;
ALTER PUBLICATION p ADD TABLES IN SCHEMA public;
ALTER PUBLICATION p DROP TABLES IN SCHEMA CURRENT_SCHEMA;
ALTER PUBLICATION p SET CURRENT_SCHEMA;

-- YB_TODO: begin: remove after tracking yb_join_batching
SET enable_hashjoin = off;
SET enable_mergejoin = off;
SET enable_seqscan = off;
SET enable_material = off;
SET yb_prefer_bnl = on;
SET yb_bnl_batch_size = 3;
CREATE TABLE t10 (r1 int, r2 int, r3 int, r4 int);

INSERT INTO t10
  SELECT DISTINCT
    i1, i2+5, i3, i4
  FROM generate_series(1, 5) i1,
       generate_series(1, 5) i2,
       generate_series(1, 5) i3,
       generate_series(1, 10) i4;

CREATE index i_t ON t10 (r1 ASC, r2 ASC, r3 ASC, r4 ASC);

CREATE TABLE t11 (c1 int, c3 int, x int);
INSERT INTO t11 VALUES (1,2,0), (1,3,0), (5,2,0), (5,3,0), (5,4,0);

CREATE TABLE t12 (c4 int, c2 int, y int);
INSERT INTO t12 VALUES (3,7,0),(6,9,0),(9,7,0),(4,9,0);

EXPLAIN (COSTS OFF) /*+ Leading((t12 (t11 t10))) Set(enable_seqscan true) */ SELECT t10.* FROM t12, t11, t10 WHERE x = y AND c1 = r1 AND c2 = r2 AND c3 = r3 AND c4 = r4 order by c1, c2, c3, c4;

/*+ Leading((t12 (t11 t10))) Set(enable_seqscan true) */ SELECT t10.* FROM t12, t11, t10 WHERE x = y AND c1 = r1 AND c2 = r2 AND c3 = r3 AND c4 = r4 order by c1, c2, c3, c4;

DROP TABLE t10;
DROP TABLE t11;
DROP TABLE t12;
RESET enable_hashjoin;
RESET enable_mergejoin;
RESET enable_seqscan;
RESET enable_material;
RESET yb_prefer_bnl;
RESET yb_bnl_batch_size;
-- YB_TODO: end

-- YB_TODO: begin: remove after tracking yb_pg_foreign_key
-- (the following test is a simplified version of the test under
--  'Foreign keys and partitioned tables' section there).
CREATE TABLE fk_notpartitioned_pk (a int, b int,PRIMARY KEY (a, b));
INSERT INTO fk_notpartitioned_pk VALUES (2501, 2503);

CREATE TABLE fk_partitioned_fk (a int, b int) PARTITION BY HASH (a);
ALTER TABLE fk_partitioned_fk ADD FOREIGN KEY (a, b) REFERENCES fk_notpartitioned_pk;

CREATE TABLE fk_partitioned_fk_0 PARTITION OF fk_partitioned_fk FOR VALUES WITH (MODULUS 5, REMAINDER 0);
CREATE TABLE fk_partitioned_fk_1 PARTITION OF fk_partitioned_fk FOR VALUES WITH (MODULUS 5, REMAINDER 1);
INSERT INTO fk_partitioned_fk (a,b) VALUES (2501, 2503);

-- this update fails because there is no referenced row
UPDATE fk_partitioned_fk SET a = a + 1 WHERE a = 2501;
-- but we can fix it thusly:
INSERT INTO fk_notpartitioned_pk (a,b) VALUES (2502, 2503);
UPDATE fk_partitioned_fk SET a = a + 1 WHERE a = 2501;
-- YB_TODO: end

-- YB_TODO: begin: remove after tracking postgres_fdw
CREATE EXTENSION postgres_fdw;
CREATE SERVER testserver1 FOREIGN DATA WRAPPER postgres_fdw;
DO $d$
    BEGIN
        EXECUTE $$CREATE SERVER loopback FOREIGN DATA WRAPPER postgres_fdw
            OPTIONS (dbname '$$||current_database()||$$',
                     host '$$||current_setting('listen_addresses')||$$',
                     port '$$||current_setting('port')||$$'
            )$$;
    END;
$d$;

CREATE USER MAPPING FOR public SERVER testserver1
    OPTIONS (user 'value', password 'value');
CREATE USER MAPPING FOR CURRENT_USER SERVER loopback;

create table ctrtest (a int, b text) partition by list (a);
create table loct1 (a int check (a in (1)), b text);
create foreign table remp1 (a int check (a in (1)), b text) server loopback options (table_name 'loct1');
alter table ctrtest attach partition remp1 for values in (1);

copy ctrtest from stdin;
1	foo
\.
select * from ctrtest;

create table return_test (a int, b int, c int);
insert into return_test values (1, 1, 1);
update return_test set c = c returning *;
update return_test set c = c returning c;
update return_test set c = c returning a,b;
-- YB_TODO: end

-- YB_TODO: begin: remove after generated is tracked
CREATE TABLE sales (
    id SERIAL PRIMARY KEY,
    product_id INT NOT NULL,
    quantity INT NOT NULL,
    price_per_unit NUMERIC(10, 2) NOT NULL,
    total_price NUMERIC GENERATED ALWAYS AS (quantity * price_per_unit) STORED
);
INSERT INTO sales (product_id, quantity, price_per_unit) VALUES (1, 10, 100), (2, 10, 200);
SELECT * FROM SALES;
UPDATE sales set quantity = quantity + 1;
SELECT * FROM SALES;
UPDATE sales set price_per_unit = price_per_unit + 100;
SELECT * FROM SALES;
UPDATE sales SET total_price = 0;
UPDATE sales SET product_id = product_id + 10;
SELECT * FROM SALES;

CREATE OR REPLACE FUNCTION update_count() RETURNS trigger LANGUAGE plpgsql AS
$func$
BEGIN
   NEW.count := NEW.count+1;
   RETURN NEW;
END
$func$;
CREATE TABLE test(a int, b int, count int, double_count int GENERATED ALWAYS AS (2 * count) STORED);
CREATE TRIGGER update_count_test_trig BEFORE UPDATE OF a ON test FOR ROW EXECUTE PROCEDURE update_count();
INSERT INTO test(a, b, count) values (1, 1, 1), (2, 2, 1);
SELECT * FROM test ORDER BY a DESC;
UPDATE test set a = a + 5;
SELECT * FROM test ORDER BY a DESC;
UPDATE test set b = b + 5;
SELECT * FROM test ORDER BY a DESC;

-- YB_TODO: end
-- YB_TODO: begin: remove once subselect is tracked
create temp table outer_text (f1 text, f2 text);
insert into outer_text values ('a', 'a');
insert into outer_text values ('b', 'a');
insert into outer_text values ('a', null);
insert into outer_text values ('b', null);

create temp table inner_text (c1 text, c2 text);
insert into inner_text values ('a', null);
insert into inner_text values ('123', '456');

select * from outer_text where (f1, f2) not in (select * from inner_text) order by f2;

drop table outer_text, inner_text;

create table outer_text (f1 text, f2 text);
insert into outer_text values ('a', 'a');
insert into outer_text values ('b', 'a');
insert into outer_text values ('a', null);
insert into outer_text values ('b', null);

create table inner_text (c1 text, c2 text);
insert into inner_text values ('a', null);
insert into inner_text values ('123', '456');

select * from outer_text where (f1, f2) not in (select * from inner_text) order by f2;
-- YB_TODO: begin: remove after tracking yb_tablespaces
CREATE TABLESPACE y WITH (replica_placement='{"num_replicas":3, "placement_blocks":[{"cloud":"cloud1","region":"r1","zone":"z1","min_num_replicas":1},{"cloud":"cloud2","region":"r2", "zone":"z2", "min_num_replicas":1}]}');
-- YB_TODO: end
