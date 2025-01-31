--
-- Create index on existing table with data
--
CREATE TABLE index_test (col1 int, col2 int);
INSERT INTO index_test (col1, col2) VALUES (1, 100);
INSERT INTO index_test (col1, col2) VALUES (2, 200);

CREATE INDEX index_test_idx1 ON index_test(col1);
CREATE INDEX index_test_idx2 ON index_test(col1, col2);

DROP INDEX index_test_idx1;
DROP INDEX index_test_idx2;
DROP TABLE index_test;

CREATE TABLE test_index (v1 INT, v2 INT, v3 INT);
CREATE INDEX ON test_index (v1);
CREATE INDEX ON test_index (v2, v3);
INSERT INTO test_index VALUES (1, 11, 21), (2, 12, 22), (3, 13, 23), (4, 14, 24), (5, 15, 25);

-- Verify order by on indexed column
SELECT * FROM test_index ORDER BY v1;

-- Verify delete with hash value in index
DELETE FROM test_index WHERE v2 = 12 OR v2 = 13;
SELECT * FROM test_index ORDER BY v1;

-- Verify different WHERE conditions are supported.
SELECT * FROM test_index WHERE v1 IS NULL;
SELECT * FROM test_index WHERE v1 IS NOT NULL ORDER BY v1;
SELECT * FROM test_index WHERE v1 IN (1, 2, 3);


-- Verify indexes on system catalog tables are updated properly

CREATE TABLE test_sys_catalog_update (k int primary key, v int);

EXPLAIN (COSTS OFF) SELECT relname FROM pg_class WHERE relname = 'test_sys_catalog_update';
SELECT relname  FROM pg_class WHERE relname = 'test_sys_catalog_update';

EXPLAIN (COSTS OFF) SELECT typname FROM pg_type WHERE typname = 'test_sys_catalog_update';
SELECT typname FROM pg_type WHERE typname = 'test_sys_catalog_update';

EXPLAIN (COSTS OFF) SELECT attname, atttypid FROM pg_attribute WHERE attname = 'v';
SELECT attname, atttypid FROM pg_attribute WHERE attname = 'v';

ALTER TABLE test_sys_catalog_update RENAME TO test_sys_catalog_update_new;
ALTER TABLE test_sys_catalog_update_new RENAME COLUMN v TO w;

SELECT relname FROM pg_class WHERE relname = 'test_sys_catalog_update';
SELECT typname FROM pg_type WHERE typname = 'test_sys_catalog_update';
SELECT attname, atttypid FROM pg_attribute WHERE attname = 'v';

SELECT relname FROM pg_class WHERE relname = 'test_sys_catalog_update_new';
SELECT typname FROM pg_type WHERE typname = 'test_sys_catalog_update_new';
SELECT attname, atttypid FROM pg_attribute WHERE attname = 'w';

-- Test primary key as index
CREATE TABLE t1 (h INT, r INT, v1 INT, v2 INT, PRIMARY KEY (h hash, r));
CREATE INDEX ON t1 (v1);
CREATE UNIQUE INDEX ON t1 (v1, v2);
CREATE TABLE t2 (h INT, r INT, v1 INT, v2 INT, PRIMARY KEY ((h) hash, r));

\d t1
\d t2

INSERT INTO t1 VALUES (1, 1, 11, 11), (1, 2, 11, 12);
INSERT INTO t2 VALUES (1, 1, 21, 21);

-- The following 2 inserts should produce error due to duplicate primary key / unique index value
INSERT INTO t1 VALUES (1, 1, 99, 99);
INSERT INTO t1 VALUES (1, 3, 11, 11);

INSERT INTO t1 VALUES (1, 3, 11, 13), (2, 1, 12, 13), (2, 2, 12, 14);

EXPLAIN (COSTS OFF) SELECT * FROM t1 ORDER BY h, r;
SELECT * FROM t1 ORDER BY h, r;

EXPLAIN (COSTS OFF) SELECT * FROM t1 WHERE h = 1 ORDER BY r;
SELECT * FROM t1 WHERE h = 1 ORDER BY r;

EXPLAIN (COSTS OFF) SELECT * FROM t1 WHERE yb_hash_code(h) = yb_hash_code(1) ORDER BY r;
SELECT * FROM t1 WHERE yb_hash_code(h) = yb_hash_code(1) ORDER BY r;

