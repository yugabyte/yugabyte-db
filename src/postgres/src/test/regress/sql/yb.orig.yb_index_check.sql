SET yb_explain_hide_non_deterministic_fields = true;

-- Setup
CREATE TABLE abcd(a int primary key, b int, c int, d int);
CREATE INDEX abcd_b_c_d_idx ON abcd (b ASC) INCLUDE (c, d);
INSERT INTO abcd SELECT i, i, i, i FROM generate_series(1, 10) i;

-- Basic test
SET yb_bnl_batch_size = 3;
EXPLAIN (ANALYZE, DIST, COSTS OFF, SUMMARY OFF) SELECT yb_index_check('abcd_b_c_d_idx'::regclass::oid);
SELECT yb_index_check('abcd_b_c_d_idx'::regclass::oid);
RESET yb_bnl_batch_size;

-- Insert more data
INSERT INTO abcd SELECT i, i, i, i FROM generate_series(11, 2000) i;
INSERT INTO abcd values (2001, NULL, NULL, NULL);
INSERT INTO abcd values (2002, 2002, 1, 2);
INSERT INTO abcd values (2003, 2003, 3, NULL);
INSERT INTO abcd values (2004, 2004, NULL, 4);

-- Partial index
CREATE INDEX abcd_b_c_idx ON abcd(b) INCLUDE (c) WHERE d > 50;
SELECT yb_index_check('abcd_b_c_idx'::regclass::oid);

CREATE OR REPLACE FUNCTION double_value(input_value NUMERIC)
RETURNS NUMERIC AS $$
BEGIN
    RETURN input_value * 2;
END;
$$ LANGUAGE plpgsql IMMUTABLE;

CREATE INDEX abcd_b_c_idx1 ON abcd(b) INCLUDE (c) WHERE double_value(d) > 50;
SELECT yb_index_check('abcd_b_c_idx1'::regclass::oid);

-- Expression index
CREATE INDEX abcd_expr_expr1_d_idx ON abcd ((2*c) ASC, (2*b) ASC) INCLUDE (d);
SELECT yb_index_check('abcd_expr_expr1_d_idx'::regclass::oid);
CREATE INDEX abcd_double_value_d_idx ON abcd (double_value(c) ASC) INCLUDE (d);
SELECT yb_index_check('abcd_double_value_d_idx'::regclass::oid);

-- Unique index
CREATE UNIQUE INDEX abcd_b_c_d_idx1 ON abcd (b ASC) INCLUDE (c, d);
SELECT yb_index_check('abcd_b_c_d_idx1'::regclass::oid);

-- NULLS NOT DISTINCT
CREATE UNIQUE INDEX abcd_b_c_d_idx2 ON abcd (b ASC) INCLUDE (c, d) NULLS NOT DISTINCT;
SELECT yb_index_check('abcd_b_c_d_idx2'::regclass::oid);

-- Index does not exist
SELECT yb_index_check('nonexisting_idx'::regclass::oid);

-- PK index
SELECT yb_index_check('abcd_pkey'::regclass::oid);

-- Non-LSM index
CREATE TEMP TABLE mytemp(a int unique);
SELECT yb_index_check('mytemp_a_key'::regclass::oid);

-- Non index relation oid
SELECT yb_index_check('abcd'::regclass::oid);

-- Types without equality operator
CREATE TABLE json_table (a TEXT, b JSON, c INT);
CREATE INDEX json_table_a_b_idx ON json_table(a) INCLUDE (b);
INSERT INTO json_table VALUES ('test', '{"test1":"test2", "test3": 4}', 1);
SELECT yb_index_check('json_table_a_b_idx'::regclass::oid);

-- Inconsistent Indexes
SELECT oid AS db_oid FROM pg_database WHERE datname = (
    SELECT CASE
        WHEN COUNT(*) = 1 THEN 'template1'
        ELSE current_database() END FROM pg_yb_catalog_version) \gset
