CREATE TABLE p1 (a int, b int, c varchar, primary key(a,b));
INSERT INTO p1 SELECT i, i % 25, to_char(i, 'FM0000') FROM generate_series(0, 599) i WHERE i % 2 = 0;
CREATE INDEX p1_b_idx ON p1 (b ASC);
ANALYZE p1;

CREATE TABLE p2 (a int, b int, c varchar, primary key(a,b));
INSERT INTO p2 SELECT i, i % 25, to_char(i, 'FM0000') FROM generate_series(0, 599) i WHERE i % 3 = 0;
ANALYZE p2;

CREATE TABLE p3 (a int, b int, c varchar, primary key(a,b));
INSERT INTO p3 SELECT i, i % 25, to_char(i, 'FM0000') FROM generate_series(0, 599) i WHERE i % 5 = 0;
ANALYZE p3;

CREATE TABLE p4 (a int, b int, c varchar, primary key(a,b));
INSERT INTO p4 SELECT i, i % 25, to_char(i, 'FM0000') FROM generate_series(0, 599) i WHERE i % 7 = 0;
ANALYZE p4;

CREATE TABLE p5 (a int, b int, c varchar, primary key(a asc,b asc));
INSERT INTO p5 SELECT i / 10, i % 10, to_char(i, 'FM0000') FROM generate_series(0, 599) i;
CREATE INDEX p5_hash ON p5((a,b) hash);
CREATE INDEX p5_hash_asc ON p5(a hash, b asc);
ANALYZE p5;

-- We're testing nested loop join batching in this file
SET enable_hashjoin = off;
SET enable_mergejoin = off;
SET enable_seqscan = off;
SET enable_material = off;
SET yb_prefer_bnl = on;

SET yb_bnl_batch_size = 3;

EXPLAIN (COSTS OFF) SELECT * FROM p1 t1 JOIN p2 t2 ON t1.a = t2.a WHERE t1.a <= 100 AND t2.a <= 100;
SELECT * FROM p1 t1 JOIN p2 t2 ON t1.a = t2.a WHERE t1.a <= 100 AND t2.a <= 100;

EXPLAIN (COSTS OFF) SELECT * FROM p1 t1 JOIN p2 t2 ON t1.a = t2.a + 1 WHERE t1.a <= 100 AND t2.a <= 100;
SELECT * FROM p1 t1 JOIN p2 t2 ON t1.a = t2.a + 1 WHERE t1.a <= 100 AND t2.a <= 100;

EXPLAIN (COSTS OFF) SELECT * FROM p1 t1 JOIN p2 t2 ON t1.a - 1 = t2.a + 1 WHERE t1.a <= 100 AND t2.a <= 100;
SELECT * FROM p1 t1 JOIN p2 t2 ON t1.a - 1 = t2.a + 1 WHERE t1.a <= 100 AND t2.a <= 100;

