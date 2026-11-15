--
-- Batched nested loop joins that must not be built
--
-- Each block forces YbBatchedNL on a join that cannot be batched, or leaves
-- the join free where its comment says why, and expects no YB Batched Nested
-- Loop at that join.  Where a block has a control, the control is a join that
-- can be batched, hinted the same way as far as it can be, and expects one.
-- The rows of the result queries are asserted too.  Which join method replaces
-- the batched one is not part of the assertion.
--
-- Every statement names its scans in hints, and its join methods unless its
-- block says otherwise; no session setting enables or disables a join method.
-- The shared tables come from yb.orig.join_batching_setup; a block whose keys
-- have a type of their own creates its tables, and no table is dropped.
-- The negatives turn off Material and Memoize, whose placement over the
-- replacing nested loop differs between PostgreSQL versions.
--

SET yb_bnl_batch_size = 3;
SET yb_prefer_bnl = off; -- disable pre-BNL-hint compatibility mode

-- The inner key is an expression, t1.a - 1, which the index on t1.a cannot
-- take a batch for, so the join is not batched.  The control batches the
-- same join when only the outer key is an expression (#14069).
/*+ Leading((t2 t1)) YbBatchedNL(t1 t2) SeqScan(t2) IndexScan(t1 p1_pkey) */
EXPLAIN (COSTS OFF) SELECT * FROM p1 t1 JOIN p2 t2 ON t1.a = t2.a + 1 WHERE t1.a <= 100 AND t2.a <= 100;
/*+ Leading((t2 t1)) YbBatchedNL(t1 t2) SeqScan(t2) IndexScan(t1 p1_pkey) Set(enable_material off) Set(enable_memoize off) */
EXPLAIN (COSTS OFF) SELECT * FROM p1 t1 JOIN p2 t2 ON t1.a - 1 = t2.a + 1 WHERE t1.a <= 100 AND t2.a <= 100;
/*+ Leading((t2 t1)) YbBatchedNL(t1 t2) SeqScan(t2) IndexScan(t1 p1_pkey) Set(enable_material off) Set(enable_memoize off) */
SELECT * FROM p1 t1 JOIN p2 t2 ON t1.a - 1 = t2.a + 1 WHERE t1.a <= 100 AND t2.a <= 100 ORDER BY t1.a, t2.a;

-- A join clause that the inner index scan can only apply as a filter keeps the
-- join from being batched (#15496).  Scanning p1 by p1_b_idx makes p2.a = p1.b
-- the index condition and p1.a = p2.b a filter on p1; the control scans the
-- primary key, where both clauses are index conditions.
/*+ Leading((p2 p1)) YbBatchedNL(p1 p2) SeqScan(p2) IndexScan(p1 p1_pkey) */
EXPLAIN (COSTS OFF) SELECT * FROM p1 JOIN p2 ON p1.a = p2.b AND p2.a = p1.b;
/*+ Leading((p2 p1)) YbBatchedNL(p1 p2) SeqScan(p2) IndexScan(p1 p1_b_idx) Set(enable_material off) Set(enable_memoize off) */
EXPLAIN (COSTS OFF) SELECT * FROM p1 JOIN p2 ON p1.a = p2.b AND p2.a = p1.b;
/*+ Leading(((p2 p3) p1)) YbBatchedNL(p2 p3) YbBatchedNL(p1 p2 p3) SeqScan(p2) IndexScan(p3 p3_pkey) IndexScan(p1 p1_b_idx) Set(enable_material off) Set(enable_memoize off) */
EXPLAIN (COSTS OFF) SELECT * FROM p1, p2, p3 where p1.a = p3.a AND p2.a = p3.a and p1.b = p2.b;

-- An inequality on the next key column, p1.b < p2.b + 1, has no batched form,
-- so with the primary key scan the join is not batched.
/*+ Leading((p2 p1)) YbBatchedNL(p1 p2) SeqScan(p2) IndexScan(p1 p1_pkey) Set(enable_material off) Set(enable_memoize off) */
EXPLAIN (COSTS OFF) SELECT * FROM p1 JOIN p2 ON p1.a = p2.b AND p1.b < p2.b + 1;

-- A batched nested loop is never a right join.  Forced in the right join's
-- orientation, the batched join is built as a left join with the sides
-- swapped, the orientation the control asks for.
/*+ Leading((t3 (t1 t2))) YbBatchedNL(t1 t2) YbBatchedNL(t1 t2 t3) SeqScan(t1) IndexScan(t2 p2_pkey) IndexScan(t3 p3_pkey) */
EXPLAIN (COSTS OFF) SELECT * FROM p3 t3 RIGHT OUTER JOIN (SELECT t1.a as a FROM p1 t1 JOIN p2 t2 ON t1.a = t2.b) s ON t3.b = s.a;
/*+ Leading(((t1 t2) t3)) YbBatchedNL(t1 t2) YbBatchedNL(t1 t2 t3) SeqScan(t1) IndexScan(t2 p2_pkey) IndexScan(t3 p3_pkey) */
EXPLAIN (COSTS OFF) SELECT * FROM p3 t3 RIGHT OUTER JOIN (SELECT t1.a as a FROM p1 t1 JOIN p2 t2 ON t1.a = t2.b) s ON t3.b = s.a;

-- A float8 inner key takes a batch of float4 outer values cast to float8.  A
-- float4 inner key cannot take a batch of float8 outer values, so with f2
-- inner the join is not batched; the control batches f2's float4 values into
-- f1's float8 key (#20715).
create table floattable1(a float8, b float4, primary key(a asc, b asc));
create table floattable2(a float4, b float8, primary key(a asc, b asc));
insert into floattable2 values (1.12312, 29401.212931231);
insert into floattable2 values (1.12312, 2941.212931231);
insert into floattable1 select * from floattable2;
/*+ Leading((f2 f1)) YbBatchedNL(f1 f2) IndexScan(f2 floattable2_pkey) IndexScan(f1 floattable1_pkey) */ explain (costs off) select * from floattable1 f1, floattable2 f2 where f1.a = f2.a;
/*+ Leading((f2 f1)) YbBatchedNL(f1 f2) IndexScan(f2 floattable2_pkey) IndexScan(f1 floattable1_pkey) */ select * from floattable1 f1, floattable2 f2 where f1.a = f2.a order by 1, 2, 3, 4;
/*+ Leading((f1 f2)) YbBatchedNL(f1 f2) IndexScan(f1 floattable1_pkey) IndexScan(f2 floattable2_pkey) Set(enable_material off) Set(enable_memoize off) */ explain (costs off) select * from floattable1 f1, floattable2 f2 where f1.a = f2.a;
/*+ Leading((f1 f2)) YbBatchedNL(f1 f2) IndexScan(f1 floattable1_pkey) IndexScan(f2 floattable2_pkey) Set(enable_material off) Set(enable_memoize off) */ select * from floattable1 f1, floattable2 f2 where f1.a = f2.a order by 1, 2, 3, 4;

-- The same with a two-column key whose second column is float4 on the inner
-- side (#20715).
create table int_float8(a int, b float8, primary key(a asc, b asc));
create table bigint_float4(a bigint, b float4, primary key(a asc, b asc));
insert into int_float8 values (1, 0.0), (1, 0.5), (3, 0.625), (2, 0.625 + POW(2, -25)), (2, 0.625);
insert into bigint_float4 values (1, 0.0), (1, 0.5), (x'8000000000000001'::bigint, 0.5), (2, 0.625);
/*+ Leading((bigint_float4 int_float8)) YbBatchedNL(int_float8 bigint_float4) IndexScan(bigint_float4 bigint_float4_pkey) IndexScan(int_float8 int_float8_pkey) */ explain (costs off) select * from int_float8, bigint_float4 where int_float8.a = bigint_float4.a and int_float8.b = bigint_float4.b;
/*+ Leading((bigint_float4 int_float8)) YbBatchedNL(int_float8 bigint_float4) IndexScan(bigint_float4 bigint_float4_pkey) IndexScan(int_float8 int_float8_pkey) */ select * from int_float8, bigint_float4 where int_float8.a = bigint_float4.a and int_float8.b = bigint_float4.b order by 1, 2, 3, 4;
/*+ Leading((int_float8 bigint_float4)) YbBatchedNL(int_float8 bigint_float4) IndexScan(int_float8 int_float8_pkey) IndexScan(bigint_float4 bigint_float4_pkey) Set(enable_material off) Set(enable_memoize off) */ explain (costs off) select * from int_float8, bigint_float4 where int_float8.a = bigint_float4.a and int_float8.b = bigint_float4.b;
/*+ Leading((int_float8 bigint_float4)) YbBatchedNL(int_float8 bigint_float4) IndexScan(int_float8 int_float8_pkey) IndexScan(bigint_float4 bigint_float4_pkey) Set(enable_material off) Set(enable_memoize off) */ select * from int_float8, bigint_float4 where int_float8.a = bigint_float4.a and int_float8.b = bigint_float4.b order by 1, 2, 3, 4;

-- A date inner key cannot take a batch of timestamp outer values, although the
-- unbatched nested loop probes it with one timestamp at a time; the control
-- batches the date values into the timestamp key (#20715).
create table ts_key(a timestamp primary key);
create table date_key(a date primary key);
insert into ts_key values (to_timestamp('27 Nov 2100 08:14:30', 'DD MON YYYY HH:MI:SS')), (to_timestamp('27 Nov 2100', 'DD MON YYYY'));
insert into date_key values (to_date('27 Nov 2100 08:14:30', 'DD MON YYYY')), (to_date('27 Feb 2010 08:14:30', 'DD MON YYYY'));
/*+ Leading((date_key ts_key)) YbBatchedNL(ts_key date_key) SeqScan(date_key) IndexScan(ts_key ts_key_pkey) */ explain (costs off) select * from ts_key, date_key where ts_key.a = date_key.a;
/*+ Leading((date_key ts_key)) YbBatchedNL(ts_key date_key) SeqScan(date_key) IndexScan(ts_key ts_key_pkey) */ select * from ts_key, date_key where ts_key.a = date_key.a;
/*+ Leading((ts_key date_key)) YbBatchedNL(ts_key date_key) SeqScan(ts_key) IndexScan(date_key date_key_pkey) Set(enable_material off) Set(enable_memoize off) */ explain (costs off) select * from ts_key, date_key where ts_key.a = date_key.a;
/*+ Leading((ts_key date_key)) YbBatchedNL(ts_key date_key) SeqScan(ts_key) IndexScan(date_key date_key_pkey) Set(enable_material off) Set(enable_memoize off) */ select * from ts_key, date_key where ts_key.a = date_key.a;

-- A clause of a left join's ON condition that the nullable side can apply but
-- that has no batched form keeps the join from being batched (#21366).  The
-- control has the same clause in WHERE, where the batched join applies it to
-- its output as a filter.
/*+ Leading((t1 t2)) YbBatchedNL(t1 t2) SeqScan(t1) IndexScan(t2 rk1_pkey) */ explain (costs off) select * FROM h t1 left outer join rk1 t2 on t1.a = t2.a where (t1.a + 1 > t2.a or t1.a = 1) order by t1.a;
/*+ Leading((t1 t2)) YbBatchedNL(t1 t2) SeqScan(t1) IndexScan(t2 rk1_pkey) Set(enable_material off) Set(enable_memoize off) */ explain (costs off) select * FROM h t1 left outer join rk1 t2 on t1.a = t2.a and (t1.a + 1 > t2.a or t1.a = 1) order by t1.a;
/*+ Leading((t1 t2)) YbBatchedNL(t1 t2) SeqScan(t1) IndexScan(t2 rk1_pkey) Set(enable_material off) Set(enable_memoize off) */ select * FROM h t1 left outer join rk1 t2 on t1.a = t2.a and (t1.a + 1 > t2.a or t1.a = 1) order by t1.a;

-- q3.a = q2.a + q1.a is batched only by a join that has both q1 and q2 on its
-- outer side (#17150).  Joined as (q1 (q2 q3)), neither join can batch it;
-- the control joins q1 and q2 first, and the batched join probes q3 with
-- their cross product.
/*+ Leading((q1 (q2 q3))) YbBatchedNL(q2 q3) YbBatchedNL(q1 q2 q3) SeqScan(q1) SeqScan(q2) IndexScan(q3 rk1_pkey) Set(enable_material off) Set(enable_memoize off) */ explain (costs off) select * from h q1, h q2, rk1 q3 where q3.a = q2.a + q1.a;
/*+ Leading(((q1 q2) q3)) NestLoop(q1 q2) YbBatchedNL(q1 q2 q3) SeqScan(q1) SeqScan(q2) IndexScan(q3 rk1_pkey) Set(enable_material off) */ explain (costs off) select * from h q1, h q2, rk1 q3 where q3.a = q2.a + q1.a;

-- (ss2 ss3) has a clause, ss3.a <> ss2.a, that the inner index scan can only
-- apply as a filter, so that join is not batched, while the join above it
-- batches ss1 into the same scan (#21292).  The inner join has no method
-- hint and hash and merge joins are off, so the planner picks its nested
-- loop; forcing YbBatchedNL on it leaves only disabled paths there and loses
-- the batched join above.
/*+ Leading((ss1 (ss2 ss3))) YbBatchedNL(ss1 ss2 ss3) SeqScan(ss1) SeqScan(ss2) IndexScan(ss3 rk2_pkey) Set(enable_hashjoin off) Set(enable_mergejoin off) Set(enable_material off) Set(enable_memoize off) */ explain (costs off) select * from h ss1, h ss2, rk2 ss3 where ss3.a = ss2.a and ss3.b = ss1.a and ss3.a <> ss2.a;
/*+ Leading((ss1 (ss2 ss3))) YbBatchedNL(ss1 ss2 ss3) SeqScan(ss1) SeqScan(ss2) IndexScan(ss3 rk2_pkey) Set(enable_hashjoin off) Set(enable_mergejoin off) Set(enable_material off) Set(enable_memoize off) */ select * from h ss1, h ss2, rk2 ss3 where ss3.a = ss2.a and ss3.b = ss1.a and ss3.a <> ss2.a;

-- p3.a = p1.a + p2.a can be batched only by a join with p1 and p2 on its outer
-- side.  Forced as (p1 (p2 p3)), neither join is batched, whether the inner
-- join is forced to batch or to be a nested loop; the unbatched nested loops
-- follow as the baseline, and the control batches ((p1 p2) p3)
-- (#19642, #19946).
/*+ YbBatchedNL(p1 p2 p3) YbBatchedNL(p2 p3) Leading((p1 (p2 p3))) SeqScan(p1) IndexScan(p2 p2_pkey) IndexScan(p3 p3_pkey) Set(enable_material off) Set(enable_memoize off) */ explain (costs off) select * from p1, p2, p3 where p1.a + 1 = p2.a and p3.a = p1.a + p2.a;
/*+ YbBatchedNL(p1 p2 p3) NestLoop(p2 p3) Leading((p1 (p2 p3))) SeqScan(p1) IndexScan(p2 p2_pkey) IndexScan(p3 p3_pkey) Set(enable_material off) Set(enable_memoize off) */ explain (costs off) select * from p1, p2, p3 where p1.a + 1 = p2.a and p3.a = p1.a + p2.a;
/*+ NestLoop(p1 p2 p3) NestLoop(p2 p3) Leading((p1 (p2 p3))) SeqScan(p1) IndexScan(p2 p2_pkey) IndexScan(p3 p3_pkey) Set(enable_material off) Set(enable_memoize off) */ explain (costs off) select * from p1, p2, p3 where p1.a + 1 = p2.a and p3.a = p1.a + p2.a;
/*+ YbBatchedNL(p1 p2 p3) NestLoop(p1 p2) Leading(((p1 p2) p3)) SeqScan(p1) IndexScan(p2 p2_pkey) IndexScan(p3 p3_pkey) */ explain (costs off) select * from p1, p2, p3 where p1.a + 1 = p2.a and p3.a = p1.a + p2.a;

-- A temporary inner relation is not batched; a temporary outer relation is
-- (#28864).
CREATE TABLE reg_bnl (a int, b int, PRIMARY KEY(a ASC));
INSERT INTO reg_bnl SELECT i, i % 25 FROM generate_series(1, 100) i;
CREATE INDEX reg_bnl_b_idx ON reg_bnl(b ASC);
CREATE TEMP TABLE tmp_bnl (a int, b int);
INSERT INTO tmp_bnl SELECT i, i % 25 FROM generate_series(1, 100) i;
CREATE INDEX tmp_bnl_b_idx ON tmp_bnl(b ASC);
ANALYZE reg_bnl;
ANALYZE tmp_bnl;
/*+ Leading((reg_bnl tmp_bnl)) YbBatchedNL(reg_bnl tmp_bnl) IndexScan(reg_bnl reg_bnl_pkey) IndexScan(tmp_bnl tmp_bnl_b_idx) Set(enable_material off) Set(enable_memoize off) */
EXPLAIN (COSTS OFF) SELECT * FROM reg_bnl JOIN tmp_bnl ON reg_bnl.b = tmp_bnl.b WHERE reg_bnl.a <= 50;
/*+ Leading((tmp_bnl reg_bnl)) YbBatchedNL(tmp_bnl reg_bnl) SeqScan(tmp_bnl) IndexScan(reg_bnl reg_bnl_b_idx) */
EXPLAIN (COSTS OFF) SELECT * FROM tmp_bnl JOIN reg_bnl ON tmp_bnl.b = reg_bnl.b WHERE tmp_bnl.a <= 50;
