--
-- Inspired by upstream PG's partition_aggregate test, modified to exercise
-- YB's aggregate pushdown.  Practically, this involves two main changes:
-- - Remove GROUP BY clauses as GROUP BY disables aggregate pushdown.  This
--   also means removing HAVING, ORDER BY, and non-aggregate targets.  Since
--   this transformation can map two different queries in the original test to
--   the same query here, skip the duplicates.
-- - Skip queries on dummy relations (e.g. subselects).
-- Keep code comments the same as in the original test for better diffing, even
-- though the comments may not match the queries.
--

\set explain 'EXPLAIN (COSTS OFF)'
\set hintfalse '/*+Set(enable_partitionwise_aggregate false)*/'
\set hinttrue '/*+Set(enable_partitionwise_aggregate true)*/'
\set run ':explain :hintfalse :query; :explain :hinttrue :query; :hintfalse :query; :hinttrue :query;'

-- Enable partitionwise aggregate, which by default is disabled.
SET enable_partitionwise_aggregate TO true;
-- Enable partitionwise join, which by default is disabled.
SET enable_partitionwise_join TO true;
-- Disable parallel plans.
SET max_parallel_workers_per_gather TO 0;
-- Disable incremental sort, which can influence selected plans due to fuzz factor.
SET enable_incremental_sort TO off;

--
-- Tests for list partitioned tables.
--
CREATE TABLE pagg_tab (a int, b int, c text, d int) PARTITION BY LIST(c);
CREATE TABLE pagg_tab_p1 PARTITION OF pagg_tab FOR VALUES IN ('0000', '0001', '0002', '0003', '0004');
CREATE TABLE pagg_tab_p2 PARTITION OF pagg_tab FOR VALUES IN ('0005', '0006', '0007', '0008');
CREATE TABLE pagg_tab_p3 PARTITION OF pagg_tab FOR VALUES IN ('0009', '0010', '0011');
INSERT INTO pagg_tab SELECT i % 20, i % 30, to_char(i % 12, 'FM0000'), i % 30 FROM generate_series(0, 2999) i;
ANALYZE pagg_tab;

-- When GROUP BY clause matches; full aggregation is performed for each partition.
\set query 'SELECT sum(a), avg(b), count(*), min(a), max(b) FROM pagg_tab'
:run


-- JOIN query

CREATE TABLE pagg_tab1(x int, y int) PARTITION BY RANGE(x);
CREATE TABLE pagg_tab1_p1 PARTITION OF pagg_tab1 FOR VALUES FROM (0) TO (10);
CREATE TABLE pagg_tab1_p2 PARTITION OF pagg_tab1 FOR VALUES FROM (10) TO (20);
CREATE TABLE pagg_tab1_p3 PARTITION OF pagg_tab1 FOR VALUES FROM (20) TO (30);

CREATE TABLE pagg_tab2(x int, y int) PARTITION BY RANGE(y);
CREATE TABLE pagg_tab2_p1 PARTITION OF pagg_tab2 FOR VALUES FROM (0) TO (10);
CREATE TABLE pagg_tab2_p2 PARTITION OF pagg_tab2 FOR VALUES FROM (10) TO (20);
CREATE TABLE pagg_tab2_p3 PARTITION OF pagg_tab2 FOR VALUES FROM (20) TO (30);

INSERT INTO pagg_tab1 SELECT i % 30, i % 20 FROM generate_series(0, 299, 2) i;
INSERT INTO pagg_tab2 SELECT i % 20, i % 30 FROM generate_series(0, 299, 3) i;

ANALYZE pagg_tab1;
ANALYZE pagg_tab2;

-- When GROUP BY clause matches; full aggregation is performed for each partition.
\set query 'SELECT sum(t1.y), count(*) FROM pagg_tab1 t1, pagg_tab2 t2 WHERE t1.x = t2.y'
:run

-- Check with whole-row reference; partitionwise aggregation does not apply
\set query 'SELECT sum(t1.y), count(t1) FROM pagg_tab1 t1, pagg_tab2 t2 WHERE t1.x = t2.y'
:run

