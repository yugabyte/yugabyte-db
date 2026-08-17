--
-- BNL with partitioned tables
--
-- Derived from yb.port.partition_join by forcing batched nested loop join.
--
-- BNL batches into an Append over the partitions: create_append_path
-- accumulates the batching requirement of the subpaths, so the whole Append
-- becomes a batched inner.  That beats a partitionwise join on cost for every
-- shape below, so none of these plans is partitionwise even where the join
-- keys would allow it.  enable_partitionwise_join is toggled anyway, so that
-- a costing change which starts choosing per-partition child joins shows up
-- here as a plan diff.
--
-- The outer joins matter for a second reason.  A qual referencing a left
-- join's null-producing side exists as several RestrictInfo clones differing
-- only in varnullingrels, and BNL re-adds such quals to its Join Filter, so
-- it has to install the clone valid at the join being built.  The 3-way
-- cases below cover that: the upper BNL's Join Filter references a relation
-- the lower left join can null.
--
-- Plans are forced with GUCs, not pg_hint_plan hints: the extension is not
-- built on this branch yet, so hints would be silently ignored.
--

CREATE TABLE prt1 (a int, b int, c varchar) PARTITION BY RANGE(a);
CREATE TABLE prt1_p1 PARTITION OF prt1 FOR VALUES FROM (0) TO (250);
CREATE TABLE prt1_p3 PARTITION OF prt1 FOR VALUES FROM (500) TO (600);
CREATE TABLE prt1_p2 PARTITION OF prt1 FOR VALUES FROM (250) TO (500);
INSERT INTO prt1 SELECT i, i % 25, to_char(i, 'FM0000') FROM generate_series(0, 599) i WHERE i % 2 = 0;
CREATE INDEX iprt1_p1_a on prt1_p1(a);
CREATE INDEX iprt1_p2_a on prt1_p2(a);
CREATE INDEX iprt1_p3_a on prt1_p3(a);
ANALYZE prt1;

CREATE TABLE prt2 (a int, b int, c varchar) PARTITION BY RANGE(b);
CREATE TABLE prt2_p1 PARTITION OF prt2 FOR VALUES FROM (0) TO (250);
CREATE TABLE prt2_p2 PARTITION OF prt2 FOR VALUES FROM (250) TO (500);
CREATE TABLE prt2_p3 PARTITION OF prt2 FOR VALUES FROM (500) TO (600);
INSERT INTO prt2 SELECT i % 25, i, to_char(i, 'FM0000') FROM generate_series(0, 599) i WHERE i % 3 = 0;
CREATE INDEX iprt2_p1_b on prt2_p1(b);
CREATE INDEX iprt2_p2_b on prt2_p2(b);
CREATE INDEX iprt2_p3_b on prt2_p3(b);
ANALYZE prt2;

-- Ranges do not line up with prt1, so partitionwise join cannot apply.
CREATE TABLE prt4_n (a int, b int, c text) PARTITION BY RANGE(a);
CREATE TABLE prt4_n_p1 PARTITION OF prt4_n FOR VALUES FROM (0) TO (300);
CREATE TABLE prt4_n_p2 PARTITION OF prt4_n FOR VALUES FROM (300) TO (500);
CREATE TABLE prt4_n_p3 PARTITION OF prt4_n FOR VALUES FROM (500) TO (600);
INSERT INTO prt4_n SELECT i, i, to_char(i, 'FM0000') FROM generate_series(0, 599, 2) i;
ANALYZE prt4_n;

-- Force BNL: no other join method, and prefer batching over a plain nestloop.
SET enable_hashjoin = off;
SET enable_mergejoin = off;
SET enable_material = off;
SET enable_nestloop = off;
SET yb_prefer_bnl = on;
SET yb_bnl_batch_size = 3;
SET max_parallel_workers_per_gather = 0;
SET enable_partitionwise_join = on;

--
-- enable_partitionwise_join = on (BNL over Append still wins)
--
EXPLAIN (COSTS OFF)
SELECT t1.a, t1.c, t2.b, t2.c FROM prt1 t1, prt2 t2 WHERE t1.a = t2.b AND t1.b = 0 ORDER BY t1.a, t2.b;
SELECT t1.a, t1.c, t2.b, t2.c FROM prt1 t1, prt2 t2 WHERE t1.a = t2.b AND t1.b = 0 ORDER BY t1.a, t2.b;

-- Partially-redundant join clauses: both derive from one EquivalenceClass, so
-- only one of them should end up in the Join Filter.
EXPLAIN (COSTS OFF)
SELECT t1.a, t1.c, t2.b, t2.c FROM prt1 t1, prt2 t2 WHERE t1.a = t2.a AND t1.a = t2.b ORDER BY t1.a, t2.b;
SELECT t1.a, t1.c, t2.b, t2.c FROM prt1 t1, prt2 t2 WHERE t1.a = t2.a AND t1.a = t2.b ORDER BY t1.a, t2.b;

