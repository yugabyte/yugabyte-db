SET yb_fetch_row_limit TO 1024;

--
-- Tests to validate index updates in a table with no primary key.
--
DROP TABLE IF EXISTS no_pkey_table;
CREATE TABLE no_pkey_table (v1 INT, v2 INT, v3 INT, v4 INT);
CREATE INDEX NONCONCURRENTLY no_pkey_v1 ON no_pkey_table (v1 HASH);
CREATE INDEX NONCONCURRENTLY no_pkey_v2_hash_v3 ON no_pkey_table (v2 HASH) INCLUDE (v3);
CREATE INDEX NONCONCURRENTLY no_pkey_v2_range_v3 ON no_pkey_table (v2 ASC) INCLUDE (v3);

INSERT INTO no_pkey_table (SELECT i, i, i, i FROM generate_series(1, 10) AS i);

-- Updating a column with no indexes should not require index writes
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE no_pkey_table SET v4 = v4 + 1 WHERE v1 = 1;
-- Updating the key columns of an index should require a DELETE + INSERT
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE no_pkey_table SET v2 = v2 + 1 WHERE v1 = 1;
-- Updating non-key columns of an index should only require an UPDATE
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE no_pkey_table SET v3 = v3 + 1 WHERE v1 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE no_pkey_table SET v3 = v3 + 1, v4 = v4 + 1 WHERE v1 = 1;
-- Updating a mix of key and non-key columns of an index should require a DELETE + INSERT
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE no_pkey_table SET v2 = v2 + 1, v3 = v3 + 1 WHERE v1 = 1;

-- Validate the updates above using both SeqScan and IndexOnlyScan
/*+ SeqScan(no_pkey_table) */ SELECT * FROM no_pkey_table WHERE v1 = 1 ORDER BY v1;
/*+ IndexOnlyScan(no_pkey_table no_pkey_v2_hash_v3) */ SELECT v2, v3 FROM no_pkey_table WHERE v2 = 3 ORDER BY (v2, v3);
/*+ IndexOnlyScan(no_pkey_table no_pkey_v2_range_v3) */ SELECT v2, v3 FROM no_pkey_table WHERE v2 = 3 ORDER BY (v2, v3);

DROP TABLE IF EXISTS t_simple;
CREATE TABLE t_simple (k1 INT, k2 INT NULL, v1 INT, v2 INT, v3 INT, v4 INT, PRIMARY KEY (k1, k2));
INSERT INTO t_simple (SELECT i, i, i, i, i, i FROM generate_series(1, 10) AS i);

--
-- Vanilla tests to validate index updates in a table with a primary key.
--
CREATE INDEX NONCONCURRENTLY simple_v1 ON t_simple (v1 HASH);
CREATE INDEX NONCONCURRENTLY simple_v2_hash_v3 ON t_simple (v2 HASH) INCLUDE (v3);
CREATE INDEX NONCONCURRENTLY simple_v2_range_v3 ON t_simple (v2 ASC) INCLUDE (v3);

-- Updating a column with no indexes should not require index writes
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v4 = v4 + 1 WHERE k1 = 1;
-- Updating the key columns of an index should require a DELETE + INSERT
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v2 = v2 + 1 WHERE k1 = 1;
-- Updating non-key columns of an index should only require an UPDATE
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v3 = v3 + 1 WHERE k1 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v3 = v3 + 1, v4 = v4 + 1 WHERE k1 = 1;
-- Updating a mix of key and non-key columns of an index should require a DELETE + INSERT
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v2 = v2 + 1, v3 = v3 + 1 WHERE k1 = 1;

-- Validate the updates above using both SeqScan and IndexOnlyScan
/*+ SeqScan(t_simple) */ SELECT * FROM t_simple WHERE k1 = 1 ORDER BY (k1, k2);
/*+ IndexOnlyScan(t_simple simple_v2_hash_v3) */ SELECT v2, v3 FROM t_simple WHERE v2 = 3 ORDER BY (v2, v3);
/*+ IndexOnlyScan(t_simple simple_v2_range_v3) */ SELECT v2, v3 FROM t_simple WHERE v2 = 3 ORDER BY (v2, v3);

