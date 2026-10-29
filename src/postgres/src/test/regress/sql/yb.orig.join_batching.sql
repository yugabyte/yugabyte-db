--
-- Batched nested loop joins that must be built and must run correctly
--
-- The prelude forces the batched join the pre-hint way: every other join
-- method off, yb_prefer_bnl on; the tests at the end of the file switch to
-- the cost model and the YbBatchedNL hints.  The tables shared with
-- yb.orig.join_batching_negative come from yb.orig.join_batching_setup; a
-- test whose data carries a key type, a physical layout or a property of
-- its own creates a table of its own, and no table is dropped.
--

-- Force BNL
SET enable_hashjoin = off;
SET enable_mergejoin = off;
SET enable_seqscan = off;
SET enable_material = off;
SET yb_prefer_bnl = on;
SET enable_nestloop = off;

SET yb_bnl_batch_size = 3;

EXPLAIN (COSTS OFF) SELECT * FROM p1 t1 JOIN p2 t2 ON t1.a = t2.a WHERE t1.a <= 100 AND t2.a <= 100;
SELECT * FROM p1 t1 JOIN p2 t2 ON t1.a = t2.a WHERE t1.a <= 100 AND t2.a <= 100;

EXPLAIN (COSTS OFF) SELECT * FROM p1 t1 JOIN p2 t2 ON t1.a = t2.a + 1 WHERE t1.a <= 100 AND t2.a <= 100;
SELECT * FROM p1 t1 JOIN p2 t2 ON t1.a = t2.a + 1 WHERE t1.a <= 100 AND t2.a <= 100;

-- Batching on compound clauses
/*+ Leading((p2 p1)) */ EXPLAIN (ANALYZE, SUMMARY OFF, TIMING OFF, COSTS OFF) SELECT * FROM p1 JOIN p2 ON p1.a = p2.b AND p2.a = p1.b;
/*+ Leading((p2 p1)) */ SELECT * FROM p1 JOIN p2 ON p1.a = p2.b AND p2.a = p1.b;

explain (costs off) select * from p1 left join p5 on p1.a - 1 = p5.a and p1.b - 1 = p5.b where p1.a <= 30;
select * from p1 left join p5 on p1.a - 1 = p5.a and p1.b - 1 = p5.b where p1.a <= 30;

/*+IndexScan(p5 p5_hash)*/explain (costs off) select * from p1 left join p5 on p1.a - 1 = p5.a and p1.b - 1 = p5.b where p1.a <= 30;
/*+IndexScan(p5 p5_hash)*/ select * from p1 left join p5 on p1.a - 1 = p5.a and p1.b - 1 = p5.b where p1.a <= 30;

/*+IndexScan(p5 p5_hash_asc)*/explain (costs off) select * from p1 left join p5 on p1.a - 1 = p5.a and p1.b - 1 = p5.b where p1.a <= 30;
/*+IndexScan(p5 p5_hash_asc)*/ select * from p1 left join p5 on p1.a - 1 = p5.a and p1.b - 1 = p5.b where p1.a <= 30;

/*+ Leading((p2 p1)) */ EXPLAIN (COSTS OFF) SELECT * FROM p1 JOIN p2 ON p1.a = p2.b AND p2.a = p1.b AND p1.a = p2.a;

/*+ Leading((p2 p1)) */ SELECT * FROM p1 JOIN p2 ON p1.a = p2.b AND p2.a = p1.b AND p1.a = p2.a;

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

CREATE TABLE strtable(a varchar(26), b varchar(23), primary key(a, b));
CREATE TABLE strtable2(a varchar(26), b varchar(23), primary key(a, b));
INSERT INTO strtable VALUES ('123', 'abc'), ('1234', 'abcd'), ('123', 'pqr');
INSERT INTO strtable2 VALUES ('123', 'abc'), ('123', 'abcd'), ('123', 'pqr');

EXPLAIN (COSTS OFF) SELECT * FROM strtable, strtable2 WHERE strtable.a = strtable2.a;
SELECT * FROM strtable, strtable2 WHERE strtable.a = strtable2.a;

EXPLAIN (COSTS OFF) SELECT * FROM strtable, strtable2 WHERE strtable.a = strtable2.a AND strtable.b = strtable2.b;
SELECT * FROM strtable, strtable2 WHERE strtable.a = strtable2.a AND strtable.b = strtable2.b;

create table dp_key (a double precision, b double precision, primary key (a, b));
insert into dp_key values (12.34, 99.99), (12.345, 99.99);
create table dec_key (a decimal(6, 2), b decimal(6, 2), primary key (a, b));
insert into dec_key values (12.34, 99.99), (12.345, 99.99);
explain (costs off) select * from dp_key, dec_key where dp_key.a = dec_key.a and dp_key.b = dec_key.b;
select * from dp_key, dec_key where dp_key.a = dec_key.a and dp_key.b = dec_key.b;

create table char_key (a char(6), b char(6), primary key (a, b));
insert into char_key values ('abc', 'def'), ('xyz', 'uvw');
create table vchar_key (a varchar(6), b varchar(6), primary key (a, b));
insert into vchar_key values ('abc  ', 'def  '), ('xyz', 'uvw');
explain (costs off) select * from char_key, vchar_key where char_key.a = vchar_key.a and char_key.b = vchar_key.b;
select * from char_key, vchar_key where char_key.a = vchar_key.a and char_key.b = vchar_key.b;

explain (costs off) select * from dp_key, dec_key where dp_key.a::decimal = dec_key.a and dp_key.b::decimal = dec_key.b;
select * from dp_key, dec_key where dp_key.a::decimal = dec_key.a and dp_key.b::decimal = dec_key.b;
explain (costs off) select * from dp_key, dec_key where dp_key.a = dec_key.a::decimal and dp_key.b = dec_key.b::decimal;
select * from dp_key, dec_key where dp_key.a = dec_key.a::decimal and dp_key.b = dec_key.b::decimal;
explain (costs off) select * from char_key, vchar_key where char_key.a::text = vchar_key.a and char_key.b::text = vchar_key.b;
select * from char_key, vchar_key where char_key.a::text = vchar_key.a and char_key.b::text = vchar_key.b;
explain (costs off) select * from char_key, vchar_key where char_key.a = vchar_key.a::text and char_key.b = vchar_key.b::text;
select * from char_key, vchar_key where char_key.a = vchar_key.a::text and char_key.b = vchar_key.b::text;

-- Expected: the varchar keys of vchar_key are batched into char_key's bpchar key.
explain (costs off) /*+ Leading((vchar_key char_key)) YbBatchedNL(char_key vchar_key) IndexScan(char_key) Set(yb_prefer_bnl off) */select * from char_key, vchar_key where char_key.a = vchar_key.a and char_key.b = vchar_key.b;
/*+ Leading((vchar_key char_key)) YbBatchedNL(char_key vchar_key) IndexScan(char_key) Set(yb_prefer_bnl off) */select * from char_key, vchar_key where char_key.a = vchar_key.a and char_key.b = vchar_key.b;

explain (costs off) /*+ Leading((char_key vchar_key)) IndexScan(vchar_key) */select * from char_key, vchar_key where char_key.a::text = vchar_key.a and char_key.b::text = vchar_key.b;
/*+ Leading((char_key vchar_key)) IndexScan(vchar_key) */select * from char_key, vchar_key where char_key.a::text = vchar_key.a and char_key.b::text = vchar_key.b;