--
-- Outer joins over the partition boundary
--
EXPLAIN (COSTS OFF)
SELECT t1.a, t1.c, t2.b, t2.c FROM prt1 t1 LEFT JOIN prt2 t2 ON t1.a = t2.b WHERE t1.b = 0 ORDER BY t1.a, t2.b;
SELECT t1.a, t1.c, t2.b, t2.c FROM prt1 t1 LEFT JOIN prt2 t2 ON t1.a = t2.b WHERE t1.b = 0 ORDER BY t1.a, t2.b;

EXPLAIN (COSTS OFF)
SELECT t1.a, t1.c, t2.b, t2.c FROM prt2 t2 RIGHT JOIN prt1 t1 ON t1.a = t2.b WHERE t1.b = 0 ORDER BY t1.a, t2.b;
SELECT t1.a, t1.c, t2.b, t2.c FROM prt2 t2 RIGHT JOIN prt1 t1 ON t1.a = t2.b WHERE t1.b = 0 ORDER BY t1.a, t2.b;

-- 3-way left outer join.  The upper join's qual references t2, which the
-- lower join can null, so the qual has clones; when re-applying it in the
-- Join Filter, BNL must install the clone valid at the join it is building.
EXPLAIN (COSTS OFF)
SELECT COUNT(*) FROM prt1 t1
  LEFT JOIN prt1 t2 ON t1.a = t2.a
  LEFT JOIN prt1 t3 ON t2.a = t3.a;
SELECT COUNT(*) FROM prt1 t1
  LEFT JOIN prt1 t2 ON t1.a = t2.a
  LEFT JOIN prt1 t3 ON t2.a = t3.a;

-- Same shape, non-trivial payload so null-extended rows are visible.
EXPLAIN (COSTS OFF)
SELECT t1.a, t2.a, t3.a FROM prt1 t1
  LEFT JOIN prt2 t2 ON t2.b = t1.a
  LEFT JOIN prt1 t3 ON t3.a = t2.b
  WHERE t1.a < 60 ORDER BY t1.a, t2.a, t3.a;
SELECT t1.a, t2.a, t3.a FROM prt1 t1
  LEFT JOIN prt2 t2 ON t2.b = t1.a
  LEFT JOIN prt1 t3 ON t3.a = t2.b
  WHERE t1.a < 60 ORDER BY t1.a, t2.a, t3.a;

-- The tuplestore strategy must apply the Join Filter like the hash strategy.
SET yb_bnl_enable_hashing = off;
SELECT COUNT(*) FROM prt1 t1
  LEFT JOIN prt1 t2 ON t1.a = t2.a
  LEFT JOIN prt1 t3 ON t2.a = t3.a;
SELECT t1.a, t2.a, t3.a FROM prt1 t1
  LEFT JOIN prt2 t2 ON t2.b = t1.a
  LEFT JOIN prt1 t3 ON t3.a = t2.b
  WHERE t1.a < 60 ORDER BY t1.a, t2.a, t3.a;
RESET yb_bnl_enable_hashing;

--
-- Same shapes with partitionwise join off: plans should be unchanged
--
SET enable_partitionwise_join = off;

EXPLAIN (COSTS OFF)
SELECT t1.a, t1.c, t2.b, t2.c FROM prt1 t1, prt2 t2 WHERE t1.a = t2.b AND t1.b = 0 ORDER BY t1.a, t2.b;
SELECT t1.a, t1.c, t2.b, t2.c FROM prt1 t1, prt2 t2 WHERE t1.a = t2.b AND t1.b = 0 ORDER BY t1.a, t2.b;

EXPLAIN (COSTS OFF)
SELECT COUNT(*) FROM prt1 t1
  LEFT JOIN prt1 t2 ON t1.a = t2.a
  LEFT JOIN prt1 t3 ON t2.a = t3.a;
SELECT COUNT(*) FROM prt1 t1
  LEFT JOIN prt1 t2 ON t1.a = t2.a
  LEFT JOIN prt1 t3 ON t2.a = t3.a;

RESET enable_partitionwise_join;

--
-- Mismatched partition bounds: partitionwise join cannot apply even when
-- enabled, so this exercises BNL with a partitioned inner directly.
--
SET enable_partitionwise_join = on;

EXPLAIN (COSTS OFF)
SELECT t1.a, t1.c, t2.b, t2.c FROM prt1 t1, prt4_n t2 WHERE t1.a = t2.a ORDER BY t1.a;
SELECT t1.a, t1.c, t2.b, t2.c FROM prt1 t1, prt4_n t2 WHERE t1.a = t2.a ORDER BY t1.a;

RESET enable_partitionwise_join;

DROP TABLE prt1;
DROP TABLE prt2;
DROP TABLE prt4_n;

RESET yb_bnl_batch_size;
RESET yb_prefer_bnl;
RESET enable_nestloop;
RESET enable_material;
RESET enable_mergejoin;
RESET enable_hashjoin;
RESET max_parallel_workers_per_gather;
