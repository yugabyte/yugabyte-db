\set ECHO all
SET client_min_messages = warning;
drop schema if exists yb_hints cascade;
create schema yb_hints;
reset all;
set search_path to yb_hints;

-- Turn on internal hint generation testing. For all query without hints,
-- hints will be generated and used to generate a plan that should be
-- identical (in terms on plan shape and access/join methods) to the one
-- the planner returned for the query originally.
set pg_hint_plan.yb_enable_internal_hint_test to on;

-- Want to issue a warning if we find bad hints.
set pg_hint_plan.yb_bad_hint_mode to warn;

create  table t0 (a0 int, b0 int, c0 int, d0 int, e0 int, f0 int, g0 int, h0 int, i0 int, j0 int, k0 int, ch0 char(5), vch0 varchar(5), unn0 int unique not null, primary key(unn0));
insert into t0 values(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '00000', '00000', 0);
insert into t0 values(1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, '11111', '11111', 1);
insert into t0 values(2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, '22222', '22222', 2);
insert into t0 values(3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, '33333', '33333', 3);
insert into t0 values(3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, '3333', '3333', 4);
insert into t0 values(null, null, null, null, null, null, null, null, null, null, null, null, null, 5);
insert into t0 values(6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, '66666', '66666', 6);
insert into t0 values(7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, '77777', '77777', 7);
insert into t0 values(7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, '7777', '7777', 8);
insert into t0 values(null, null, null, null, null, null, null, null, null, null, null, null, null, 9);
create  table t1 (a1 int, b1 int, c1 int, d1 int, ch1 char(5), vch1 varchar(5), unn1 int unique not null, primary key(unn1));
insert into t1 select a0, b0, c0, d0, ch0, vch0, unn0 from t0;
insert into t1 select a0, b0, c0, d0, ch0, vch0, unn0+10 from t0;
create index t1_a1_asc_idx on t1(a1);
create index t1_a1_asc_b1_asc_idx on t1(a1 asc, b1 asc);
create index t1_a1_desc_idx on t1(a1 desc);
create  table t2 (a2 int, b2 int, c2 int, d2 int, ch2 char(5), vch2 varchar(5), unn2 int unique not null, primary key(unn2));
create index t2_a2_idx on t2(a2) ;
insert into t2 select * from t1;
insert into t2 select a1, b1, c1, d1, ch1, vch1, unn1+20 from t1;
insert into t2 select a1, null, c1, null, ch1, vch1, unn1+40 from t1;
insert into t2 select null, b1, null, d1, ch1, vch1, unn1+60 from t1;
create  table t3 (a3 int, b3 int, c3 int, d3 int, ch3 char(5), vch3 varchar(5), unn3 int unique not null, primary key(unn3));
insert into t3 select * from t2;
insert into t3 select a0, b0, c0, d0, ch0, vch0, unn0+80 from t0;
create  table t4 (a4 int, b4 int, c4 int, d4 int, ch4 char(5), vch4 varchar(5), unn4 int unique not null, primary key(unn4));
insert into t4 select * from t3;
insert into t4 select a0, b0, c0, d0, ch0, vch0, unn0+90 from t0;
create  table t5 (a5 int, b5 int, c5 int, d5 int, ch5 char(5), vch5 varchar(5), unn5 int unique not null, primary key(unn5));
insert into t5 select * from t4;
insert into t5 select a0, b0, c0, d0, ch0, vch0, unn0+100 from t0;
create  table t6 (a6 int, b6 int, c6 int, d6 int, ch6 char(5), vch6 varchar(5), unn6 int unique not null, primary key(unn6));
insert into t6 select * from t5;
insert into t6 select a0, b0, c0, d0, ch0, vch0, unn0+110 from t0;
create  table t7 (a7 int, b7 int, c7 int, d7 int, ch7 char(5), vch7 varchar(5), unn7 int unique not null, primary key(unn7));
insert into t7 select * from t6;
insert into t7 select a0, b0, c0, d0, ch0, vch0, unn0+120 from t0;
create  table t8 (a8 int, b8 int, c8 int, d8 int, ch8 char(5), vch8 varchar(5), unn8 int unique not null, primary key(unn8));
insert into t8 select * from t7;
insert into t8 select a0, b0, c0, d0, ch0, vch0, unn0+130 from t0;
create  table t9 (a9 int, b9 int, c9 int, d9 int, ch9 char(5), vch9 varchar(5), unn9 int unique not null, primary key(unn9));
insert into t9 select * from t8;
insert into t9 select a0, b0, c0, d0, ch0, vch0, unn0+140 from t0;
create  table t10 (a10 int, b10 int, c10 int, d10 int, ch10 char(5), vch10 varchar(5), unn10 int unique not null, primary key(unn10));
insert into t10 select * from t9;
insert into t10 select a0, b0, c0, d0, ch0, vch0, unn0+150 from t0;