EXPLAIN (COSTS OFF) SELECT * FROM t1 WHERE h > 1 ORDER BY h, r;
SELECT * FROM t1 WHERE h > 1 ORDER BY h, r;

EXPLAIN (COSTS OFF) SELECT * FROM t1 WHERE h = 1 AND r = 1;
SELECT * FROM t1 WHERE h = 1 AND r = 1;

EXPLAIN (COSTS OFF) SELECT * FROM t1 WHERE yb_hash_code(h) = yb_hash_code(1) AND r = 1;
SELECT * FROM t1 WHERE yb_hash_code(h) = yb_hash_code(1) AND r = 1;

EXPLAIN (COSTS OFF) SELECT * FROM t1 WHERE v1 = 11 ORDER BY h, r;
SELECT * FROM t1 WHERE v1 = 11 ORDER BY h, r;

-- Disabled this test because we do not have proper stats. We return the same cost estimate
-- for indexes t1_v1_idx and t1_v1_v2_idx and Postgres will be either of them at random.
-- EXPLAIN (COSTS OFF) SELECT * FROM t1 WHERE v1 = 11 AND v2 = 11;
-- SELECT * FROM t1 WHERE v1 = 11 AND v2 = 11;

EXPLAIN (COSTS OFF) SELECT t1.h, t1.r, t1.v1, t2.v1 FROM t1, t2 WHERE t1.h = t2.h AND t1.r = t2.r;
SELECT t1.h, t1.r, t1.v1, t2.v1 FROM t1, t2 WHERE t1.h = t2.h AND t1.r = t2.r;

--
-- NULL value in index
--
CREATE TABLE null_index(k int, v int);
CREATE INDEX null_index_v ON null_index(v);
INSERT INTO null_index(k) VALUES(1);
INSERT INTO null_index VALUES(2, NULL);
INSERT INTO null_index VALUES(3, 3);

SELECT * FROM null_index ORDER BY k;
SELECT * FROM null_index WHERE v IS NULL ORDER BY k;
SELECT * FROM null_index WHERE v IS NOT NULL ORDER BY k;

--
-- NULL value in unique index
--
CREATE TABLE null_unique_index(k int, v int);
CREATE UNIQUE INDEX ON null_unique_index(k);
INSERT INTO null_unique_index(v) values(1);
INSERT INTO null_unique_index values (NULL, 2), (3, 3), (4, 4);
INSERT INTO null_unique_index values(4, 5); -- fail
EXPLAIN(COSTS OFF) SELECT * FROM null_unique_index WHERE k = 4;
EXPLAIN(COSTS OFF) SELECT * FROM null_unique_index WHERE k IS NULL ORDER BY k;
EXPLAIN(COSTS OFF) SELECT * FROM null_unique_index WHERE k IS NOT NULL ORDER BY k;
SELECT * FROM null_unique_index WHERE k = 4;
SELECT * FROM null_unique_index WHERE k IS NULL ORDER BY v;
SELECT * FROM null_unique_index WHERE k IS NOT NULL ORDER BY v;
DELETE FROM null_unique_index WHERE k = 3;
SELECT * FROM null_unique_index WHERE k IS NULL ORDER BY v;
SELECT * FROM null_unique_index WHERE k IS NOT NULL ORDER BY v;
EXPLAIN(COSTS OFF) DELETE FROM null_unique_index WHERE k IS NULL;
DELETE FROM null_unique_index WHERE k IS NULL;
SELECT * FROM null_unique_index ORDER BY v;
INSERT INTO null_unique_index values (NULL, 2), (3, 3), (NULL, 5);
EXPLAIN(COSTS OFF) UPDATE null_unique_index SET k = NULL WHERE k IS NOT NULL;
UPDATE null_unique_index SET k = NULL WHERE k IS NOT NULL;
SELECT * FROM null_unique_index ORDER BY v;

-- Test index update with UPDATE and DELETE
CREATE TABLE test_unique (k int PRIMARY KEY, v1 int, v2 int);
CREATE UNIQUE INDEX ON test_unique (v1);
CREATE INDEX ON test_unique (v2);

-- Insert a row
INSERT INTO test_unique VALUES (1, 1, 1);
SELECT * FROM test_unique;

