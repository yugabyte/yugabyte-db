DROP TABLE IF EXISTS t0_gen;
DROP TABLE IF EXISTS t1_gen;
CREATE TABLE t0_gen (k INT PRIMARY KEY, v1 INT, v2 INT, v1_gen TEXT GENERATED ALWAYS AS ('Text ' || v1::text) STORED);
CREATE TABLE t1_gen (k INT PRIMARY KEY, k_gen TEXT GENERATED ALWAYS AS ('Text ' || k::text) STORED, v1 INT, v2 INT, v3 INT, v1_v2_gen INT GENERATED ALWAYS AS (v1 + v2) STORED);
INSERT INTO t0_gen (k, v1, v2) (SELECT i, i, i FROM generate_series(1, 10) AS i);
INSERT INTO t1_gen (k, v1, v2, v3) (SELECT i, i, i, NULL FROM generate_series(1, 10) AS i);
SELECT * FROM t0_gen ORDER BY k;
SELECT * FROM t1_gen ORDER BY k;

-- Updates that don't involve generated columns + columns they depend on should
-- eligible for single-row optimization or expression pushdown.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t0_gen SET v2 = v2 + 1 WHERE k = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t0_gen SET v2 = (random() * 10)::int WHERE k = 2;
-- Updates involving generated columns should not be eligible for single-row optimization
-- and should not be pushed down.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t0_gen SET v1 = v1 + 1 WHERE k = 3;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t0_gen SET v1 = v2 + 1, v2 = v1 + 1 WHERE k = 4;
SELECT * FROM t0_gen WHERE k NOT IN (2) ORDER BY k;

CREATE INDEX NONCONCURRENTLY v1_gen_idx ON t0_gen (v1_gen) WHERE k > 100;
-- Modifytable operations must correctly work with indexes on generated columns.
EXPLAIN (ANALYZE, DIST, COSTS OFF) INSERT INTO t0_gen VALUES (101, 101, 101), (102, 102, 102);
EXPLAIN (ANALYZE, DIST, COSTS OFF) INSERT INTO t0_gen (k, v2) VALUES (103, 103);
EXPLAIN (ANALYZE, DIST, COSTS OFF) INSERT INTO t0_gen (k, v1) VALUES (104, 104);
SELECT * FROM t0_gen WHERE k >= 100 ORDER BY k;
EXPLAIN (COSTS OFF) /*+ IndexOnlyScan(t0_gen v1_gen_idx) */ SELECT COUNT(v1_gen) FROM t0_gen WHERE k > 100;
-- COUNT(col) will skip over NULL values.
/*+ IndexOnlyScan(t0_gen v1_gen_idx) */ SELECT COUNT(v1_gen) FROM t0_gen WHERE k > 100;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t0_gen SET v1 = v1 + 1, v2 = v2 + 1 WHERE k > 100;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t0_gen SET v2 = v2 - 1 WHERE k > 100;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t0_gen SET k = k + 10 WHERE k > 100;
SELECT * FROM t0_gen WHERE k > 100 ORDER BY k;
/*+ IndexOnlyScan(t0_gen v1_gen_idx) */ SELECT COUNT(v1_gen) FROM t0_gen WHERE k > 100;
EXPLAIN (ANALYZE, DIST, COSTS OFF) DELETE FROM t0_gen WHERE k > 110 AND k < 112;
EXPLAIN (ANALYZE, DIST, COSTS OFF) DELETE FROM t0_gen WHERE v1_gen = 'Text 103';
EXPLAIN (ANALYZE, DIST, COSTS OFF) DELETE FROM t0_gen WHERE v1_gen IS NULL;
SELECT * FROM t0_gen WHERE k > 100 ORDER BY k;
/*+ IndexOnlyScan(t0_gen v1_gen_idx) */ SELECT COUNT(v1_gen) FROM t0_gen WHERE k > 100;

