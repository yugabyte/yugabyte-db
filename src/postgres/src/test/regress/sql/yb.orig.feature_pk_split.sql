--
-- YB_FEATURE_PARTITIONING Testsuite
--   An introduction on whether or not a feature is supported in YugaByte.
--   This test suite does not go in depth for each command.
--
-- Testing basic SPLIT AT functionalities.
--
-- Table with RANGE primary key.
--
CREATE TABLE feature_pk_split (
		col_integer INTEGER,
		col_varchar VARCHAR(100),
		col_text TEXT,
		col_double DOUBLE PRECISION,
		PRIMARY KEY (col_integer ASC, col_varchar ASC))
	SPLIT AT VALUES ((1, 'a'), (10, 'e'), (100, 'i'), (1000, 'o'), (10000, 'u'));
--
-- Secondary index for some of the splits.
-- This work needs optimization.
--
CREATE INDEX idx_small ON feature_pk_split (col_double ASC) WHERE col_double <= 9;
CREATE INDEX idx_large ON feature_pk_split (col_double ASC) WHERE col_double >= 10;
--
-- INSERT at least 1 row for each partition.
--
INSERT INTO feature_pk_split
	VALUES  ( -1, '-', 'partition 1', 1 ),
			( 0, 'm', 'partition 1', 2 ),
			( 1, '9', 'partition 1', 3 ),

			( 1, 'a', 'partition 2', 4 ),
			( 5, 'm', 'partition 2', 5 ),
			( 10, 'd', 'partition 2', 6 ),

			( 10, 'e', 'partition 3', 7 ),
			( 50, 'a', 'partition 3', 8 ),
			( 100, 'h', 'partition 3', 9 ),

			( 100, 'i', 'partition 4', 10 ),
			( 500, 'm', 'partition 4', 11 ),
			( 1000, 'n', 'partition 4', 12 ),

			( 1000, 'o', 'partition 5', 13 ),
			( 5000, 'm', 'partition 5', 14 ),
			( 10000, 't', 'partition 5', 15 ),

			( 10000, 'u', 'partition 6', 16 ),
			( 50000, 'm', 'partition 6', 17 ),
			( 100000, 'z', 'partition 6', 18 );
--
-- Full scan.
--
EXPLAIN (COSTS OFF) SELECT * FROM feature_pk_split;
SELECT * FROM feature_pk_split;
--
-- Full scan with conditional operators.
--
-- Operator `=`
EXPLAIN (COSTS OFF) SELECT * FROM feature_pk_split WHERE col_text = 'partition 3';
SELECT * FROM feature_pk_split WHERE col_text = 'partition 3';
-- Operator `IN`
EXPLAIN (COSTS OFF) SELECT * FROM feature_pk_split WHERE col_text IN ('partition 2', 'partition 5');
SELECT * FROM feature_pk_split WHERE col_text IN ('partition 2', 'partition 5');
-- Operator `<=`
EXPLAIN (COSTS OFF) SELECT * FROM feature_pk_split WHERE col_double <= 10;
SELECT * FROM feature_pk_split WHERE col_double <= 10;
-- Operator `AND`
EXPLAIN (COSTS OFF) SELECT * FROM feature_pk_split WHERE col_text >= 'partition 3' AND col_double <= 10;
SELECT * FROM feature_pk_split WHERE col_text >= 'partition 3' AND col_double <= 10;
--
-- Full scan with aggregate functions.
--
EXPLAIN (COSTS OFF) SELECT COUNT(*) FROM feature_pk_split;
SELECT COUNT(*) FROM feature_pk_split;
EXPLAIN (COSTS OFF) SELECT MAX(col_integer) FROM feature_pk_split;
SELECT MAX(col_integer) FROM feature_pk_split;
EXPLAIN (COSTS OFF) SELECT MIN(col_varchar) FROM feature_pk_split;
SELECT MIN(col_varchar) FROM feature_pk_split;
EXPLAIN (COSTS OFF) SELECT AVG(col_double) FROM feature_pk_split;
SELECT AVG(col_double) FROM feature_pk_split;
--
-- Primary key scan.
-- This work needs to be optimized.
--
EXPLAIN (COSTS OFF) SELECT * FROM feature_pk_split WHERE col_integer = 50 AND col_varchar = 'a';
SELECT * FROM feature_pk_split WHERE col_integer = 50 AND col_varchar = 'a';
EXPLAIN (COSTS OFF) SELECT * FROM feature_pk_split
	WHERE col_integer >= 500 AND col_integer <= 5000 AND
		  col_varchar >= 'a' AND col_varchar <= 'n'
	ORDER BY col_integer, col_varchar;
SELECT * FROM feature_pk_split
	WHERE col_integer >= 500 AND col_integer <= 5000 AND
		  col_varchar >= 'a' AND col_varchar <= 'n'
	ORDER BY col_integer, col_varchar;
