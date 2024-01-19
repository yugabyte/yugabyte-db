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

SET yb_enable_optimizer_statistics = on;
SET yb_prefer_bnl = true;

-- We're testing nested loop join batching in this file
SET yb_bnl_batch_size = 1024;

EXPLAIN (COSTS OFF) SELECT * FROM p1 t1 JOIN p2 t2 ON t1.a = t2.a WHERE t1.a <= 100 AND t2.a <= 100;

EXPLAIN (COSTS OFF) SELECT * FROM p1 t1 JOIN p2 t2 ON t1.a = t2.a + 1 WHERE t1.a <= 100 AND t2.a <= 100;

EXPLAIN (COSTS OFF) SELECT * FROM p1 t1 JOIN p2 t2 ON t1.a - 1 = t2.a + 1 WHERE t1.a <= 100 AND t2.a <= 100;

-- Batching on compound clauses
/*+ Leading((p2 p1)) */ EXPLAIN (COSTS OFF) SELECT * FROM p1 JOIN p2 ON p1.a = p2.b AND p2.a = p1.b;

explain (costs off) select * from p1 left join p5 on p1.a - 1 = p5.a and p1.b - 1 = p5.b where p1.a <= 30;

/*+IndexScan(p5 p5_hash)*/explain (costs off) select * from p1 left join p5 on p1.a - 1 = p5.a and p1.b - 1 = p5.b where p1.a <= 30;

/*+IndexScan(p5 p5_hash_asc)*/explain (costs off) select * from p1 left join p5 on p1.a - 1 = p5.a and p1.b - 1 = p5.b where p1.a <= 30;

EXPLAIN (COSTS OFF) SELECT * FROM p1 JOIN p2 ON p1.a = p2.b AND p2.a = p1.b;

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
ANALYZE;

EXPLAIN (COSTS OFF) SELECT t10.* FROM t12, t11, t10 WHERE x = y AND c1 = r1 AND c2 = r2 AND c3 = r3 AND c4 = r4 order by c1, c2, c3, c4;

DROP TABLE t10;
DROP TABLE t11;
DROP TABLE t12;

EXPLAIN (COSTS OFF) SELECT * FROM p3 t3 LEFT OUTER JOIN (SELECT t1.a as a FROM p1 t1 JOIN p2 t2 ON t1.a = t2.b WHERE t1.a <= 100 AND t2.a <= 100) s ON t3.a = s.a WHERE t3.a <= 30;

EXPLAIN (COSTS OFF) SELECT * FROM p3 t3 RIGHT OUTER JOIN (SELECT t1.a as a FROM p1 t1 JOIN p2 t2 ON t1.a = t2.b WHERE t1.b <= 10 AND t2.b <= 15) s ON t3.a = s.a;

-- anti join--
EXPLAIN (COSTS OFF) SELECT * FROM p1 t1 WHERE NOT EXISTS (SELECT 1 FROM p2 t2 WHERE t1.a = t2.a) AND t1.a <= 40;

EXPLAIN (COSTS OFF) SELECT * FROM p1 t1 WHERE NOT EXISTS (SELECT 1 FROM p2 t2 WHERE t1.a = t2.b) AND t1.a <= 40;

-- semi join--
EXPLAIN (COSTS OFF) SELECT * FROM p1 t1 WHERE EXISTS (SELECT 1 FROM p2 t2 WHERE t1.a = t2.a) AND t1.a <= 40;

EXPLAIN (COSTS OFF) SELECT * FROM p1 t1 WHERE EXISTS (SELECT 1 FROM p2 t2 WHERE t1.a = t2.b) AND t1.a <= 40;

explain (costs off) select * from p1 a join p2 b on a.a = b.a join p3 c on b.a = c.a join p4 d on a.b = d.b where a.b = 10 ORDER BY a.a, b.a, c.a, d.a;

CREATE INDEX p1_a_asc ON p1(a asc);
CREATE INDEX p2_a_asc ON p2(a asc);
ANALYZE;

-- Since we don't have many rows in p1, p2 it isn't too bad to use the extra
-- sort operator imposed by nested loop join batching.
explain (costs off) select * from p1, p2 where p1.a = p2.a order by p2.a asc;

INSERT INTO p1 SELECT i, i % 25, to_char(i, 'FM0000') FROM generate_series(600, 200000) i WHERE i % 2 = 0;
INSERT INTO p2 SELECT i, i % 25, to_char(i, 'FM0000') FROM generate_series(600, 500000) i WHERE i % 3 = 0;
ANALYZE;

-- After we have inserted many rows into each table, we expect that other
-- join methods that preserve the sort order of its input relations to be better.
explain (costs off) select * from p1, p2 where p1.a = p2.a order by p2.a asc;