-- Modifytable operations must correctly work with multi-column indexes involving generated columns.
CREATE INDEX NONCONCURRENTLY v2_v1_gen_idx ON t0_gen (v2, v1_gen) WHERE k > 200;
EXPLAIN (ANALYZE, DIST, COSTS OFF) INSERT INTO t0_gen VALUES (201, 201, 201), (202, 202, 202);
EXPLAIN (ANALYZE, DIST, COSTS OFF) INSERT INTO t0_gen (k, v2) VALUES (203, 203);
EXPLAIN (ANALYZE, DIST, COSTS OFF) INSERT INTO t0_gen (k, v1) VALUES (204, 204);
SELECT * FROM t0_gen WHERE k > 200 ORDER BY k;
EXPLAIN (COSTS OFF) /*+ IndexOnlyScan(t0_gen v2_v1_gen_idx) */ SELECT COUNT(v1_gen) FROM t0_gen WHERE k > 200;
/*+ IndexOnlyScan(t0_gen v2_v1_gen_idx) */ SELECT COUNT(v1_gen) FROM t0_gen WHERE k > 200;
-- The following query updates both indexes on the table. Rows corresponding to k = 201, 202, 204
-- need entries in both indexes to be deleted and reinserted, while rows corresponding to k = 203
-- needs only the entry in v2_v1_gen_idx to be updated because the entry in v1_gen_idx evaluates to
-- NULL both before and after the update.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t0_gen SET v1 = v1 + 1, v2 = v2 + 1 WHERE k > 200;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t0_gen SET v2 = v2 - 1 WHERE k > 200;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t0_gen SET v1 = v1 + 1 WHERE k = 204;
SELECT * FROM t0_gen WHERE k > 200 ORDER BY k;
/*+ IndexOnlyScan(t0_gen v2_v1_gen_idx) */ SELECT COUNT(v1_gen) FROM t0_gen WHERE k > 200;
EXPLAIN (ANALYZE, DIST, COSTS OFF) DELETE FROM t0_gen WHERE k = 201;
EXPLAIN (ANALYZE, DIST, COSTS OFF) DELETE FROM t0_gen WHERE k > 200 AND (v1 < 203 OR v1 IS NULL);
EXPLAIN (ANALYZE, DIST, COSTS OFF) DELETE FROM t0_gen WHERE k > 100 AND v2 < 203;
SELECT * FROM t0_gen WHERE k > 200 ORDER BY k;
/*+ IndexOnlyScan(t0_gen v2_v1_gen_idx) */ SELECT COUNT(v1_gen) FROM t0_gen WHERE k > 200;
/*+ IndexOnlyScan(t0_gen v1_gen_idx) */ SELECT COUNT(v1_gen) FROM t0_gen WHERE k > 100;

-- Update operations involving a generated column that depends on multiple columns.
-- If the update operations involves any of the columns that a generated column
-- depends on, then the query should not be eligible for single-row optimization
-- or expression pushdown on those columns.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t1_gen SET v1 = v1 + 1 WHERE k = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t1_gen SET v2 = 2 WHERE k = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t1_gen SET v1 = v2 + 1, v2 = v1 + 1 WHERE k = 2;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t1_gen SET v3 = 2 WHERE k = 2;
SELECT * FROM t1_gen WHERE k IN (1, 2) ORDER BY k;

CREATE INDEX NONCONCURRENTLY t1_gen_v1_v2_gen ON t1_gen (v1_v2_gen);
CREATE INDEX NONCONCURRENTLY ON t1_gen (k_gen) WHERE k < 100;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t1_gen SET v1 = v2 + 1, v2 = v1 + 1 WHERE k = 3;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t1_gen SET v1 = v2, v3 = v3 + 1 WHERE k = 4;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t1_gen SET v1 = k + 1 WHERE k = 5;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t1_gen SET k = k + 10 WHERE k = 6;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t1_gen SET k = k + 10, v1 = v1 + 5, v2 = v2 + 5 WHERE k = 7;
SELECT * FROM t1_gen ORDER BY k;

-- Generated columns should work well with BEFORE UPDATE triggers.
CREATE OR REPLACE FUNCTION increment_v1() RETURNS TRIGGER
LANGUAGE PLPGSQL AS $$
BEGIN
  NEW.v1 = NEW.v1 + 1;
  RETURN NEW;
END;
$$;

CREATE OR REPLACE FUNCTION increment_v2() RETURNS TRIGGER
LANGUAGE PLPGSQL AS $$
BEGIN
  NEW.v2 = NEW.v2 + 1;
  RETURN NEW;
END;
$$;

-- Naming convention: zz_ prefix to ensure that the function is executed last.
CREATE OR REPLACE FUNCTION zz_gen_col_notice() RETURNS TRIGGER
LANGUAGE PLPGSQL AS $$
BEGIN
  RAISE NOTICE '% ROW trigger on generated column invoked', TG_argv[0];
  RETURN NEW;
END;
$$;

CREATE TRIGGER t1_gen_v1_trigger BEFORE UPDATE ON t1_gen FOR EACH ROW EXECUTE FUNCTION increment_v1();
BEGIN;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t1_gen SET v3 = v3 + 1 WHERE k = 8;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t1_gen SET v1 = v1 + 1, v2 = v2 + 1 WHERE k = 9;
SELECT * FROM t1_gen WHERE k > 7 ORDER BY k;
ROLLBACK;