CREATE TABLE prt1 (a int, b int, c varchar) PARTITION BY RANGE(a);
CREATE TABLE prt1_p1 PARTITION OF prt1 FOR VALUES FROM (0) TO (250);
CREATE TABLE prt1_p3 PARTITION OF prt1 FOR VALUES FROM (500) TO (600);
CREATE TABLE prt1_p2 PARTITION OF prt1 FOR VALUES FROM (250) TO (500);
INSERT INTO prt1 SELECT i, i % 25, to_char(i, 'FM0000') FROM generate_series(0, 599) i WHERE i % 2 = 0;
CREATE INDEX iprt1_p1_a on prt1_p1(a);
CREATE INDEX iprt1_p1_a_asc on prt1_p1(a asc);
CREATE INDEX iprt1_p2_a on prt1_p2(a);
CREATE INDEX iprt1_p3_a on prt1_p3(a);

CREATE TABLE prt2 (a int, b int, c varchar) PARTITION BY RANGE(b);
CREATE TABLE prt2_p1 PARTITION OF prt2 FOR VALUES FROM (0) TO (250);
CREATE TABLE prt2_p2 PARTITION OF prt2 FOR VALUES FROM (250) TO (500);
CREATE TABLE prt2_p3 PARTITION OF prt2 FOR VALUES FROM (500) TO (600);
INSERT INTO prt2 SELECT i % 25, i, to_char(i, 'FM0000') FROM generate_series(0, 599) i WHERE i % 3 = 0;
CREATE INDEX iprt2_p1_b on prt2_p1(b);
CREATE INDEX iprt2_p1_b_desc on prt2_p1(b desc);
CREATE INDEX iprt2_p2_b on prt2_p2(b);
CREATE INDEX iprt2_p3_b on prt2_p3(b);

CREATE TABLE tbl (c1 INT, c2 INT, PRIMARY KEY (c1 ASC, c2 ASC));

analyze t0 ;
analyze t1 ;
analyze t2 ;
analyze t3 ;
analyze t4 ;
analyze t5 ;
analyze t6 ;
analyze t7 ;
analyze t8 ;
analyze t9 ;
analyze t10 ;
ANALYZE prt1;
ANALYZE prt2;
ANALYZE tbl;

-- Simple query.
explain (hints on, costs off) select * from t1 where a1=1;

-- Simple join query.
explain (hints on, costs on) select * from t1, t2 where a1<5 and b1=b2 order by a1 asc;

-- Force an index and change the join order.
/*+ Leading((t1 t2)) SeqScan(t2) IndexScan(t1 t1_a1_desc_idx) NestLoop(t2 t1) */ explain (hints on, costs off) select * from t1, t2 where a1<5 and b1=b2 order by a1 asc;

-- OK since t1 and t2 do not have to be directly joined. Should not generate warnings.
/*+ noNestLoop(t1 t2) */ explain (costs off) select max(a1) from t1 join t2 on a1<a2 join t3 on a1=a3;

-- Force t0-t1 join to be first. Should see no warnings/errors.
/*+ Leading(((t0 t1) t2)) */ explain (hints on, costs off) select count(*) from t0 left join t1 on a0=a1 inner join t2 on b0=b2 where unn2=1;

-- Force t2-t0 join first and make this inner input to hash join with t1. Should see no errors/warnings.
/*+ Leading((t1 (t2 t0))) hashJoin(t0 t1 t2)  */ explain (hints on, costs off) select count(*) from t0 left join t1 on a0=a1 inner join t2 on b0=b2 where unn2=1;

-- 'dt' should appear in the EXPLAIN since a subquery scan is required. No warnings/errors expected.
explain (hints on, costs off) select count(*) from t1, t2, t3, (select b4 from t4, t5 where a4=a5 group by b4, b5) dt where t1.a1=t2.a2 and a1=a3 and a1=b4;

-- 'dt' will not appear in the EXPLAIN since a subquery scan is not required. (Removed a GROUP BY column so simple subquery scan is removed).
explain (costs off) select count(*) from t1, t2, t3, (select b4 from t4, t5 where a4=a5 group by b4) dt where t1.a1=t2.a2 and a1=a3 and a1=b4;

-- Turn hints on for previous query. 'dt' will now appear in the EXPLAIN as 'Hint Alias'. No warnings/errors expected.
explain (hints on, costs off) select count(*) from t1, t2, t3, (select b4 from t4, t5 where a4=a5 group by b4) dt where t1.a1=t2.a2 and a1=a3 and a1=b4;

-- Force a bushy plan.
/*+ Leading(((t3 dt) (t1 t2))) */ explain (costs off) select count(*) from t1, t2, t3, (select b4 from t4, t5 where a4=a5 group by b4) dt where t1.a1=t2.a2 and a1=a3 and a1=b4;

-- Change top join method to NLJ.
/*+ Leading(((t3 dt) (t1 t2))) NestLoop(t2 dt t3 t1) */ explain (costs off) select count(*) from t1, t2, t3, (select b4 from t4, t5 where a4=a5 group by b4) dt where t1.a1=t2.a2 and a1=a3 and a1=b4;