-- UPDATE a row and verify the content of associated indexes via index-only scan
UPDATE test_unique SET v1 = 2 WHERE k = 1;
SELECT v1 FROM test_unique WHERE v1 IN (1, 2);
SELECT v2 FROM test_unique WHERE v2 IN (1, 2);

-- DELETE a row and verify the content of associated indexes via index-only scan
DELETE FROM test_unique WHERE k = 1;
SELECT v1 FROM test_unique WHERE v1 IN (1, 2);
SELECT v2 FROM test_unique WHERE v2 IN (1, 2);

-- Insert 2 rows of the affected v1 values. Make sure both can be inserted
-- with no duplicate key violation.
INSERT INTO test_unique VALUES (1, 1, 1);
INSERT INTO test_unique VALUES (2, 2, 2);
SELECT * FROM test_unique;

-- Test cascade-truncate indexes
CREATE TABLE test_truncate (a int PRIMARY KEY, b int);
CREATE UNIQUE INDEX test_truncate_index ON test_truncate (b);

INSERT INTO test_truncate VALUES (1, 2);
INSERT INTO test_truncate VALUES (2, 2);

EXPLAIN (COSTS OFF) SELECT b FROM test_truncate WHERE b = 2;
SELECT b FROM test_truncate WHERE b = 2;

TRUNCATE test_truncate;
SELECT b FROM test_truncate WHERE b = 2;

INSERT INTO test_truncate VALUES (2, 2);
INSERT INTO test_truncate VALUES (1, 2);

DROP TABLE test_truncate;

-- Test index methods
CREATE TABLE test_method (k int PRIMARY KEY, v int);
CREATE INDEX ON test_method USING btree (v);
CREATE INDEX ON test_method USING hash (v);
CREATE INDEX ON test_method USING foo (v);
\d test_method
DROP TABLE test_method;

-- Test include columns
CREATE TABLE test_include (c1 int, c2 int, c3 int);
INSERT INTO test_include VALUES (1, 1, 1), (1, 2, 2), (2, 2, 2), (3, 3, 3);
-- Expect duplicate key error
CREATE UNIQUE INDEX ON test_include (c1) include (c2);
\d test_include
DROP INDEX test_include_c1_c2_idx;
DELETE FROM test_include WHERE c1 = 1 AND c2 = 2;
CREATE UNIQUE INDEX ON test_include (c1) include (c2);
EXPLAIN (COSTS OFF) SELECT c1, c2 FROM test_include WHERE c1 = 1;
SELECT c1, c2 FROM test_include WHERE c1 = 1;
\d test_include
-- Verify the included column is updated in both the base table and the index. Use WHERE condition
-- on c1 below to force the scan on the index vs. base table. Select the non-included column c3 in
-- the other case to force the use of sequential scan on the base table.
UPDATE test_include SET c2 = 22 WHERE c1 = 2;
EXPLAIN (COSTS OFF) SELECT c1, c2 FROM test_include WHERE c1 > 0 ORDER BY c2;
EXPLAIN (COSTS OFF) SELECT * FROM test_include ORDER BY c2;
SELECT c1, c2 FROM test_include WHERE c1 > 0 ORDER BY c2;
SELECT * FROM test_include ORDER BY c2;
UPDATE test_include SET c2 = NULL WHERE c1 = 1;
-- TODO(mihnea) Disabled temporarily due to issue #1611
-- UPDATE test_include SET c2 = 33 WHERE c2 = 3;
DELETE FROM test_include WHERE c1 = 2;
SELECT c1, c2 FROM test_include WHERE c1 > 0 ORDER BY c2;
SELECT * FROM test_include ORDER BY c2;

-- Test SPLIT INTO
CREATE TABLE test_split (
  h1 int, h2 int, r1 int, r2 int, v1 int, v2 int,
  PRIMARY KEY ((h1, h2) HASH, r1, r2));
CREATE INDEX ON test_split (h2 HASH, r2, r1) SPLIT INTO 20 TABLETS;
CREATE INDEX ON test_split ((r1,r2) HASH) SPLIT INTO 20 TABLETS;
CREATE INDEX ON test_split (h2) SPLIT INTO 20 TABLETS;
\d test_split