SELECT
$force_cache_refresh$
SET yb_non_ddl_txn_for_sys_tables_allowed TO on;
UPDATE pg_yb_catalog_version
   SET current_version       = current_version + 1,
       last_breaking_version = current_version + 1
 WHERE db_oid = :db_oid;
RESET yb_non_ddl_txn_for_sys_tables_allowed;
DO
$$
BEGIN
    PERFORM pg_sleep(1);
END;
$$;
$force_cache_refresh$ AS force_cache_refresh \gset


SET yb_non_ddl_txn_for_sys_tables_allowed = TRUE;

-- Missing Index Row
UPDATE pg_index SET indisready = FALSE, indisvalid = FALSE, indislive = FALSE WHERE indexrelid = 'abcd_b_c_d_idx'::regclass;
:force_cache_refresh
INSERT INTO abcd VALUES (2005, 2005, 2005, 2005);
SELECT yb_index_check('abcd_b_c_d_idx'::regclass::oid);
UPDATE pg_index SET indisready = TRUE, indisvalid = TRUE, indislive = TRUE WHERE indexrelid = 'abcd_b_c_d_idx'::regclass;
:force_cache_refresh
SELECT yb_index_check('abcd_b_c_d_idx'::regclass::oid);

DELETE FROM abcd WHERE a = 2005; -- Reset
SELECT yb_index_check('abcd_b_c_d_idx'::regclass::oid);

-- Spurious Index Row
UPDATE pg_index SET indisready = FALSE, indisvalid = FALSE, indislive = FALSE WHERE indexrelid = 'abcd_b_c_d_idx'::regclass;
:force_cache_refresh
DELETE FROM abcd WHERE a = 1;
UPDATE pg_index SET indisready = TRUE, indisvalid = TRUE, indislive = TRUE WHERE indexrelid = 'abcd_b_c_d_idx'::regclass;
:force_cache_refresh
SELECT yb_index_check('abcd_b_c_d_idx'::regclass::oid);

INSERT INTO abcd VALUES (1, 1, 1, 1);
SELECT yb_index_check('abcd_b_c_d_idx'::regclass::oid);

-- Spurious Index Row with NULL Index Attributes
UPDATE pg_index SET indisready = FALSE, indisvalid = FALSE, indislive = FALSE WHERE indexrelid = 'abcd_b_c_d_idx'::regclass;
:force_cache_refresh
DELETE FROM abcd WHERE a = 2001;
UPDATE pg_index SET indisready = TRUE, indisvalid = TRUE, indislive = TRUE WHERE indexrelid = 'abcd_b_c_d_idx'::regclass;
:force_cache_refresh
SELECT yb_index_check('abcd_b_c_d_idx'::regclass::oid);

INSERT INTO abcd VALUES (2001, NULL, NULL, NULL);
SELECT yb_index_check('abcd_b_c_d_idx'::regclass::oid);

-- Inconsistent Non-Key Column
UPDATE pg_index SET indisready = FALSE, indisvalid = FALSE, indislive = FALSE WHERE indexrelid = 'abcd_b_c_d_idx'::regclass;
:force_cache_refresh
UPDATE abcd SET d = d + 1 WHERE a = 1;
UPDATE pg_index SET indisready = TRUE, indisvalid = TRUE, indislive = TRUE WHERE indexrelid = 'abcd_b_c_d_idx'::regclass;
:force_cache_refresh
SELECT yb_index_check('abcd_b_c_d_idx'::regclass::oid);

UPDATE abcd SET d = 1 WHERE a = 1;
SELECT yb_index_check('abcd_b_c_d_idx'::regclass::oid);

-- Inconsistent Key Column
UPDATE pg_index SET indisready = FALSE, indisvalid = FALSE, indislive = FALSE WHERE indexrelid = 'abcd_b_c_d_idx'::regclass;
:force_cache_refresh
UPDATE abcd SET b = 9999 WHERE a = 1;
UPDATE pg_index SET indisready = TRUE, indisvalid = TRUE, indislive = TRUE WHERE indexrelid = 'abcd_b_c_d_idx'::regclass;
:force_cache_refresh
SELECT yb_index_check('abcd_b_c_d_idx'::regclass::oid);

