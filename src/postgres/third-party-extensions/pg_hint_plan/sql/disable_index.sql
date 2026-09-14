-- Tests for disable index hints

LOAD 'pg_hint_plan';
SET search_path TO public;
SET pg_hint_plan.debug_print TO on;
SET client_min_messages TO LOG;
SET pg_hint_plan.enable_hint TO on;

CREATE SCHEMA disable_index;

-- Single table
CREATE TABLE disable_index.t1 (c1 int, c2 int, PRIMARY KEY (c1));
CREATE INDEX t1_i1 ON disable_index.t1(c1);
INSERT INTO disable_index.t1 SELECT i, i % 10
  FROM (SELECT generate_series(0, 300) i) t;

-- Tables with inheritance with indexes
CREATE TABLE disable_index.p3 (c1 int, c2 int, c3 int, c4 int, PRIMARY KEY (c1));
CREATE TABLE disable_index.p3_1_100 (CHECK (c4 >= 1 AND c4 < 101))
  INHERITS (disable_index.p3);
CREATE TABLE disable_index.p3_100_200 (CHECK (c4 >= 101 AND c4 < 201))
  INHERITS (disable_index.p3);
CREATE TABLE disable_index.p3_200_300 (CHECK (c4 >= 201 AND c4 < 301))
  INHERITS (disable_index.p3);
INSERT INTO disable_index.p3 SELECT i, i, i % 10, i
  FROM (SELECT generate_series(0, 300) i) t;
CREATE INDEX p3_c1_idx ON disable_index.p3(c1);
CREATE INDEX p3_1_100_c1_c2_idx ON disable_index.p3_1_100(c1, c2);
CREATE INDEX p3_1_100_c1_idx ON disable_index.p3_1_100(c1);
CREATE INDEX p3_100_200_c1_idx ON disable_index.p3_100_200(c1);
CREATE INDEX p3_200_300_c1_idx ON disable_index.p3_200_300(c1);

-- Partitioned tables and partitions
CREATE TABLE disable_index.pt2 (c1 int, c2 int, c3 int, c4 text, PRIMARY KEY (c1, c2))
  PARTITION BY RANGE(c1);
CREATE TABLE disable_index.pt2_0_1 PARTITION OF disable_index.pt2
  FOR VALUES FROM (MINVALUE) TO (101) PARTITION BY RANGE (c2);
CREATE TABLE disable_index.pt2_0_2 PARTITION OF disable_index.pt2
  FOR VALUES FROM (101) TO (201) PARTITION BY RANGE (c2);
CREATE TABLE disable_index.pt2_0_3 PARTITION OF disable_index.pt2
  FOR VALUES FROM (201) TO (MAXVALUE) PARTITION BY RANGE (c2);
CREATE TABLE disable_index.pt2_1_1 PARTITION OF disable_index.pt2_0_1
  FOR VALUES FROM (MINVALUE) TO (151);
CREATE TABLE disable_index.pt2_1_2 PARTITION OF disable_index.pt2_0_1
  FOR VALUES FROM (151) TO (MAXVALUE);
CREATE TABLE disable_index.pt2_1_3 PARTITION OF disable_index.pt2_0_2
  FOR VALUES FROM (MINVALUE) TO (151);
CREATE TABLE disable_index.pt2_1_4 PARTITION OF disable_index.pt2_0_2
  FOR VALUES FROM (151) TO (MAXVALUE);
CREATE TABLE disable_index.pt2_1_5 PARTITION OF disable_index.pt2_0_3
  FOR VALUES FROM (MINVALUE) TO (151);
CREATE TABLE disable_index.pt2_1_6 PARTITION OF disable_index.pt2_0_3
  FOR VALUES FROM (151) TO (MAXVALUE);
INSERT INTO disable_index.pt2 SELECT i, i, i % 10, i
  FROM (SELECT generate_series(0, 300) i) t;
-- Extra index, created on all partitions.
CREATE INDEX pt2_c1_idx ON disable_index.pt2 (c1, c2);

-- Hint conflicts

/*+ DisableIndex(t1) */
EXPLAIN (COSTS false) SELECT * FROM disable_index.t1 WHERE c1 = 1;

/*+DisableIndex(t1 t1_pkey)DisableIndex(t1 t1_i1)*/
EXPLAIN (COSTS false) SELECT * FROM disable_index.t1 WHERE c1 = 1;

-- non-existent index

/*+ DisableIndex(pt2 non_existent) */
EXPLAIN (COSTS OFF) SELECT * FROM disable_index.pt2 WHERE c1 IN (1, 101, 201);

-- DisableIndex + Scan method hint

/*+
DisableIndex(t1 t1_i1)
IndexScan(t1 t1_i1)
*/
EXPLAIN (COSTS false) SELECT * FROM disable_index.t1 WHERE c1 = 1;

/*+
DisableIndex(t1 t1_pkey)
NoIndexScan(t1)
*/
EXPLAIN (COSTS false) SELECT * FROM disable_index.t1 WHERE c1 = 1;

-- Regular tables
/*+ DisableIndex(t1 t1_i1) */
EXPLAIN (COSTS false) SELECT * FROM disable_index.t1 WHERE c1 = 1;

/*+ DisableIndex(t1 t1_pkey) */
EXPLAIN (COSTS false) SELECT * FROM disable_index.t1 WHERE c1 = 1;

/*+ DisableIndex(t1 t1_pkey t1_i1) */
EXPLAIN (COSTS false) SELECT * FROM disable_index.t1 WHERE c1 = 1;

-- Partitioning
SET enable_bitmapscan = OFF;
SET enable_seqscan = OFF;
-- Disable parallelism.
SET max_parallel_workers_per_gather = 0;

-- Push down the parent hint

/*+ DisableIndex(pt2 pt2_c1_idx) */
EXPLAIN (COSTS OFF) SELECT * FROM disable_index.pt2 WHERE c1 IN (1, 101, 201);
/*+ DisableIndex(p3 p3_c1_idx) */
EXPLAIN (COSTS OFF) SELECT * FROM disable_index.p3 WHERE c1 IN (1, 101, 201);

-- Specific partition

/*+
DisableIndex(pt2_1_1 pt2_1_1_c1_c2_idx)
DisableIndex(pt2_1_6 pt2_1_6_c1_c2_idx)
*/
EXPLAIN (COSTS OFF) SELECT * FROM disable_index.pt2 WHERE c1 IN (1, 101, 201);
/*+
DisableIndex(p3_1_100 p3_1_100_c1_c2_idx)
*/
EXPLAIN (COSTS OFF) SELECT * FROM disable_index.p3 WHERE c1 IN (1, 101, 201);

RESET enable_bitmapscan;
RESET enable_seqscan;
RESET max_parallel_workers_per_gather;

-- Cleanup
DROP SCHEMA disable_index CASCADE;