-- Updating the primary key columns should require a DELETE + INSERT on the main table
-- as well as on non-unique indexes.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET k1 = k1 + 10, v3 = v3 + 1 WHERE k1 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET k2 = k2 + 10, v3 = v3 + 1 WHERE k1 = 11;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET k1 = 12, k2 = 22, v3 = v3 + 1 WHERE k1 < 3;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET k1 = k1 + 10, k2 = k2 + 10, v2 = v2 + 1 WHERE k2 = 3;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET k1 = k1 + 10, k2 = k2 + 10, v2 = v2 + 1, v3 = v3 + 1 WHERE k2 = 4;

/*+ SeqScan(t_simple) */ SELECT * FROM t_simple WHERE k1 > 10 ORDER BY (k1, k2);
/*+ IndexOnlyScan(t_simple simple_v2_hash_v3) */ SELECT v2, v3 FROM t_simple WHERE v2 <= 5 ORDER BY (v2, v3);
/*+ IndexOnlyScan(t_simple simple_v2_range_v3) */ SELECT v2, v3 FROM t_simple WHERE v2 <= 5 ORDER BY (v2, v3);
/*+ IndexScan(simple_v2_hash_v3) */ SELECT * FROM t_simple WHERE v2 <= 5 ORDER BY (v2, v3);
/*+ IndexScan(simple_v2_range_v3) */ SELECT * FROM t_simple WHERE v2 <= 5 ORDER BY (v2, v3);

DROP INDEX simple_v1;
DROP INDEX simple_v2_hash_v3;
DROP INDEX simple_v2_range_v3;

--
-- Vanilla tests to validate index updates in unique indexes.
--
TRUNCATE t_simple;
INSERT INTO t_simple (SELECT i, i, i, i, i, i FROM generate_series(1, 10) AS i);
CREATE UNIQUE INDEX NONCONCURRENTLY simple_v2_hash_v3 ON t_simple (v2 HASH) INCLUDE (v3);
CREATE UNIQUE INDEX NONCONCURRENTLY simple_v2_range_v3 ON t_simple (v2 ASC) INCLUDE (v3);

-- Updating the key columns of a unique index should require a DELETE + INSERT
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v2 = v2 + 10 WHERE k1 = 1;
-- Updating non-key columns of an index should only require an UPDATE
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v3 = v3 + 1 WHERE k1 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v3 = v3 + 1, v4 = v4 + 1 WHERE k1 = 1;
-- Updating a mix of key and non-key columns of an index should require a DELETE + INSERT
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v2 = v2 + 10, v3 = v3 + 1 WHERE k1 = 1;

-- Updating the primary key columns should require a DELETE + INSERT on the main table
-- but only an UPDATE on a non-unique index.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET k1 = k1 + 10, v3 = v3 + 1 WHERE k1 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET k2 = k2 + 10, v3 = v3 + 1 WHERE k1 = 11;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET k1 = 12, k2 = 22, v3 = v3 + 1 WHERE k1 < 3;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET k1 = k1 + 10, k2 = k2 + 10, v2 = v2 + 10 WHERE k2 = 3;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET k1 = k1 + 10, k2 = k2 + 10, v2 = v2 + 10, v3 = v3 + 1 WHERE k2 = 4;

/*+ SeqScan(t_simple) */ SELECT * FROM t_simple WHERE k1 > 10 ORDER BY (k1, k2);
/*+ IndexOnlyScan(t_simple simple_v2_hash_v3) */ SELECT v2, v3 FROM t_simple WHERE v2 <= 5 ORDER BY (v2, v3);
/*+ IndexOnlyScan(t_simple simple_v2_range_v3) */ SELECT v2, v3 FROM t_simple WHERE v2 <= 5 ORDER BY (v2, v3);
/*+ IndexScan(simple_v2_hash_v3) */ SELECT * FROM t_simple WHERE v2 <= 5 ORDER BY (v2, v3);
/*+ IndexScan(simple_v2_range_v3) */ SELECT * FROM t_simple WHERE v2 <= 5 ORDER BY (v2, v3);

DROP INDEX simple_v2_hash_v3;
DROP INDEX simple_v2_range_v3;