-- Batching on compound clauses
/*+ Leading((p2 p1)) */ EXPLAIN (ANALYZE, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT * FROM p1 JOIN p2 ON p1.a = p2.b AND p2.a = p1.b;
/*+ Leading((p2 p1)) */ SELECT * FROM p1 JOIN p2 ON p1.a = p2.b AND p2.a = p1.b;

explain (costs off) select * from p1 left join p5 on p1.a - 1 = p5.a and p1.b - 1 = p5.b where p1.a <= 30;
select * from p1 left join p5 on p1.a - 1 = p5.a and p1.b - 1 = p5.b where p1.a <= 30;

-- Batching should still be disabled if there is a filter
-- clause on a batched relation.
/*+ set(enable_seqscan on) IndexScan(p1 p1_b_idx) Leading((p2 p1)) */ EXPLAIN (COSTS OFF) SELECT * FROM p1 JOIN p2 ON p1.a = p2.b AND p2.a = p1.b;

/*+ set(enable_seqscan on) IndexScan(p1 p1_b_idx) Leading((p2 p3)) */ EXPLAIN (COSTS OFF) SELECT * FROM p1, p2, p3 where p1.a = p3.a AND p2.a = p3.a and p1.b = p2.b;

/*+ set(enable_seqscan on) Leading((p2 p1)) */ EXPLAIN (COSTS OFF) SELECT * FROM p1 JOIN p2 ON p1.a = p2.b AND p1.b < p2.b + 1;

/*+IndexScan(p5 p5_hash)*/explain (costs off) select * from p1 left join p5 on p1.a - 1 = p5.a and p1.b - 1 = p5.b where p1.a <= 30;
/*+IndexScan(p5 p5_hash)*/ select * from p1 left join p5 on p1.a - 1 = p5.a and p1.b - 1 = p5.b where p1.a <= 30;

/*+IndexScan(p5 p5_hash_asc)*/explain (costs off) select * from p1 left join p5 on p1.a - 1 = p5.a and p1.b - 1 = p5.b where p1.a <= 30;
/*+IndexScan(p5 p5_hash_asc)*/ select * from p1 left join p5 on p1.a - 1 = p5.a and p1.b - 1 = p5.b where p1.a <= 30;

/*+ set(enable_seqscan true) Leading((p2 p1)) IndexScan(p1 p1_b_idx) */ EXPLAIN (COSTS OFF) SELECT * FROM p1 JOIN p2 ON p1.a = p2.b AND p2.a = p1.b;

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

CREATE TABLE strtable(a varchar(26), b varchar(23), primary key(a, b));
CREATE TABLE strtable2(a varchar(26), b varchar(23), primary key(a, b));
INSERT INTO strtable VALUES ('123', 'abc'), ('1234', 'abcd'), ('123', 'pqr');
INSERT INTO strtable2 VALUES ('123', 'abc'), ('123', 'abcd'), ('123', 'pqr');

EXPLAIN (COSTS OFF) SELECT * FROM strtable, strtable2 WHERE strtable.a = strtable2.a;
SELECT * FROM strtable, strtable2 WHERE strtable.a = strtable2.a;

EXPLAIN (COSTS OFF) SELECT * FROM strtable, strtable2 WHERE strtable.a = strtable2.a AND strtable.b = strtable2.b;
SELECT * FROM strtable, strtable2 WHERE strtable.a = strtable2.a AND strtable.b = strtable2.b;

DROP TABLE strtable;
DROP TABLE strtable2;

create table q1 (a double precision, b double precision, primary key (a, b));
insert into q1 values (12.34, 99.99), (12.345, 99.99);
create table q2 (a decimal(6, 2), b decimal(6, 2), primary key (a, b));
insert into q2 values (12.34, 99.99), (12.345, 99.99);
explain (costs off) select * from q1, q2 where q1.a = q2.a and q1.b = q2.b;
select * from q1, q2 where q1.a = q2.a and q1.b = q2.b;

create table q3 (a char(6), b char(6), primary key (a, b));
insert into q3 values ('abc', 'def'), ('xyz', 'uvw');
create table q4 (a varchar(6), b varchar(6), primary key (a, b));
insert into q4 values ('abc  ', 'def  '), ('xyz', 'uvw');
explain (costs off) select * from q3, q4 where q3.a = q4.a and q3.b = q4.b;
select * from q3, q4 where q3.a = q4.a and q3.b = q4.b;

explain (costs off) select * from q1, q2 where q1.a::decimal = q2.a and q1.b::decimal = q2.b;
select * from q1, q2 where q1.a::decimal = q2.a and q1.b::decimal = q2.b;
explain (costs off) select * from q1, q2 where q1.a = q2.a::decimal and q1.b = q2.b::decimal;
select * from q1, q2 where q1.a = q2.a::decimal and q1.b = q2.b::decimal;
explain (costs off) select * from q3, q4 where q3.a::text = q4.a and q3.b::text = q4.b;
select * from q3, q4 where q3.a::text = q4.a and q3.b::text = q4.b;
explain (costs off) select * from q3, q4 where q3.a = q4.a::text and q3.b = q4.b::text;
select * from q3, q4 where q3.a = q4.a::text and q3.b = q4.b::text;

explain (costs off) /*+ Leading((q3 q4)) IndexScan(q4) */select * from q3, q4 where q3.a = q4.a and q3.b = q4.b;
/*+ Leading((q3 q4)) IndexScan(q4) */select * from q3, q4 where q3.a = q4.a and q3.b = q4.b;

explain (costs off) /*+ Leading((q3 q4)) IndexScan(q4) */select * from q3, q4 where q3.a::text = q4.a and q3.b::text = q4.b;
/*+ Leading((q3 q4)) IndexScan(q4) */select * from q3, q4 where q3.a::text = q4.a and q3.b::text = q4.b;

drop table q1;
drop table q2;
drop table q3;
drop table q4;

create table d1(a int, primary key(a));
create table d2(a int, primary key(a));
create table d3(a int, primary key(a));
create table d4(a int, primary key(a));

/*+Leading(((d2 (d3 d4)) d1))*/ explain (costs off) select * from d1,d2,d3,d4 where d1.a = d3.a and d2.a = d3.a and d4.a = d2.a;

drop table d1;
drop table d2;
drop table d3;
drop table d4;

create table test (
    id   uuid NOT NULL,
    num  int4 NOT NULL,
    PRIMARY KEY ((id)HASH, num DESC)
);
insert into test(id, num) VALUES
('774cee8f-f0e9-4c46-8f3d-3b5e7db8b839'::uuid, 1);

/*+ Set(yb_bnl_batch_size 3) */ explain (costs off)
select * from test t1
join test t2 on (t1.id = t2.id and t1.num = t2.num)
where t1.id in (
	'774cee8f-f0e9-4c46-8f3d-3b5e7db8b839'::uuid,
	'884cee8f-f0e9-4c46-8f3d-3b5e7db8b847'::uuid);

/*+ Set(yb_bnl_batch_size 3) */
select * from test t1
join test t2 on (t1.id = t2.id and t1.num = t2.num)
where t1.id in (
	'774cee8f-f0e9-4c46-8f3d-3b5e7db8b839'::uuid,
	'884cee8f-f0e9-4c46-8f3d-3b5e7db8b847'::uuid);

drop table test;

EXPLAIN (COSTS OFF) SELECT * FROM p3 t3 LEFT OUTER JOIN (SELECT t1.a as a FROM p1 t1 JOIN p2 t2 ON t1.a = t2.b WHERE t1.a <= 100 AND t2.a <= 100) s ON t3.a = s.a WHERE t3.a <= 30;
SELECT * FROM p3 t3 LEFT OUTER JOIN (SELECT t1.a as a FROM p1 t1 JOIN p2 t2 ON t1.a = t2.b WHERE t1.a <= 100 AND t2.a <= 100) s ON t3.a = s.a WHERE t3.a <= 30;

EXPLAIN (COSTS OFF) SELECT * FROM p3 t3 RIGHT OUTER JOIN (SELECT t1.a as a FROM p1 t1 JOIN p2 t2 ON t1.a = t2.b WHERE t1.b <= 10 AND t2.b <= 15) s ON t3.a = s.a;
SELECT * FROM p3 t3 RIGHT OUTER JOIN (SELECT t1.a as a FROM p1 t1 JOIN p2 t2 ON t1.a = t2.b WHERE t1.b <= 10 AND t2.b <= 15) s ON t3.a = s.a;

-- anti join--
EXPLAIN (COSTS OFF) SELECT * FROM p1 t1 WHERE NOT EXISTS (SELECT 1 FROM p2 t2 WHERE t1.a = t2.a) AND t1.a <= 40;
SELECT * FROM p1 t1 WHERE NOT EXISTS (SELECT 1 FROM p2 t2 WHERE t1.a = t2.a) AND t1.a <= 40;

EXPLAIN (COSTS OFF) SELECT * FROM p1 t1 WHERE NOT EXISTS (SELECT 1 FROM p2 t2 WHERE t1.a = t2.b) AND t1.a <= 40;
SELECT * FROM p1 t1 WHERE NOT EXISTS (SELECT 1 FROM p2 t2 WHERE t1.a = t2.b) AND t1.a <= 40;

-- semi join--
EXPLAIN (COSTS OFF) SELECT * FROM p1 t1 WHERE EXISTS (SELECT 1 FROM p2 t2 WHERE t1.a = t2.a) AND t1.a <= 40;
SELECT * FROM p1 t1 WHERE EXISTS (SELECT 1 FROM p2 t2 WHERE t1.a = t2.a) AND t1.a <= 40;

EXPLAIN (COSTS OFF) SELECT * FROM p1 t1 WHERE EXISTS (SELECT 1 FROM p2 t2 WHERE t1.a = t2.b) AND t1.a <= 40;
SELECT * FROM p1 t1 WHERE EXISTS (SELECT 1 FROM p2 t2 WHERE t1.a = t2.b) AND t1.a <= 40;

CREATE TABLE int2type (a int2, PRIMARY KEY(a ASC));
INSERT INTO int2type VALUES (1), (4), (555), (-33), (6923);

-- testing batching on join conditions across different types (int2, int4)
-- We shouldn't cause any casting on inner expressions.
/*+Leading((i2 p))*/ EXPLAIN (COSTS OFF) SELECT * FROM int2type i2 JOIN p1 p ON i2.a = p.a;
/*+Leading((i2 p))*/ SELECT * FROM int2type i2 JOIN p1 p ON i2.a = p.a;
-- This shouldn't be batched as the LHS of the batched IN expression would
-- have a cast.
/*+Leading((p i2))*/ EXPLAIN (COSTS OFF) SELECT * FROM int2type i2 JOIN p1 p ON i2.a = p.a;
/*+Leading((p i2))*/ SELECT * FROM int2type i2 JOIN p1 p ON i2.a = p.a;
DROP TABLE int2type;

CREATE TABLE q1(a int);
CREATE TABLE q2(a int);
CREATE TABLE q3(a int primary key);

-- We shouldn't be producing dangerous plans that have join clauses
-- that involve more than one rel on a side. Leading hint should not be
-- respected here.
-- See issue #17150
/*+Set(enable_mergejoin false) Set(enable_hashjoin false) Set(enable_material false) Leading((q1 (q2 q3)))*/explain (costs off) select * from q1, q2, q3 where q3.a = q2.a + q1.a;

-- This join is not dangerous as the clause q3.a = q2.a + q1.a
-- will actually receive a cross product of q1 and q2 here.
/*+Set(enable_mergejoin false) Set(enable_hashjoin false) Set(enable_material false) Leading(((q1 q2) q3))*/explain (costs off) select * from q1, q2, q3 where q3.a = q2.a + q1.a;

DROP TABLE q1;
DROP TABLE q2;
DROP TABLE q3;

set yb_bnl_batch_size to 10;
explain (costs off) select * from p1 a join p2 b on a.a = b.a join p3 c on b.a = c.a join p4 d on a.b = d.b where a.b = 10 ORDER BY a.a, b.a, c.a, d.a;
select * from p1 a join p2 b on a.a = b.a join p3 c on b.a = c.a join p4 d on a.b = d.b where a.b = 10 ORDER BY a.a, b.a, c.a, d.a;

DROP TABLE p1;
DROP TABLE p2;
DROP TABLE p3;
DROP TABLE p4;
DROP TABLE p5;

CREATE TABLE s1(r1 int, r2 int, r3 int);
CREATE TABLE s2(r1 int, r2 int, r3 int);
CREATE TABLE s3(r1 int, r2 int);
CREATE INDEX ON s3 (r1 asc, r2 asc);

INSERT INTO s1 select i,i,i from generate_series(1,10) i;
INSERT INTO s2 select i,i,i from generate_series(1,10) i;
INSERT INTO s3 select i,i from generate_series(1,100) i;
/*+Set(enable_nestloop true) Set(enable_seqscan true) Set(yb_bnl_batch_size 3) Leading((s2 (s1 s3))) YbBatchedNL(s1 s3)*/ explain (costs off) select s3.* from s1, s2, s3 where s3.r1 = s1.r1 and s3.r2 = s2.r2 and s1.r3 = s2.r3 order by s3.r1, s3.r2;
/*+Set(enable_nestloop true) Set(enable_seqscan true) Set(yb_bnl_batch_size 3) Leading((s2 (s1 s3))) YbBatchedNL(s1 s3)*/ select s3.* from s1, s2, s3 where s3.r1 = s1.r1 and s3.r2 = s2.r2 and s1.r3 = s2.r3 order by s3.r1, s3.r2;

DROP TABLE s3;
DROP TABLE s2;
DROP TABLE s1;

create table s1(a int, primary key (a asc));
create table s2(a int, primary key (a asc));
create table s3(a int, primary key (a asc));

insert into s1 values (24), (25);
insert into s2 values (24), (25);
insert into s3 values (24), (25);

explain (costs off) /*+set(yb_bnl_batch_size 3) Leading(( ( s1 s2 ) s3 )) MergeJoin(s1 s2)*/select * from s1 left outer join s2
on s1.a = s2.a left outer join s3 on s2.a = s3.a where s1.a > 20;

/*+set(yb_bnl_batch_size 3) Leading(( ( s1 s2 ) s3 )) MergeJoin(s1 s2)*/ select * from s1 left outer join s2
on s1.a = s2.a left outer join s3 on s2.a = s3.a where s1.a > 20;

drop table s1;
drop table s2;
drop table s3;

SET yb_bnl_batch_size = 3;

-- Testing column groups in HybridScanChoices
create table test2 (a int, pp int, b int, pp2 int, c int, primary key(a asc, pp asc, b asc, pp2 asc, c asc));
insert into test2 values (1,0, 2,0,1), (2,0, 3,0,3), (2,0,3,0,5);
create table test1 (a int, pp int, b int, pp2 int, c int, primary key(a asc, pp asc, b asc, pp2 asc, c asc));
insert into test1 values (1,0,2,0,1), (1,0,2,0,2), (2,0,3,0,3), (2,0,4,0,4), (2,0,4,0,5), (2,0,4,0,6);
explain (costs off) select * from test1 p1 join test2 p2 on p1.a = p2.a AND p1.b = p2.b AND p1.c = p2.c;
select * from test1 p1 join test2 p2 on p1.a = p2.a AND p1.b = p2.b AND p1.c = p2.c;
drop table test1;
drop table test2;

-- Test on unhashable join operations. These should use the tuplestore
-- strategy.
CREATE TABLE m1 (a money, primary key(a asc));
INSERT INTO m1 SELECT i*2 FROM generate_series(1, 2000) i;

CREATE TABLE m2 (a money, primary key(a asc));
INSERT INTO m2 SELECT i*5 FROM generate_series(1, 2000) i;

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM m1 t1 JOIN m2 t2 ON t1.a = t2.a WHERE t1.a <= 50::money;
SELECT * FROM m1 t1 JOIN m2 t2 ON t1.a = t2.a WHERE t1.a <= 50::money;

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM m2 t1 LEFT JOIN m1 t2 ON t1.a = t2.a WHERE t1.a <= 50::money;
SELECT * FROM m2 t1 LEFT JOIN m1 t2 ON t1.a = t2.a WHERE t1.a <= 50::money;

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM m2 t1 WHERE NOT EXISTS (SELECT 1 FROM m1 t2 WHERE t1.a = t2.a) AND t1.a <= 50::money;
SELECT * FROM m2 t1 WHERE NOT EXISTS (SELECT 1 FROM m1 t2 WHERE t1.a = t2.a) AND t1.a <= 50::money;

DROP TABLE m1;
DROP TABLE m2;

create table q1 (c1 int, c2 int, primary key (c1 asc, c2 asc));
create table q2 (c1 int, c2 int, primary key (c2 hash, c1 asc));
insert into q1 select i, i / 4 from generate_series(0, 199) i;
insert into q2 select i, i / 2 from generate_series(0, 999) i;

analyze q1;
analyze q2;

SET yb_bnl_batch_size = 3;

-- Make sure a sort node is inserted above a batched NL join when appropriate

explain (costs off) select q1.c1 from q1 join q2 on q1.c2 = q2.c2 order by q1.c1 limit 10;
select q1.c1 from q1 join q2 on q1.c2 = q2.c2 order by q1.c1 limit 10;

explain (costs off) select q1.c1 from q1 join q2 on q1.c2 = q2.c2 order by q1.c1 DESC limit 10;
select q1.c1 from q1 join q2 on q1.c2 = q2.c2 order by q1.c1 DESC limit 10;

CREATE TABLE q1nulls (a int, b int);
CREATE INDEX ON q1nulls (a ASC NULLS FIRST, b DESC NULLS LAST);
INSERT INTO q1nulls SELECT i/10, i % 10 from generate_series(1, 100) i;
INSERT INTO q1nulls VALUES (null, 9), (null, 8), (null, 8);
EXPLAIN (COSTS OFF) SELECT q1nulls.a, q1nulls.b FROM q1nulls, q2 WHERE q1nulls.b = q2.c2 ORDER BY q1nulls.a ASC LIMIT 10;
SELECT q1nulls.a, q1nulls.b FROM q1nulls, q2 WHERE q1nulls.b = q2.c2 ORDER BY q1nulls.a ASC LIMIT 10;

EXPLAIN (COSTS OFF) SELECT q1nulls.a, q1nulls.b FROM q1nulls, q2 WHERE q1nulls.b = q2.c2 ORDER BY q1nulls.a ASC NULLS FIRST LIMIT 10;
SELECT q1nulls.a, q1nulls.b FROM q1nulls, q2 WHERE q1nulls.b = q2.c2 ORDER BY q1nulls.a ASC NULLS FIRST LIMIT 10;

EXPLAIN (COSTS OFF) SELECT q1nulls.a, q1nulls.b FROM q1nulls, q2 WHERE q1nulls.b = q2.c2 ORDER BY q1nulls.a ASC NULLS FIRST, q1nulls.b DESC LIMIT 10;
SELECT q1nulls.a, q1nulls.b FROM q1nulls, q2 WHERE q1nulls.b = q2.c2 ORDER BY q1nulls.a ASC NULLS FIRST, q1nulls.b DESC LIMIT 10;

EXPLAIN (COSTS OFF) SELECT q1nulls.a, q1nulls.b FROM q1nulls, q2 WHERE q1nulls.b = q2.c2 ORDER BY q1nulls.a ASC NULLS FIRST, q1nulls.b DESC NULLS LAST LIMIT 10;
SELECT q1nulls.a, q1nulls.b FROM q1nulls, q2 WHERE q1nulls.b = q2.c2 ORDER BY q1nulls.a ASC NULLS FIRST, q1nulls.b DESC NULLS LAST LIMIT 10;

DROP TABLE q1nulls;

create table q3(a int, b int, c name, primary key(a,b));
create index q3_range on q3(a asc);

/*+Set(enable_hashjoin off) Set(enable_mergejoin off) Set(yb_bnl_batch_size 3) Set(enable_seqscan off) Set(enable_material off)*/ explain (costs off) select * from q1 p1 left join (SELECT p2.c1 as a1, p3.a as a2 from q2 p2 join q3 p3 on true) j1 on j1.a1 = p1.c1;

-- this should not be a batched NL join as it contains an unbatchable clause
-- (j1.a2 <= p1.c1) even though the batchable clause (j1.a1 = p1.c1) is also
-- present

/*+Set(enable_hashjoin off) Set(enable_mergejoin off) Set(yb_bnl_batch_size 3) Set(enable_seqscan off) Set(enable_material off)*/ explain (costs off) select * from q1 p1 left join (SELECT p2.c1 as a1, p3.a as a2 from q2 p2 join q3 p3 on true) j1 on j1.a1 = p1.c1 and j1.a2 <= p1.c1;

/*+Set(enable_hashjoin off) Set(enable_mergejoin off) Set(yb_bnl_batch_size 3) Set(enable_seqscan on) Set(enable_material off) Leading((q3 (q2 q1)))*/ explain (costs off) select * from q1, q2, q3 where q1.c1 = q2.c1 and q3.a = q1.c2;

explain (costs off) SELECT * FROM q1, q2 where pg_backend_pid() >= 0 and q1.c1 = q2.c1;

DROP TABLE q1;
DROP TABLE q2;
DROP TABLE q3;

create table q1(a int, b int);
create table q2(a int, b int);
create index on q2(b, a);
insert into q1 values (1,2), (1,4), (6,7), (2,0), (10,24), (4,2);
insert into q2 values (2,1), (6,6), (2,4), (0,2), (1,2), (2,2), (4,2);

/*+Leading((q1 q2))*/ explain (costs off) select * from q1,q2 where q1.a = q2.a and q1.b = q2.b order by q1.a;
/*+Leading((q1 q2))*/ select * from q1,q2 where q1.a = q2.a and q1.b = q2.b order by q1.a;

drop table q1;
drop table q2;

create table g1(h int, r int, primary key(h hash, r asc));
create table g2(h int, r int, primary key(h hash, r asc));
create table main(h1 int, h2 int, r1 int, r2 int, primary key((h1,h2) hash, r1 asc, r2 asc));
insert into main select i/1000, (i/100) % 10, (i/10) % 10, i % 10 from generate_series(1,9999) i;
insert into g1 values (1,3), (5,7);
insert into g2 values (2,4), (6,8);

/*+Leading((g1 (g2 main))) Set(enable_hashjoin off) Set(enable_mergejoin off) Set(yb_bnl_batch_size 3) Set(enable_material off) Set(enable_seqscan on)*/ explain (costs off) select main.* from g1,g2,main where main.h1 = g1.h and main.h2 = g2.h and main.r2 = g1.r and main.r1 = g2.r;

/*+Leading((g1 (g2 main))) Set(enable_hashjoin off) Set(enable_mergejoin off) Set(yb_bnl_batch_size 3) Set(enable_material off) Set(enable_seqscan on)*/ select main.* from g1,g2,main where main.h1 = g1.h and main.h2 = g2.h and main.r2 = g1.r and main.r1 = g2.r;

drop table g1;
drop table g2;
drop table main;

create table oidtable(a oid, primary key(a asc));
create table int4table(a int4, primary key(a asc));
insert into oidtable select i from generate_series(1,20) i where i % 2 = 0;
insert into int4table select i from generate_series(1,20) i where i % 3 = 0;
/*+Set(enable_hashjoin off) Set(enable_mergejoin off) Set(yb_bnl_batch_size 3) Set(enable_seqscan off) Set(enable_material off) Leading((oidtable int4table))*/explain (costs off) select * from oidtable, int4table where oidtable.
a = int4table.a;
/*+Set(enable_hashjoin off) Set(enable_mergejoin off) Set(yb_bnl_batch_size 3) Set(enable_seqscan off) Set(enable_material off) Leading((oidtable int4table))*/ select * from oidtable, int4table where oidtable.a = int4table.a;
drop table oidtable;
drop table int4table;

SELECT '' AS "xxx", *
  FROM J1_TBL AS tx order by 1, 2, 3, 4;

SELECT '' AS "xxx", *
  FROM J1_TBL tx  order by 1, 2, 3, 4;

SELECT '' AS "xxx", *
  FROM J1_TBL AS t1 (a, b, c)  order by 1, 2, 3, 4;

SELECT '' AS "xxx", *
  FROM J1_TBL t1 (a, b, c)  order by 1, 2, 3, 4;

SELECT '' AS "xxx", *
  FROM J1_TBL t1 (a, b, c), J2_TBL t2 (d, e)  order by 1, 2, 3, 4, 5, 6;

SELECT '' AS "xxx", t1.a, t2.e
  FROM J1_TBL t1 (a, b, c), J2_TBL t2 (d, e)
  WHERE t1.a = t2.d  order by 1, 2, 3;

create table p1(a int, primary key(a asc));
create table p2(a int, primary key(a asc));
create table p3(a int, primary key(a asc));

insert into p1 select generate_series(1, 1000);
insert into p2 select generate_series(1, 1000);
insert into p3 select generate_series(1, 1000);

analyze p1;
analyze p2;
analyze p3;

-- The following hints try to force an illegal BNL
/*+YbBatchedNL(p1 p2 p3) YbBatchedNL(p2 p3) Leading((p1 (p2 p3))) IndexScan(p3)*/explain (costs off) select * from p1, p2, p3 where p1.a + 1 = p2.a and
p3.a = p1.a + p2.a;

/*+YbBatchedNL(p1 p2 p3) NestLoop(p2 p3) Leading((p1 (p2 p3))) IndexScan(p3)*/explain (costs off) select * from p1, p2, p3 where p1.a + 1 = p2.a and
p3.a = p1.a + p2.a;

/*+NestLoop(p1 p2 p3) NestLoop(p2 p3) Leading((p1 (p2 p3))) IndexScan(p3)*/explain (costs off) select * from p1, p2, p3 where p1.a + 1 = p2.a and
p3.a = p1.a + p2.a;

-- This is a legal BNL
/*+YbBatchedNL(p1 p2 p3) Leading(((p1 p2) p3)) IndexScan(p3)*/explain (costs off) select * from p1, p2, p3 where p1.a + 1 = p2.a and
p3.a = p1.a + p2.a;

drop table p1;
drop table p2;
drop table p3;

-- Test the scenarios where parameterized column values from the outer most
-- loop (x1) are used at difference nesting levels (x2 and x3).
CREATE TABLE x1 (a int PRIMARY KEY, b int);
CREATE INDEX i_x1_b ON x1 (b);
INSERT INTO x1 VALUES (1, 0), (2, 1), (3, 0), (4, 1), (5, 2), (6, 3);

CREATE TABLE x2 (a int PRIMARY KEY, b int);
CREATE INDEX i_x2_b ON x2 (b);
INSERT INTO x2 VALUES (1, 0), (2, 1), (3, 0), (4, 1);

CREATE TABLE x3 (a int PRIMARY KEY, b int);
CREATE INDEX i_x3_b ON x3 (b);
INSERT INTO x3 VALUES (1, 0), (2, 1), (5, 2), (6, 3);

ANALYZE x1;
ANALYZE x2;
ANALYZE x3;

-- Before 8ac82f4247 (2.21.0.0-b227), the planner was producing incorrect plans
-- for the following queries, but the results happened to be correct with smaller
-- batch size e.g. 2, 3, etc.
SET yb_bnl_batch_size = 10;

EXPLAIN (COSTS OFF)
SELECT * FROM x1 LEFT JOIN LATERAL (
  SELECT * FROM x2 LEFT JOIN LATERAL (
    SELECT x3.b FROM x3 WHERE x3.a = x1.a AND x3.b = x2.b LIMIT ALL
  ) AS v1 ON true
  WHERE x2.a = x1.a
) v2 ON true
ORDER BY 1, 2, 3, 4, 5;

SELECT * FROM x1 LEFT JOIN LATERAL (
  SELECT * FROM x2 LEFT JOIN LATERAL (
    SELECT x3.b FROM x3 WHERE x3.a = x1.a AND x3.b = x2.b LIMIT ALL
  ) AS v1 ON true
  WHERE x2.a = x1.a
) v2 ON true
ORDER BY 1, 2, 3, 4, 5;


EXPLAIN (COSTS OFF)
SELECT * FROM x1 LEFT JOIN LATERAL (
  SELECT * FROM x2 LEFT JOIN x1 AS x4 ON x2.a = x4.a
    LEFT JOIN LATERAL (
      SELECT x3.b FROM x3 WHERE x3.b = x1.b LIMIT ALL
    ) AS v1 ON true
  WHERE x2.a = x1.a
) v2 ON true
ORDER BY 1, 2, 3, 4, 5, 6, 7;

SELECT * FROM x1 LEFT JOIN LATERAL (
  SELECT * FROM x2 LEFT JOIN x1 AS x4 ON x2.a = x4.a
    LEFT JOIN LATERAL (
      SELECT x3.b FROM x3 WHERE x3.b = x1.b LIMIT ALL
    ) AS v1 ON true
  WHERE x2.a = x1.a
) v2 ON true
ORDER BY 1, 2, 3, 4, 5, 6, 7;


DROP TABLE x1;
DROP TABLE x2;
DROP TABLE x3;

SET yb_bnl_batch_size = 3;

--
--
-- Inner joins (equi-joins)
--
--

--
-- Inner joins (equi-joins) with USING clause
-- The USING syntax changes the shape of the resulting table
-- by including a column in the USING clause only once in the result.
--

-- Inner equi-join on specified column
SELECT '' AS "xxx", *
  FROM J1_TBL INNER JOIN J2_TBL USING (i) order by 1, 2, 3, 4, 5;

-- Same as above, slightly different syntax
SELECT '' AS "xxx", *
  FROM J1_TBL JOIN J2_TBL USING (i) order by 1, 2, 3, 4, 5;

SELECT '' AS "xxx", *
  FROM J1_TBL t1 (a, b, c) JOIN J2_TBL t2 (a, d) USING (a)
  ORDER BY a, d;

SELECT '' AS "xxx", *
  FROM J1_TBL t1 (a, b, c) JOIN J2_TBL t2 (a, b) USING (b)
  ORDER BY b, t1.a;

--
-- NATURAL JOIN
-- Inner equi-join on all columns with the same name
--

SELECT '' AS "xxx", *
  FROM J1_TBL NATURAL JOIN J2_TBL order by 1, 2, 3, 4, 5;

SELECT '' AS "xxx", *
  FROM J1_TBL t1 (a, b, c) NATURAL JOIN J2_TBL t2 (a, d) order by 1, 2, 3, 4, 5;

SELECT '' AS "xxx", *
  FROM J1_TBL t1 (a, b, c) NATURAL JOIN J2_TBL t2 (d, a) order by 1, 2, 3, 4, 5;

-- mismatch number of columns
-- currently, Postgres will fill in with underlying names
SELECT '' AS "xxx", *
  FROM J1_TBL t1 (a, b) NATURAL JOIN J2_TBL t2 (a) order by 1, 2, 3, 4, 5;


--
-- Inner joins (equi-joins)
--

SELECT '' AS "xxx", *
  FROM J1_TBL JOIN J2_TBL ON (J1_TBL.i = J2_TBL.i) order by 1, 2, 3, 4, 5, 6;

SELECT '' AS "xxx", *
  FROM J1_TBL JOIN J2_TBL ON (J1_TBL.i = J2_TBL.k) order by 1, 2, 3, 4, 5, 6;


--
-- Non-equi-joins
--

SELECT '' AS "xxx", *
  FROM J1_TBL JOIN J2_TBL ON (J1_TBL.i <= J2_TBL.k) order by 1, 2, 3, 4, 5, 6;


--
-- Outer joins
-- Note that OUTER is a noise word
--

SELECT '' AS "xxx", *
  FROM J1_TBL LEFT OUTER JOIN J2_TBL USING (i)
  ORDER BY i, k, t;

SELECT '' AS "xxx", *
  FROM J1_TBL LEFT JOIN J2_TBL USING (i)
  ORDER BY i, k, t;

SELECT '' AS "xxx", *
  FROM J1_TBL RIGHT OUTER JOIN J2_TBL USING (i) order by 1, 2, 3, 4, 5;

SELECT '' AS "xxx", *
  FROM J1_TBL RIGHT JOIN J2_TBL USING (i) order by 1, 2, 3, 4, 5;

SELECT '' AS "xxx", *
  FROM J1_TBL FULL OUTER JOIN J2_TBL USING (i)
  ORDER BY i, k, t;

SELECT '' AS "xxx", *
  FROM J1_TBL FULL JOIN J2_TBL USING (i)
  ORDER BY i, k, t;

SELECT '' AS "xxx", *
  FROM J1_TBL LEFT JOIN J2_TBL USING (i) WHERE (k = 1) order by 1, 2, 3, 4, 5;

SELECT '' AS "xxx", *
  FROM J1_TBL LEFT JOIN J2_TBL USING (i) WHERE (i = 1) order by 1, 2, 3, 4, 5;

--
-- semijoin selectivity for <>
--
-- explain (costs off)
-- select * from int4_tbl i4, tenk1 a
-- where exists(select * from tenk1 b
--              where a.twothousand = b.twothousand and a.fivethous <> b.fivethous)
--       and i4.f1 = a.tenthous;


--
-- More complicated constructs
--

--
-- Multiway full join
--

SELECT * FROM t1 FULL JOIN t2 USING (name) FULL JOIN t3 USING (name) order by 1, 2, 3, 4;

--
-- Test interactions of join syntax and subqueries
--

-- Basic cases (we expect planner to pull up the subquery here)
SELECT * FROM
(SELECT * FROM t2) as s2
INNER JOIN
(SELECT * FROM t3) s3
USING (name) order by 1, 2, 3;

SELECT * FROM
(SELECT * FROM t2) as s2
LEFT JOIN
(SELECT * FROM t3) s3
USING (name) order by 1, 2, 3;

SELECT * FROM
(SELECT * FROM t2) as s2
FULL JOIN
(SELECT * FROM t3) s3
USING (name) order by 1, 2, 3;

-- Cases with non-nullable expressions in subquery results;
-- make sure these go to null as expected
SELECT * FROM
(SELECT name, n as s2_n, 2 as s2_2 FROM t2) as s2
NATURAL INNER JOIN
(SELECT name, n as s3_n, 3 as s3_2 FROM t3) s3 order by 1, 2, 3, 4, 5;

SELECT * FROM
(SELECT name, n as s2_n, 2 as s2_2 FROM t2) as s2
NATURAL LEFT JOIN
(SELECT name, n as s3_n, 3 as s3_2 FROM t3) s3 order by 1, 2, 3, 4, 5;

SELECT * FROM
(SELECT name, n as s2_n, 2 as s2_2 FROM t2) as s2
NATURAL FULL JOIN
(SELECT name, n as s3_n, 3 as s3_2 FROM t3) s3 order by 1, 2, 3, 4;

SELECT * FROM
(SELECT name, n as s1_n, 1 as s1_1 FROM t1) as s1
NATURAL INNER JOIN
(SELECT name, n as s2_n, 2 as s2_2 FROM t2) as s2
NATURAL INNER JOIN
(SELECT name, n as s3_n, 3 as s3_2 FROM t3) s3 order by 1, 2, 3, 4, 5, 6, 7;

SELECT * FROM
(SELECT name, n as s1_n, 1 as s1_1 FROM t1) as s1
NATURAL FULL JOIN
(SELECT name, n as s2_n, 2 as s2_2 FROM t2) as s2
NATURAL FULL JOIN
(SELECT name, n as s3_n, 3 as s3_2 FROM t3) s3 order by 1, 2, 3, 4, 5, 6, 7;

SELECT * FROM
(SELECT name, n as s1_n FROM t1) as s1
NATURAL FULL JOIN
  (SELECT * FROM
    (SELECT name, n as s2_n FROM t2) as s2
    NATURAL FULL JOIN
    (SELECT name, n as s3_n FROM t3) as s3
  ) ss2 order by 1, 2, 3, 4;

SELECT * FROM
(SELECT name, n as s1_n FROM t1) as s1
NATURAL FULL JOIN
  (SELECT * FROM
    (SELECT name, n as s2_n, 2 as s2_2 FROM t2) as s2
    NATURAL FULL JOIN
    (SELECT name, n as s3_n FROM t3) as s3
  ) ss2 order by 1, 2, 3, 4, 5;


-- Test for propagation of nullability constraints into sub-joins

create temp table x (x1 int, x2 int);
insert into x values (1,11);
insert into x values (2,22);
insert into x values (3,null);
insert into x values (4,44);
insert into x values (5,null);

create temp table y (y1 int, y2 int);
insert into y values (1,111);
insert into y values (2,222);
insert into y values (3,333);
insert into y values (4,null);

select * from x order by 1, 2;
select * from y order by 1, 2;

select * from x left join y on (x1 = y1 and x2 is not null) order by 1, 2, 3, 4;
select * from x left join y on (x1 = y1 and y2 is not null) order by 1, 2, 3, 4;

select * from (x left join y on (x1 = y1)) left join x xx(xx1,xx2)
on (x1 = xx1) order by 1, 2, 3, 4, 5, 6;
select * from (x left join y on (x1 = y1)) left join x xx(xx1,xx2)
on (x1 = xx1 and x2 is not null) order by 1, 2, 3, 4, 5, 6;
select * from (x left join y on (x1 = y1)) left join x xx(xx1,xx2)
on (x1 = xx1 and y2 is not null) order by 1, 2, 3, 4, 5, 6;
select * from (x left join y on (x1 = y1)) left join x xx(xx1,xx2)
on (x1 = xx1 and xx2 is not null) order by 1, 2, 3, 4, 5, 6;
-- these should NOT give the same answers as above
select * from (x left join y on (x1 = y1)) left join x xx(xx1,xx2)
on (x1 = xx1) where (x2 is not null) order by 1;
select * from (x left join y on (x1 = y1)) left join x xx(xx1,xx2)
on (x1 = xx1) where (y2 is not null) order by 1;
select * from (x left join y on (x1 = y1)) left join x xx(xx1,xx2)
on (x1 = xx1) where (xx2 is not null) order by 1;

-- Tests for EXPLAIN output of BNL
-- These tests ensure we don't resolve fieldnames for batched expressions in Index Cond
CREATE FUNCTION public.dummy(OUT a integer, OUT b integer)
RETURNS SETOF record
LANGUAGE sql
IMMUTABLE PARALLEL SAFE STRICT
AS 'SELECT 1, 1';
EXPLAIN (COSTS OFF) SELECT 1 FROM pg_type t, (SELECT dummy() as x) AS ss WHERE t.oid = (ss.x).a;

CREATE TABLE tbl (c1 INT, c2 INT, PRIMARY KEY (c1 ASC, c2 ASC));
/*+Set(enable_hashjoin off) Set(enable_mergejoin off) Set(yb_bnl_batch_size 3) Set(enable_material off)*/ EXPLAIN (COSTS OFF) SELECT 1 FROM tbl, (SELECT dummy() as x) AS ss, (SELECT dummy() as x2) AS ss2 WHERE tbl.c1 = (ss.x).a AND tbl.c2 = (ss2.x2).a;

CREATE TABLE tbl2 (c1 int, c2 int, PRIMARY KEY(c1  ASC, c2 ASC));
/*+Set(enable_hashjoin off) Set(enable_mergejoin off) Set(yb_bnl_batch_size 3) Set(enable_material off) NestLoop(tbl tbl2) YbBatchedNL(ss tbl tbl2) IndexScan(tbl) SeqScan(tbl2)*/
EXPLAIN (COSTS OFF) SELECT 1 FROM (SELECT dummy() as x) AS ss LEFT JOIN (SELECT tbl.c1 FROM tbl, tbl2) j1 ON j1.c1 = (ss.x).a;

/*+Set(enable_mergejoin off) Set(enable_hashjoin off) Set(yb_bnl_batch_size 3) Set(enable_material off) YbBatchedNL(tbl2 ss tbl) NestLoop(ss tbl) IndexScan(tbl)*/
EXPLAIN (COSTS OFF) SELECT 1 FROM (SELECT dummy() as x) AS ss, tbl, tbl2 WHERE tbl2.c1 = (ss.x).a AND (ss.x).a < 40 AND tbl.c1 = ANY(ARRAY[(ss.x).a, (ss.x).a]);