UPDATE abcd SET b = 1 WHERE a = 1;
SELECT yb_index_check('abcd_b_c_d_idx'::regclass::oid);

-- Spurious Row Due to Partial Index
UPDATE pg_index SET indisready = FALSE, indisvalid = FALSE, indislive = FALSE WHERE indexrelid = 'abcd_b_c_idx'::regclass;
:force_cache_refresh
UPDATE abcd SET d = 50 WHERE a = 51;
UPDATE pg_index SET indisready = TRUE, indisvalid = TRUE, indislive = TRUE WHERE indexrelid = 'abcd_b_c_idx'::regclass;
:force_cache_refresh
SELECT yb_index_check('abcd_b_c_idx'::regclass::oid);

UPDATE abcd SET d = 51 WHERE a = 51;
SELECT yb_index_check('abcd_b_c_idx'::regclass::oid);
SET yb_non_ddl_txn_for_sys_tables_allowed = FALSE;

-- Index of a partitioned table
CREATE TABLE part(a int, b int, c int, d int) PARTITION BY RANGE(a);
CREATE INDEX ON part(b) include (c, d);
CREATE TABLE part_1_2k PARTITION OF part FOR VALUES FROM(1) TO(2000);
CREATE TABLE part_2 PARTITION OF part FOR VALUES FROM(2000) TO(6000) PARTITION BY RANGE(a);
CREATE TABLE part_2k_4k PARTITION OF part_2 FOR VALUES FROM(2000) TO(4000);
CREATE TABLE part_4k_6k PARTITION OF part_2 FOR VALUES FROM(4000) TO(6000);
INSERT INTO part SELECT i, i, i, i FROM generate_series(1, 5999) i;

SELECT yb_index_check('part_b_c_d_idx'::regclass::oid);
SELECT yb_index_check('part_1_2k_b_c_d_idx'::regclass::oid);
SELECT yb_index_check('part_2_b_c_d_idx'::regclass::oid);
SELECT yb_index_check('part_2k_4k_b_c_d_idx'::regclass::oid);
SELECT yb_index_check('part_4k_6k_b_c_d_idx'::regclass::oid);

-- Index of a colocated relation
CREATE DATABASE colocateddb COLOCATION = TRUE;
\c colocateddb
CREATE TABLE abcd1(a int primary key, b int, c int, d int);
CREATE INDEX abcd1_b_c_d_idx ON abcd1(b ASC) INCLUDE (c, d);
INSERT INTO abcd1 SELECT i, i, i, i FROM generate_series(1, 1000) i;

CREATE TABLE abcd2(a int primary key, b int, c int, d int);
CREATE INDEX abcd2_b_c_d_idx ON abcd2(b ASC) INCLUDE (c, d);
INSERT INTO abcd2 SELECT i, i, i, i FROM generate_series(1, 2000) i;

SELECT yb_index_check('abcd1_b_c_d_idx'::regclass::oid);
SELECT yb_index_check('abcd2_b_c_d_idx'::regclass::oid);

-- GIN indexes (not support yet)
CREATE TABLE vectors (i serial PRIMARY KEY, v tsvector);
CREATE INDEX ON vectors USING ybgin (v) where i > 5;
SELECT yb_index_check('vectors_v_idx'::regclass::oid);

-- Variable length data type
CREATE TABLE test_bytea(a int primary key, b int, c bytea);
CREATE INDEX test_bytea_b_c_idx ON test_bytea (b ASC) INCLUDE (c);
CREATE INDEX test_bytea_c_b_idx ON test_bytea (c) INCLUDE (b);
INSERT INTO test_bytea (a, b, c)  VALUES (1, 42, E'\\x48656c6c6f20576f726c64');
INSERT INTO test_bytea (a, b, c)  VALUES (2, 42, 'test');
SELECT yb_index_check('test_bytea_b_c_idx'::regclass::oid);
SELECT yb_index_check('test_bytea_c_b_idx'::regclass::oid);
