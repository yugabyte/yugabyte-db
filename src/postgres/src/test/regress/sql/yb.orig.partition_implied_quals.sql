--
-- Index scans of a partition skip quals implied by its partition constraint.
--
SET yb_enable_partition_implied_qual_removal = on;
CREATE TABLE pt (k int, region smallint NOT NULL, a int NOT NULL, b text NOT NULL,
                 v int, PRIMARY KEY (k HASH, region)) PARTITION BY LIST (region);
CREATE TABLE pt_r1 PARTITION OF pt FOR VALUES IN (1);
CREATE TABLE pt_r23 PARTITION OF pt FOR VALUES IN (2, 3);
CREATE INDEX pt_a_b_idx ON pt (a ASC, b ASC);
INSERT INTO pt SELECT i, 1 + i % 3, i % 2, 'b' || (i % 4), i
  FROM generate_series(1, 120) i;
ANALYZE pt;

-- region = 1 holds for every row of pt_r1, so the scan needs no filter and
-- can be an index-only distinct scan. The int4 literal is compared with a
-- smallint partition key.
EXPLAIN (COSTS OFF)
SELECT DISTINCT b FROM pt WHERE region = 1 AND a = 1;
SELECT DISTINCT b FROM pt WHERE region = 1 AND a = 1 ORDER BY b;

-- Range quals implied by the constraint are skipped too.
EXPLAIN (COSTS OFF)
SELECT b FROM pt WHERE region >= 1 AND region < 2 AND a = 1 AND b = 'b1';

-- pt_r23 holds two regions, so region = 2 is not implied and stays a filter.
EXPLAIN (COSTS OFF)
SELECT DISTINCT b FROM pt WHERE region = 2 AND a = 1;
SELECT DISTINCT b FROM pt WHERE region = 2 AND a = 1 ORDER BY b;

-- Querying the partition directly.
EXPLAIN (COSTS OFF)
SELECT DISTINCT b FROM pt_r1 WHERE region = 1 AND a = 1;

-- A qual on an index key column is kept as an index condition.
EXPLAIN (COSTS OFF) SELECT v FROM pt WHERE k = 3 AND region = 1;
SELECT v FROM pt WHERE k = 3 AND region = 1;

-- Including a hash key column, so the lookup still targets one tablet.
CREATE TABLE hk (region int NOT NULL, k int NOT NULL, v int,
                 PRIMARY KEY ((region, k) HASH)) PARTITION BY LIST (region);
CREATE TABLE hk_r1 PARTITION OF hk FOR VALUES IN (1);
INSERT INTO hk SELECT 1, i, i FROM generate_series(1, 10) i;
EXPLAIN (COSTS OFF) SELECT v FROM hk WHERE region = 1 AND k = 5;
SELECT v FROM hk WHERE region = 1 AND k = 5;

-- Leaves of a multi-level hierarchy inherit the ancestor's constraint.
CREATE TABLE ml (k int, region int NOT NULL, a int, b text,
                 PRIMARY KEY (k HASH, region)) PARTITION BY LIST (region);
CREATE TABLE ml_r1 PARTITION OF ml FOR VALUES IN (1) PARTITION BY HASH (k);
CREATE TABLE ml_r1_h0 PARTITION OF ml_r1 FOR VALUES WITH (MODULUS 2, REMAINDER 0);
CREATE TABLE ml_r1_h1 PARTITION OF ml_r1 FOR VALUES WITH (MODULUS 2, REMAINDER 1);
CREATE INDEX ml_a_b_idx ON ml (a ASC, b ASC);
INSERT INTO ml SELECT i, 1, i % 2, 'b' || (i % 4) FROM generate_series(1, 40) i;
EXPLAIN (COSTS OFF) SELECT b FROM ml WHERE region = 1 AND a = 1 AND b = 'b1';
SELECT count(*) FROM ml WHERE region = 1 AND a = 1 AND b = 'b1';

-- Target relations keep the qual so EvalPlanQual rechecks it.
EXPLAIN (COSTS OFF)
SELECT b FROM pt WHERE region = 1 AND a = 1 AND b = 'b1' FOR UPDATE;
EXPLAIN (COSTS OFF) UPDATE pt SET v = v + 1 WHERE region = 1 AND a = 1 AND b = 'b1';

-- A generic plan prunes at run time, so nothing is implied at plan time.
PREPARE q(int) AS SELECT DISTINCT b FROM pt WHERE region = $1 AND a = 1;
SET plan_cache_mode = force_generic_plan;
EXPLAIN (COSTS OFF) EXECUTE q(1);
EXECUTE q(1);
RESET plan_cache_mode;
DEALLOCATE q;

-- The GUC is off by default, which keeps the filter.
RESET yb_enable_partition_implied_qual_removal;
EXPLAIN (COSTS OFF)
SELECT DISTINCT b FROM pt WHERE region = 1 AND a = 1;
SELECT DISTINCT b FROM pt WHERE region = 1 AND a = 1 ORDER BY b;

DROP TABLE pt, hk, ml;