--
-- Tests to validate multi-column index update behavior
--
TRUNCATE t_simple;
INSERT INTO t_simple (SELECT i, i, i, i, i, i FROM generate_series(1, 10) AS i);
CREATE INDEX NONCONCURRENTLY simple_v1_v2_v3 ON t_simple ((v1, v2) HASH) INCLUDE (v3);
-- Updating any of the key columns of the non-unique index should require a DELETE + INSERT
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v1 = v1 + 10, v2 = v2 + 10 WHERE k1 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v1 = v1 + 10, v3 = v3 + 1 WHERE k1 = 2;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v2 = v1 + 10, v3 = v3 + 1 WHERE k1 = 3;
-- Similaryly, updating the primary key should require a DELETE + INSERT
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET k2 = k2 + 10, v3 = v3 + 1 WHERE k1 = 4;
-- Updating non-key columns of the index should only require an UPDATE
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v3 = v3 + 1, v4 = v4 + 1 WHERE k1 = 5;

/*+ IndexOnlyScan(t_simple simple_v1_v2_v3) */ SELECT v1, v2, v3 FROM t_simple WHERE v2 <= 5 ORDER BY (v1, v2);
/*+ IndexScan(simple_v1_v2_v3) */ SELECT * FROM t_simple WHERE k1 <= 5 ORDER BY (v1, v2);

DROP INDEX simple_v1_v2_v3;

-- Create an index that has columns out of order and repeat the test above.
CREATE INDEX NONCONCURRENTLY out_of_order_v1_v2_v3 ON t_simple ((v3, v1) HASH) INCLUDE (v2);
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v1 = v1 + 10, v2 = v2 + 10 WHERE k1 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v1 = v1 + 10, v3 = v3 + 1 WHERE k1 = 2;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v2 = v1 + 10, v3 = v3 + 1 WHERE k1 = 3;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET k2 = k2 + 10, v2 = v2 + 1 WHERE k1 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v2 = v2 + 1, v4 = v4 + 1 WHERE k1 = 1;

/*+ IndexOnlyScan(t_simple out_of_order_v1_v2_v3) */ SELECT v1, v2, v3 FROM t_simple WHERE v2 <= 5 ORDER BY (v1, v2);
/*+ IndexScan(out_of_order_v1_v2_v3) */ SELECT * FROM t_simple WHERE k1 <= 5 ORDER BY (v1, v2);

DROP INDEX out_of_order_v1_v2_v3;

-- Range indexes

--
-- Tests to validate index update behavior when the index contains NULL values
--
CREATE INDEX NONCONCURRENTLY simple_v1_v2_v3 ON t_simple (v1, v2) INCLUDE (v3);
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v2 = NULL WHERE k1 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v2 = NULL, v3 = NULL WHERE k1 = 2;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v3 = NULL, v4 = NULL WHERE k1 = 3;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v1 = NULL WHERE k1 = 4;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v1 = NULL, v2 = NULL, v3 = NULL WHERE k1 = 5;

/*+ IndexOnlyScan(t_simple simple_v1_v2_v3) */ SELECT v1, v2, v3 FROM t_simple WHERE v1 <= 25 OR v1 IS NULL ORDER BY (v1, v2);
/*+ IndexScan(simple_v1_v2_v3) */ SELECT * FROM t_simple WHERE k1 <= 5 ORDER BY (v1, v2);

EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v2 = 1 WHERE k1 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v2 = 2, v3 = k1 WHERE k1 = 2;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v4 = k1 - k2 + v1, v3 = 3 WHERE k1 = 3;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v1 = 4 WHERE k1 = 4;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v1 = 5, v2 = 5, v3 = 5 WHERE k1 = 5;

/*+ IndexOnlyScan(t_simple simple_v1_v2_v3) */ SELECT v1, v2, v3 FROM t_simple WHERE v1 <= 25 OR v1 IS NULL ORDER BY (v1, v2);
/*+ IndexScan(simple_v1_v2_v3) */ SELECT * FROM t_simple WHERE k1 <= 5 ORDER BY (v1, v2);

DROP INDEX simple_v1_v2_v3;

-- Create a unique index with nullable values and repeat the tests above.
CREATE INDEX NONCONCURRENTLY simple_unique_v1_v2_v3 ON t_simple (v1, v2) INCLUDE (v3);
-- Setting any of the primary key columns to NULL should be done via a single UPDATE
-- Setting any of the secondary index key columns to NULL will still require the delete + update
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v2 = NULL WHERE k1 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v2 = NULL, v3 = NULL WHERE k1 = 2;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v3 = NULL, v4 = NULL WHERE k1 = 3;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v1 = NULL WHERE k1 = 4;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v1 = NULL, v2 = NULL, v3 = NULL WHERE k1 = 5;