-- Hint generation for WHERE subqueries.
explain (hints on, costs off) select count(*) from t1, t2, t3 where a1=a2 and a1=a3 and b1 in (select a4 from t4 group by a4, b4) and b2 in (select a5 from t5 group by a5, b5);

-- Change join order and use all merge joins.
/*+ Leading((((t1 ANY_subquery) ANY_subquery_1) t2)) MergeJoin(t1 ANY_subquery) MergeJoin(t1 ANY_subquery ANY_subquery_1) MergeJoin(ANY_subquery ANY_subquery_1 t1 t2) */ explain (hints on, costs off) select count(*) from t1, t2 where a1=a2 and b1 in (select a4 from t4 group by a4, b4) and b2 in (select a5 from t5 group by a5, b5);

-- Hint generation for VALUES clause(s). Should see no errors/warnings.
explain (hints on, costs off) select val1.c1 from (values(1, 1), (2, 2), (3, 3)) val1(c0, c1), t0, t1, (values(1, 1), (2, 2), (3, 3)) val2(c0, c1)  where val1.c1=a0 and a0=a1 and val2.c1=val1.c1;

-- Change query and force cross join between VALUES derived tables. Should work fine.
/*+ Leading((((*VALUES* *VALUES*_1) t1) t0)) */ explain (hints on, costs off) select val1.c1 from (values(1, 1), (2, 2), (3, 3)) val1(c0, c1), t0, t1, (values(1, 1), (2, 2), (3, 3)) val2(c0, c1)  where val1.c1=a0 and a0=a1 and b0=b1;

-- Disable all join methods but hint the query. Should still give plan defined by hints.
set enable_mergejoin to 0; set enable_hashjoin to 0; set enable_nestloop to 0;
/*+ Leading(((t2 t1) dt)) SeqScan(t2) IndexScan(t1 t1_a1_asc_b1_asc_idx) YbBatchedNL(t1 t2) NestLoop(dt t1 t2)  Leading((t4 (t3 t5))) SeqScan(t4) SeqScan(t3) IndexScan(t5 t5_pkey) HashJoin(t3 t5) HashJoin(t3 t4 t5) */ explain (hints on, costs off) select * from t1, t2, (select a3 from t3 join t4 on a3=a4 join t5 on a3=a5 where unn5=1 group by a3) dt where a1=1 and b1=b2 and b1=a3;
reset enable_mergejoin; reset enable_hashjoin; reset enable_nestloop;

-- Hint a cross join with ROWS. Should work.
/*+ Leading(((((t5 t4) t1) t3) t2)) Rows(t4 t5 #10000) */ explain (hints on, costs off) select count(*) from t1, t2, t3, t4, t5 where a2=a3 and a2=a4 and a2=a5;

-- Query with derived tables but no actual tables. Will have *RESULT* type names in generated hints.
explain (hints on, costs off) SELECT * FROM
( SELECT 1 as key1 ) sub1
LEFT JOIN
( SELECT sub3.key3, sub4.value2, COALESCE(sub4.value2, 66) as value3 FROM
    ( SELECT 1 as key3 ) sub3
    LEFT JOIN
    ( SELECT sub5.key5, COALESCE(sub6.value1, 1) as value2 FROM
        ( SELECT 1 as key5 ) sub5
        LEFT JOIN
        ( SELECT 2 as key6, 42 as value1 ) sub6
        ON sub5.key5 = sub6.key6
    ) sub4
    ON sub4.key5 = sub3.key3
) sub2
ON sub1.key1 = sub2.key3;

-- Lateral join
explain (hints on, costs off)
select * from
  t1 as i41,
  lateral
    (select 1 as x from
      (select i41.a1 as lat,
              i42.a1 as loc from
         t2 as i81, t1 as i42) as ss1
      right join t1 as i43 on (i43.a1 > 1)
      where ss1.loc = ss1.lat) as ss2
where i41.a1 > 0;

-- Outer joins and VALUES clause.
explain (costs off, hints on)
select a1, b1, a2, b2 from
  t1
  inner join t2
    left join (select v1.x2, v2.y1, 11 AS d1
               from (values(1,0)) v1(x1,x2)
               left join (values(3,1)) v2(y1,y2)
               on v1.x1 = v2.y2) subq1
    on b2 = subq1.x2
  on unn1 = subq1.d1
  left join t3
  on subq1.y1 = unn2
where unn1 < 4 and ch1 > ch2;

-- Hints with CTEs. Should work.
/*+ Leading((y x)) Leading(((t2 t1) t0)) */ explain (hints on, costs off) with cte1 as (select a0, a1 from t0, t1, t2 where a0=a1 and a0=a2) select count(*) from cte1 x, cte1 y where x.a0=y.a0 and x.a1<y.a1;

-- Complex query;
explain (hints on, costs off) select count(*) from t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, (select a1 x from t1, t2, t3, t4, t5, t6, t7, t8, t9, t10 where a1=a2 and a1=a3 and a1=a4 and a1=a5 and a5=a6 and a5=a7 and a5=a8 and a5=a9 and b7=1) dt where a1=a2 and a1=a3 and a1=a4 and a1=a5 and a5=a6 and a5=a7 and a5=a8 and a5=a9 and b7=1 and a1=x;