-- These should fail
CREATE INDEX ON test_split (r1 ASC) SPLIT INTO 20 TABLETS;
CREATE INDEX ON test_split (h2 ASC, r1) SPLIT INTO 20 TABLETS;
CREATE INDEX ON test_split (h1 HASH) SPLIT INTO 10000 TABLETS;

-- Test hash methods
CREATE TABLE test_method (
  h1 int, h2 int, r1 int, r2 int, v1 int, v2 int,
  PRIMARY KEY ((h1, h2) HASH, r1, r2));
CREATE INDEX ON test_method (h2 HASH, r2, r1);
CREATE INDEX ON test_method (r1, r2);
CREATE UNIQUE INDEX ON test_method (v1, v2);
CREATE INDEX ON test_method ((h1, h2) HASH, r2, r1);
CREATE INDEX ON test_method ((h2, h1), r2 DESC, r1);
CREATE UNIQUE INDEX ON test_method ((h1, (h2 % 8)) HASH, r2, r1);
\d test_method

-- These should issue NOTICE and verify pg_get_indexdef() output doesn't have
-- NULLS FIRST/NULLS LAST
CREATE INDEX ON test_method (h1 HASH NULLS FIRST);
SELECT pg_get_indexdef('test_method_h1_idx'::regclass);
CREATE INDEX ON test_method (h1 HASH NULLS LAST);
SELECT pg_get_indexdef('test_method_h1_idx1'::regclass);
CREATE INDEX ON test_method (h1 NULLS LAST);
SELECT pg_get_indexdef('test_method_h1_idx2'::regclass);
CREATE INDEX ON test_method (h1 NULLS LAST);
SELECT pg_get_indexdef('test_method_h1_idx3'::regclass);
CREATE INDEX ON test_method ((h1 % 8) HASH NULLS FIRST);
SELECT pg_get_indexdef('test_method_expr_idx'::regclass);
CREATE INDEX ON test_method ((h1 % 8) HASH NULLS LAST);
SELECT pg_get_indexdef('test_method_expr_idx1'::regclass);
CREATE INDEX ON test_method ((h1 % 8) NULLS FIRST);
SELECT pg_get_indexdef('test_method_expr_idx2'::regclass);
CREATE INDEX ON test_method ((h1 % 8) NULLS LAST);
SELECT pg_get_indexdef('test_method_expr_idx3'::regclass);
\d test_method
DROP INDEX test_method_expr_idx;
DROP INDEX test_method_expr_idx1;
DROP INDEX test_method_expr_idx2;
DROP INDEX test_method_expr_idx3;
DROP INDEX test_method_h1_idx;
DROP INDEX test_method_h1_idx1;
DROP INDEX test_method_h1_idx2;
DROP INDEX test_method_h1_idx3;

-- Test should not issue NOTICE
CREATE INDEX ON test_method (r1 ASC NULLS FIRST, r2 ASC NULLS LAST);
CREATE INDEX ON test_method (r1 DESC NULLS FIRST, r2 DESC NULLS LAST);

CREATE DATABASE colocation_test colocation = true;
\c colocation_test
CREATE TABLE test_method (r1 int, r2 int, v1 int, v2 int,
  PRIMARY KEY (r1, r2));
CREATE INDEX ON test_method (r1 NULLS FIRST);
CREATE INDEX ON test_method (r1 NULLS LAST);
\d test_method
\c yugabyte
DROP DATABASE colocation_test;

CREATE TABLEGROUP tbl_group;
CREATE TABLE tbl_group_tbl (r1 int, r2 int, v1 int, v2 int,
  PRIMARY KEY (r1, r2)) TABLEGROUP tbl_group;
CREATE INDEX idx_tbl_group_tbl ON tbl_group_tbl (r1 NULLS FIRST);
CREATE INDEX idx2_tbl_group_tbl ON tbl_group_tbl (r1 NULLS LAST);
\d tbl_group_tbl
DROP TABLE tbl_group_tbl;
DROP TABLEGROUP tbl_group;