/*+ IndexOnlyScan(t_simple simple_unique_v1_v2_v3) */ SELECT v1, v2, v3 FROM t_simple WHERE v1 <= 25 OR v1 IS NULL ORDER BY (v1, v2);
/*+ IndexScan(simple_unique_v1_v2_v3) */ SELECT * FROM t_simple WHERE k1 <= 5 ORDER BY (v1, v2);

EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v2 = 1 WHERE k1 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v2 = 2, v3 = k1 WHERE k1 = 2;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v4 = k1 - k2 + v1, v3 = 3 WHERE k1 = 3;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v1 = 4 WHERE k1 = 4;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v1 = 5, v2 = 5, v3 = 5 WHERE k1 = 5;

/*+ IndexOnlyScan(t_simple simple_unique_v1_v2_v3) */ SELECT v1, v2, v3 FROM t_simple WHERE v1 <= 25 OR v1 IS NULL ORDER BY (v1, v2);
/*+ IndexScan(simple_unique_v1_v2_v3) */ SELECT * FROM t_simple WHERE k1 <= 5 ORDER BY (v1, v2);

DROP INDEX simple_unique_v1_v2_v3;

--
-- Tests to validate index update behavior for partial indexes
--
TRUNCATE t_simple;
INSERT INTO t_simple (SELECT i, i, i, i, i, i FROM generate_series(1, 10) AS i);
CREATE INDEX NONCONCURRENTLY simple_partial_or ON t_simple (v1, v2) INCLUDE (v3) WHERE v1 < 5 OR v2 < 10;
CREATE INDEX NONCONCURRENTLY simple_partial_and ON t_simple (v1, v2) INCLUDE (v3) WHERE v1 < 5 AND v2 < 10;

-- The row must be deleted from both indexes
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v1 = 11, v2 = 10 WHERE k1 = 1;
-- The rows must be deleted from the AND index but not the OR index
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v1 = 12, v2 = 8 WHERE k1 = 2;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v2 = 15 WHERE k1 = 3;
-- Modifying the INCLUDE columns should have no impact on the index
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v3 = v3 + 10 WHERE k1 = 6;
-- The row must be inserted into both indexes
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v1 = 5, v2 = -5 WHERE k1 = 1;
-- The row must be inserted into one of the indexes but not the other
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v1 = 4 WHERE k1 = 2;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v1 = 4, v2 = 8, v3 = 100 WHERE k1 = 7;
-- Modifying the primary key columns should neither delete nor insert into the index
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET k2 = k1 + 10, v3 = v3 + 1 WHERE k1 = 8;

/*+ IndexOnlyScan(t_simple simple_partial_or) */ SELECT v1, v2, v3 FROM t_simple WHERE v1 < 5 OR v2 < 10 ORDER BY (v1, v2);
/*+ IndexScan(simple_unique_v1_v2_v3) */ SELECT * FROM t_simple WHERE v1 < 5 OR v2 < 10 ORDER BY (v1, v2);
/*+ IndexOnlyScan(t_simple simple_partial_and) */ SELECT v1, v2, v3 FROM t_simple WHERE v1 < 5 AND v2 < 10 ORDER BY (v1, v2);
/*+ IndexScan(simple_unique_v1_v2_v3) */ SELECT * FROM t_simple WHERE v1 < 5 AND v2 < 10 ORDER BY (v1, v2);
SELECT * FROM t_simple;

DROP INDEX simple_partial_or;
DROP INDEX simple_partial_and;

--
-- Tests to validate index update behavior for expression indexes
--
TRUNCATE t_simple;
INSERT INTO t_simple (SELECT i, i, i, i, i, i FROM generate_series(1, 10) AS i);
CREATE UNIQUE INDEX NONCONCURRENTLY simple_expr ON t_simple ((v1 + 10)) INCLUDE (v3);

-- Updating any of the columns making up the expression should require a DELETE + INSERT
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v1 = 11 WHERE k1 = 1;
-- Any other column update should only require an UPDATE
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET k2 = k2 + 1 WHERE k1 = 2;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET k1 = k1 + 10 WHERE k1 = 3;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET k2 = k2 + 5, v3 = v3 + 5 WHERE k1 = 4;

/*+ IndexScan(simple_expr) */ SELECT *, v1 + 10 FROM t_simple WHERE v1 + 10 IN (21, 12, 13, 14, 15, 16, 17, 18, 19, 20) ORDER BY (v1, v2);
SELECT * FROM t_simple;

