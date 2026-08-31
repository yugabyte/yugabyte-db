--
-- IN clause pushdown.
-- A WHERE <key> IN (...) condition must reach DocDB as a key condition, so that only the requested
-- keys are read instead of the whole table or index. EXPLAIN (ANALYZE, DIST) reports the number of
-- rows read from DocDB, which is what the pushdown saves.
-- Parameterized queries are checked with a custom plan, which substitutes the parameters, and with
-- a generic plan, which has to push the parameters down; UPDATE and DELETE use the generic plan.
--

--
-- Single hash key column.
--
CREATE TABLE in_pk_simple (h int PRIMARY KEY, v int) SPLIT INTO 2 TABLETS;
INSERT INTO in_pk_simple SELECT i, i FROM generate_series(0, 999) i;

-- Reference: v is not a key column, so the whole table is read.
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF)
SELECT * FROM in_pk_simple WHERE v IN (10, 20, 30);

EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF)
SELECT * FROM in_pk_simple WHERE h IN (10, 20, 30);
SELECT * FROM in_pk_simple WHERE h IN (10, 20, 30) ORDER BY h;

PREPARE simple_select(int, int, int) AS SELECT * FROM in_pk_simple WHERE h IN ($1, $2, $3);
SET plan_cache_mode = force_custom_plan;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) EXECUTE simple_select(10, 20, 30);
SET plan_cache_mode = force_generic_plan;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) EXECUTE simple_select(10, 20, 30);
EXECUTE simple_select(10, 20, 30);

EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF)
UPDATE in_pk_simple SET v = 123456789 WHERE h IN (10, 20, 30);
SELECT * FROM in_pk_simple WHERE v = 123456789 ORDER BY h;
UPDATE in_pk_simple SET v = h WHERE h IN (10, 20, 30);

PREPARE simple_update(int, int, int, int) AS
    UPDATE in_pk_simple SET v = $1 WHERE h IN ($2, $3, $4);
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF)
EXECUTE simple_update(123456789, 10, 20, 30);
SELECT * FROM in_pk_simple WHERE v = 123456789 ORDER BY h;

EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF)
DELETE FROM in_pk_simple WHERE h IN (10, 20, 30);
SELECT count(*) FROM in_pk_simple;
INSERT INTO in_pk_simple SELECT i, i FROM unnest(ARRAY[10, 20, 30]) i;

PREPARE simple_delete(int, int, int) AS DELETE FROM in_pk_simple WHERE h IN ($1, $2, $3);
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) EXECUTE simple_delete(10, 20, 30);
SELECT count(*) FROM in_pk_simple;

RESET plan_cache_mode;
DROP TABLE in_pk_simple;

--
-- Two hash key columns and a range key column.
--
CREATE TABLE in_pk_hash2_range (h1 int, h2 int, r int, v int,
                                PRIMARY KEY ((h1, h2) HASH, r ASC)) SPLIT INTO 2 TABLETS;
INSERT INTO in_pk_hash2_range
SELECT h1, h2, r, h1 * 10000 + h2 * 100 + r
    FROM generate_series(0, 7) h1, generate_series(0, 7) h2, generate_series(0, 7) r;

EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF)
SELECT * FROM in_pk_hash2_range WHERE h1 IN (1, 2) AND h2 IN (3, 4) AND r IN (5, 6);
SELECT * FROM in_pk_hash2_range WHERE h1 IN (1, 2) AND h2 IN (3, 4) AND r IN (5, 6)
    ORDER BY h1, h2, r;

PREPARE hash2_range_select(int, int, int, int, int, int) AS
    SELECT * FROM in_pk_hash2_range WHERE h1 IN ($1, $2) AND h2 IN ($3, $4) AND r IN ($5, $6);
SET plan_cache_mode = force_custom_plan;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF)
EXECUTE hash2_range_select(1, 2, 3, 4, 5, 6);
SET plan_cache_mode = force_generic_plan;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF)
EXECUTE hash2_range_select(1, 2, 3, 4, 5, 6);
EXECUTE hash2_range_select(1, 2, 3, 4, 5, 6);

EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF)
UPDATE in_pk_hash2_range SET v = 123456789 WHERE h1 IN (1, 2) AND h2 IN (3, 4) AND r IN (5, 6);
SELECT * FROM in_pk_hash2_range WHERE v = 123456789 ORDER BY h1, h2, r;
UPDATE in_pk_hash2_range SET v = h1 * 10000 + h2 * 100 + r WHERE v = 123456789;

