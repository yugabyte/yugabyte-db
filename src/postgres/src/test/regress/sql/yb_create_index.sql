--
-- CREATE_INDEX
-- Create ancillary data structures (i.e. indices)
--

--
-- BTREE
--
CREATE INDEX onek_unique1 ON onek USING btree(unique1 int4_ops);

CREATE INDEX IF NOT EXISTS onek_unique1 ON onek USING btree(unique1 int4_ops);

CREATE INDEX IF NOT EXISTS ON onek USING btree(unique1 int4_ops);

CREATE INDEX onek_unique2 ON onek USING btree(unique2 int4_ops);

CREATE INDEX onek_hundred ON onek USING btree(hundred int4_ops);

CREATE INDEX onek_stringu1 ON onek USING btree(stringu1 name_ops);

CREATE INDEX tenk1_unique1 ON tenk1 USING btree(unique1 int4_ops);

CREATE INDEX tenk1_unique2 ON tenk1 USING btree(unique2 int4_ops);

CREATE INDEX tenk1_hundred ON tenk1 USING btree(hundred int4_ops);

CREATE INDEX tenk1_thous_tenthous ON tenk1 (thousand, tenthous);

CREATE INDEX tenk2_unique1 ON tenk2 USING btree(unique1 int4_ops);

CREATE INDEX tenk2_unique2 ON tenk2 USING btree(unique2 int4_ops);

CREATE INDEX tenk2_hundred ON tenk2 USING btree(hundred int4_ops);

CREATE INDEX rix ON road USING btree (name text_ops);

CREATE INDEX iix ON ihighway USING btree (name text_ops);

CREATE INDEX six ON shighway USING btree (name text_ops);

CREATE INDEX onek_two_idx ON onek USING btree(two);

DROP INDEX onek_two_idx;

DROP INDEX onek_two_idx;

DROP INDEX IF EXISTS onek_two_idx;

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
SELECT * FROM test_index WHERE v1 IS NOT NULL;
SELECT * FROM test_index WHERE v1 IN (1, 2, 3);


-- Verify indexes on system catalog tables are updated properly

CREATE TABLE test_sys_catalog_update (k int primary key, v int);

EXPLAIN SELECT relname FROM pg_class WHERE relname = 'test_sys_catalog_update';
SELECT relname  FROM pg_class WHERE relname = 'test_sys_catalog_update';

EXPLAIN SELECT typname FROM pg_type WHERE typname = 'test_sys_catalog_update';
SELECT typname FROM pg_type WHERE typname = 'test_sys_catalog_update';

EXPLAIN SELECT attname, atttypid FROM pg_attribute WHERE attname = 'v';
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
CREATE TABLE t1 (h INT, r INT, v1 INT, v2 INT, PRIMARY KEY (h, r));
CREATE INDEX ON t1 (v1);
CREATE UNIQUE INDEX ON t1 (v1, v2);
CREATE TABLE t2 (h INT, r INT, v1 INT, v2 INT, PRIMARY KEY (h, r));

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

EXPLAIN (COSTS OFF) SELECT * FROM t1 WHERE h > 1 ORDER BY h, r;
SELECT * FROM t1 WHERE h > 1 ORDER BY h, r;

EXPLAIN (COSTS OFF) SELECT * FROM t1 WHERE h = 1 AND r = 1;
SELECT * FROM t1 WHERE h = 1 AND r = 1;

EXPLAIN (COSTS OFF) SELECT * FROM t1 WHERE v1 = 11 ORDER BY h, r;
SELECT * FROM t1 WHERE v1 = 11 ORDER BY h, r;

-- Disabled this test because we do not have proper stats. We return the same cost estimate
-- for indexes t1_v1_idx and t1_v1_v2_idx and Postgres will be either of them at random.
-- EXPLAIN (COSTS OFF) SELECT * FROM t1 WHERE v1 = 11 AND v2 = 11;
-- SELECT * FROM t1 WHERE v1 = 11 AND v2 = 11;

EXPLAIN (COSTS OFF) SELECT t1.h, t1.r, t1.v1, t2.v1 FROM t1, t2 WHERE t1.h = t2.h AND t1.r = t2.r;
SELECT t1.h, t1.r, t1.v1, t2.v1 FROM t1, t2 WHERE t1.h = t2.h AND t1.r = t2.r;