-- CREATE INDEX NONCONCURRENTLY complex_expr_v2 ON t_simple ((v1 * v2), v3) INCLUDE (v4);

--
-- Tests to validate multiple include columns
--
TRUNCATE t_simple;
INSERT INTO t_simple (SELECT i, i, i, i, i, i FROM generate_series(1, 10) AS i);
CREATE UNIQUE INDEX NONCONCURRENTLY multi_include ON t_simple (v1, v2) INCLUDE (v4, v3, v2);

-- Updating any of the key columns should require a DELETE + INSERT
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v1 = 11 WHERE k1 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v2 = v2 + 1 WHERE k1 = 2;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v1 = v2 + 10, v2 = v1 + 1 WHERE k1 = 3;
-- Any other column update should only require an UPDATE
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v3 = v3 + 1 WHERE k1 = 4;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v3 = v4, v4 = v3 WHERE k1 = 5;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET v4 = v2 + 1 WHERE k1 = 6;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET k1 = k1 + 10, k2 = k2 + 1 WHERE k1 = 7;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE t_simple SET k1 = k1 + 10, k2 = k2 + 1 WHERE k1 = 8;

EXPLAIN (VERBOSE) /*+ IndexOnlyScan(t_simple multi_include) */ SELECT v1, v2, v3, v4 FROM t_simple ORDER BY (v1, v2);
/*+ IndexOnlyScan(t_simple multi_include) */ SELECT v1, v2, v3, v4 FROM t_simple ORDER BY (v1, v2);
EXPLAIN (VERBOSE) /*+ IndexScan(multi_include) */ SELECT * FROM t_simple ORDER BY (v1, v2);
/*+ IndexScan(multi_include) */ SELECT * FROM t_simple ORDER BY (v1, v2);
SELECT * FROM t_simple;

--
-- Primary key updates on unique indexes with NULL key columns must not use
-- in-place index updates because ybuniqueidxkeysuffix stores the base ybctid.
--
SET yb_enable_inplace_index_update TO on;
DROP TABLE IF EXISTS t_unique_null_pk_update;
CREATE TABLE t_unique_null_pk_update (
  h BIGINT,
  r BIGINT,
  k SMALLINT DEFAULT 0 NOT NULL,
  v BYTEA,
  active BOOLEAN,
  PRIMARY KEY ((h) HASH, r ASC, k ASC)
);
CREATE UNIQUE INDEX NONCONCURRENTLY t_unique_null_pk_update_uq
ON t_unique_null_pk_update (h HASH, v ASC)
WHERE active;
INSERT INTO t_unique_null_pk_update (h, r, k, v, active)
VALUES (1, 1, 0, NULL, true);
UPDATE t_unique_null_pk_update SET k = 1
WHERE h = 1 AND r = 1 AND k = 0;
SELECT yb_index_check('t_unique_null_pk_update_uq'::regclass);
DROP TABLE t_unique_null_pk_update;


--
-- Additional coverage for hidden unique index key suffix changes.
--
DROP TABLE IF EXISTS t_unique_key_update;
CREATE TABLE t_unique_key_update (
  h INT,
  r INT,
  k INT,
  a INT,
  b INT,
  active BOOLEAN DEFAULT true,
  PRIMARY KEY ((h) HASH, r ASC, k ASC)
);
CREATE UNIQUE INDEX NONCONCURRENTLY t_unique_key_update_uq
ON t_unique_key_update (h HASH, a ASC, b ASC);
INSERT INTO t_unique_key_update (h, r, k, a, b) VALUES
  (1, 1, 0, NULL, 10),
  (1, 2, 0, 20, 20),
  (1, 3, 0, NULL, 30),
  (1, 4, 0, NULL, 40);
-- Non-expression unique index: key changes from NULL to not-NULL.
UPDATE t_unique_key_update SET a = 10 WHERE h = 1 AND r = 1;
-- Non-expression unique index: key changes from not-NULL to NULL.
UPDATE t_unique_key_update SET a = NULL WHERE h = 1 AND r = 2;
-- Non-expression unique index: key is NULL; primary key update.
UPDATE t_unique_key_update SET k = 1 WHERE h = 1 AND r = 3 AND k = 0;
-- Non-expression unique index: some keys are NULL; another key becomes NULL.
UPDATE t_unique_key_update SET b = NULL WHERE h = 1 AND r = 4;
SELECT yb_index_check('t_unique_key_update_uq'::regclass);
DROP TABLE t_unique_key_update;