-- Chaining of triggers with generated columns involved.
CREATE TRIGGER t1_gen_v2_trigger BEFORE UPDATE OF v2 ON t1_gen FOR EACH ROW EXECUTE FUNCTION increment_v2();
BEGIN;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t1_gen SET v2 = v2, v3 = v3 + 1 WHERE k = 8;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t1_gen SET v1 = v1 + 1, v2 = v2 + 1 WHERE k = 9;
SELECT * FROM t1_gen WHERE k > 7 ORDER BY k;
ROLLBACK;

-- Triggers should fire on generated columns.
CREATE TRIGGER t1_gen_before_gen BEFORE UPDATE OF v1_v2_gen ON t1_gen FOR EACH ROW EXECUTE FUNCTION zz_gen_col_notice('BEFORE');
CREATE TRIGGER t1_gen_after_gen AFTER UPDATE OF v1_v2_gen ON t1_gen FOR EACH ROW EXECUTE FUNCTION zz_gen_col_notice('AFTER');
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t1_gen SET v3 = v3 + 1 WHERE k = 10;
SELECT * FROM t1_gen WHERE k > 7 ORDER BY k;

-- Partitioned tables
CREATE TABLE part (
    amount NUMERIC,
    double_amount NUMERIC GENERATED ALWAYS AS (amount * 2) STORED
) partition by range(amount);

CREATE TABLE part_1_100 partition of part for values from (1) to (100);
CREATE TABLE part_2_200 partition of part for values from (101) to (200);
INSERT INTO part VALUES (1), (101);
INSERT INTO part_1_100 VALUES (2), (3);
INSERT INTO part_2_200 VALUES (102), (103);
SELECT * FROM part ORDER BY amount;
SELECT * FROM part_1_100 ORDER BY amount;
SELECT * FROM part_2_200 ORDER BY amount;

ALTER TABLE part_1_100 ALTER COLUMN double_amount DROP EXPRESSION; -- error
ALTER TABLE part ALTER COLUMN double_amount DROP EXPRESSION;
\d part
\d part_1_100
\d part_2_200
INSERT INTO part VALUES (4), (104);
SELECT * FROM part ORDER BY amount;
DROP TABLE part;

--- DROP COLUMN CASCASE should DROP dependent columns

-- base case
CREATE TABLE table1(id INT, c1 INT, stored_col INT GENERATED ALWAYS AS (c1 * 2) STORED);
ALTER TABLE table1 DROP COLUMN c1; -- error
ALTER TABLE table1 DROP COLUMN c1 CASCADE;
ALTER TABLE table1 ADD COLUMN stored_col INT;
ALTER TABLE table1 ADD COLUMN c1 INT;
DROP TABLE table1;

-- generated column is PK
CREATE TABLE table1(id INT, c1 INT, stored_col INT GENERATED ALWAYS AS (c1 * 2) STORED, PRIMARY KEY (stored_col));
ALTER TABLE table1 DROP COLUMN c1 CASCADE;
ALTER TABLE table1 ADD COLUMN stored_col INT;
DROP TABLE table1;

-- partitioned table
CREATE TABLE part(a INT, b INT, c INT GENERATED ALWAYS AS (b * 2) STORED) PARTITION BY RANGE (a);
CREATE TABLE part1_100 PARTITION OF part FOR VALUES FROM (1) TO (100);
ALTER TABLE part DROP COLUMN b CASCADE;
ALTER TABLE part ADD COLUMN c INT;
DROP TABLE part;

-- partitioned table where generated col is PK
CREATE TABLE part(a INT, b INT, c INT GENERATED ALWAYS AS (b * 2) STORED, PRIMARY KEY (c, a)) PARTITION BY RANGE (a);
CREATE TABLE part1_100 PARTITION OF part FOR VALUES FROM (1) TO (100);
ALTER TABLE part DROP COLUMN b CASCADE;
ALTER TABLE part ADD COLUMN c INT;
DROP TABLE part;

-- partitioned table, generated col is PK but only in leaf
CREATE TABLE part(a INT, b INT, c INT GENERATED ALWAYS AS (b * 2) STORED) PARTITION BY RANGE (a);
CREATE TABLE part1_100(a INT, b INT, c INT GENERATED ALWAYS AS (b * 2) STORED, PRIMARY KEY (c));
ALTER TABLE part ATTACH PARTITION part1_100 FOR VALUES FROM (1) TO (100);
CREATE TABLE part2_200 PARTITION OF part FOR VALUES FROM (101) TO (200);
ALTER TABLE part DROP COLUMN b CASCADE;
ALTER TABLE part ADD COLUMN c INT;
ALTER TABLE part ADD COLUMN b INT;
DROP TABLE part;