-- These should fail
CREATE INDEX ON test_method (h1 HASH, h2 HASH, r2, r1);
CREATE INDEX ON test_method (r1, h1 HASH);
CREATE INDEX ON test_method (() HASH);
CREATE INDEX ON test_method (());
CREATE INDEX ON test_method (r1 DESC, (h2, h1));
CREATE INDEX ON test_method ((h1, h2) HASH NULLS FIRST);
CREATE INDEX ON test_method ((h1, h2) HASH NULLS LAST);
CREATE INDEX ON test_method ((h1 % 8, h2) HASH NULLS FIRST);
CREATE INDEX ON test_method ((h1 % 8, h2) HASH NULLS LAST);

INSERT INTO test_method VALUES
  (1, 1, 1, 1, 1, 11),
  (1, 1, 1, 2, 2, 12),
  (1, 1, 2, 1, 3, 13),
  (1, 1, 2, 2, 4, 14),
  (1, 2, 1, 1, 5, 15),
  (1, 2, 1, 2, 6, 16),
  (1, 2, 2, 1, 7, 17),
  (1, 2, 2, 2, 8, 18),
  (2, 0, 1, 1, 9, 19),
  (2, 1, 1, 2, 10, 20);

-- Test scans using different indexes. Verify order by.
EXPLAIN (COSTS OFF) SELECT * FROM test_method ORDER BY h1, h2;
SELECT * FROM test_method ORDER BY h1, h2;
EXPLAIN (COSTS OFF) SELECT * FROM test_method WHERE h1 = 1 AND h2 = 1 ORDER BY r1, r2;
SELECT * FROM test_method WHERE h1 = 1 AND h2 = 1 ORDER BY r1, r2;
EXPLAIN (COSTS OFF) SELECT * FROM test_method ORDER BY r1, r2;
SELECT * FROM test_method ORDER BY r1, r2;
EXPLAIN (COSTS OFF) SELECT * FROM test_method WHERE v1 > 5 ORDER BY v1, v2;
SELECT * FROM test_method WHERE v1 > 5 ORDER BY v1, v2;
EXPLAIN (COSTS OFF) SELECT * FROM test_method WHERE h2 = 2 ORDER BY r1, r2;
SELECT * FROM test_method WHERE h2 = 2 ORDER BY r1, r2;
EXPLAIN (COSTS OFF) SELECT * FROM test_method WHERE h2 = 1 AND h1 = 1 ORDER BY r2 DESC, r1;
SELECT * FROM test_method WHERE h2 = 1 AND h1 = 1 ORDER BY r2 DESC, r1;
EXPLAIN (COSTS OFF) SELECT * FROM test_method WHERE h2 = 1 AND h1 = 1 ORDER BY r2, r1;
SELECT * FROM test_method WHERE h2 = 1 AND h1 = 1 ORDER BY r2, r1;
EXPLAIN (COSTS OFF) SELECT * FROM test_method WHERE h2 % 8 = 2 AND h1 = 1 ORDER BY r2, r1;
SELECT * FROM test_method WHERE h2 % 8 = 2 AND h1 = 1 ORDER BY r2, r1;

-- Test update using a hash index
EXPLAIN (COSTS OFF) UPDATE test_method SET v2 = v2 + 10 WHERE h2 = 2;
UPDATE test_method SET v2 = v2 + 10 WHERE h2 = 2;
SELECT * FROM test_method ORDER BY h1, h2;
SELECT * FROM test_method ORDER BY r1, r2;

-- Test delete using a unique index
EXPLAIN (COSTS OFF) DELETE FROM test_method WHERE v1 = 5 AND v2 = 25;
DELETE FROM test_method WHERE v1 = 5 AND v2 = 25;

-- Test delete using the primary key
EXPLAIN (COSTS OFF) DELETE FROM test_method WHERE h1 = 2 AND h2 = 0;
DELETE FROM test_method WHERE h1 = 2 AND h2 = 0;

SELECT * FROM test_method ORDER BY h1, h2;

-- Test update using a unique index on hashed expr
UPDATE test_method SET h2 = 258 WHERE h2 % 8 = 1 AND h1 = 2;
SELECT * FROM test_method ORDER BY h1, h2;

-- This should fail
UPDATE test_method SET h2 = 257 WHERE h2 % 8 = 2 AND h1 = 1;

-- Test insert using a unique index on hashed expr
-- This should fail
INSERT INTO test_method VALUES (1, 10, 2, 2, 8, 100);