DROP TABLE IF EXISTS t_unique_key_update_nnd;
CREATE TABLE t_unique_key_update_nnd (
  h INT,
  r INT,
  k INT,
  a INT,
  b INT,
  active BOOLEAN DEFAULT true,
  PRIMARY KEY ((h) HASH, r ASC, k ASC)
);
CREATE UNIQUE INDEX NONCONCURRENTLY t_unique_key_update_nnd_uq
ON t_unique_key_update_nnd (h HASH, a ASC, b ASC) NULLS NOT DISTINCT;
INSERT INTO t_unique_key_update_nnd (h, r, k, a, b) VALUES
  (2, 1, 0, NULL, 10),
  (2, 2, 0, 20, 20),
  (2, 3, 0, NULL, 30),
  (2, 4, 0, NULL, 40);
-- Non-expression unique NULLS NOT DISTINCT index: key changes from NULL to not-NULL.
UPDATE t_unique_key_update_nnd SET a = 10 WHERE h = 2 AND r = 1;
-- Non-expression unique NULLS NOT DISTINCT index: key changes from not-NULL to NULL.
UPDATE t_unique_key_update_nnd SET a = NULL WHERE h = 2 AND r = 2;
-- Non-expression unique NULLS NOT DISTINCT index: key is NULL; primary key update.
UPDATE t_unique_key_update_nnd SET k = 1 WHERE h = 2 AND r = 3 AND k = 0;
-- Non-expression unique NULLS NOT DISTINCT index: some keys are NULL; another key becomes NULL.
UPDATE t_unique_key_update_nnd SET b = NULL WHERE h = 2 AND r = 4;
SELECT yb_index_check('t_unique_key_update_nnd_uq'::regclass);
DROP TABLE t_unique_key_update_nnd;

DROP TABLE IF EXISTS t_unique_expr_update;
CREATE TABLE t_unique_expr_update (
  r INT,
  k INT,
  a INT,
  b INT,
  c INT,
  d INT,
  active BOOLEAN DEFAULT true,
  PRIMARY KEY (r ASC, k ASC)
);
CREATE UNIQUE INDEX NONCONCURRENTLY t_unique_expr_update_uq
ON t_unique_expr_update ((a + b), (c + d));
INSERT INTO t_unique_expr_update (r, k, a, b, c, d) VALUES
  (1, 0, NULL, 10, 1, 1),
  (2, 0, 20, 20, 2, 2),
  (3, 0, NULL, 30, 3, 3),
  (4, 0, NULL, 40, 4, 4);
-- Expression unique index: key changes from NULL to not-NULL.
UPDATE t_unique_expr_update SET a = 10 WHERE r = 1;
-- Expression unique index: key changes from not-NULL to NULL.
UPDATE t_unique_expr_update SET a = NULL WHERE r = 2;
-- Expression unique index: key is NULL; primary key update.
UPDATE t_unique_expr_update SET k = 1 WHERE r = 3 AND k = 0;
-- Expression unique index: some keys are NULL; another key becomes NULL.
UPDATE t_unique_expr_update SET c = NULL WHERE r = 4;
SELECT yb_index_check('t_unique_expr_update_uq'::regclass);
DROP TABLE t_unique_expr_update;

DROP TABLE IF EXISTS t_unique_expr_update_nnd;
CREATE TABLE t_unique_expr_update_nnd (
  r INT,
  k INT,
  a INT,
  b INT,
  c INT,
  d INT,
  active BOOLEAN DEFAULT true,
  PRIMARY KEY (r ASC, k ASC)
);
CREATE UNIQUE INDEX NONCONCURRENTLY t_unique_expr_update_nnd_uq
ON t_unique_expr_update_nnd ((a + b), (c + d)) NULLS NOT DISTINCT;
INSERT INTO t_unique_expr_update_nnd (r, k, a, b, c, d) VALUES
  (1, 0, NULL, 10, 1, 1),
  (2, 0, 20, 20, 2, 2),
  (3, 0, NULL, 30, 3, 3),
  (4, 0, NULL, 40, 4, 4);