/*+ Leading(((((t10 (((((((t7_1 t3_1) t6_1) t8_1) (t5_1 t4_1)) (t10_1 t9_1)) t2_1) t1_1)) ((((t9 t3) t8) (t6 t5)) (t7 t4))) t2) t1)) SeqScan(t10) SeqScan(t7_1) SeqScan(t3_1) HashJoin(t3_1 t7_1) SeqScan(t6_1) HashJoin(t3_1 t6_1 t7_1) SeqScan(t8_1) HashJoin(t3_1 t6_1 t7_1 t8_1) SeqScan(t5_1) SeqScan(t4_1) HashJoin(t4_1 t5_1) HashJoin(t3_1 t4_1 t5_1 t6_1 t7_1 t8_1) SeqScan(t10_1) SeqScan(t9_1) NestLoop(t10_1 t9_1) HashJoin(t10_1 t3_1 t4_1 t5_1 t6_1 t7_1 t8_1 t9_1) IndexOnlyScan(t2_1 t2_a2_idx) YbBatchedNL(t10_1 t2_1 t3_1 t4_1 t5_1 t6_1 t7_1 t8_1 t9_1) IndexOnlyScan(t1_1 t1_a1_asc_idx) YbBatchedNL(t10_1 t1_1 t2_1 t3_1 t4_1 t5_1 t6_1 t7_1 t8_1 t9_1) NestLoop(t10 t10_1 t1_1 t2_1 t3_1 t4_1 t5_1 t6_1 t7_1 t8_1 t9_1) SeqScan(t9) SeqScan(t3) HashJoin(t3 t9) SeqScan(t8) HashJoin(t3 t8 t9) SeqScan(t6) SeqScan(t5) HashJoin(t5 t6) HashJoin(t3 t5 t6 t8 t9) SeqScan(t7) SeqScan(t4) HashJoin(t4 t7) HashJoin(t3 t4 t5 t6 t7 t8 t9) HashJoin(t10 t10_1 t1_1 t2_1 t3 t3_1 t4 t4_1 t5 t5_1 t6 t6_1 t7 t7_1 t8 t8_1 t9 t9_1) IndexOnlyScan(t2 t2_a2_idx) YbBatchedNL(t10 t10_1 t1_1 t2 t2_1 t3 t3_1 t4 t4_1 t5 t5_1 t6 t6_1 t7 t7_1 t8 t8_1 t9 t9_1) IndexOnlyScan(t1 t1_a1_asc_idx) YbBatchedNL(t1 t10 t10_1 t1_1 t2 t2_1 t3 t3_1 t4 t4_1 t5 t5_1 t6 t6_1 t7 t7_1 t8 t8_1 t9 t9_1) Set(enable_hashagg on) Set(enable_material on) Set(enable_memoize on) Set(enable_sort on) Set(enable_incremental_sort on) Set(max_parallel_workers_per_gather 2) Set(parallel_tuple_cost 0.10) Set(parallel_setup_cost 1000.00) Set(min_parallel_table_scan_size 1024) Set(yb_prefer_bnl on) Set(yb_bnl_batch_size 1024) Set(yb_fetch_row_limit 1024) Set(from_collapse_limit 20) Set(join_collapse_limit 20) Set(geqo false) */ explain (hints on, costs off) select count(*) from t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, (select a1 x from t1, t2, t3, t4, t5, t6, t7, t8, t9, t10 where a1=a2 and a1=a3 and a1=a4 and a1=a5 and a5=a6 and a5=a7 and a5=a8 and a5=a9 and b7=1) dt where a1=a2 and a1=a3 and a1=a4 and a1=a5 and a5=a6 and a5=a7 and a5=a8 and a5=a9 and b7=1 and a1=x;

-- Correlated subquery with multiple blocks. Should not give any warnings/errors.
explain (hints on, costs off) select * from t1 left join t2 on a1=a2 or b1+b2 not in (select b3 from t3, t4, t5 where a3=a4 and a3=a5 and c3 != c1-c2) where a1 < (select max(a4) from t4, t5 where a4 != a5);

-- Try a view that is a security barrier. Should see an inherited hint alias.
create temporary view v1(x, y, z) with(security_barrier) as select a1, a2, a3 from t1 right join t2 on a1+1=a2 join t3 on b2=b3;
explain (hints on, costs off) select sum(a1+x) from v1, t1, t2 where x=a1 and x=y and y=z and z=unn2;

-- View on a view. Should see 2 inherited hint aliases and no warnings/errors.
create temporary view v2(x, y, z) as select x, y, z from v1 where x<5 group by x, y, z ;
explain (hints on, costs off) select sum(a1+x) from v2, t1, t2 where x=a1 and x=y and y=z and z=unn2;

-- Views with set ops. Should get no warnings/errors.
create temporary view v3 as select a7, a8, a9 z from t7, t8, t9 where a7=a8 and a7=a9 union select a7, a8, a9 from t7, t8, t9 where a7=a8 and a7=a9;
explain (hints on, costs off) select * from v3, t9 where z=a9;