-- Three batched joins nest in the hinted order, each taking its own outer's
-- keys: the planner restores the set of relations available for batching
-- after each join level, so the join of d1 above (d2 (d3 d4)) still
-- batches (#15878).
/*+Leading(((d2 (d3 d4)) d1))*/ explain (costs off) select * from p1 d1, p2 d2, p3 d3, p4 d4 where d1.a = d3.a and d2.a = d3.a and d4.a = d2.a;

create table uuid_key (
    id   uuid NOT NULL,
    num  int4 NOT NULL,
    PRIMARY KEY ((id)HASH, num DESC)
);
insert into uuid_key(id, num) VALUES
('774cee8f-f0e9-4c46-8f3d-3b5e7db8b839'::uuid, 1);

/*+ Set(yb_bnl_batch_size 3) */ explain (costs off)
select * from uuid_key t1
join uuid_key t2 on (t1.id = t2.id and t1.num = t2.num)
where t1.id in (
	'774cee8f-f0e9-4c46-8f3d-3b5e7db8b839'::uuid,
	'884cee8f-f0e9-4c46-8f3d-3b5e7db8b847'::uuid);

/*+ Set(yb_bnl_batch_size 3) */
select * from uuid_key t1
join uuid_key t2 on (t1.id = t2.id and t1.num = t2.num)
where t1.id in (
	'774cee8f-f0e9-4c46-8f3d-3b5e7db8b839'::uuid,
	'884cee8f-f0e9-4c46-8f3d-3b5e7db8b847'::uuid);

EXPLAIN (COSTS OFF) SELECT * FROM p3 t3 LEFT OUTER JOIN (SELECT t1.a as a FROM p1 t1 JOIN p2 t2 ON t1.a = t2.b WHERE t1.a <= 100 AND t2.a <= 100) s ON t3.a = s.a WHERE t3.a <= 30;
SELECT * FROM p3 t3 LEFT OUTER JOIN (SELECT t1.a as a FROM p1 t1 JOIN p2 t2 ON t1.a = t2.b WHERE t1.a <= 100 AND t2.a <= 100) s ON t3.a = s.a WHERE t3.a <= 30;

EXPLAIN (COSTS OFF) SELECT * FROM p3 t3 RIGHT OUTER JOIN (SELECT t1.a as a FROM p1 t1 JOIN p2 t2 ON t1.a = t2.b WHERE t1.b <= 10 AND t2.b <= 15) s ON t3.a = s.a;
SELECT * FROM p3 t3 RIGHT OUTER JOIN (SELECT t1.a as a FROM p1 t1 JOIN p2 t2 ON t1.a = t2.b WHERE t1.b <= 10 AND t2.b <= 15) s ON t3.a = s.a;

-- Anti joins
EXPLAIN (COSTS OFF) SELECT * FROM p1 t1 WHERE NOT EXISTS (SELECT 1 FROM p2 t2 WHERE t1.a = t2.a) AND t1.a <= 40;
SELECT * FROM p1 t1 WHERE NOT EXISTS (SELECT 1 FROM p2 t2 WHERE t1.a = t2.a) AND t1.a <= 40;

EXPLAIN (COSTS OFF) SELECT * FROM p1 t1 WHERE NOT EXISTS (SELECT 1 FROM p2 t2 WHERE t1.a = t2.b) AND t1.a <= 40;
SELECT * FROM p1 t1 WHERE NOT EXISTS (SELECT 1 FROM p2 t2 WHERE t1.a = t2.b) AND t1.a <= 40;

-- Semi joins
EXPLAIN (COSTS OFF) SELECT * FROM p1 t1 WHERE EXISTS (SELECT 1 FROM p2 t2 WHERE t1.a = t2.a) AND t1.a <= 40;
SELECT * FROM p1 t1 WHERE EXISTS (SELECT 1 FROM p2 t2 WHERE t1.a = t2.a) AND t1.a <= 40;

EXPLAIN (COSTS OFF) SELECT * FROM p1 t1 WHERE EXISTS (SELECT 1 FROM p2 t2 WHERE t1.a = t2.b) AND t1.a <= 40;
SELECT * FROM p1 t1 WHERE EXISTS (SELECT 1 FROM p2 t2 WHERE t1.a = t2.b) AND t1.a <= 40;

CREATE TABLE int2type (a int2, PRIMARY KEY(a ASC));
INSERT INTO int2type VALUES (1), (4), (555), (-33), (6923);

-- testing batching on join conditions across different types (int2, int4)
/*+Leading((i2 p))*/ EXPLAIN (COSTS OFF) SELECT * FROM int2type i2 JOIN p1 p ON i2.a = p.a;
/*+Leading((i2 p))*/ SELECT * FROM int2type i2 JOIN p1 p ON i2.a = p.a;
/*+Leading((p i2))*/ EXPLAIN (COSTS OFF) SELECT * FROM int2type i2 JOIN p1 p ON i2.a = p.a;
/*+Leading((p i2))*/ SELECT * FROM int2type i2 JOIN p1 p ON i2.a = p.a;

create table bigint_int(a bigint, b int, primary key(b asc, a asc));
create table int_bigint(a int, b bigint, primary key(a asc, b asc));
-- 0x8000000000000001 should not join with 1 and 0x8000000000000002 should not join with 2
insert into bigint_int values (1,2), (3,4), (x'8000000000000001'::bigint, 1);
insert into int_bigint values (1,2), (3,4), (1,1), (1, x'8000000000000002'::bigint);
explain (costs off) /*+Leading((bigint_int int_bigint))*/ select * from bigint_int, int_bigint where bigint_int.a = int_bigint.a and bigint_int.b = int_bigint.b;
/*+Leading((bigint_int int_bigint))*/ select * from bigint_int, int_bigint where bigint_int.a = int_bigint.a and bigint_int.b = int_bigint.b;
explain (costs off) /*+Leading((int_bigint bigint_int))*/ select * from bigint_int, int_bigint where bigint_int.a = int_bigint.a and bigint_int.b = int_bigint.b;
/*+Leading((int_bigint bigint_int))*/ select * from bigint_int, int_bigint where bigint_int.a = int_bigint.a and bigint_int.b = int_bigint.b;
explain (costs off) /*+Leading((int_bigint bigint_int))*/ select * from bigint_int, int_bigint where bigint_int.a = int_bigint.a and bigint_int.b = int_bigint.b and bigint_int.b < 10;
/*+Leading((int_bigint bigint_int))*/ select * from bigint_int, int_bigint where bigint_int.a = int_bigint.a and bigint_int.b = int_bigint.b and bigint_int.b < 10;

create table vc_int4(a varchar(16), b int4);
insert into vc_int4 values ('abc', 123), ('abc', 124);
insert into vc_int4 values ('xyz', 123), ('xyz', 124);
insert into vc_int4 values ('ijk', 123), ('xyz', 456);
create table vc_int8(a varchar(16), b int8);
create index vc_int8_ind on vc_int8(a asc, b asc);
insert into vc_int8 values ('abc', 123), ('abc', 124);
insert into vc_int8 values ('xyz', 123), ('xyz', 124);
/*+YbBatchedNL(vc_int4 vc_int8) Leading((vc_int4 vc_int8)) IndexOnlyScan(vc_int8 vc_int8_ind)*/ explain (costs off) select * from vc_int4, vc_int8 where vc_int4.a = vc_int8.a and vc_int4.b = vc_int8.b and vc_int8.b <= 100000 order by 1,2;
/*+YbBatchedNL(vc_int4 vc_int8) Leading((vc_int4 vc_int8)) IndexOnlyScan(vc_int8 vc_int8_ind)*/ select * from vc_int4, vc_int8 where vc_int4.a = vc_int8.a and vc_int4.b = vc_int8.b and vc_int8.b <= 100000 order by 1,2;

create table nf_inner(a int, b text, primary key(a asc, b asc));
create table nf_outer(c1 int, c2 text);
create index nf_outer_c2_idx on nf_outer(c2 asc NULLS FIRST);
insert into nf_inner values (1, 'abcd');
insert into nf_outer values (1, NULL), (1, 'abcd');
/*+Leading((nf_outer nf_inner)) IndexScan(nf_outer nf_outer_c2_idx)*/ explain (costs off) select * from nf_outer, nf_inner where nf_outer.c1 = nf_inner.a and nf_outer.c2 = nf_inner.b and nf_inner.a <= 4;
/*+Leading((nf_outer nf_inner)) IndexScan(nf_outer nf_outer_c2_idx)*/ select * from nf_outer, nf_inner where nf_outer.c1 = nf_inner.a and nf_outer.c2 = nf_inner.b and nf_inner.a <= 4;

-- A left join whose WHERE clause has an OR over columns of both sides: the
-- clause can be neither batched nor pushed to the inner scan, so the batched
-- join applies it as its filter instead of being given up (#21366).
explain (costs off) select * FROM h t1 left outer join rk1 t2 on t1.a = t2.a where t1.a = 1 or t2.a = 2 order by t1.a;

select * FROM h t1 left outer join rk1 t2 on t1.a = t2.a where t1.a = 1 or t2.a = 2 order by t1.a;

explain (costs off) select * FROM h t1 left outer join rk1 t2 on t1.a = t2.a where t1.a >= t2.a or t1.a = 1 order by t1.a;

select * FROM h t1 left outer join rk1 t2 on t1.a = t2.a where t1.a >= t2.a or t1.a = 1 order by t1.a;

set yb_bnl_batch_size to 10;
explain (costs off) select * from p1 a join p2 b on a.a = b.a join p3 c on b.a = c.a join p4 d on a.b = d.b where a.b = 10 ORDER BY a.a, b.a, c.a, d.a;
select * from p1 a join p2 b on a.a = b.a join p3 c on b.a = c.a join p4 d on a.b = d.b where a.b = 10 ORDER BY a.a, b.a, c.a, d.a;

-- expression-based index
/*+ Set(yb_enable_cbo on) Leading((p1 p2)) YbBatchedNL(p1 p2) */
EXPLAIN (COSTS OFF)
SELECT * FROM p1 JOIN p2 ON p1.a = p2.b + 123 and p1.b >= 22;

/*+ Set(yb_enable_cbo on) Leading((p1 p2)) YbBatchedNL(p1 p2) */
SELECT * FROM p1 JOIN p2 ON p1.a = p2.b + 123 and p1.b >= 22;

-------------------------------------------------------------------------
-- #34069: t1b.b + t1a.b must not become a batched Index Cond on t2b while
-- two different joins batch t1a and t1b.  Expanding it with one batch index
-- for both batches probes only their diagonal, so the count drops below the
-- unbatched baseline.  Two relations in one batched expression stay legal
-- when a single join batches both; the #17150 and #19642 controls of
-- yb.orig.join_batching_negative cover that.
-- Expected: the top join does not batch t1b, so t1b.b + t1a.b is applied as
-- its join filter instead of a batched Index Cond on t2b, and the count
-- matches the unbatched baseline.
-- Both p1 aliases are capped at the range p5.a covers to keep the unbatched
-- baseline from rescanning 300x300 outer pairs.
-------------------------------------------------------------------------
/*+
  Set(enable_hashjoin off)
  Set(enable_mergejoin off)
  Set(enable_material off)
  Set(enable_memoize off)
  Set(enable_nestloop on)
  Set(enable_seqscan on)
  Set(yb_bnl_batch_size 3)
  Leading((t1b (t1a (t2b t2a))))
  SeqScan(t1a)
  SeqScan(t1b)
  IndexScan(t2a p5_pkey)
  IndexScan(t2b p5_pkey)
  YbBatchedNL(t1a t1b t2a t2b)
  YbBatchedNL(t1a t2a t2b)
  YbBatchedNL(t2a t2b)
*/
EXPLAIN (COSTS OFF)
SELECT COUNT(*) FROM p1 t1a, p1 t1b, p5 t2a, p5 t2b
 WHERE t2a.a = t1a.a AND t2b.a = t2a.b AND t2a.b = t1b.b + t1a.b
   AND t1a.a < 60 AND t1b.a < 60;
/*+
  Set(enable_hashjoin off)
  Set(enable_mergejoin off)
  Set(enable_material off)
  Set(enable_memoize off)
  Set(enable_nestloop on)
  Set(enable_seqscan on)
  Set(yb_bnl_batch_size 3)
  Leading((t1b (t1a (t2b t2a))))
  SeqScan(t1a)
  SeqScan(t1b)
  IndexScan(t2a p5_pkey)
  IndexScan(t2b p5_pkey)
  YbBatchedNL(t1a t1b t2a t2b)
  YbBatchedNL(t1a t2a t2b)
  YbBatchedNL(t2a t2b)
*/
SELECT COUNT(*) FROM p1 t1a, p1 t1b, p5 t2a, p5 t2b
 WHERE t2a.a = t1a.a AND t2b.a = t2a.b AND t2a.b = t1b.b + t1a.b
   AND t1a.a < 60 AND t1b.a < 60;

-- Baseline: same join order with batching disabled.
/*+
  Set(enable_hashjoin off)
  Set(enable_mergejoin off)
  Set(enable_material off)
  Set(enable_memoize off)
  Set(enable_nestloop on)
  Set(enable_seqscan on)
  Set(yb_bnl_batch_size 1)
  Leading((t1b (t1a (t2b t2a))))
  SeqScan(t1a)
  SeqScan(t1b)
  IndexScan(t2a p5_pkey)
  IndexScan(t2b p5_pkey)
*/
EXPLAIN (COSTS OFF)
SELECT COUNT(*) FROM p1 t1a, p1 t1b, p5 t2a, p5 t2b
 WHERE t2a.a = t1a.a AND t2b.a = t2a.b AND t2a.b = t1b.b + t1a.b
   AND t1a.a < 60 AND t1b.a < 60;
/*+
  Set(enable_hashjoin off)
  Set(enable_mergejoin off)
  Set(enable_material off)
  Set(enable_memoize off)
  Set(enable_nestloop on)
  Set(enable_seqscan on)
  Set(yb_bnl_batch_size 1)
  Leading((t1b (t1a (t2b t2a))))
  SeqScan(t1a)
  SeqScan(t1b)
  IndexScan(t2a p5_pkey)
  IndexScan(t2b p5_pkey)
*/
SELECT COUNT(*) FROM p1 t1a, p1 t1b, p5 t2a, p5 t2b
 WHERE t2a.a = t1a.a AND t2b.a = t2a.b AND t2a.b = t1b.b + t1a.b
   AND t1a.a < 60 AND t1b.a < 60;

-- The same split, where only the index condition shows the group: t2b.a's
-- equivalence class also holds the plain Var t1a.a, which the scan's movable
-- clauses pick over t1b.b + t1a.b, while the index path parameterized by t1a
-- and t1b probes the expression.  A partitioned t2b puts an Append over the
-- scans, which has to take the group over from its children.  t2b.b = t1b.b
-- keeps that path from being pruned in favor of the one parameterized by t1a
-- alone, and the cost model is on because legacy costing does not choose the
-- split here.
-- Expected: the top join does not batch t1b in either form, and both counts
-- match the unbatched baseline.
CREATE TABLE p5_part (a int, b int, c varchar, primary key(a asc, b asc))
  PARTITION BY RANGE (a);
CREATE TABLE p5_part_1 PARTITION OF p5_part FOR VALUES FROM (MINVALUE) TO (30);
CREATE TABLE p5_part_2 PARTITION OF p5_part FOR VALUES FROM (30) TO (MAXVALUE);
INSERT INTO p5_part SELECT * FROM p5;
ANALYZE p5_part;
/*+
  Set(yb_enable_cbo on)
  Set(enable_hashjoin off)
  Set(enable_mergejoin off)
  Set(enable_material off)
  Set(enable_memoize off)
  Set(enable_nestloop on)
  Set(enable_seqscan on)
  Set(yb_bnl_batch_size 3)
  Leading((t1b (t1a t2b)))
  SeqScan(t1a)
  SeqScan(t1b)
  IndexScan(t2b p5_pkey)
  YbBatchedNL(t1a t1b t2b)
  YbBatchedNL(t1a t2b)
*/
EXPLAIN (COSTS OFF)
SELECT COUNT(*) FROM p1 t1a, p1 t1b, p5 t2b
 WHERE t2b.a = t1b.b + t1a.b AND t2b.a = t1a.a AND t2b.b = t1b.b
   AND t1a.a < 60 AND t1b.a < 60;
/*+
  Set(yb_enable_cbo on)
  Set(enable_hashjoin off)
  Set(enable_mergejoin off)
  Set(enable_material off)
  Set(enable_memoize off)
  Set(enable_nestloop on)
  Set(enable_seqscan on)
  Set(yb_bnl_batch_size 3)
  Leading((t1b (t1a t2b)))
  SeqScan(t1a)
  SeqScan(t1b)
  IndexScan(t2b p5_pkey)
  YbBatchedNL(t1a t1b t2b)
  YbBatchedNL(t1a t2b)
*/
SELECT COUNT(*) FROM p1 t1a, p1 t1b, p5 t2b
 WHERE t2b.a = t1b.b + t1a.b AND t2b.a = t1a.a AND t2b.b = t1b.b
   AND t1a.a < 60 AND t1b.a < 60;
/*+
  Set(yb_enable_cbo on)
  Set(enable_hashjoin off)
  Set(enable_mergejoin off)
  Set(enable_material off)
  Set(enable_memoize off)
  Set(enable_nestloop on)
  Set(enable_seqscan on)
  Set(yb_bnl_batch_size 3)
  Leading((t1b (t1a t2b)))
  SeqScan(t1a)
  SeqScan(t1b)
  IndexScan(t2b)
  YbBatchedNL(t1a t1b t2b)
  YbBatchedNL(t1a t2b)
*/
EXPLAIN (COSTS OFF)
SELECT COUNT(*) FROM p1 t1a, p1 t1b, p5_part t2b
 WHERE t2b.a = t1b.b + t1a.b AND t2b.a = t1a.a AND t2b.b = t1b.b
   AND t1a.a < 60 AND t1b.a < 60;
/*+
  Set(yb_enable_cbo on)
  Set(enable_hashjoin off)
  Set(enable_mergejoin off)
  Set(enable_material off)
  Set(enable_memoize off)
  Set(enable_nestloop on)
  Set(enable_seqscan on)
  Set(yb_bnl_batch_size 3)
  Leading((t1b (t1a t2b)))
  SeqScan(t1a)
  SeqScan(t1b)
  IndexScan(t2b)
  YbBatchedNL(t1a t1b t2b)
  YbBatchedNL(t1a t2b)
*/
SELECT COUNT(*) FROM p1 t1a, p1 t1b, p5_part t2b
 WHERE t2b.a = t1b.b + t1a.b AND t2b.a = t1a.a AND t2b.b = t1b.b
   AND t1a.a < 60 AND t1b.a < 60;

-- Baseline: same join order with batching disabled.
/*+
  Set(yb_enable_cbo on)
  Set(enable_hashjoin off)
  Set(enable_mergejoin off)
  Set(enable_material off)
  Set(enable_memoize off)
  Set(enable_nestloop on)
  Set(enable_seqscan on)
  Set(yb_bnl_batch_size 1)
  Leading((t1b (t1a t2b)))
  SeqScan(t1a)
  SeqScan(t1b)
  IndexScan(t2b p5_pkey)
*/
SELECT COUNT(*) FROM p1 t1a, p1 t1b, p5 t2b
 WHERE t2b.a = t1b.b + t1a.b AND t2b.a = t1a.a AND t2b.b = t1b.b
   AND t1a.a < 60 AND t1b.a < 60;

-- GEQO ignores join hints, and without the check its own search picks the
-- split for the four-relation query above.
-- Expected: t1b.b + t1a.b is never probed with t1a and t1b batched by different
-- joins, and the count matches that query's unbatched baseline.
/*+
  Set(geqo_threshold 2)
  Set(enable_seqscan on)
  Set(yb_bnl_batch_size 3)
*/
EXPLAIN (COSTS OFF)
SELECT COUNT(*) FROM p1 t1a, p1 t1b, p5 t2a, p5 t2b
 WHERE t2a.a = t1a.a AND t2b.a = t2a.b AND t2a.b = t1b.b + t1a.b
   AND t1a.a < 60 AND t1b.a < 60;
/*+
  Set(geqo_threshold 2)
  Set(enable_seqscan on)
  Set(yb_bnl_batch_size 3)
*/
SELECT COUNT(*) FROM p1 t1a, p1 t1b, p5 t2a, p5 t2b
 WHERE t2a.a = t1a.a AND t2b.a = t2a.b AND t2a.b = t1b.b + t1a.b
   AND t1a.a < 60 AND t1b.a < 60;

-- Under a nested loop with s2 outer, the batched join of s1 and s3 takes a
-- batch of s1.a and the plain parameter s2.b in the same index condition:
-- s2's rows are not batched into a join whose outer, s1, cannot pass a batch
-- on (#15156).
/*+Set(enable_nestloop true) Set(enable_seqscan true) Set(yb_bnl_batch_size 3) Leading((s2 (s1 s3))) YbBatchedNL(s1 s3)*/ explain (costs off) select s3.* from p1 s1, p2 s2, p5 s3 where s3.a = s1.a and s3.b = s2.b and s1.c = s2.c order by s3.a, s3.b;
/*+Set(enable_nestloop true) Set(enable_seqscan true) Set(yb_bnl_batch_size 3) Leading((s2 (s1 s3))) YbBatchedNL(s1 s3)*/ select s3.* from p1 s1, p2 s2, p5 s3 where s3.a = s1.a and s3.b = s2.b and s1.c = s2.c order by s3.a, s3.b;

-- The batched key is a placeholder value, the NULL constant the subquery
-- supplies: the batched index condition is built from it as from a column,
-- and the right join keeps the subquery's row (#24186).
/*+Leading(r2 r1)*/
EXPLAIN (COSTS OFF)
SELECT *
FROM rk1 r1
RIGHT OUTER JOIN (
    (SELECT
        NULL::int AS nullcol
     FROM
        h
     LIMIT 1)) AS r2
ON (r1.a = r2.nullcol);

SELECT *
FROM rk1 r1
RIGHT OUTER JOIN (
    (SELECT
        NULL::int AS nullcol
     FROM
        h
     LIMIT 1)) AS r2
ON (r1.a = r2.nullcol);

-- The batched join's outer is a merge join that is exhausted inside a batch,
-- five rows at a batch size of three.  The join must not pull the outer
-- again after it has returned its last row (#15393).
explain (costs off) /*+set(yb_bnl_batch_size 3) Leading(( ( s1 s2 ) s3 )) MergeJoin(s1 s2)*/select * from rk1 s1 left outer join rk1 s2
on s1.a = s2.a left outer join rk1 s3 on s2.a = s3.a where s1.a > 35;

/*+set(yb_bnl_batch_size 3) Leading(( ( s1 s2 ) s3 )) MergeJoin(s1 s2)*/ select * from rk1 s1 left outer join rk1 s2
on s1.a = s2.a left outer join rk1 s3 on s2.a = s3.a where s1.a > 35;

SET yb_bnl_batch_size = 3;

-- Testing column groups in HybridScanChoices
create table test2 (a int, pp int, b int, pp2 int, c int, primary key(a asc, pp asc, b asc, pp2 asc, c asc));
insert into test2 values (1,0, 2,0,1), (2,0, 3,0,3), (2,0,3,0,5);
create table test1 (a int, pp int, b int, pp2 int, c int, primary key(a asc, pp asc, b asc, pp2 asc, c asc));
insert into test1 values (1,0,2,0,1), (1,0,2,0,2), (2,0,3,0,3), (2,0,4,0,4), (2,0,4,0,5), (2,0,4,0,6);
explain (costs off) select * from test1 p1 join test2 p2 on p1.a = p2.a AND p1.b = p2.b AND p1.c = p2.c;
select * from test1 p1 join test2 p2 on p1.a = p2.a AND p1.b = p2.b AND p1.c = p2.c;

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

explain (costs off) select q2.c1, q1.c1 from q1 join q2 on q1.c2 = q2.c2 order by q1.c1 limit 10;
select q2.c1, q1.c1 from q1 join q2 on q1.c2 = q2.c2 order by q1.c1 limit 10;

create index on q2(c1 asc);
create table q3d(c1 int, c2 int, c3 int, primary key(c3 desc));
insert into q3d select i, i, i from generate_series(0,999) i;

/*+Leading((q3d q2))*/explain (costs off) select q3d.c3, q2.c2 from q2, q3d where q3d.c3 = q2.c1 order by 1 desc limit 10;
/*+Leading((q3d q2))*/select q3d.c3, q2.c2 from q2, q3d where q3d.c3 = q2.c1 order by 1 desc limit 10;

drop index q2_c1_idx;
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

delete from q1;
delete from q2;
insert into q1 values
  (10, 1), (20, 2), (30, 3), (40, 4), (50, 5), (60, 6),
  (5, 10), (99, 99), (88, 88), (77, 77), (41, 99), (42, 99);
insert into q2 values
  (10, 1), (20, 2), (30, 3), (40, 4), (50, 5), (60, 6),
  (5, 10), (70, 7), (80, 8), (90, 9), (91, 11), (92, 12);
analyze q1;
analyze q2;
create table q3(a int, b int, c name, primary key(a,b));
create index q3_range on q3(a asc);
insert into q3 values
  (1, 1, 'q3row01_AAAAAAAAAAAA'), (2, 2, 'q3row02_BBBBBBBBBBBB'), (3, 3, 'q3row03_CCCCCCCCCCCC'),
  (4, 4, 'q3row04_DDDDDDDDDDDD'), (5, 5, 'q3row05_EEEEEEEEEEEE'), (6, 6, 'q3row06_FFFFFFFFFFFF'),
  (7, 7, 'q3row07_GGGGGGGGGGGG'), (8, 8, 'q3row08_HHHHHHHHHHHH'), (9, 9, 'q3row09_IIIIIIIIIIII'),
  (10, 10, 'q3row10_JJJJJJJJJJJJ'), (100, 10, 'q3row11_KKKKKKKKKKKK'), (12, 12, 'q3row12_LLLLLLLLLLLL');
analyze q3;

/*+
  Set(enable_hashjoin off)
  Set(enable_mergejoin off)
  Set(yb_bnl_batch_size 3)
  Set(enable_seqscan off)
  Set(enable_material off)
*/
explain (costs off)
select * from q1 p1 left join (select p2.c1 as a1, p3.a as a2 from q2 p2 join q3 p3 on true) j1
  on j1.a1 = p1.c1 and j1.a1 + j1.a2 = p1.c1 + p1.c2
order by p1.c1, p1.c2;
/*+
  Set(enable_hashjoin off)
  Set(enable_mergejoin off)
  Set(yb_bnl_batch_size 3)
  Set(enable_seqscan off)
  Set(enable_material off)
*/
select * from q1 p1 left join (select p2.c1 as a1, p3.a as a2 from q2 p2 join q3 p3 on true) j1
  on j1.a1 = p1.c1 and j1.a1 + j1.a2 = p1.c1 + p1.c2
order by p1.c1, p1.c2;

-- only the batchable equality (j1.a1 = p1.c1) is pushed into the inner index
-- scan, while the unbatchable clauses (j1.a2 <= p1.c1 and the expression
-- equality, which spans both inner relations) are relegated to the left join's
-- filter.  The inner index scan never mixes batched and unbatched references
-- to p1.  See #20495, #31760.  This enhancement is disabled in legacy mode for
-- plan stability.
/*+
  Set(yb_enable_cbo on)
  Set(enable_nestloop off)
  Set(enable_hashjoin off)
  Set(enable_mergejoin off)
  Set(yb_bnl_batch_size 3)
  Set(enable_seqscan off)
  Set(enable_material off)
*/
explain (costs off)
select * from
    q1 p1 left join
    (select p2.c1 as a1, p3.a as a2 from q2 p2 join q3 p3 on true) j1
      on j1.a1 = p1.c1 and j1.a2 <= p1.c1
      and j1.a1 + j1.a2 = p1.c1 + p1.c2
order by p1.c1, p1.c2;
/*+
  Set(yb_enable_cbo on)
  Set(enable_nestloop off)
  Set(enable_hashjoin off)
  Set(enable_mergejoin off)
  Set(yb_bnl_batch_size 3)
  Set(enable_seqscan off)
  Set(enable_material off)
*/
select * from
    q1 p1 left join
    (select p2.c1 as a1, p3.a as a2 from q2 p2 join q3 p3 on true) j1
      on j1.a1 = p1.c1 and j1.a2 <= p1.c1
      and j1.a1 + j1.a2 = p1.c1 + p1.c2
order by p1.c1, p1.c2;

-- An UPDATE whose join has both a batchable equality (target.c1 = src.c1)
-- and an unbatchable inequality (target.c2 < src.c2) on the same inner/outer
-- relation pair.  Under the cost-based optimizer the planner builds a BNL by
-- batching the equality into the inner index condition and relegating the
-- unbatchable inequality to the join filter; the inner index scan never mixes
-- batched and unbatched references to the same outer relation.  See #31760.
-- yb_enable_cbo is set per query because this relegation is only done under
-- the cost-based optimizer.  The target q1 is the batched inner relation and
-- q2 is the outer relation.  Wrapped in a transaction so the row changes
-- made by the actual UPDATE are rolled back.
BEGIN;
/*+
  Set(enable_nestloop off)
  Set(enable_hashjoin off)
  Set(enable_mergejoin off)
  Set(yb_bnl_batch_size 3)
  Set(enable_seqscan off)
  Set(enable_material off)
  Set(yb_enable_cbo on)
  Leading((q2 q1))
*/
explain (costs off)
update q1
   set c2 = q1.c2 + 1000
  from q2
 where q1.c1 = q2.c1 and q1.c2 < q2.c2;
/*+
  Set(enable_nestloop off)
  Set(enable_hashjoin off)
  Set(enable_mergejoin off)
  Set(yb_bnl_batch_size 3)
  Set(enable_seqscan off)
  Set(enable_material off)
  Set(yb_enable_cbo on)
  Leading((q2 q1))
*/
update q1
   set c2 = q1.c2 + 1000
  from q2
 where q1.c1 = q2.c1 and q1.c2 < q2.c2;
ROLLBACK;

/*+
  Set(enable_nestloop off)
  Set(enable_hashjoin off)
  Set(enable_mergejoin off)
  Set(yb_bnl_batch_size 3)
  Set(enable_seqscan on)
  Set(enable_material off)
  Leading((q3 (q2 q1)))
*/
explain (costs off)
select * from q1, q2, q3 where q1.c1 = q2.c1 and q3.a = q1.c2;
/*+
  Set(enable_nestloop off)
  Set(enable_hashjoin off)
  Set(enable_mergejoin off)
  Set(yb_bnl_batch_size 3)
  Set(enable_seqscan on)
  Set(enable_material off)
  Leading((q3 (q2 q1)))
*/
select * from q1, q2, q3 where q1.c1 = q2.c1 and q3.a = q1.c2
order by q1.c1, q2.c1, q3.a;

explain (costs off)
SELECT * FROM q1, q2 where pg_backend_pid() >= 0 and q1.c1 = q2.c1;

-- The batched outer value pos.i reaches n's index condition through the
-- nested loop of r and n, as the subscript of r's array in
-- r.ref_names[pos.i] (#25917).
create table names (namespace_id int, name_id smallint, name_value text);
create index on names(name_id);
create table name_refs (namespace_id int, ref_names smallint[], ref_comment text);
insert into names values
  (1, 1, 'One'), (1, 3, 'Three'), (1, 5, 'Five'), (1, 7, 'Seven'), (1, 9, 'Nine'),
  (10, 1, 'Eleven'), (10, 4, 'Fourteen'), (10, 7, 'Seventeen');
insert into name_refs values
  (1, '{3, 7, 1}', 'All found'), (1, '{9, 2}', 'Some found'), (1, '{0, 2, 8, 4}', 'None found'),
  (10, '{3, 7, 1}', 'Other namespace'), (10, '{}', 'Empty refs');
/*+ NestLoop(r n) Leading((pos (r n))) IndexScan(n) Set(yb_bnl_batch_size 1024) */
explain (analyze, costs off, timing off, summary off)
SELECT r.ref_comment, n.name_value
FROM names n, name_refs r, generate_series(1, 32) pos(i)
WHERE n.namespace_id = r.namespace_id AND n.name_id = r.ref_names[pos.i];

-- The LIMIT equals the rows the two outer rows produce, so the first batch
-- is sized to exactly the outer's rows and the outer is exhausted while the
-- batch is built; every row must still be returned (#20707).
/*+Set(enable_seqscan off) Set(yb_bnl_batch_size 1024) Leading((ss1 ss2))*/ explain (costs off) select * from rk1 ss1, rk2 ss2 where ss1.a = ss2.a and ss1.a <= 2 order by ss1.a limit 10;
/*+Set(enable_seqscan off) Set(yb_bnl_batch_size 1024) Leading((ss1 ss2))*/ select * from rk1 ss1, rk2 ss2 where ss1.a = ss2.a and ss1.a <= 2 order by ss1.a limit 10;

-- The inner index orders its columns (b, a) against join clauses on (a, b):
-- the batched index condition is built in index column order (#17115).
/*+Leading((q1 q2)) IndexScan(q2 h_ba_idx)*/ explain (costs off) select * from p1 q1, h q2 where q1.a = q2.a and q1.b = q2.b order by q1.a;
/*+Leading((q1 q2)) IndexScan(q2 h_ba_idx)*/ select * from p1 q1, h q2 where q1.a = q2.a and q1.b = q2.b order by q1.a;

create table g1(h int, r int, primary key(h hash, r asc));
create table g2(h int, r int, primary key(h hash, r asc));
create table main(h1 int, h2 int, r1 int, r2 int, primary key((h1,h2) hash, r1 asc, r2 asc));
insert into main select i/1000, (i/100) % 10, (i/10) % 10, i % 10 from generate_series(1,9999) i;
insert into g1 values (1,3), (5,7);
insert into g2 values (2,4), (6,8);

/*+Leading((g1 (g2 main))) Set(enable_hashjoin off) Set(enable_mergejoin off) Set(yb_bnl_batch_size 3) Set(enable_material off) Set(enable_seqscan on)*/ explain (costs off) select main.* from g1,g2,main where main.h1 = g1.h and main.h2 = g2.h and main.r2 = g1.r and main.r1 = g2.r;

/*+Leading((g1 (g2 main))) Set(enable_hashjoin off) Set(enable_mergejoin off) Set(yb_bnl_batch_size 3) Set(enable_material off) Set(enable_seqscan on)*/ select main.* from g1,g2,main where main.h1 = g1.h and main.h2 = g2.h and main.r2 = g1.r and main.r1 = g2.r;

create table oidtable(a oid, primary key(a asc));
create table int4table(a int4, primary key(a asc));
insert into oidtable select i from generate_series(1,20) i where i % 2 = 0;
insert into int4table select i from generate_series(1,20) i where i % 3 = 0;
-- Expected: the int4 keys of int4table are batched into oidtable's oid key.
/*+ Set(yb_bnl_batch_size 3) Set(yb_prefer_bnl off) Leading((int4table oidtable)) YbBatchedNL(int4table oidtable) SeqScan(int4table) IndexScan(oidtable oidtable_pkey) */explain (costs off) select * from oidtable, int4table where oidtable.a = int4table.a;
/*+ Set(yb_bnl_batch_size 3) Set(yb_prefer_bnl off) Leading((int4table oidtable)) YbBatchedNL(int4table oidtable) SeqScan(int4table) IndexScan(oidtable oidtable_pkey) */ select * from oidtable, int4table where oidtable.a = int4table.a;

CREATE TABLE p_inner(a int, b text, PRIMARY KEY(b asc, a asc));
CREATE TABLE p_outer(a bigint, b text);
INSERT INTO p_outer VALUES (0, '1');
INSERT INTO p_inner VALUES (0, '1');
EXPLAIN (COSTS OFF) SELECT * FROM p_outer, p_inner where p_outer.a = p_inner.a AND p_outer.b = p_inner.b;
SELECT * FROM p_outer, p_inner where p_outer.a = p_inner.a AND p_outer.b = p_inner.b;

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

-- Batch size 10 exceeds every relation's row count, so a plan that batched
-- x1.a or x2.b at the wrong nesting level could not return the right rows by
-- chance (#19642, #19946).
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

SET yb_bnl_batch_size = 3;
create table wide_vc1(a varchar(1024), b char(10));
create table wide_vc2(a varchar(1024));
create index on wide_vc2 (a asc);
explain (costs off) select * from wide_vc1 t1, wide_vc2 t2 where t1.a = t2.a and t1.b = t2.a;

-- The join clause on ss3.v runs a subquery that needs ss1, the batched outer,
-- and ss2, the relation joined above.  It is not pushed into the batched
-- index scan on ss3, where ss2's value would be missing: it stays in the
-- filter of the join above, and the join of ss1 and ss3 is still batched
-- (#21878).
/*+Set(enable_hashjoin OFF) Set(enable_mergejoin OFF) Set(enable_material OFF) Leading((ss2 (ss1 ss3)))
*/EXPLAIN (COSTS OFF) SELECT *
FROM h ss1, h ss2, rk2 ss3
WHERE ss1.a = ss3.a and ss3.v <= (SELECT a FROM rk2 ss3s where a < ss2.a + ss1.a limit 1) and ss3.b <= ss2.a;

/*+Set(enable_hashjoin OFF) Set(enable_mergejoin OFF) Set(enable_material OFF) Leading((ss2 (ss1 ss3)))
*/SELECT *
FROM h ss1, h ss2, rk2 ss3
WHERE ss1.a = ss3.a and ss3.v <= (SELECT a FROM rk2 ss3s where a < ss2.a + ss1.a limit 1) and ss3.b <= ss2.a;

-- The batched key is a subquery.  The join's hash table computes its keys in
-- an expression context of its own, as a hash join does, so running the
-- subplan for a key does not disturb the per-tuple context, and every outer
-- row finds its inner rows (#21266).
/*+Set(enable_hashjoin OFF) Set(enable_mergejoin OFF) Set(enable_material OFF) */EXPLAIN (COSTS OFF)
SELECT *
FROM (
    SELECT ss1.a as a, other.a as othera
    FROM h ss1
    CROSS JOIN h other
    WHERE ss1.a <= 2 AND other.a <= 2
) AS cross_join
LEFT OUTER JOIN rk2 ss2 ON ss2.a = (
    SELECT other.a
    FROM h other, h ss1
    WHERE other.a = ss1.a + cross_join.a limit 1
);

/*+Set(enable_hashjoin OFF) Set(enable_mergejoin OFF) Set(enable_material OFF) */
SELECT *
FROM (
    SELECT ss1.a as a, other.a as othera
    FROM h ss1
    CROSS JOIN h other
    WHERE ss1.a <= 2 AND other.a <= 2
) AS cross_join
LEFT OUTER JOIN rk2 ss2 ON ss2.a = (
    SELECT other.a
    FROM h other, h ss1
    WHERE other.a = ss1.a + cross_join.a limit 1
);

-- Tests for EXPLAIN output of BNL
-- These tests ensure we don't resolve fieldnames for batched expressions in Index Cond
CREATE FUNCTION public.dummy(OUT a integer, OUT b integer)
RETURNS SETOF record
LANGUAGE sql
IMMUTABLE PARALLEL SAFE STRICT
AS 'SELECT 1, 1';
EXPLAIN (COSTS OFF) SELECT 1 FROM pg_type t, (SELECT dummy() as x) AS ss WHERE t.oid = (ss.x).a;

/*+Set(enable_hashjoin off) Set(enable_mergejoin off) Set(yb_bnl_batch_size 3) Set(enable_material off)*/ EXPLAIN (COSTS OFF) SELECT 1 FROM rk2 tbl, (SELECT dummy() as x) AS ss, (SELECT dummy() as x2) AS ss2 WHERE tbl.a = (ss.x).a AND tbl.b = (ss2.x2).a;

/*+Set(enable_hashjoin off) Set(enable_mergejoin off) Set(yb_bnl_batch_size 3) Set(enable_material off) NestLoop(tbl tbl2) YbBatchedNL(ss tbl tbl2) IndexScan(tbl) SeqScan(tbl2)*/
EXPLAIN (COSTS OFF) SELECT 1 FROM (SELECT dummy() as x) AS ss LEFT JOIN (SELECT tbl.a AS c1 FROM rk2 tbl, rk2 tbl2) j1 ON j1.c1 = (ss.x).a;

/*+Set(enable_mergejoin off) Set(enable_hashjoin off) Set(yb_bnl_batch_size 3) Set(enable_material off) YbBatchedNL(tbl2 ss tbl) NestLoop(ss tbl) IndexScan(tbl)*/
EXPLAIN (COSTS OFF) SELECT 1 FROM (SELECT dummy() as x) AS ss, rk2 tbl, rk2 tbl2 WHERE tbl2.a = (ss.x).a AND (ss.x).a < 40 AND tbl.a = ANY(ARRAY[(ss.x).a, (ss.x).a]);

-- The batch holds the same key, a = 1, twice with different b, and the
-- filter p.b <= ss2.b + 1 refers to the second outer relation: the local
-- lookup that matches an inner row to the outer rows of its batch compares
-- only the hashed part of the join clause, so both outer rows with the key
-- get their inner rows (#20531).
create table lk_outer1 (a int, b int);
create table lk_outer2 (a int, b int);
create table lk_inner (a int, b int);
create index on lk_inner (a asc);
insert into lk_outer1 values (1,1), (1,2);
insert into lk_outer2 values (1,1), (1,2);
insert into lk_inner values (1,1), (1,3);

explain (costs off) /*+Set(enable_hashjoin off)
Set(enable_material off)
Set(enable_mergejoin off)
Set(yb_bnl_batch_size 1024)
NestLoop(ss1 ss2) Rows(ss1 ss2 #1024)*/select ss1.*, p.* from lk_outer1 ss1, lk_outer2 ss2, lk_inner p where ss1.a = ss2.a and ss1.b = ss2.b and p.b <= ss2.b + 1 and p.a = ss1.a order by 1,2,3,4;

/*+Set(enable_hashjoin off)
Set(enable_material off)
Set(enable_mergejoin off)
Set(yb_bnl_batch_size 1024)
NestLoop(ss1 ss2) Rows(ss1 ss2 #1024)*/select ss1.*, p.* from lk_outer1 ss1, lk_outer2 ss2, lk_inner p where ss1.a = ss2.a and ss1.b = ss2.b and p.b <= ss2.b + 1 and p.a = ss1.a order by 1,2,3,4;

-- The outer is a UNION ALL whose arms deliver tuples in different formats, a
-- table's heap tuples and the sort's minimal tuples: the batched join reads
-- both into its batch (#25651).
explain (costs off)
select * from (select a as k, b as v from p3 union all select a, a from rk1) s join p1 it0 on s.k = it0.a where s.k <= 20 order by s.k;
select * from (select a as k, b as v from p3 union all select a, a from rk1) s join p1 it0 on s.k = it0.a where s.k <= 20 order by s.k;

-- No Memoize is placed under a batched join, whether the key has few
-- distinct values (x) or one (y); under a plain nested loop the same joins
-- get one (#25251).
create table memo_outer (pk int, x int, y int, primary key (pk asc));
insert into memo_outer select i, i % 3, 2 from generate_series(1, 10) i;
analyze memo_outer;

explain (costs off)
/*+ Leading((s r)) */
select * from memo_outer s join p1 r on r.b = s.x;

explain (costs off)
/*+ Leading((s r)) */
select * from memo_outer s join p1 r on r.b = s.y;

-- Memoize under NL
explain (costs off)
/*+ Leading((s r)) NoYbBatchedNL(s r) */
select * from memo_outer s join p1 r on r.b = s.x;

explain (costs off)
/*+ Leading((s r)) NoYbBatchedNL(s r) */
select * from memo_outer s join p1 r on r.b = s.y;

-- Test BNL join with IN + inequality on the same column.
SET yb_bnl_batch_size = 1024;
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF)
/*+ YbBatchedNL(o i) Leading((o i)) */
SELECT * FROM rk1 o JOIN rk2 i ON o.a = i.a WHERE i.a >= 3;
-- test yb_enable_advanced_index_cond_fold flag off
SET yb_enable_advanced_index_cond_fold = off;
EXPLAIN (ANALYZE, DIST, SUMMARY OFF, TIMING OFF, COSTS OFF)
/*+ YbBatchedNL(o i) Leading((o i)) */
SELECT * FROM rk1 o JOIN rk2 i ON o.a = i.a WHERE i.a >= 3;
RESET yb_enable_advanced_index_cond_fold;

-------------------------------------------------------------------------
-- #31724: canonical BNL + correlated SubPlan on Join Filter
-------------------------------------------------------------------------
SET yb_bnl_batch_size = 10;

CREATE TABLE repro31724 (
    pk        INT PRIMARY KEY,
    c_int     INT,
    c_int_key INT,
    c_vc      VARCHAR(64),
    c_vc_key  VARCHAR(64)
);

INSERT INTO repro31724 (pk, c_int, c_int_key, c_vc, c_vc_key) VALUES
    ( 1, NULL,         -33, '',                     'dfk'                 ),
    ( 2,   56,           4, '3',                    'k'                   ),
    ( 3, -124,         -68, 'u',                    'osycoknpaxpunhsenncn'),
    ( 4,    4, NULL,        '10',                   ''                    ),
    ( 5,  -94,           4, ' ',                    'dot'                 ),
    ( 6,   34,          92, '3',                    ''                    ),
    ( 7, 436011008,      3, NULL,                   '-116'                ),
    ( 8,   62,           5, '0',                    'kn'                  ),
    ( 9,    1,   270401536, ' ',                    'w'                   ),
    (10, NULL,           0, '-123',                 NULL                  ),
    (11,    8,  -876609536, 'xp',                   'paxpunhsenncnbhbmkcb'),
    (12, NULL,           1, 'p',                    '8'                   ),
    (13,  -89, NULL,        'museum',               ' '                   ),
    (14, NULL, NULL,        'p',                    '-99'                 ),
    (15,   59, NULL,        '8',                    'l'                   ),
    (16,    2, NULL,        '',                     'nhse'                ),
    (17,    6,  2092236800, NULL,                   's'                   ),
    (18,  -26,           1, ' ',                    NULL                  ),
    (19,    1,          53, '3',                    'perhaps'             ),
    (20, 1088552960,   -63, 'm',                    'balanced'            ),
    (21, NULL,          84, NULL,                   '4'                   ),
    (22,   34,           6, 'nncnbhbmkcbezrzxzgng', ' '                   ),
    (23,    1, NULL,        NULL,                   'h'                   ),
    (24,   20,         -18, 'artificial',           'o'                   ),
    (25,    2,           4, '1',                    ''                    ),
    (26,    7,          -6, 'e',                    'j'                   ),
    (27,    5,  1993211904, 'r',                    NULL                  ),
    (28, 651231232,      3, '125',                  '0'                   ),
    (29,    3,           3, NULL,                   'transport'           ),
    (30,   52, NULL,        'u',                    'booth'               ),
    (31,    6,         -15, 'v',                    ''                    ),
    (32,    44,  1104740352, NULL,                   ''                    );

CREATE INDEX repro31724_c_int_key_idx ON repro31724 (c_int_key);
CREATE INDEX repro31724_c_vc_key_idx ON repro31724 (c_vc_key);
ANALYZE repro31724;

-- No SubPlan.
EXPLAIN (COSTS OFF)
/*+ Leading((t2 (t1 t3))) IndexScan(t3 repro31724_c_int_key_idx) YbBatchedNL(t1 t3) */
SELECT COUNT(*)
FROM repro31724 AS t1, repro31724 AS t2, repro31724 AS t3
WHERE t3.c_vc_key  = t2.c_vc
  AND t3.c_int_key = t2.c_int + t1.pk;

/*+ Leading((t2 (t1 t3))) IndexScan(t3 repro31724_c_int_key_idx) YbBatchedNL(t1 t3) */
SELECT COUNT(*)
FROM repro31724 AS t1, repro31724 AS t2, repro31724 AS t3
WHERE t3.c_vc_key  = t2.c_vc
  AND t3.c_int_key = t2.c_int + t1.pk;

-- Baseline with batching disabled.
/*+ Leading((t2 (t1 t3))) IndexScan(t3 repro31724_c_int_key_idx) NoYbBatchedNL(t1 t3) */
SELECT COUNT(*)
FROM repro31724 AS t1, repro31724 AS t2, repro31724 AS t3
WHERE t3.c_vc_key  = t2.c_vc
  AND t3.c_int_key = t2.c_int + t1.pk;

-- Force the issue plan shape: SubPlan on BNL Join Filter with batched
-- c_int_key lookup. Correct COUNT is 1; unfixed builds may return 4.
EXPLAIN (COSTS OFF)
/*+ Leading((t2 (t1 t3))) IndexScan(t3 repro31724_c_int_key_idx) YbBatchedNL(t1 t3) */
SELECT COUNT(*)
FROM repro31724 AS t1, repro31724 AS t2, repro31724 AS t3
WHERE t3.c_vc_key  = t2.c_vc
  AND t3.c_int_key = t2.c_int + t1.pk
  AND t2.c_int IN (
      SELECT s.c_int FROM repro31724 AS s WHERE s.c_vc = t1.c_vc
  );

/*+ Leading((t2 (t1 t3))) IndexScan(t3 repro31724_c_int_key_idx) YbBatchedNL(t1 t3) */
SELECT COUNT(*)
FROM repro31724 AS t1, repro31724 AS t2, repro31724 AS t3
WHERE t3.c_vc_key  = t2.c_vc
  AND t3.c_int_key = t2.c_int + t1.pk
  AND t2.c_int IN (
      SELECT s.c_int FROM repro31724 AS s WHERE s.c_vc = t1.c_vc
  );

-- The same with BNL hashing off, where the tuplestore strategy re-applies the
-- join clauses.
SET yb_bnl_enable_hashing = off;

/*+ Leading((t2 (t1 t3))) IndexScan(t3 repro31724_c_int_key_idx) YbBatchedNL(t1 t3) */
SELECT COUNT(*)
FROM repro31724 AS t1, repro31724 AS t2, repro31724 AS t3
WHERE t3.c_vc_key  = t2.c_vc
  AND t3.c_int_key = t2.c_int + t1.pk;

/*+ Leading((t2 (t1 t3))) IndexScan(t3 repro31724_c_int_key_idx) YbBatchedNL(t1 t3) */
SELECT COUNT(*)
FROM repro31724 AS t1, repro31724 AS t2, repro31724 AS t3
WHERE t3.c_vc_key  = t2.c_vc
  AND t3.c_int_key = t2.c_int + t1.pk
  AND t2.c_int IN (
      SELECT s.c_int FROM repro31724 AS s WHERE s.c_vc = t1.c_vc
  );

RESET yb_bnl_enable_hashing;

-- Test BNL on a table with a large number of tablets
CREATE TABLE t_multi_tablet(h1 bigint, h2 bigint, v text, primary key((h1, h2) hash)) split into 48 tablets;
INSERT INTO t_multi_tablet SELECT i, j, 'Value(' || i::text || ', ' || j::text || ')' FROM generate_series(1, 100) i, generate_series(1, 100) j;

SET yb_bnl_batch_size = 1024;

EXPLAIN (ANALYZE, DIST, COSTS OFF, TIMING OFF, SUMMARY OFF)
SELECT t.* FROM generate_series(1, 1000) i, t_multi_tablet t WHERE h1 = 1 AND h2 = i;
EXPLAIN (ANALYZE, DIST, COSTS OFF, TIMING OFF, SUMMARY OFF)
SELECT t.* FROM generate_series(1, 100) i, generate_series(1, 100) j, t_multi_tablet t WHERE h1 = i AND h2 = j;

-- First batch sizing under a pushed-down LIMIT.  EXPLAIN DEBUG shows the
-- outer rows the planner sized the first batch to (First Batch Size) and the
-- size the executor used (Actual First Batch Size); they differ only when the
-- LIMIT was not known at plan time.  The DEBUG fields print only with the
-- non-deterministic-field guard off; the storage counters, estimates and
-- sort memory are masked, so the plan shape and the batch sizes are the
-- assertion.
CREATE FUNCTION explain_first_batch(query text) RETURNS SETOF text
LANGUAGE plpgsql AS $$
DECLARE
    line text;
BEGIN
    FOR line IN EXECUTE 'EXPLAIN (ANALYZE, DIST, DEBUG, COSTS OFF, SUMMARY OFF, TIMING OFF) ' || query
    LOOP
        CONTINUE WHEN line ~ '^\s*Metric ';
        RETURN NEXT regexp_replace(line, '^(\s*(Storage|Estimated|Sort Method)[^:]*: ).*$', '\1#');
    END LOOP;
END;
$$;

CREATE TABLE fb_outer (k int, v int, PRIMARY KEY (k ASC));
CREATE TABLE fb_inner (k int, s int, PRIMARY KEY (k ASC, s ASC));
-- four inner rows per outer row
INSERT INTO fb_outer SELECT i, i FROM generate_series(1, 1000) i;
INSERT INTO fb_inner SELECT i, j FROM generate_series(1, 1000) i, generate_series(1, 4) j;
ANALYZE fb_outer, fb_inner;

SET yb_enable_base_scans_cost_model = on;
SET yb_explain_hide_non_deterministic_fields = off;
SET yb_bnl_batch_size = 1024;

-- no LIMIT: no first batch sizing
SELECT explain_first_batch('/*+ YbBatchedNL(o i) */ SELECT o.k, i.s FROM fb_outer o JOIN fb_inner i ON i.k = o.v');
-- LIMIT 10 at four rows per outer row: 3 outer rows sized and used
SELECT explain_first_batch('/*+ YbBatchedNL(o i) */ SELECT o.k, i.s FROM fb_outer o JOIN fb_inner i ON i.k = o.v LIMIT 10');
-- OFFSET counts toward the rows the LIMIT needs: 30 rows, 8 outer rows
SELECT explain_first_batch('/*+ YbBatchedNL(o i) */ SELECT o.k, i.s FROM fb_outer o JOIN fb_inner i ON i.k = o.v LIMIT 10 OFFSET 20');
-- sorted BNL: the ordered outer lets the LIMIT stop after the first batch
SELECT explain_first_batch('/*+ YbBatchedNL(o i) */ SELECT o.k, i.s FROM fb_outer o JOIN fb_inner i ON i.k = o.v ORDER BY o.k LIMIT 10');
-- a Sort between the LIMIT and the join: the planner sizes the first batch,
-- the executor cannot trim it
SELECT explain_first_batch('/*+ YbBatchedNL(o i) */ SELECT o.k, i.s FROM fb_outer o JOIN fb_inner i ON i.k = o.v ORDER BY i.s, o.k LIMIT 10');
-- the estimate needs more than a full batch for the LIMIT: the planner does
-- not size the first batch, the executor still trims it to the LIMIT count
SELECT explain_first_batch('/*+ YbBatchedNL(o i) */ SELECT o.k, i.s FROM fb_outer o JOIN fb_inner i ON i.k = o.v AND i.s = 4 AND i.k % 500 = 0 LIMIT 10');

-- parameterized LIMIT and OFFSET: a custom plan sees the values and sizes the
-- first batch; a generic plan cannot, so only the executor's trim shows
PREPARE fb_limit(int) AS /*+ YbBatchedNL(o i) */ SELECT o.k, i.s FROM fb_outer o JOIN fb_inner i ON i.k = o.v LIMIT $1;
PREPARE fb_offset(int) AS /*+ YbBatchedNL(o i) */ SELECT o.k, i.s FROM fb_outer o JOIN fb_inner i ON i.k = o.v LIMIT 10 OFFSET $1;
SET plan_cache_mode = force_custom_plan;
SELECT explain_first_batch('EXECUTE fb_limit(10)');
SELECT explain_first_batch('EXECUTE fb_offset(20)');
SET plan_cache_mode = force_generic_plan;
SELECT explain_first_batch('EXECUTE fb_limit(10)');
SELECT explain_first_batch('EXECUTE fb_limit(40)');
SELECT explain_first_batch('EXECUTE fb_offset(20)');
RESET plan_cache_mode;
DEALLOCATE fb_limit;
DEALLOCATE fb_offset;

-- without the first batch optimization the planner does not size the first
-- batch; the executor still trims it to the LIMIT count
SET yb_bnl_optimize_first_batch = off;
SELECT explain_first_batch('/*+ YbBatchedNL(o i) */ SELECT o.k, i.s FROM fb_outer o JOIN fb_inner i ON i.k = o.v LIMIT 10');
RESET yb_bnl_optimize_first_batch;

RESET yb_explain_hide_non_deterministic_fields;
RESET yb_enable_base_scans_cost_model;
DROP FUNCTION explain_first_batch(text);

-------------------------------------------------------------------------
-- #33788: a join clause that references the batched outer relation, the
-- inner relation and a third relation that only supplies a scalar parameter
-- to the inner index scan: w3.v < w1.b + w2.c, with w1 batched into the w3
-- index condition and w2 joined above the batched join.  The clause has no
-- batched form, so it is withheld from the inner scan and must be applied by
-- the batched nested loop join right above, where w1's values are available
-- per tuple.  Without it, every row satisfying the other two conditions is
-- returned (36 instead of 19).  Baselines with batching disabled and with a
-- hash join produce the same rows.
-------------------------------------------------------------------------
CREATE TABLE w1 (a INT PRIMARY KEY, b INT);
CREATE TABLE w2 (j INT PRIMARY KEY, c INT);
CREATE TABLE w3 (k INT, j INT, v INT, PRIMARY KEY (k ASC, j ASC));
CREATE TABLE w4 (j INT PRIMARY KEY, c INT);
INSERT INTO w1 SELECT g, g % 3 FROM generate_series(1, 8) g;
INSERT INTO w2 SELECT g, g % 2 FROM generate_series(1, 8) g;
INSERT INTO w3 SELECT g, g, 1 FROM generate_series(1, 8) g;
INSERT INTO w4 SELECT g, g % 2 FROM generate_series(1, 8) g;
ANALYZE w1, w2, w3, w4;

-- The scalar parameter (w2) comes from the join directly above the batched
-- join.
/*+
  Set(yb_enable_cbo on)
  Set(enable_hashjoin off)
  Set(enable_mergejoin off)
  Set(enable_material off)
  Set(yb_bnl_batch_size 3)
  Set(enable_nestloop on)
  Set(enable_seqscan on)
  Leading((w2 (w1 w3)))
  IndexScan(w3)
  YbBatchedNL(w1 w3)
*/
EXPLAIN (COSTS OFF)
SELECT w1.a, w2.j FROM w1, w2, w3
 WHERE w3.k = w1.a AND w3.j <= w2.j AND w3.v < w1.b + w2.c
 ORDER BY 1, 2;
/*+
  Set(yb_enable_cbo on)
  Set(enable_hashjoin off)
  Set(enable_mergejoin off)
  Set(enable_material off)
  Set(yb_bnl_batch_size 3)
  Set(enable_nestloop on)
  Set(enable_seqscan on)
  Leading((w2 (w1 w3)))
  IndexScan(w3)
  YbBatchedNL(w1 w3)
*/
SELECT w1.a, w2.j FROM w1, w2, w3
 WHERE w3.k = w1.a AND w3.j <= w2.j AND w3.v < w1.b + w2.c
 ORDER BY 1, 2;

-- Baseline: same join order with batching disabled.
/*+
  Set(yb_enable_cbo on)
  Set(enable_hashjoin off)
  Set(enable_mergejoin off)
  Set(enable_material off)
  Set(yb_bnl_batch_size 1)
  Set(enable_nestloop on)
  Set(enable_seqscan on)
  Leading((w2 (w1 w3)))
  IndexScan(w3)
*/
EXPLAIN (COSTS OFF)
SELECT w1.a, w2.j FROM w1, w2, w3
 WHERE w3.k = w1.a AND w3.j <= w2.j AND w3.v < w1.b + w2.c
 ORDER BY 1, 2;
/*+
  Set(yb_enable_cbo on)
  Set(enable_hashjoin off)
  Set(enable_mergejoin off)
  Set(enable_material off)
  Set(yb_bnl_batch_size 1)
  Set(enable_nestloop on)
  Set(enable_seqscan on)
  Leading((w2 (w1 w3)))
  IndexScan(w3)
*/
SELECT w1.a, w2.j FROM w1, w2, w3
 WHERE w3.k = w1.a AND w3.j <= w2.j AND w3.v < w1.b + w2.c
 ORDER BY 1, 2;

-- Baseline: hash join between w1 and w3 under a plain nested loop for w2,
-- which has no equality condition to hash on.
/*+
  Set(yb_enable_cbo on)
  Set(enable_hashjoin on)
  Set(enable_mergejoin off)
  Set(enable_nestloop off)
  Set(enable_material off)
  Set(enable_seqscan on)
  Set(yb_bnl_batch_size 1)
  Leading((w2 (w1 w3)))
*/
EXPLAIN (COSTS OFF)
SELECT w1.a, w2.j FROM w1, w2, w3
 WHERE w3.k = w1.a AND w3.j <= w2.j AND w3.v < w1.b + w2.c
 ORDER BY 1, 2;
/*+
  Set(yb_enable_cbo on)
  Set(enable_hashjoin on)
  Set(enable_mergejoin off)
  Set(enable_nestloop off)
  Set(enable_material off)
  Set(enable_seqscan on)
  Set(yb_bnl_batch_size 1)
  Leading((w2 (w1 w3)))
*/
SELECT w1.a, w2.j FROM w1, w2, w3
 WHERE w3.k = w1.a AND w3.j <= w2.j AND w3.v < w1.b + w2.c
 ORDER BY 1, 2;

-- The scalar parameter (w2) comes from two joins above the batched join; the
-- withheld clause passes through the intermediate batched join on w4.
/*+
  Set(yb_enable_cbo on)
  Set(enable_hashjoin off)
  Set(enable_mergejoin off)
  Set(enable_material off)
  Set(yb_bnl_batch_size 3)
  Set(enable_nestloop on)
  Set(enable_seqscan on)
  Leading((w2 (w4 (w1 w3))))
  IndexScan(w3)
  YbBatchedNL(w1 w3)
*/
EXPLAIN (COSTS OFF)
SELECT COUNT(*) FROM w1, w2, w3, w4
 WHERE w3.k = w1.a AND w3.j <= w2.j AND w4.j = w1.a AND w3.v < w1.b + w2.c;
/*+
  Set(yb_enable_cbo on)
  Set(enable_hashjoin off)
  Set(enable_mergejoin off)
  Set(enable_material off)
  Set(yb_bnl_batch_size 3)
  Set(enable_nestloop on)
  Set(enable_seqscan on)
  Leading((w2 (w4 (w1 w3))))
  IndexScan(w3)
  YbBatchedNL(w1 w3)
*/
SELECT COUNT(*) FROM w1, w2, w3, w4
 WHERE w3.k = w1.a AND w3.j <= w2.j AND w4.j = w1.a AND w3.v < w1.b + w2.c;

-- The scalar parameter comes from the intermediate relation (w4) itself.
/*+
  Set(yb_enable_cbo on)
  Set(enable_hashjoin off)
  Set(enable_mergejoin off)
  Set(enable_material off)
  Set(yb_bnl_batch_size 3)
  Set(enable_nestloop on)
  Set(enable_seqscan on)
  Leading((w2 (w4 (w1 w3))))
  IndexScan(w3)
  YbBatchedNL(w1 w3)
*/
EXPLAIN (COSTS OFF)
SELECT COUNT(*) FROM w1, w2, w3, w4
 WHERE w3.k = w1.a AND w3.j <= w4.j AND w4.j = w2.j AND w3.v < w1.b + w4.c;
/*+
  Set(yb_enable_cbo on)
  Set(enable_hashjoin off)
  Set(enable_mergejoin off)
  Set(enable_material off)
  Set(yb_bnl_batch_size 3)
  Set(enable_nestloop on)
  Set(enable_seqscan on)
  Leading((w2 (w4 (w1 w3))))
  IndexScan(w3)
  YbBatchedNL(w1 w3)
*/
SELECT COUNT(*) FROM w1, w2, w3, w4
 WHERE w3.k = w1.a AND w3.j <= w4.j AND w4.j = w2.j AND w3.v < w1.b + w4.c;

-- Batched nested loop over a partitioned inner relation under the cost
-- model, forced the pre-hint way (#25078): each scanned partition takes the
-- batch, the partitions outside p.i <= 201 are pruned, and no Memoize sits
-- under the batched join (#25251).  It keeps yb_prefer_bnl on with nested
-- loops enabled: with yb_prefer_bnl off, no batched nested loop is built over
-- a partitioned inner relation (#35384).
CREATE TABLE t_part (i int, a int, b int) PARTITION BY RANGE (i);
CREATE TABLE t_part1 PARTITION OF t_part FOR VALUES FROM (1) TO (200);
CREATE TABLE t_part2 PARTITION OF t_part FOR VALUES FROM (200) TO (400);
CREATE TABLE t_part3 PARTITION OF t_part FOR VALUES FROM (400) TO (600);
CREATE TABLE t_part4 PARTITION OF t_part FOR VALUES FROM (600) TO (MAXVALUE);

CREATE INDEX ON t_part (b);

INSERT INTO t_part SELECT (i % 200) + 1,   ((i-1) / 4) * 4, i  FROM generate_series(1, 2500) i;
INSERT INTO t_part SELECT (i % 200) + 201, ((i-1) / 4) * 4 + 1, i  FROM generate_series(1, 2500) i;
INSERT INTO t_part SELECT (i % 200) + 401, ((i-1) / 4) * 4 + 2, i  FROM generate_series(1, 2500) i;
INSERT INTO t_part SELECT (i % 200) + 601, ((i-1) / 4) * 4 + 3, i  FROM generate_series(1, 2500) i;

CREATE TABLE t_nopart (i int, a int, b int);
INSERT INTO t_nopart SELECT * FROM t_part;

ANALYZE t_part;
ANALYZE t_nopart;

/*+
  Leading((np p))
  SeqScan(np)
  IndexScan(p)
  Set(enable_nestloop on)
  Set(yb_bnl_batch_size 1024)
  Set(yb_enable_cbo on)
*/
EXPLAIN (COSTS OFF)SELECT * FROM t_nopart np JOIN t_part p ON np.a = p.b and np.b < 1000 and p.i <= 201;

-- The tests from here to the end of the file run under the cost model and
-- force the batched join with YbBatchedNL hints, which are ignored while
-- yb_prefer_bnl is on.
SET yb_enable_cbo = on;
SET yb_prefer_bnl = off;

-- #35565: a Unique outer child of BNL must not return a key twice. Unique
-- remembers its last returned tuple in its result slot, and BNL must not
-- store its own tuples there. The inner index is hash-sharded, so the
-- tuple BNL stores last differs from the one Unique returned last. The
-- DISTINCT join must count every row of bnl_uniq exactly once (45).
CREATE TABLE bnl_uniq (k1 int, k2 int, v int PRIMARY KEY);
CREATE INDEX ON bnl_uniq (k1 HASH, k2 ASC);
INSERT INTO bnl_uniq SELECT i, i % 3, i FROM generate_series(1, 30) i;
INSERT INTO bnl_uniq SELECT i, i % 3, i + 100
  FROM generate_series(1, 30) i WHERE i % 2 = 0;
/*+ YbBatchedNL(t d) Set(yb_bnl_batch_size 3) Set(enable_hashagg off)
    Set(yb_enable_distinct_pushdown off) */
EXPLAIN (COSTS OFF)
SELECT count(*) FROM bnl_uniq t
  JOIN (SELECT DISTINCT k1, k2 FROM bnl_uniq) d
    ON t.k1 = d.k1 AND t.k2 = d.k2;
/*+ YbBatchedNL(t d) Set(yb_bnl_batch_size 3) Set(enable_hashagg off)
    Set(yb_enable_distinct_pushdown off) */
SELECT count(*) FROM bnl_uniq t
  JOIN (SELECT DISTINCT k1, k2 FROM bnl_uniq) d
    ON t.k1 = d.k1 AND t.k2 = d.k2;

-- #35565: the same rule on BNL's other writes into its outer slot: the
-- tuplestore batch (hashing off) and the left-join pass that returns
-- unmatched outer rows. With t.v > 100 only the 15 even keys match one row
-- each and the 15 odd keys come back unmatched, so the left joins must
-- return 30; the inner join must still return 45. Hashing is turned off by
-- a session SET because the executor reads it after a hint's Set() has
-- been reverted.
/*+ YbBatchedNL(t d) Set(yb_bnl_batch_size 3) Set(enable_hashagg off)
    Set(yb_enable_distinct_pushdown off) */
EXPLAIN (COSTS OFF)
SELECT count(*) FROM (SELECT DISTINCT k1, k2 FROM bnl_uniq) d
  LEFT JOIN bnl_uniq t ON t.k1 = d.k1 AND t.k2 = d.k2 AND t.v > 100;
/*+ YbBatchedNL(t d) Set(yb_bnl_batch_size 3) Set(enable_hashagg off)
    Set(yb_enable_distinct_pushdown off) */
SELECT count(*) FROM (SELECT DISTINCT k1, k2 FROM bnl_uniq) d
  LEFT JOIN bnl_uniq t ON t.k1 = d.k1 AND t.k2 = d.k2 AND t.v > 100;
SET yb_bnl_enable_hashing = off;
/*+ YbBatchedNL(t d) Set(yb_bnl_batch_size 3) Set(enable_hashagg off)
    Set(yb_enable_distinct_pushdown off) */
EXPLAIN (COSTS OFF)
SELECT count(*) FROM bnl_uniq t
  JOIN (SELECT DISTINCT k1, k2 FROM bnl_uniq) d
    ON t.k1 = d.k1 AND t.k2 = d.k2;
/*+ YbBatchedNL(t d) Set(yb_bnl_batch_size 3) Set(enable_hashagg off)
    Set(yb_enable_distinct_pushdown off) */
SELECT count(*) FROM bnl_uniq t
  JOIN (SELECT DISTINCT k1, k2 FROM bnl_uniq) d
    ON t.k1 = d.k1 AND t.k2 = d.k2;
/*+ YbBatchedNL(t d) Set(yb_bnl_batch_size 3) Set(enable_hashagg off)
    Set(yb_enable_distinct_pushdown off) */
SELECT count(*) FROM (SELECT DISTINCT k1, k2 FROM bnl_uniq) d
  LEFT JOIN bnl_uniq t ON t.k1 = d.k1 AND t.k2 = d.k2 AND t.v > 100;
RESET yb_bnl_enable_hashing;

-- #35565: the planner builds a Unique on its own, so the bug needs no
-- DISTINCT in the query. Each query must count every row of bnl_uniq once
-- (45): an IN subquery uniquified as the outer of the join, a UNION, and a
-- DISTINCT ON.
/*+ Leading((d t)) YbBatchedNL(d t) Set(yb_bnl_batch_size 3)
    Set(enable_hashagg off) */
EXPLAIN (COSTS OFF)
SELECT count(*) FROM bnl_uniq t WHERE t.k1 IN (SELECT d.k1 FROM bnl_uniq d);
/*+ Leading((d t)) YbBatchedNL(d t) Set(yb_bnl_batch_size 3)
    Set(enable_hashagg off) */
SELECT count(*) FROM bnl_uniq t WHERE t.k1 IN (SELECT d.k1 FROM bnl_uniq d);
SET yb_bnl_enable_hashing = off;
/*+ Leading((d t)) YbBatchedNL(d t) Set(yb_bnl_batch_size 3)
    Set(enable_hashagg off) */
SELECT count(*) FROM bnl_uniq t WHERE t.k1 IN (SELECT d.k1 FROM bnl_uniq d);
RESET yb_bnl_enable_hashing;
/*+ YbBatchedNL(t d) Set(yb_bnl_batch_size 3) Set(enable_hashagg off) */
EXPLAIN (COSTS OFF)
SELECT count(*) FROM (SELECT k1 FROM bnl_uniq UNION SELECT k2 FROM bnl_uniq) d
  JOIN bnl_uniq t ON t.k1 = d.k1;
/*+ YbBatchedNL(t d) Set(yb_bnl_batch_size 3) Set(enable_hashagg off) */
SELECT count(*) FROM (SELECT k1 FROM bnl_uniq UNION SELECT k2 FROM bnl_uniq) d
  JOIN bnl_uniq t ON t.k1 = d.k1;
SET yb_bnl_enable_hashing = off;
/*+ YbBatchedNL(t d) Set(yb_bnl_batch_size 3) Set(enable_hashagg off) */
SELECT count(*) FROM (SELECT k1 FROM bnl_uniq UNION SELECT k2 FROM bnl_uniq) d
  JOIN bnl_uniq t ON t.k1 = d.k1;
RESET yb_bnl_enable_hashing;
/*+ YbBatchedNL(t d) Set(yb_bnl_batch_size 3) Set(enable_hashagg off)
    Set(yb_enable_distinct_pushdown off) */
EXPLAIN (COSTS OFF)
SELECT count(*)
  FROM (SELECT DISTINCT ON (k1) k1, v FROM bnl_uniq ORDER BY k1, v) d
  JOIN bnl_uniq t ON t.k1 = d.k1;
/*+ YbBatchedNL(t d) Set(yb_bnl_batch_size 3) Set(enable_hashagg off)
    Set(yb_enable_distinct_pushdown off) */
SELECT count(*)
  FROM (SELECT DISTINCT ON (k1) k1, v FROM bnl_uniq ORDER BY k1, v) d
  JOIN bnl_uniq t ON t.k1 = d.k1;
SET yb_bnl_enable_hashing = off;
/*+ YbBatchedNL(t d) Set(yb_bnl_batch_size 3) Set(enable_hashagg off)
    Set(yb_enable_distinct_pushdown off) */
SELECT count(*)
  FROM (SELECT DISTINCT ON (k1) k1, v FROM bnl_uniq ORDER BY k1, v) d
  JOIN bnl_uniq t ON t.k1 = d.k1;
RESET yb_bnl_enable_hashing;

-- #35565: semi and anti joins return outer rows through the same slot on
-- their single-match and unmatched-outer passes. 15 even keys match a row
-- with v > 100, so the semi join must return 15; with the extra
-- k1 % 4 = 0 filter only 7 keys match, so the anti join must return 23.
/*+ YbBatchedNL(t d) Set(yb_bnl_batch_size 3) Set(enable_hashagg off)
    Set(yb_enable_distinct_pushdown off) */
EXPLAIN (COSTS OFF)
SELECT count(*) FROM (SELECT DISTINCT k1, k2 FROM bnl_uniq) d
  WHERE EXISTS (SELECT 1 FROM bnl_uniq t
                WHERE t.k1 = d.k1 AND t.k2 = d.k2 AND t.v > 100);
/*+ YbBatchedNL(t d) Set(yb_bnl_batch_size 3) Set(enable_hashagg off)
    Set(yb_enable_distinct_pushdown off) */
SELECT count(*) FROM (SELECT DISTINCT k1, k2 FROM bnl_uniq) d
  WHERE EXISTS (SELECT 1 FROM bnl_uniq t
                WHERE t.k1 = d.k1 AND t.k2 = d.k2 AND t.v > 100);
SET yb_bnl_enable_hashing = off;
/*+ YbBatchedNL(t d) Set(yb_bnl_batch_size 3) Set(enable_hashagg off)
    Set(yb_enable_distinct_pushdown off) */
SELECT count(*) FROM (SELECT DISTINCT k1, k2 FROM bnl_uniq) d
  WHERE EXISTS (SELECT 1 FROM bnl_uniq t
                WHERE t.k1 = d.k1 AND t.k2 = d.k2 AND t.v > 100);
RESET yb_bnl_enable_hashing;
/*+ YbBatchedNL(t d) Set(yb_bnl_batch_size 3) Set(enable_hashagg off)
    Set(yb_enable_distinct_pushdown off) */
EXPLAIN (COSTS OFF)
SELECT count(*) FROM (SELECT DISTINCT k1, k2 FROM bnl_uniq) d
  WHERE NOT EXISTS (SELECT 1 FROM bnl_uniq t
                    WHERE t.k1 = d.k1 AND t.k2 = d.k2 AND t.v > 100
                      AND t.k1 % 4 = 0);
/*+ YbBatchedNL(t d) Set(yb_bnl_batch_size 3) Set(enable_hashagg off)
    Set(yb_enable_distinct_pushdown off) */
SELECT count(*) FROM (SELECT DISTINCT k1, k2 FROM bnl_uniq) d
  WHERE NOT EXISTS (SELECT 1 FROM bnl_uniq t
                    WHERE t.k1 = d.k1 AND t.k2 = d.k2 AND t.v > 100
                      AND t.k1 % 4 = 0);
SET yb_bnl_enable_hashing = off;
/*+ YbBatchedNL(t d) Set(yb_bnl_batch_size 3) Set(enable_hashagg off)
    Set(yb_enable_distinct_pushdown off) */
SELECT count(*) FROM (SELECT DISTINCT k1, k2 FROM bnl_uniq) d
  WHERE NOT EXISTS (SELECT 1 FROM bnl_uniq t
                    WHERE t.k1 = d.k1 AND t.k2 = d.k2 AND t.v > 100
                      AND t.k1 % 4 = 0);
RESET yb_bnl_enable_hashing;

-- #35565: a SetOp with ALL returns its result slot once per copy of a row.
-- Batch size 2 puts a group's second copy after the batch boundary, so
-- each join must return 75. Up to PostgreSQL 17 a projecting Subquery
-- Scan sits between BNL and the SetOp, so BNL never reads the SetOp's
-- slot; from PostgreSQL 18 on, BNL reads it directly.
/*+ YbBatchedNL(t d) Set(yb_bnl_batch_size 2) Set(enable_hashagg off) */
EXPLAIN (COSTS OFF)
SELECT count(*) FROM (SELECT k1 FROM bnl_uniq
                      EXCEPT ALL SELECT k1 FROM bnl_uniq WHERE v > 200) d
  JOIN bnl_uniq t ON t.k1 = d.k1;
/*+ YbBatchedNL(t d) Set(yb_bnl_batch_size 2) Set(enable_hashagg off) */
SELECT count(*) FROM (SELECT k1 FROM bnl_uniq
                      EXCEPT ALL SELECT k1 FROM bnl_uniq WHERE v > 200) d
  JOIN bnl_uniq t ON t.k1 = d.k1;
/*+ YbBatchedNL(t d) Set(yb_bnl_batch_size 2) */
EXPLAIN (COSTS OFF)
SELECT count(*) FROM (SELECT k1 FROM bnl_uniq
                      INTERSECT ALL SELECT k1 FROM bnl_uniq) d
  JOIN bnl_uniq t ON t.k1 = d.k1;
/*+ YbBatchedNL(t d) Set(yb_bnl_batch_size 2) */
SELECT count(*) FROM (SELECT k1 FROM bnl_uniq
                      INTERSECT ALL SELECT k1 FROM bnl_uniq) d
  JOIN bnl_uniq t ON t.k1 = d.k1;
SET yb_bnl_enable_hashing = off;
/*+ YbBatchedNL(t d) Set(yb_bnl_batch_size 2) Set(enable_hashagg off) */
SELECT count(*) FROM (SELECT k1 FROM bnl_uniq
                      EXCEPT ALL SELECT k1 FROM bnl_uniq WHERE v > 200) d
  JOIN bnl_uniq t ON t.k1 = d.k1;
/*+ YbBatchedNL(t d) Set(yb_bnl_batch_size 2) */
SELECT count(*) FROM (SELECT k1 FROM bnl_uniq
                      INTERSECT ALL SELECT k1 FROM bnl_uniq) d
  JOIN bnl_uniq t ON t.k1 = d.k1;
RESET yb_bnl_enable_hashing;