-- Expression unique NULLS NOT DISTINCT index: key changes from NULL to not-NULL.
UPDATE t_unique_expr_update_nnd SET a = 10 WHERE r = 1;
-- Expression unique NULLS NOT DISTINCT index: key changes from not-NULL to NULL.
UPDATE t_unique_expr_update_nnd SET a = NULL WHERE r = 2;
-- Expression unique NULLS NOT DISTINCT index: key is NULL; primary key update.
UPDATE t_unique_expr_update_nnd SET k = 1 WHERE r = 3 AND k = 0;
-- Expression unique NULLS NOT DISTINCT index: some keys are NULL; another key becomes NULL.
UPDATE t_unique_expr_update_nnd SET c = NULL WHERE r = 4;
SELECT yb_index_check('t_unique_expr_update_nnd_uq'::regclass);
DROP TABLE t_unique_expr_update_nnd;

DROP TABLE IF EXISTS t_partial_unique_key_update;
CREATE TABLE t_partial_unique_key_update (
  h INT,
  r INT,
  k INT,
  a INT,
  b INT,
  active BOOLEAN,
  PRIMARY KEY ((h) HASH, r ASC, k ASC)
);
CREATE UNIQUE INDEX NONCONCURRENTLY t_partial_unique_key_update_uq
ON t_partial_unique_key_update (h HASH, a ASC, b ASC) WHERE active;
INSERT INTO t_partial_unique_key_update VALUES
  (3, 1, 0, NULL, 10, true),
  (3, 2, 0, 20, 20, false),
  (3, 3, 0, NULL, 30, true),
  (3, 4, 0, NULL, 40, false),
  (3, 5, 0, NULL, 50, false);
-- Partial non-expression unique index: NULL to not-NULL and exits the index.
UPDATE t_partial_unique_key_update SET a = 10, active = false WHERE h = 3 AND r = 1;
-- Partial non-expression unique index: not-NULL to NULL and enters the index.
UPDATE t_partial_unique_key_update SET a = NULL, active = true WHERE h = 3 AND r = 2;
-- Partial non-expression unique index: key is NULL, primary key update, and exits the index.
UPDATE t_partial_unique_key_update SET k = 1, active = false WHERE h = 3 AND r = 3 AND k = 0;
-- Partial non-expression unique index: some keys are NULL, another key becomes NULL, and enters the index.
UPDATE t_partial_unique_key_update SET b = NULL, active = true WHERE h = 3 AND r = 4;
-- Partial non-expression unique index: key is NULL, primary key update, and enters the index.
UPDATE t_partial_unique_key_update SET k = 1, active = true WHERE h = 3 AND r = 5 AND k = 0;
SELECT yb_index_check('t_partial_unique_key_update_uq'::regclass);
DROP TABLE t_partial_unique_key_update;

DROP TABLE IF EXISTS t_partial_unique_key_update_nnd;
CREATE TABLE t_partial_unique_key_update_nnd (
  h INT,
  r INT,
  k INT,
  a INT,
  b INT,
  active BOOLEAN,
  PRIMARY KEY ((h) HASH, r ASC, k ASC)
);
CREATE UNIQUE INDEX NONCONCURRENTLY t_partial_unique_key_update_nnd_uq
ON t_partial_unique_key_update_nnd (h HASH, a ASC, b ASC) NULLS NOT DISTINCT WHERE active;
INSERT INTO t_partial_unique_key_update_nnd VALUES
  (4, 1, 0, NULL, 10, true),
  (4, 2, 0, 20, 20, false),
  (4, 3, 0, NULL, 30, true),
  (4, 4, 0, NULL, 40, false),
  (4, 5, 0, NULL, 50, false);
-- Partial non-expression unique NULLS NOT DISTINCT index: NULL to not-NULL and exits the index.
UPDATE t_partial_unique_key_update_nnd SET a = 10, active = false WHERE h = 4 AND r = 1;
-- Partial non-expression unique NULLS NOT DISTINCT index: not-NULL to NULL and enters the index.
UPDATE t_partial_unique_key_update_nnd SET a = NULL, active = true WHERE h = 4 AND r = 2;
-- Partial non-expression unique NULLS NOT DISTINCT index: key is NULL, primary key update, and exits the index.
UPDATE t_partial_unique_key_update_nnd SET k = 1, active = false WHERE h = 4 AND r = 3 AND k = 0;
-- Partial non-expression unique NULLS NOT DISTINCT index: some keys are NULL, another key becomes NULL, and enters the index.
UPDATE t_partial_unique_key_update_nnd SET b = NULL, active = true WHERE h = 4 AND r = 4;
-- Partial non-expression unique NULLS NOT DISTINCT index: key is NULL, primary key update, and enters the index.
UPDATE t_partial_unique_key_update_nnd SET k = 1, active = true WHERE h = 4 AND r = 5 AND k = 0;
SELECT yb_index_check('t_partial_unique_key_update_nnd_uq'::regclass);
DROP TABLE t_partial_unique_key_update_nnd;