-- Test hash with extra parenthesis on a single column
CREATE INDEX ON test_method ((h2) HASH);
\d test_method
EXPLAIN (COSTS OFF) SELECT * FROM test_method WHERE h2 = 258;
SELECT * FROM test_method WHERE h2 = 258;
DROP TABLE test_method;

-- Test more HASH key cases in PRIMARY KEY
CREATE TABLE test_hash (
  h1 int, h2 int, r1 int, r2 int, v1 int, v2 int);

-- These should fail
ALTER TABLE test_hash ADD PRIMARY KEY ((h1 % 8, h2) HASH, r1, r2);
ALTER TABLE test_hash ADD PRIMARY KEY ((h1, h2) HASH, (r1 + r2) DESC);
-- Extra parenthesis turns the column group into a RowExpr - should fail
ALTER TABLE test_hash ADD PRIMARY KEY (((h1, h2)) HASH);
ALTER TABLE test_hash ADD PRIMARY KEY (((h1, h2)));

-- This should succeed
CREATE UNIQUE INDEX test_hash_h1_h2mod8_r2_r1_idx ON test_hash ((h1, (h2 % 8)) HASH, r2, r1);
-- This should fail
ALTER TABLE test_hash ADD PRIMARY KEY USING INDEX test_hash_h1_h2mod8_r2_r1_idx;

-- These CREATE TABLE statements should fail
DROP TABLE test_hash;
CREATE TABLE test_hash (
  h1 int, h2 int, r1 int, r2 int, v1 int, v2 int,
  PRIMARY KEY (r1, (h1, h2) HASH));

CREATE TABLE test_hash (
  h1 int, h2 int, r1 int, r2 int, v1 int, v2 int,
  PRIMARY KEY ((h1 + 666) HASH));

CREATE TABLE test_hash (
  h1 int, h2 int, r1 int, r2 int, v1 int, v2 int,
  PRIMARY KEY ((h1, (h2 + 666)) HASH, r1, r2));

-- Test ordering on split indexes
CREATE TABLE tbl(k SERIAL PRIMARY KEY, v INT);
CREATE INDEX ON tbl(v ASC) SPLIT AT VALUES((10), (20), (30));
INSERT INTO tbl(v) VALUES
    (5), (6), (16), (15), (25), (26), (36), (35), (46), (10), (20), (30), (40), (6), (16), (26);
EXPLAIN (COSTS OFF) SELECT * FROM tbl ORDER BY v ASC;
SELECT * FROM tbl ORDER BY v ASC;
EXPLAIN (COSTS OFF) SELECT * FROM tbl ORDER BY v DESC;
SELECT * FROM tbl ORDER BY v DESC;
EXPLAIN (COSTS OFF) SELECT v FROM tbl WHERE v > 10 and v <= 40 ORDER BY v DESC;
SELECT v FROM tbl WHERE v > 10 and v <= 40 ORDER BY v DESC;
EXPLAIN (COSTS OFF) SELECT v FROM tbl WHERE v > 10 and v <= 40 ORDER BY v ASC;
SELECT v FROM tbl WHERE v > 10 and v <= 40 ORDER BY v ASC;

DROP TABLE tbl;
CREATE TABLE tbl(k SERIAL PRIMARY KEY, v INT);
CREATE UNIQUE INDEX ON tbl(v DESC) SPLIT AT VALUES((30), (20), (10));
INSERT INTO tbl(v) VALUES
    (5), (6), (16), (15), (25), (26), (36), (35), (46), (10), (20), (30), (40);
EXPLAIN (COSTS OFF) SELECT * FROM tbl ORDER BY v ASC;
SELECT * FROM tbl ORDER BY v ASC;
EXPLAIN (COSTS OFF) SELECT * FROM tbl ORDER BY v DESC;
SELECT * FROM tbl ORDER BY v DESC;
EXPLAIN (COSTS OFF) SELECT v FROM tbl WHERE v >= 10 and v < 40 ORDER BY v ASC;
SELECT v FROM tbl WHERE v >= 10 and v < 40 ORDER BY v ASC;
EXPLAIN (COSTS OFF) SELECT v FROM tbl WHERE v >= 10 and v < 40 ORDER BY v DESC;
SELECT v FROM tbl WHERE v >= 10 and v < 40 ORDER BY v DESC;