create temporary view v4 as select a7, a8, a9 z from t7, t8, t9 where a7=a8 and a7=a9 intersect all select a7, a8, a9 from t7, t8, t9 where a7=a8 and a7=a9;
explain (hints on, costs off) select * from v4, t9 where z=a9;

create temporary view v5 as select a7, a8, a9 z from t7, t8, t9 where a7=a8 and a7=a9 except all select a7, a8, a9 from t7, t8, t9 where a7=a8 and a7=a9;
explain (hints on, costs off) select * from v5, t9 where z=a9;

CREATE OR REPLACE FUNCTION func1(integer, integer) RETURNS integer
    AS 'select count(*) from t1, t2 where a1<$1 and b2>$2 and a1=a2;'
    LANGUAGE SQL
    IMMUTABLE
    RETURNS NULL ON NULL INPUT;

-- Scalar function should be OK.
explain (hints on, costs off) select * from t1, t2, t3 where a1<a2 and a1=a3 and a1=func1(1, 2);

CREATE OR REPLACE FUNCTION func2(integer, integer) RETURNS TABLE(f1 int, f2 text)
     AS '/*+ Leading((t2 t1)) MergeJoin(t1 t2) */ select a1, a2 from t1, t2 where a1<$1 and b2>$2 and a1=a2;'
    LANGUAGE SQL;

-- Table function should be OK.
explain (hints on, costs off) select f2 from t1, t2, t3, func2(1, 2) funky where a1<a2 and a1=a3 and a3=f1 ;

-- Make sure uniqueness can be proved using indices for hinted query generated for internal hint test.
explain (hints on, costs off) select 1 from t2, t3, t1 where a1=a2 and a1=a3 and unn1=1;

-- Check hint generation for partitioned tables.
/*
 * Unexpected error until https://github.com/yugabyte/yugabyte-db/issues/28070 is fixed.
 */
explain (hints on, costs off) select count(*) from prt1 p1 join prt2 p2 on p1.a=p2.a;

-- Partitioned table where all partition-wise joins are forced to be merge joins. Should give no warnings/errors.
SET enable_partitionwise_join to true;
/*+ Mergejoin(t1 t2) */ explain (hints on, costs off) SELECT t1.a, t1.c, t2.b, t2.c FROM prt1 t1, prt2 t2 WHERE t1.a = t2.b AND t1.b = 0 ORDER BY t1.a, t2.b;

-- Hint/join 2 partitions individually and union results. Should be OK.
/*+ NestLoop(t1_1 t2_1) Leading(t2_1 t1_1) MergeJoin(t1 t2) Leading((t1 t2)) IndexScan(t1 iprt1_p1_a) IndexScan(t2_1 iprt2_p2_b) */ explain (hints on, costs off) select count(*) from (select *
 from prt1_p1 t1, prt2_p1 t2 where t1.a+1=t2.b and t1.a=5 union all select * from prt1_p2 t1, prt2_p2 t2 where t1.a=t2.b and t1.a=10) dt;

-- Turn off join partitioning and try hinting. Should work fine.
SET enable_partitionwise_join to false;
/*+ Leading((t2 t1)) HashJoin(t1 t2) */ explain (hints on, costs off) SELECT t1.a, t1.c, t2.b, t2.c FROM prt1 t1, prt2 t2 WHERE t1.a = t2.b AND t1.b = 0 ORDER BY t1.a, t2.b;

-- Make sure the internal hint test passes.
explain (hints on, costs off) SELECT t1.a, t1.c, t2.b, t2.c FROM prt1 t1, prt2 t2 WHERE t1.a = t2.b AND t1.b = 0 ORDER BY t1.a, t2.b;

-- Test hint table using query id instead of query text.
create extension if not exists pg_hint_plan;
set pg_hint_plan.enable_hint_table to on;
set pg_hint_plan.yb_use_query_id_for_hinting to on;

delete from hint_plan.hints;

-- Query id is expected to be -7371982929224359937 for 'select * from information_schema.columns'.
INSERT INTO hint_plan.hints (norm_query_string, application_name, hints) VALUES ('-7371982929224359937', '', 'Leading(((dep seq) ((co nco) (nt (((((c a) t) (bt nbt)) ad) nc))))) set(yb_prefer_bnl false) set(yb_enable_batchednl false) Set(from_collapse_limit 12) Set(join_collapse_limit 12) Set(geqo false)');

select * from hint_plan.hints;

explain (hints on, costs off, verbose on) select * from information_schema.columns;

delete from hint_plan.hints;

reset pg_hint_plan.enable_hint_table;
reset pg_hint_plan.yb_use_query_id_for_hinting;

-- Test Leading semantics. Since this form of Leading allows (((t1 t2) t3)) or (((t2 t1) t3)) the following
-- 2 queries should give the same plan.
/*+ leading(t1 t2 t3) hashjoin(t1 t2) */ explain (hints on, costs off) select 1 from t1, t2, t3 where a1=a2 and a1=a3 and unn1=1;