DROP TABLE IF EXISTS t_partial_unique_expr_update;
CREATE TABLE t_partial_unique_expr_update (
  r INT,
  k INT,
  a INT,
  b INT,
  c INT,
  d INT,
  active BOOLEAN,
  PRIMARY KEY (r ASC, k ASC)
);
CREATE UNIQUE INDEX NONCONCURRENTLY t_partial_unique_expr_update_uq
ON t_partial_unique_expr_update ((a + b), (c + d)) WHERE active;
INSERT INTO t_partial_unique_expr_update VALUES
  (1, 0, NULL, 10, 1, 1, true),
  (2, 0, 20, 20, 2, 2, false),
  (3, 0, NULL, 30, 3, 3, true),
  (4, 0, NULL, 40, 4, 4, false),
  (5, 0, NULL, 50, 5, 5, false);
-- Partial expression unique index: NULL to not-NULL and exits the index.
UPDATE t_partial_unique_expr_update SET a = 10, active = false WHERE r = 1;
-- Partial expression unique index: not-NULL to NULL and enters the index.
UPDATE t_partial_unique_expr_update SET a = NULL, active = true WHERE r = 2;
-- Partial expression unique index: key is NULL, primary key update, and exits the index.
UPDATE t_partial_unique_expr_update SET k = 1, active = false WHERE r = 3 AND k = 0;
-- Partial expression unique index: some keys are NULL, another key becomes NULL, and enters the index.
UPDATE t_partial_unique_expr_update SET c = NULL, active = true WHERE r = 4;
-- Partial expression unique index: key is NULL, primary key update, and enters the index.
UPDATE t_partial_unique_expr_update SET k = 1, active = true WHERE r = 5 AND k = 0;
SELECT yb_index_check('t_partial_unique_expr_update_uq'::regclass);
DROP TABLE t_partial_unique_expr_update;

DROP TABLE IF EXISTS t_partial_unique_expr_update_nnd;
CREATE TABLE t_partial_unique_expr_update_nnd (
  r INT,
  k INT,
  a INT,
  b INT,
  c INT,
  d INT,
  active BOOLEAN,
  PRIMARY KEY (r ASC, k ASC)
);
CREATE UNIQUE INDEX NONCONCURRENTLY t_partial_unique_expr_update_nnd_uq
ON t_partial_unique_expr_update_nnd ((a + b), (c + d)) NULLS NOT DISTINCT WHERE active;
INSERT INTO t_partial_unique_expr_update_nnd VALUES
  (1, 0, NULL, 10, 1, 1, true),
  (2, 0, 20, 20, 2, 2, false),
  (3, 0, NULL, 30, 3, 3, true),
  (4, 0, NULL, 40, 4, 4, false),
  (5, 0, NULL, 50, 5, 5, false);
-- Partial expression unique NULLS NOT DISTINCT index: NULL to not-NULL and exits the index.
UPDATE t_partial_unique_expr_update_nnd SET a = 10, active = false WHERE r = 1;
-- Partial expression unique NULLS NOT DISTINCT index: not-NULL to NULL and enters the index.
UPDATE t_partial_unique_expr_update_nnd SET a = NULL, active = true WHERE r = 2;
-- Partial expression unique NULLS NOT DISTINCT index: key is NULL, primary key update, and exits the index.
UPDATE t_partial_unique_expr_update_nnd SET k = 1, active = false WHERE r = 3 AND k = 0;
-- Partial expression unique NULLS NOT DISTINCT index: some keys are NULL, another key becomes NULL, and enters the index.
UPDATE t_partial_unique_expr_update_nnd SET c = NULL, active = true WHERE r = 4;
-- Partial expression unique NULLS NOT DISTINCT index: key is NULL, primary key update, and enters the index.
UPDATE t_partial_unique_expr_update_nnd SET k = 1, active = true WHERE r = 5 AND k = 0;
SELECT yb_index_check('t_partial_unique_expr_update_nnd_uq'::regclass);
DROP TABLE t_partial_unique_expr_update_nnd;