EXPLAIN (COSTS OFF) SELECT COUNT(*) FROM feature_pk_split WHERE col_integer = 50 AND col_varchar = 'a';
SELECT COUNT(*) FROM feature_pk_split WHERE col_integer = 50 AND col_varchar = 'a';
EXPLAIN (COSTS OFF) SELECT COUNT(*) FROM feature_pk_split
	WHERE col_integer >= 500 AND col_integer <= 5000 AND
		  col_varchar >= 'a' AND col_varchar <= 'n';
SELECT COUNT(*) FROM feature_pk_split
	WHERE col_integer >= 500 AND col_integer <= 5000 AND
		  col_varchar >= 'a' AND col_varchar <= 'n';
--
-- Secondary key scan.
-- This work needs to be optimized.
--
-- Scan one tablet.
EXPLAIN (COSTS OFF) SELECT * FROM feature_pk_split WHERE col_double < 2;
SELECT * FROM feature_pk_split WHERE col_double < 2;
-- Scan two tablets.
EXPLAIN (COSTS OFF) SELECT * FROM feature_pk_split WHERE col_double <= 5;
SELECT * FROM feature_pk_split WHERE col_double <= 5;
-- Scan three tablets.
EXPLAIN (COSTS OFF) SELECT * FROM feature_pk_split WHERE col_double <= 8;
SELECT * FROM feature_pk_split WHERE col_double <= 8;
-- Scan four tablets.
EXPLAIN (COSTS OFF) SELECT * FROM feature_pk_split WHERE col_double <= 11;
SELECT * FROM feature_pk_split WHERE col_double <= 11;
-- Scan five tablets.
EXPLAIN (COSTS OFF) SELECT * FROM feature_pk_split WHERE col_double <= 14;
SELECT * FROM feature_pk_split WHERE col_double <= 14;
-- Scan six tablets.
EXPLAIN (COSTS OFF) SELECT * FROM feature_pk_split WHERE col_double <= 17;
SELECT * FROM feature_pk_split WHERE col_double <= 17;
-- Scan all tablets.
EXPLAIN (COSTS OFF) SELECT * FROM feature_pk_split WHERE col_double <= 100;
SELECT * FROM feature_pk_split WHERE col_double <= 100;
-- Index only scan.
EXPLAIN (COSTS OFF) SELECT col_double FROM feature_pk_split WHERE col_double <= 8;
SELECT col_double FROM feature_pk_split WHERE col_double <= 8;
--
-- Table that has min & max split values.
-- * Using 3 splits: (1, MAX), (10, MIN), and (100, MIN).
-- * Unspecified split values are defaulted to MINVALUE.
--     SPLIT (10) is (10, MIN)
--
CREATE TABLE feature_pk_split_min_max (
		col_integer INTEGER,
		col_varchar VARCHAR(100),
		col_text TEXT,
		col_double DOUBLE PRECISION,
		PRIMARY KEY (col_integer ASC, col_varchar ASC))
	SPLIT AT VALUES ((1, MAXVALUE), (10), (100, MINVALUE));
--
-- INSERT 2 rows to each partition.
--
INSERT INTO feature_pk_split_min_max
	VALUES  ( 0, '-', 'partition 1', 2 ),
			( 1, 'z', 'partition 1', 2 ),

			( 2, '-', 'partition 2', 3 ),
			( 3, '-', 'partition 2', 3 ),
			( 9, 'z', 'partition 2', 3 ),

			( 10, '-', 'partition 3', 4 ),
			( 20, '-', 'partition 3', 4 ),
			( 30, '-', 'partition 3', 4),
			( 99, 'z', 'partition 3', 4 ),

			( 100, '-', 'partition 4', 5 ),
			( 200, '-', 'partition 4', 5 ),
			( 300, '-', 'partition 4', 5 ),
			( 400, '-', 'partition 4', 5 ),
			( 999, 'z', 'partition 4', 5 );
--
-- SELECT from each partition.
-- TODO(neil) To complete this test, server must provide a method to track tablet information for
-- each row. Currently, this is verified by tracking number rows per tablet during development.
--
-- All rows must be from partition 1: (nan) < PKey < (1, max)
SELECT * FROM feature_pk_split_min_max WHERE col_integer <= 1;
-- All rows must be from partition 2: (1, max) <= PKey < (10, min)
SELECT * FROM feature_pk_split_min_max WHERE col_integer > 1 AND col_integer < 10;
-- All rows must be from partition3: (10, min) <= PKey < (100, min)
SELECT * FROM feature_pk_split_min_max WHERE col_integer >= 10 AND col_integer < 100;
-- All rows must be from partition 4: (100, min) <= PKey < (nan)
SELECT * FROM feature_pk_split_min_max WHERE col_integer >= 100;