PREPARE hash2_range_update(int, int, int, int, int, int, int) AS
    UPDATE in_pk_hash2_range SET v = $1 WHERE h1 IN ($2, $3) AND h2 IN ($4, $5) AND r IN ($6, $7);
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF)
EXECUTE hash2_range_update(123456789, 1, 2, 3, 4, 5, 6);
SELECT * FROM in_pk_hash2_range WHERE v = 123456789 ORDER BY h1, h2, r;

EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF)
DELETE FROM in_pk_hash2_range WHERE h1 IN (1, 2) AND h2 IN (3, 4) AND r IN (5, 6);
SELECT count(*) FROM in_pk_hash2_range;
INSERT INTO in_pk_hash2_range
SELECT h1, h2, r, h1 * 10000 + h2 * 100 + r
    FROM unnest(ARRAY[1, 2]) h1, unnest(ARRAY[3, 4]) h2, unnest(ARRAY[5, 6]) r;

PREPARE hash2_range_delete(int, int, int, int, int, int) AS
    DELETE FROM in_pk_hash2_range WHERE h1 IN ($1, $2) AND h2 IN ($3, $4) AND r IN ($5, $6);
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF)
EXECUTE hash2_range_delete(1, 2, 3, 4, 5, 6);
SELECT count(*) FROM in_pk_hash2_range;

RESET plan_cache_mode;
DROP TABLE in_pk_hash2_range;

--
-- Key column order differs from the table column order, see GHI #3302.
-- Columns are declared (h1, h2, h3), the key is (h3, h1, h2) and queries list (h2, h3, h1).
--
CREATE TABLE in_pk_reordered (h1 int, h2 int, h3 int, v int,
                              PRIMARY KEY ((h3, h1, h2) HASH)) SPLIT INTO 2 TABLETS;
INSERT INTO in_pk_reordered
SELECT h1, h2, h3, h1 * 10000 + h2 * 100 + h3
    FROM generate_series(0, 7) h1, generate_series(0, 7) h2, generate_series(0, 7) h3;

EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF)
SELECT * FROM in_pk_reordered WHERE h2 IN (3, 4) AND h3 IN (5, 6) AND h1 IN (1, 2);
SELECT * FROM in_pk_reordered WHERE h2 IN (3, 4) AND h3 IN (5, 6) AND h1 IN (1, 2)
    ORDER BY h1, h2, h3;

PREPARE reordered_select(int, int, int, int, int, int) AS
    SELECT * FROM in_pk_reordered WHERE h2 IN ($1, $2) AND h3 IN ($3, $4) AND h1 IN ($5, $6);
SET plan_cache_mode = force_custom_plan;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF)
EXECUTE reordered_select(3, 4, 5, 6, 1, 2);
SET plan_cache_mode = force_generic_plan;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF)
EXECUTE reordered_select(3, 4, 5, 6, 1, 2);
EXECUTE reordered_select(3, 4, 5, 6, 1, 2);

EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF)
UPDATE in_pk_reordered SET v = 123456789 WHERE h2 IN (3, 4) AND h3 IN (5, 6) AND h1 IN (1, 2);
SELECT * FROM in_pk_reordered WHERE v = 123456789 ORDER BY h1, h2, h3;
UPDATE in_pk_reordered SET v = h1 * 10000 + h2 * 100 + h3 WHERE v = 123456789;

PREPARE reordered_update(int, int, int, int, int, int, int) AS
    UPDATE in_pk_reordered SET v = $1 WHERE h2 IN ($2, $3) AND h3 IN ($4, $5) AND h1 IN ($6, $7);
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF)
EXECUTE reordered_update(123456789, 3, 4, 5, 6, 1, 2);
SELECT * FROM in_pk_reordered WHERE v = 123456789 ORDER BY h1, h2, h3;

EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF)
DELETE FROM in_pk_reordered WHERE h2 IN (3, 4) AND h3 IN (5, 6) AND h1 IN (1, 2);
SELECT count(*) FROM in_pk_reordered;
INSERT INTO in_pk_reordered
SELECT h1, h2, h3, h1 * 10000 + h2 * 100 + h3
    FROM unnest(ARRAY[1, 2]) h1, unnest(ARRAY[3, 4]) h2, unnest(ARRAY[5, 6]) h3;

PREPARE reordered_delete(int, int, int, int, int, int) AS
    DELETE FROM in_pk_reordered WHERE h2 IN ($1, $2) AND h3 IN ($3, $4) AND h1 IN ($5, $6);
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF)
EXECUTE reordered_delete(3, 4, 5, 6, 1, 2);
SELECT count(*) FROM in_pk_reordered;

RESET plan_cache_mode;
DROP TABLE in_pk_reordered;

--
-- Secondary index key column.
--
CREATE TABLE in_sidx (h int PRIMARY KEY, i int, v int) SPLIT INTO 2 TABLETS;
CREATE INDEX in_sidx_i_idx ON in_sidx (i) SPLIT INTO 2 TABLETS;
INSERT INTO in_sidx SELECT i, i, i FROM generate_series(0, 999) i;