/*+ leading(t2 t1 t3) hashjoin(t1 t2) */ explain (hints on, costs off) select 1 from t1, t2, t3 where a1=a2 and a1=a3 and unn1=1;

set pg_hint_plan.yb_bad_hint_mode to error;
-- Update statement with correlated subquery and duplicate name 'dt'. Must resolve conflict on 't1' also as with Insert. Should work.
/*+ Leading(((t3 t1_1) (t2 dt))) Leading((t4 dt_1)) */ explain (hints on, costs off) update t1 set a1=1 where exists (select 1 from t1, t2, t3, (select b4 from t4, t5 dt where a4=a5 and b5=b1 group by b4, b5) dt where t1.a1=t2.a2 and a1=a3 and a1=b4);

-- Should get no error.
/*+ Leading((((t3 t1_1) t2) dt)) Leading((t4 t1_2)) */ explain (hints on, costs off) delete from t1 where a1 not in (select b1 from t1, t2, t3, (select b4 from t4, t5 t1 where a4=a5 group by b4, b5) dt where t1.a1=t2.a2 and a1=a3 and a1=b4);

-- Simple delete with index hint. Should work.
/*+ IndexScan(t1 t1_a1_asc_b1_asc_idx) */ explain (hints on, costs off) delete from t1 where a1=5;

-- Use alias for t1. Should work.
/*+ IndexScan(x t1_a1_asc_b1_asc_idx) */ explain (hints on, costs off) delete from t1 as x where a1=5;

CREATE OR REPLACE FUNCTION dummy(OUT a integer, OUT b integer)
RETURNS SETOF record
LANGUAGE sql
IMMUTABLE PARALLEL SAFE STRICT
AS 'SELECT 1, 1';

set pg_hint_plan.yb_bad_hint_mode to warn;

-- Hint generation should work.
EXPLAIN (costs off, hints on) SELECT 1 FROM pg_type t, (SELECT dummy() as x) AS ss WHERE t.oid = (ss.x).a;

-- Hint generation should work.
EXPLAIN (costs off, hints on) SELECT 1 FROM tbl, (SELECT dummy() as x) AS ss, (SELECT dummy() as x2) AS ss2 WHERE tbl.c1 = (ss.x).a AND tbl.c2 = (ss2.x2).a;

-- Insert should work using t1_1 as alias.
/*+ Leading((((t3 t1_1) t2) dt)) */ explain (hints on, costs off) insert into t1 select t1.* from t1, t2, t3, (select b4 from t4, t5 dt where a4=a5 group by b4, b5) dt where t1.a1=t2.a2 and a1=a3 and a1=b4;

-- Should work since we are forcing a cross join t2-t3 (planner would not normally try this join).
/*+ noNestLoop(t1 t2) noNestLoop(t1 t3) Leading(t2 t3) */ explain (costs off, uids on) select max(a1) from t1 join t2 on a1<a2 join t3 on a1>a3;

-- Test hints affecting NestLoop and YBBatchedNL join methods.

-- Turn on BNL preference.
set yb_prefer_bnl to on;

-- Should get NestLoop or YBBatchedNL based on cost.
/*+ NestLoop(t1 t2) */ explain select a1 from t1 join t2 on a1=a2 order by a1;

-- Should get HashJoin or MergeJoin since hint prevents both NestLoop and
-- YBBatchedNL.
/*+ NoNestLoop(t1 t2) */ explain select a1 from t1 join t2 on a1=a2 order by a1;

-- Should get YBBatchedNL.
/*+ YBBatchedNL(t1 t2) */ explain select a1 from t1 join t2 on a1=a2 order by a1;

-- Should get NestLoop, HashJoin, or MergeJoin.
/*+ NoYBBatchedNL(t1 t2) */ explain select a1 from t1 join t2 on a1=a2 order by a1;

-- Turn off BNL preference.
set yb_prefer_bnl to off;

-- Should get NestLoop.
/*+ NestLoop(t1 t2) */ explain select a1 from t1 join t2 on a1=a2 order by a1;

-- Should get HashJoin, MergeJoin, or YBBatchedNL.
/*+ NoNestLoop(t1 t2) */ explain select a1 from t1 join t2 on a1=a2 order by a1;

-- Should get YBBatchedNL.
/*+ YBBatchedNL(t1 t2) */ explain select a1 from t1 join t2 on a1=a2 order by a1;

-- Should get NestLoop, HashJoin, or MergeJoin.
/*+ NoYBBatchedNL(t1 t2) */ explain select a1 from t1 join t2 on a1=a2 order by a1;

-- Should get HashJoin.
/*+ NoYBBatchedNL(t1 t2) set(enable_nestloop off) set(enable_mergejoin off) */ explain select a1 from t1 join t2 on a1=a2
 order by a1;

reset yb_prefer_bnl;

-- GH28072 - Make sure the internal hint test passes.
set yb_enable_parallel_append = on;
explain (hints on, costs off) SELECT t1.a, t1.c, t2.b, t2.c FROM prt1 t1, prt2 t2 WHERE t1.a = t2.b AND t1.b = 0 ORDER BY t1.a, t2.b;
reset yb_enable_parallel_append;