-- Test creating indexes with (table_oid = x)
CREATE TABLE test_index_with_oids (v1 INT, v2 INT, v3 INT);
INSERT INTO test_index_with_oids VALUES (1, 11, 21), (2, 12, 22), (3, 13, 23), (4, 14, 24), (5, 15, 25);

-- Test with variable = false
CREATE INDEX index_with_table_oid ON test_index_with_oids (v1) with (table_oid = 1111111);
-- Turn on variable and test
set yb_enable_create_with_table_oid=1;
CREATE INDEX index_with_invalid_oid ON test_index_with_oids (v1) with (table_oid = 0);
CREATE INDEX index_with_invalid_oid ON test_index_with_oids (v1) with (table_oid = -1);
CREATE INDEX index_with_invalid_oid ON test_index_with_oids (v1) with (table_oid = 123);
CREATE INDEX index_with_invalid_oid ON test_index_with_oids (v1) with (table_oid = 'test');

CREATE INDEX index_with_table_oid ON test_index_with_oids (v1) with (table_oid = 1111111);
select relname, oid from pg_class where relname = 'index_with_table_oid';
SELECT * FROM test_index_with_oids ORDER BY v1;

CREATE INDEX index_with_duplicate_table_oid ON test_index_with_oids (v1) with (table_oid = 1111111);
set yb_enable_create_with_table_oid=0;

-- Test creating index nonconcurrently (i.e. without online schema migration)
CREATE TABLE test_index_nonconcurrently (i INT, t TEXT);
INSERT INTO test_index_nonconcurrently VALUES (generate_series(1, 10), 'a');

CREATE INDEX NONCONCURRENTLY ON test_index_nonconcurrently (i);
EXPLAIN (COSTS OFF) SELECT i FROM test_index_nonconcurrently WHERE i = 1;
SELECT * FROM test_index_nonconcurrently WHERE i = 1;
DROP INDEX test_index_nonconcurrently_i_idx;

CREATE UNIQUE INDEX NONCONCURRENTLY ON test_index_nonconcurrently (i);
EXPLAIN (COSTS OFF) SELECT i FROM test_index_nonconcurrently WHERE i = 1;
INSERT INTO test_index_nonconcurrently VALUES (1, 'b');
DROP INDEX test_index_nonconcurrently_i_idx;

INSERT INTO test_index_nonconcurrently VALUES (1, 'b');
CREATE UNIQUE INDEX NONCONCURRENTLY ON test_index_nonconcurrently (i);

DROP TABLE test_index_nonconcurrently;

-- Verify that creating indexes on a YB table does not update table stats.
CREATE TABLE test_stats (i INT);
INSERT INTO test_stats VALUES (1), (2), (3);
ANALYZE test_stats;
SELECT reltuples FROM pg_class WHERE relname = 'test_stats';
CREATE INDEX CONCURRENTLY ON test_stats(i);
SELECT reltuples FROM pg_class WHERE relname = 'test_stats';
CREATE INDEX NONCONCURRENTLY ON test_stats(i);
SELECT reltuples FROM pg_class WHERE relname = 'test_stats';
DROP TABLE test_stats;

-- Split options on a partitioned table or its indexes should not be copied
-- for a newly attached partition.
CREATE TABLE test_part (a INT, PRIMARY KEY(a ASC)) PARTITION BY RANGE (a)
    SPLIT AT VALUES ((5), (10)); -- split options are ignored.
CREATE TABLE test_part_1 PARTITION OF test_part FOR VALUES FROM (1) TO (5);
SELECT yb_get_range_split_clause('test_part_1'::regclass);
CREATE INDEX test_part_idx ON test_part(a) SPLIT INTO 5 TABLETS;
SELECT num_tablets, num_hash_key_columns FROM yb_table_properties('test_part_1_a_idx'::regclass);
CREATE TABLE test_part_2 PARTITION OF test_part DEFAULT;
SELECT num_tablets, num_hash_key_columns FROM yb_table_properties('test_part_2_a_idx'::regclass);

-- Test creating temp index using lsm.
CREATE TEMP TABLE test_temp_lsm (i int);
CREATE INDEX ON test_temp_lsm USING lsm (i);

-- temp tables
CREATE TEMPORARY TABLE temp_table(a int, b int);
CREATE INDEX CONCURRENTLY temp_index ON temp_table(a);

-- Cleanup.
DISCARD TEMP;