-- Reference: v is not an index key column, so the whole table is read.
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF)
SELECT * FROM in_sidx WHERE v IN (10, 20, 30);

EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF)
SELECT * FROM in_sidx WHERE i IN (10, 20, 30);
SELECT * FROM in_sidx WHERE i IN (10, 20, 30) ORDER BY h;

PREPARE sidx_select(int, int, int) AS SELECT * FROM in_sidx WHERE i IN ($1, $2, $3);
SET plan_cache_mode = force_custom_plan;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) EXECUTE sidx_select(10, 20, 30);
SET plan_cache_mode = force_generic_plan;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) EXECUTE sidx_select(10, 20, 30);
EXECUTE sidx_select(10, 20, 30);

EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF)
UPDATE in_sidx SET v = 123456789 WHERE i IN (10, 20, 30);
SELECT * FROM in_sidx WHERE v = 123456789 ORDER BY h;
UPDATE in_sidx SET v = h WHERE i IN (10, 20, 30);

PREPARE sidx_update(int, int, int, int) AS UPDATE in_sidx SET v = $1 WHERE i IN ($2, $3, $4);
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF)
EXECUTE sidx_update(123456789, 10, 20, 30);
SELECT * FROM in_sidx WHERE v = 123456789 ORDER BY h;

EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF)
DELETE FROM in_sidx WHERE i IN (10, 20, 30);
SELECT count(*) FROM in_sidx;
INSERT INTO in_sidx SELECT i, i, i FROM unnest(ARRAY[10, 20, 30]) i;

PREPARE sidx_delete(int, int, int) AS DELETE FROM in_sidx WHERE i IN ($1, $2, $3);
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) EXECUTE sidx_delete(10, 20, 30);
SELECT count(*) FROM in_sidx;

RESET plan_cache_mode;
DROP TABLE in_sidx;

--
-- IN mixed with equality on the second hash key column.
--
CREATE TABLE in_eq_pk_hash2 (h1 int, h2 int, v int,
                             PRIMARY KEY ((h1, h2) HASH)) SPLIT INTO 2 TABLETS;
INSERT INTO in_eq_pk_hash2
SELECT h1, h2, h1 * 10000 + h2 FROM generate_series(0, 29) h1, generate_series(0, 29) h2;

EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF)
SELECT * FROM in_eq_pk_hash2 WHERE h1 IN (1, 2, 3) AND h2 = 4;
SELECT * FROM in_eq_pk_hash2 WHERE h1 IN (1, 2, 3) AND h2 = 4 ORDER BY h1;

PREPARE in_eq_select(int, int, int, int) AS
    SELECT * FROM in_eq_pk_hash2 WHERE h1 IN ($1, $2, $3) AND h2 = $4;
SET plan_cache_mode = force_custom_plan;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) EXECUTE in_eq_select(1, 2, 3, 4);
SET plan_cache_mode = force_generic_plan;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) EXECUTE in_eq_select(1, 2, 3, 4);
EXECUTE in_eq_select(1, 2, 3, 4);

EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF)
UPDATE in_eq_pk_hash2 SET v = 123456789 WHERE h1 IN (1, 2, 3) AND h2 = 4;
SELECT * FROM in_eq_pk_hash2 WHERE v = 123456789 ORDER BY h1;
UPDATE in_eq_pk_hash2 SET v = h1 * 10000 + h2 WHERE v = 123456789;

PREPARE in_eq_update(int, int, int, int, int) AS
    UPDATE in_eq_pk_hash2 SET v = $1 WHERE h1 IN ($2, $3, $4) AND h2 = $5;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF)
EXECUTE in_eq_update(123456789, 1, 2, 3, 4);
SELECT * FROM in_eq_pk_hash2 WHERE v = 123456789 ORDER BY h1;

EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF)
DELETE FROM in_eq_pk_hash2 WHERE h1 IN (1, 2, 3) AND h2 = 4;
SELECT count(*) FROM in_eq_pk_hash2;
INSERT INTO in_eq_pk_hash2 SELECT h1, 4, h1 * 10000 + 4 FROM unnest(ARRAY[1, 2, 3]) h1;

PREPARE in_eq_delete(int, int, int, int) AS
    DELETE FROM in_eq_pk_hash2 WHERE h1 IN ($1, $2, $3) AND h2 = $4;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF, TIMING OFF) EXECUTE in_eq_delete(1, 2, 3, 4);
SELECT count(*) FROM in_eq_pk_hash2;

RESET plan_cache_mode;
DROP TABLE in_eq_pk_hash2;