-- However, removing the ordering constraint in this query allows us to prefer
-- the batched nested loop join option again.
-- Commenting this test until CBO is updated.
-- explain (costs off) select * from p1, p2 where p1.a = p2.a;

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
ANALYZE;
explain (costs off) select s3.* from s1, s2, s3 where s3.r1 = s1.r1 and s3.r2 = s2.r2 and s1.r3 = s2.r3 order by s3.r1, s3.r2;

DROP TABLE s3;
DROP TABLE s2;
DROP TABLE s1;

create table s1(a int, primary key (a asc));
create table s2(a int, primary key (a asc));
create table s3(a int, primary key (a asc));

insert into s1 select generate_series(1,10);
insert into s2 select generate_series(1,10);
insert into s3 select generate_series(1,10);
ANALYZE;

explain (costs off) /*+Leading(( ( s1 s2 ) s3 )) MergeJoin(s1 s2)*/select * from s1 left outer join s2
on s1.a = s2.a left outer join s3 on s2.a = s3.a where s1.a < 5;

drop table s1;
drop table s2;
drop table s3;

create table test2 (a int, pp int, b int, pp2 int, c int, primary key(a asc, pp asc, b asc, pp2 asc, c asc));
insert into test2 values (1,0, 2,0,1), (2,0, 3,0,3), (2,0,3,0,5);
create table test1 (a int, pp int, b int, pp2 int, c int, primary key(a asc, pp asc, b asc, pp2 asc, c asc));
insert into test1 values (1,0,2,0,1), (1,0,2,0,2), (2,0,3,0,3), (2,0,4,0,4), (2,0,4,0,5), (2,0,4,0,6);
ANALYZE;
explain (costs off) /*+IndexScan(p2)*/ select * from test1 p1 join test2 p2 on p1.a = p2.a AND p1.b = p2.b AND p1.c = p2.c;
drop table test1;
drop table test2;

-- Test on unhashable join operations. These should use the tuplestore
-- strategy.
CREATE TABLE m1 (a money, primary key(a asc));
INSERT INTO m1 SELECT i*2 FROM generate_series(1, 2000) i;

CREATE TABLE m2 (a money, primary key(a asc));
INSERT INTO m2 SELECT i*5 FROM generate_series(1, 2000) i;
ANALYZE;

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM m1 t1 JOIN m2 t2 ON t1.a = t2.a WHERE t1.a <= 50::money;

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM m2 t1 LEFT JOIN m1 t2 ON t1.a = t2.a WHERE t1.a <= 50::money;

EXPLAIN (COSTS OFF, TIMING OFF, SUMMARY OFF, ANALYZE) SELECT * FROM m2 t1 WHERE NOT EXISTS (SELECT 1 FROM m1 t2 WHERE t1.a = t2.a) AND t1.a <= 50::money;

DROP TABLE m1;
DROP TABLE m2;

create table q1 (c1 int, c2 int, primary key (c1 asc, c2 asc));
create table q2 (c1 int, c2 int, primary key (c2 hash, c1 asc));
insert into q1 select i, i / 4 from generate_series(0, 199) i;
insert into q2 select i, i / 2 from generate_series(0, 999) i;

analyze q1;
analyze q2;

-- Make sure a sort node is inserted above a batched NL join when appropriate

explain (costs off) select q1.c1 from q1 join q2 on q1.c2 = q2.c2 order by q1.c1 limit 10;

explain (costs off) select q2.c1, q1.c1 from q1 join q2 on q1.c2 = q2.c2 order by q1.c1 limit 10;

create table q3(a int, b int, c name, primary key(a,b));
create index q3_range on q3(a asc);

explain (costs off) select * from q1 p1 left join (SELECT p2.c1 as a1, p3.a as a2 from q2 p2 join q3 p3 on true) j1 on j1.a1 = p1.c1;

-- this should not be a batched NL join as it contains an unbatchable clause
-- (j1.a2 <= p1.c1) even though the batchable clause (j1.a1 = p1.c1) is also
-- present

explain (costs off) select * from q1 p1 left join (SELECT p2.c1 as a1, p3.a as a2 from q2 p2 join q3 p3 on true) j1 on j1.a1 = p1.c1 and j1.a2 <= p1.c1;

DROP TABLE q1;
DROP TABLE q2;
DROP TABLE q3;

create table tab1 (id int primary key, r1 int);
insert into tab1 select generate_series(1,1000);
analyze tab1;
create table tab2 (id int primary key, r1 int, r2 int not null) partition by range (id);
create table tab2_p0 partition of tab2 default;
create table tab2_p1 partition of tab2 for values from (minvalue) to (10);
create table tab2_p2 partition of tab2 for values from (10) to (20);
create table tab2_p3 partition of tab2 for values from (20) to (maxvalue);
create index i_tab2_r2 on tab2 (r2 asc);

SET yb_bnl_batch_size = 1024;

explain (costs off) /*+ Leading((tab1 tab2)) IndexScan(tab2) NestLoop(tab1 tab2) */ select * from tab1 join tab2 on tab1.r1 = tab2.r2;
drop table tab2;
drop table tab1;