-- NEGATIVE TESTS

-- Try to use a bad index. Should get warnings.
/*+ Leading((t2 t1)) SeqScan(t2) IndexScan(t1 badIndex) YbBatchedNL(t2 t1) */ explain (hints off, costs off) select * from t1, t2 where a1=1 and b1=b2;

-- Specify a bad index with a valid one. Should get warnings.
/*+ Leading((t2 t1)) SeqScan(t2) IndexScan(t1 t1_a1_asc_b1_asc_idx badIndex) YbBatchedNL(t2 t1) */ explain (hints off, costs off) select * from t1, t2 where a1=1 and b1=b2;

-- Specify a bad index with a valid one but bad name comes first. Should get warnings.
/*+ Leading((t2 t1)) SeqScan(t2) IndexScan(t1 badIndex t1_a1_asc_b1_asc_idx) YbBatchedNL(t2 t1) */ explain (hints off, costs off) select * from t1, t2 where a1=1 and b1=b2;

-- Make index invalid for read and try to use it. Should get a warning (same as a missing index).
set yb_non_ddl_txn_for_sys_tables_allowed = true;
update pg_index set indisready=true, indisvalid = false where indrelid = 't1'::regclass;
/*+ IndexScan(t1 t1_a1_desc_idx) */ explain (costs off) select * from t1 where a1<5;
update pg_index set indisready=true, indisvalid = true where indrelid = 't1'::regclass;
set yb_non_ddl_txn_for_sys_tables_allowed = false;

-- Turn on replanning. Now should see warnings for bad index and replanning message.
set pg_hint_plan.yb_bad_hint_mode to replan;
/*+ Leading((t1 t2)) SeqScan(t2) IndexScan(t1 badIndex) NestLoop(t2 t1) */ explain (hints on, costs off) select * from t1, t2 where a1<5 and b1=b2 order by a1 asc;
set pg_hint_plan.yb_bad_hint_mode to warn;

-- Bad table name 'x'. Should generate warnings.
/*+ Leading((t2 t1)) SeqScan(t2) IndexScan(t1 t1_a1_asc_b1_asc_idx) YbBatchedNL(t2 x) */ explain (hints on, costs off) select * from t1, t2 where a1=1 and b1=b2;

-- No plan can be found using the hint. Should generate warnings.
/*+ noSeqScan(tab) */ explain (hints on, costs off) select count(*) from t2 tab where b2<10;

-- No plan can be found using the hint. Should generate warnings.
/*+ set(enable_seqscan off) */ explain (hints on, costs off) select count(*) from t2 tab where b2<10;

-- This hint makes sense but currently there cannot be > 1 join method hint for the same set of tables so will give warnings/errors.
-- Since pg_hint_plan.yb_bad_hint_mode==WARN, the noNestLoop hint is marked as a duplicate (and is not used) but the
-- NoYbBatchedNL hint remains valid and is used. (This is an artifact of how the hint processing works.)
-- Therefore, for this query and hints, NestLoop is a valid choice but BNL is not.
/*+ noNestLoop(t1 t2) NoYbBatchedNL(t1 t2) */ explain (hints on, costs off) select max(a1) from t1 join t2 on a1=a2;

-- Try to force t0-t1 join. Should see errors/warnings since this is not a legal join order.
/*+ Leading(((t0 t1) t2)) */ explain (hints on, costs off, uids on) select count(*) from t0 left join (t1 join t2 on a1=a2) on a0=a1;

-- Syntax error. Should see warnings/error.
/*+ nestLoop(t4 t5 */ explain (hints on, costs off) select count(*) from t4 full join t5 on a4=a5;

-- Fix the syntax error and try again. No joy since NLJ cannot be used for FULL join. Will give warnings.
/*+ nestLoop(t4 t5) */ explain (hints on, costs off, uids on) select count(*) from t4 full join t5 on a4=a5;

-- Cannot do a ROJ so should get a warning.
/*+ leading((t4 t5)) nestLoop(t4 t5) */ explain (costs on, uids on) select count(*) from t4 right join t5 on a4=a5;

-- Try to use hash join without an equality predicate. Should get an error, re-plan, and get first 5 rows.
set pg_hint_plan.yb_bad_hint_mode to replan;
/*+ hashJoin(t3 t4) */ select unn3, unn4 from t3 join t4 on a3<a4 order by unn3, unn4 limit 5;
set pg_hint_plan.yb_bad_hint_mode to warn;

-- Hint a cross join with bad ROWS hint syntax error. Should get warnings/errors.
/*+ Leading(((((t5 t4) t1) t3) t2)) Rows(t4 t5 !10000) */ explain (hints on, costs off) select count(*) from t1, t2, t3, t4, t5 where a2=a3 and a2=a4 and a2=a5;