-- Check with LEFT/RIGHT/FULL OUTER JOINs which produces NULL values for
-- aggregation

-- LEFT JOIN, should produce partial partitionwise aggregation plan as
-- GROUP BY is on nullable column
\set query 'SELECT sum(a.y) FROM pagg_tab1 a LEFT JOIN pagg_tab2 b ON a.x = b.y'
:run

-- RIGHT JOIN, should produce full partitionwise aggregation plan as
-- GROUP BY is on non-nullable column
\set query 'SELECT sum(a.y) FROM pagg_tab1 a RIGHT JOIN pagg_tab2 b ON a.x = b.y'
:run

-- FULL JOIN, should produce partial partitionwise aggregation plan as
-- GROUP BY is on nullable column
\set query 'SELECT sum(b.x) FROM pagg_tab1 a FULL OUTER JOIN pagg_tab2 b ON a.x = b.y'
:run


-- Partition by multiple columns

CREATE TABLE pagg_tab_m (a int, b int, c int) PARTITION BY RANGE(a, ((a+b)/2));
CREATE TABLE pagg_tab_m_p1 PARTITION OF pagg_tab_m FOR VALUES FROM (0, 0) TO (12, 12);
CREATE TABLE pagg_tab_m_p2 PARTITION OF pagg_tab_m FOR VALUES FROM (12, 12) TO (22, 22);
CREATE TABLE pagg_tab_m_p3 PARTITION OF pagg_tab_m FOR VALUES FROM (22, 22) TO (30, 30);
INSERT INTO pagg_tab_m SELECT i % 30, i % 40, i % 50 FROM generate_series(0, 2999) i;
ANALYZE pagg_tab_m;

-- Partial aggregation as GROUP BY clause does not match with PARTITION KEY
\set query 'SELECT sum(b), avg(c), count(*) FROM pagg_tab_m'
:run


-- Test with multi-level partitioning scheme

CREATE TABLE pagg_tab_ml (a int, b int, c text) PARTITION BY RANGE(a);
CREATE TABLE pagg_tab_ml_p1 PARTITION OF pagg_tab_ml FOR VALUES FROM (0) TO (12);
CREATE TABLE pagg_tab_ml_p2 PARTITION OF pagg_tab_ml FOR VALUES FROM (12) TO (20) PARTITION BY LIST (c);
CREATE TABLE pagg_tab_ml_p2_s1 PARTITION OF pagg_tab_ml_p2 FOR VALUES IN ('0000', '0001', '0002');
CREATE TABLE pagg_tab_ml_p2_s2 PARTITION OF pagg_tab_ml_p2 FOR VALUES IN ('0003');

-- This level of partitioning has different column positions than the parent
CREATE TABLE pagg_tab_ml_p3(b int, c text, a int) PARTITION BY RANGE (b);
CREATE TABLE pagg_tab_ml_p3_s1(c text, a int, b int);
CREATE TABLE pagg_tab_ml_p3_s2 PARTITION OF pagg_tab_ml_p3 FOR VALUES FROM (7) TO (10);

ALTER TABLE pagg_tab_ml_p3 ATTACH PARTITION pagg_tab_ml_p3_s1 FOR VALUES FROM (0) TO (7);
ALTER TABLE pagg_tab_ml ATTACH PARTITION pagg_tab_ml_p3 FOR VALUES FROM (20) TO (30);

INSERT INTO pagg_tab_ml SELECT i % 30, i % 10, to_char(i % 4, 'FM0000') FROM generate_series(0, 29999) i;
ANALYZE pagg_tab_ml;

-- For Parallel Append
SET max_parallel_workers_per_gather TO 2;
SET parallel_setup_cost = 0;

-- Full aggregation at level 1 as GROUP BY clause matches with PARTITION KEY
-- for level 1 only. For subpartitions, GROUP BY clause does not match with
-- PARTITION KEY, but still we do not see a partial aggregation as array_agg()
-- is not partial agg safe.
\set query 'SELECT sum(b), count(*) FROM pagg_tab_ml'
:run

RESET parallel_setup_cost;