-- Hint a cross join with bad table name ROWS hint. Should get warnings/errors.
/*+ Leading(((((t5 t4) t1) t3) t2)) Rows(t4 t0 #10000) */ explain (hints on, costs off) select count(*) from t1, t2, t3, t4, t5 where a2=a3 and a2=a4 and a2=a5;

-- Syntax error in parallel hint. Should get warnings/errors.
/*+ Parallel(t1 3 hrd) */ explain (hints on, costs off) select count(*) from t1;

-- Bad table name in parallel hint. Should get warnings/errors.
/*+ Parallel(t11 3 hard) */ explain (hints on, costs off) select count(*) from t1;

-- Use bad name 't22' in hint. Should get warnings and whatever plan was found without the bad leading hint.
/*+ Leading(((((t5 t4) t1) t3) t22)) */ explain (hints on, costs off) select count(*) from t1, t2, t3, t4, t5 where a2=a3 and a2=a4 and a2=a5;

-- Turn on flag to force failure on bad hint and try query. Should get an error.
set pg_hint_plan.yb_bad_hint_mode to error;
/*+ Leading(((((t5 t4) t1) t3) t22)) */ explain (hints on, costs off) select count(*) from t1, t2, t3, t4, t5 where a2=a3 and a2=a4 and a2=a5;

-- Turn on replanning flag and try query again.
set pg_hint_plan.yb_bad_hint_mode to replan;
/*+ Leading(((((t5 t4) t1) t3) t22)) */ explain (hints on, costs off) select count(*) from t1, t2, t3, t4, t5 where a2=a3 and a2=a4 and a2=a5;
set pg_hint_plan.yb_bad_hint_mode to error;

-- Try Insert statement. Should give an error since 't1' is used for the target relation.
/*+ Leading((((t3 t1) t2) dt)) */ explain (hints on, costs off) insert into t1 select t1.* from t1, t2, t3, (select b4 from t4, t5 dt where a4=a5 group by b4, b5) dt where t1.a1=t2.a2 and a1=a3 and a1=b4;

-- Delete statement with wrong relation 't1_3' in the hint. Should give error.
/*+ Leading((((t3 t1_1) t2) dt)) Leading((t4 t1_3)) */ explain (hints on, costs off) delete from t1 where a1 not in (select b1 from t1, t2, t3, (select b4 from t4, t5 t1 where a4=a5 group by b4, b5) dt where t1.a1=t2.a2 and a1=a3 and a1=b4);

-- Should fail because alias is not used.
/*+ IndexScan(t1 t1_a1_asc_b1_asc_idx) */ explain (hints on, costs off) delete from t1 as x where a1=5;

-- Try to join subqueries first. This is illegal because they are semijoined so should see warnings.
set pg_hint_plan.yb_bad_hint_mode to warn;
/*+ Leading((((ANY_subquery ANY_subquery_1) t1) t2)) */ explain (hints on, costs off, uids on) select count(*) from t1, t2 where a1=a2 and b1 in (select a4 from t4 group by a4, b4) and b2 in (select a5 from t5 group by a5, b5);

set pg_hint_plan.yb_bad_hint_mode to replan;
-- Not OK since forcing t1 and t2 to be directly joined. Should generate warnings and replan.
/*+ Leading(((t1 t2) t3)) noNestLoop(t1 t2) */ explain (hints on, costs off, uids on) select max(a1) from t1 join t2 on a1<a2 join t3 on a1=a3;

set pg_hint_plan.yb_bad_hint_mode to warn;
-- Should warn since planner will not try joining t2-t3.
/*+ noNestLoop(t1 t2) noNestLoop(t1 t3) */ explain (costs off, uids on) select max(a1) from t1 join t2 on a1<a2 join t3 on a1>a3;

drop schema yb_hints cascade;

-- Test fix for incorrect pruning of joins.
drop schema if exists yb26670 cascade;
create schema yb26670;
set search_path to yb26670;

create table t0(c0 int4range , c1 BIT VARYING(40) );
create table t1(c0 DECIMAL );
create table t2(c0 bytea , c1 REAL );
create table t3(c0 inet , c1 int4range ) WITHOUT OIDS ;
create table t4(c0 TEXT );
create temporary view v6(c0) AS (SELECT '132.63.53.50' FROM t2*, t1*, t3, t4*, t0* WHERE lower_inf(((((t0.c0)*(t3.c1)))+(((t3.c1)+(t3.c1))))) LIMIT 2444285747789238479);

-- Can't generate hints here because the final plan has a join replaced by a RESULT node.
explain (hints on) SELECT MAX((0.6002056)::MONEY) FROM t1*, ONLY t0, ONLY v6 LEFT OUTER JOIN t4* ON TRUE RIGHT OUTER JOIN t2* ON FALSE GROUP BY - (+ (strpos(t4.c0, v6.c0)));
SELECT MAX((0.6002056)::MONEY) FROM t1*, ONLY t0, ONLY v6 LEFT OUTER JOIN t4* ON TRUE RIGHT OUTER JOIN t2* ON FALSE GROUP BY - (+ (strpos(t4.c0, v6.c0)));

drop schema yb26670 cascade;

\set ECHO none
