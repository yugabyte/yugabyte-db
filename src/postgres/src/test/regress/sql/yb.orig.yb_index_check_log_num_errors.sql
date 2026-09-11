SET yb_bnl_batch_size = 3;

\set ECHO all
-- Multi-snapshot-only coverage for log_num_errors > 0 reporting.
\set yb_index_check_log_num_errors 100
\set check_index 'SELECT * FROM yb_index_check(:idx, false, :yb_index_check_log_num_errors);'

\i yb_commands/yb_index_check_setup.sql

-- log_num_errors > 0 is rejected in single_snapshot_mode
CREATE TABLE t_single_snap(a int PRIMARY KEY, b int);
CREATE INDEX t_single_snap_b_idx ON t_single_snap(b);
SELECT * FROM yb_index_check('t_single_snap_b_idx'::regclass::oid, true,
                             :yb_index_check_log_num_errors);
DROP TABLE t_single_snap;

--
-- Test to validate reporting across different data types.
--
CREATE TABLE t_key_mismatch_types (
    id           INT PRIMARY KEY,
    int_col      INT,
    numeric_col  NUMERIC,
    float_col    FLOAT8,
    bool_col     BOOL,
    ts_col       TIMESTAMP,
    str_col      TEXT,
    jsonb_col    JSONB,
    nullable_col INT
);
CREATE INDEX int_idx      ON t_key_mismatch_types (int_col) INCLUDE (jsonb_col);
CREATE INDEX numeric_idx  ON t_key_mismatch_types (numeric_col);
CREATE INDEX float_idx    ON t_key_mismatch_types (float_col);
CREATE INDEX bool_idx     ON t_key_mismatch_types (bool_col);
CREATE INDEX ts_idx       ON t_key_mismatch_types (ts_col);
CREATE INDEX str_idx      ON t_key_mismatch_types (str_col);
CREATE INDEX nullable_idx ON t_key_mismatch_types (nullable_col);

-- numeric_col uses id*10+0.5 and float_col uses id*10+0.25 (exact in binary)
-- so neither column value equals the id column.
INSERT INTO t_key_mismatch_types VALUES
    (1, 10, 10.5, 10.25, true,  '2024-01-01 00:00:00', 'str_1', '{"key": "value"}', 10),
    (2, 20, 20.5, 20.25, true,  '2024-02-01 00:00:00', 'str_2', '{"key": "value"}', 20),
    (3, 30, 30.5, 30.25, true,  '2024-03-01 00:00:00', 'str_3', '{"key": "value"}', 30),
    (4, 40, 40.5, 40.25, true,  '2024-04-01 00:00:00', 'str_4', '{"key": "value"}', 40),
    (5, 50, 50.5, 50.25, true,  '2024-05-01 00:00:00', 'str_5', '{"key": "value"}', 50),
    (6, 60, 60.5, 60.25, true,  '2024-06-01 00:00:00', 'str_6', '{"key": "value"}', 60),
    (7, 70, 70.5, 70.25, true,  '2024-07-01 00:00:00', 'str_7', '{"key": "value"}', 70);

-- int (row 1)
UPDATE pg_index SET indisready=FALSE, indisvalid=FALSE, indislive=FALSE WHERE indexrelid='int_idx'::regclass;
:force_cache_refresh
UPDATE t_key_mismatch_types SET int_col = 99 WHERE id = 1;
UPDATE pg_index SET indisready=TRUE, indisvalid=TRUE, indislive=TRUE WHERE indexrelid='int_idx'::regclass;
:force_cache_refresh
\set idx '''int_idx''::regclass::oid'
:check_index

-- numeric (row 2)
UPDATE pg_index SET indisready=FALSE, indisvalid=FALSE, indislive=FALSE WHERE indexrelid='numeric_idx'::regclass;
:force_cache_refresh
UPDATE t_key_mismatch_types SET numeric_col = 99.9 WHERE id = 2;
UPDATE pg_index SET indisready=TRUE, indisvalid=TRUE, indislive=TRUE WHERE indexrelid='numeric_idx'::regclass;
:force_cache_refresh
\set idx '''numeric_idx''::regclass::oid'
:check_index

-- float (row 3): 30.25 and 9.5 are both exact in binary floating point
UPDATE pg_index SET indisready=FALSE, indisvalid=FALSE, indislive=FALSE WHERE indexrelid='float_idx'::regclass;
:force_cache_refresh
UPDATE t_key_mismatch_types SET float_col = 9.5 WHERE id = 3;
UPDATE pg_index SET indisready=TRUE, indisvalid=TRUE, indislive=TRUE WHERE indexrelid='float_idx'::regclass;
:force_cache_refresh
\set idx '''float_idx''::regclass::oid'
:check_index

-- bool (row 4)
UPDATE pg_index SET indisready=FALSE, indisvalid=FALSE, indislive=FALSE WHERE indexrelid='bool_idx'::regclass;
:force_cache_refresh
UPDATE t_key_mismatch_types SET bool_col = false WHERE id = 4;
UPDATE pg_index SET indisready=TRUE, indisvalid=TRUE, indislive=TRUE WHERE indexrelid='bool_idx'::regclass;
:force_cache_refresh
\set idx '''bool_idx''::regclass::oid'
:check_index

-- timestamp (row 5)
UPDATE pg_index SET indisready=FALSE, indisvalid=FALSE, indislive=FALSE WHERE indexrelid='ts_idx'::regclass;
:force_cache_refresh
UPDATE t_key_mismatch_types SET ts_col = '2025-06-15 12:00:00' WHERE id = 5;
UPDATE pg_index SET indisready=TRUE, indisvalid=TRUE, indislive=TRUE WHERE indexrelid='ts_idx'::regclass;
:force_cache_refresh
\set idx '''ts_idx''::regclass::oid'
:check_index

-- text (row 6)
UPDATE pg_index SET indisready=FALSE, indisvalid=FALSE, indislive=FALSE WHERE indexrelid='str_idx'::regclass;
:force_cache_refresh
UPDATE t_key_mismatch_types SET str_col = 'updated' WHERE id = 6;
UPDATE pg_index SET indisready=TRUE, indisvalid=TRUE, indislive=TRUE WHERE indexrelid='str_idx'::regclass;
:force_cache_refresh
\set idx '''str_idx''::regclass::oid'
:check_index

-- NULL (row 7): index retains nullable_col=70; base table updated to NULL
UPDATE pg_index SET indisready=FALSE, indisvalid=FALSE, indislive=FALSE WHERE indexrelid='nullable_idx'::regclass;
:force_cache_refresh
UPDATE t_key_mismatch_types SET nullable_col = NULL WHERE id = 7;
UPDATE pg_index SET indisready=TRUE, indisvalid=TRUE, indislive=TRUE WHERE indexrelid='nullable_idx'::regclass;
:force_cache_refresh
\set idx '''nullable_idx''::regclass::oid'
:check_index

INSERT INTO t_key_mismatch_types VALUES
    (8, 80, 80.5, 80.25, true, '2024-08-01 00:00:00', 'str_8', '{"key": "value"}', 80),
    (9, 90, 90.5, 90.25, true, '2024-09-01 00:00:00', 'str_9', '{"key": "value"}', 90);

UPDATE pg_index SET indisready=FALSE, indisvalid=FALSE, indislive=FALSE WHERE indexrelid='int_idx'::regclass;
:force_cache_refresh
UPDATE t_key_mismatch_types SET jsonb_col = '{"key": "updated"}' WHERE id = 8;
UPDATE t_key_mismatch_types SET int_col = 999, jsonb_col = '{"key": "conflict"}' WHERE id = 9;
UPDATE pg_index SET indisready=TRUE, indisvalid=TRUE, indislive=TRUE WHERE indexrelid='int_idx'::regclass;
:force_cache_refresh
-- int_idx has three index inconsistencies:
--   row 1: BINARY_KEY_MISMATCH  (int_col 10 -> 99, from the earlier test)
--   row 8: SEMANTIC_NONKEY_MISMATCH (jsonb_col stale; key unchanged; jsonb has "=")
--   row 9: BINARY_KEY_MISMATCH  (int_col 90 -> 999, jsonb_col stale)
--          jsonb include mismatch is not reported because the key mismatch is found first.
\set idx '''int_idx''::regclass::oid'
:check_index

DROP TABLE t_key_mismatch_types;

--
-- Test to validate reporting across expression indexes.
--
CREATE TABLE t_expr (
    id      INT PRIMARY KEY,
    str_col TEXT,
    int_col INT
);
CREATE INDEX expr_idx ON t_expr (lower(str_col), (int_col::TEXT));

INSERT INTO t_expr VALUES
    (1, 'HELLO', 100),
    (2, 'WORLD', 200),
    (3, 'FOO',   300);

UPDATE pg_index SET indisready=FALSE, indisvalid=FALSE, indislive=FALSE WHERE indexrelid='expr_idx'::regclass;
:force_cache_refresh
-- BINARY_KEY_MISMATCH: index retains 'hello'; base is now 'UPDATED'
UPDATE t_expr SET str_col = 'UPDATED' WHERE id = 1;
-- NULL_MISMATCH: index retains 'world'; base is now NULL
UPDATE t_expr SET str_col = NULL WHERE id = 2;
-- SPURIOUS_ROW: base row deleted; index still retains ('foo', '300')
DELETE FROM t_expr WHERE id = 3;
-- MISSING_ROW: new row inserted into base; index has no entry for id=4
INSERT INTO t_expr VALUES (4, 'NEW', 400);
UPDATE pg_index SET indisready=TRUE, indisvalid=TRUE, indislive=TRUE WHERE indexrelid='expr_idx'::regclass;
:force_cache_refresh
\set idx '''expr_idx''::regclass::oid'
:check_index

DROP TABLE t_expr;

--
-- Test to validate reporting across partitioned indexes.
--
CREATE TABLE t_part (id INT, val INT, PRIMARY KEY (id)) PARTITION BY RANGE (id);
CREATE TABLE t_part_p1      PARTITION OF t_part FOR VALUES FROM (0) TO (100);
CREATE TABLE t_part_p2      PARTITION OF t_part FOR VALUES FROM (100) TO (200);
CREATE TABLE t_part_default PARTITION OF t_part DEFAULT;
CREATE INDEX ON t_part (val);

INSERT INTO t_part VALUES (10, 10), (110, 110), (999, 999);

-- p1: BINARY_KEY_MISMATCH
UPDATE pg_index SET indisready=FALSE, indisvalid=FALSE, indislive=FALSE WHERE indexrelid='t_part_p1_val_idx'::regclass;
:force_cache_refresh
UPDATE t_part SET val = 99 WHERE id = 10;
UPDATE pg_index SET indisready=TRUE, indisvalid=TRUE, indislive=TRUE WHERE indexrelid='t_part_p1_val_idx'::regclass;
:force_cache_refresh

-- p2: SPURIOUS_ROW
UPDATE pg_index SET indisready=FALSE, indisvalid=FALSE, indislive=FALSE WHERE indexrelid='t_part_p2_val_idx'::regclass;
:force_cache_refresh
DELETE FROM t_part WHERE id = 110;
UPDATE pg_index SET indisready=TRUE, indisvalid=TRUE, indislive=TRUE WHERE indexrelid='t_part_p2_val_idx'::regclass;
:force_cache_refresh

-- default: MISSING_ROW
UPDATE pg_index SET indisready=FALSE, indisvalid=FALSE, indislive=FALSE WHERE indexrelid='t_part_default_val_idx'::regclass;
:force_cache_refresh
INSERT INTO t_part VALUES (500, 500);
UPDATE pg_index SET indisready=TRUE, indisvalid=TRUE, indislive=TRUE WHERE indexrelid='t_part_default_val_idx'::regclass;
:force_cache_refresh

-- Checking the parent index automatically checks all child partition indexes.
\set idx '''t_part_val_idx''::regclass::oid'
:check_index

-- Test that yb_index_check returns exactly LIMIT errors when the actual number of errors exceeds the LIMIT clause.
SELECT * FROM yb_index_check(:idx, false, :yb_index_check_log_num_errors) LIMIT 2;

DROP TABLE t_part;

--
-- Test to validate log_num_errors is honored when the actual number of errors exceeds it.
--
-- Partition p1: 3 spurious row errors + 2 missing row errors (5 total)
-- Partition p2: 1 spurious row error  + 1 missing row error  (2 total)
-- Partition p3: no errors
-- Total: 7 errors
--
CREATE TABLE t_abort (id INT, val INT, PRIMARY KEY (id)) PARTITION BY RANGE (id);
CREATE TABLE t_abort_p1 PARTITION OF t_abort FOR VALUES FROM (1)   TO (101);
CREATE TABLE t_abort_p2 PARTITION OF t_abort FOR VALUES FROM (101) TO (201);
CREATE TABLE t_abort_p3 PARTITION OF t_abort FOR VALUES FROM (201) TO (300);
CREATE INDEX ON t_abort (val);

INSERT INTO t_abort SELECT i, i FROM generate_series(1,   20) i;  -- p1
INSERT INTO t_abort SELECT i, i FROM generate_series(101, 120) i; -- p2
INSERT INTO t_abort SELECT i, i FROM generate_series(201, 210) i; -- p3

-- p1: 3 spurious rows (delete ids 1-3 from base while index is disabled)
UPDATE pg_index SET indisready=FALSE, indisvalid=FALSE, indislive=FALSE WHERE indexrelid='t_abort_p1_val_idx'::regclass;
:force_cache_refresh
DELETE FROM t_abort WHERE id IN (1, 2, 3);
UPDATE pg_index SET indisready=TRUE, indisvalid=TRUE, indislive=TRUE WHERE indexrelid='t_abort_p1_val_idx'::regclass;
:force_cache_refresh

-- p1: 2 missing rows (insert ids 21-22 into base while index is disabled)
UPDATE pg_index SET indisready=FALSE, indisvalid=FALSE, indislive=FALSE WHERE indexrelid='t_abort_p1_val_idx'::regclass;
:force_cache_refresh
INSERT INTO t_abort VALUES (21, 21), (22, 22);
UPDATE pg_index SET indisready=TRUE, indisvalid=TRUE, indislive=TRUE WHERE indexrelid='t_abort_p1_val_idx'::regclass;
:force_cache_refresh

-- p2: 1 spurious row (delete id 100 from base while index is disabled)
UPDATE pg_index SET indisready=FALSE, indisvalid=FALSE, indislive=FALSE WHERE indexrelid='t_abort_p2_val_idx'::regclass;
:force_cache_refresh
DELETE FROM t_abort WHERE id = 101;
UPDATE pg_index SET indisready=TRUE, indisvalid=TRUE, indislive=TRUE WHERE indexrelid='t_abort_p2_val_idx'::regclass;
:force_cache_refresh

-- p2: 1 missing row (insert id 121 into base while index is disabled)
UPDATE pg_index SET indisready=FALSE, indisvalid=FALSE, indislive=FALSE WHERE indexrelid='t_abort_p2_val_idx'::regclass;
:force_cache_refresh
INSERT INTO t_abort VALUES (121, 121);
UPDATE pg_index SET indisready=TRUE, indisvalid=TRUE, indislive=TRUE WHERE indexrelid='t_abort_p2_val_idx'::regclass;
:force_cache_refresh

-- log_num_errors >= 7: all 7 errors are logged across both partitions
\set idx '''t_abort_val_idx''::regclass::oid'
:check_index

-- log_num_errors = 3: aborts after p1's 3 spurious row errors
\set yb_index_check_log_num_errors 3
:check_index

-- log_num_errors = 5: aborts after all 5 errors in p1 (3 spurious + 2 missing)
\set yb_index_check_log_num_errors 5
:check_index

-- log_num_errors = 6: logs all 5 errors in p1, then aborts after p2's 1 spurious row error
\set yb_index_check_log_num_errors 6
:check_index

-- log_num_errors = 7: logs all 7 errors across both partitions
\set yb_index_check_log_num_errors 7
:check_index
\set yb_index_check_log_num_errors 100

--
-- Test to validate multiple yb_index_check() calls in a single statement.
--
\set idx_p1 '''t_abort_p1_val_idx''::regclass::oid'
\set idx_p2 '''t_abort_p2_val_idx''::regclass::oid'
\set idx_p3 '''t_abort_p3_val_idx''::regclass::oid'

-- Left join: p1 errors (5) LEFT JOIN p3 errors (0).
-- All a_ columns show p1 errors; b_ columns are NULL because p3 is clean.
SELECT
    a.tablerelid  AS a_tablerelid,  a.indexrelid AS a_indexrelid, a.table_cols  AS a_table_cols,  a.index_cols AS a_index_cols,
    b.tablerelid  AS b_tablerelid,  b.indexrelid AS b_indexrelid, b.table_cols  AS b_table_cols,  b.index_cols AS b_index_cols
FROM yb_index_check(:idx_p1, false, :yb_index_check_log_num_errors) a
LEFT JOIN yb_index_check(:idx_p3, false, :yb_index_check_log_num_errors) b
    ON a.ybctid = b.ybctid
ORDER BY a.ybbasectid, b.ybbasectid;

-- Left join: p2 errors (2) LEFT JOIN p3 errors (0).
-- All a_ columns show p2 errors; b_ columns are NULL.
SELECT
    a.tablerelid  AS a_tablerelid,  a.indexrelid AS a_indexrelid, a.table_cols  AS a_table_cols,  a.index_cols AS a_index_cols,
    b.tablerelid  AS b_tablerelid,  b.indexrelid AS b_indexrelid, b.table_cols  AS b_table_cols,  b.index_cols AS b_index_cols
FROM yb_index_check(:idx_p3, false, :yb_index_check_log_num_errors) a
LEFT JOIN yb_index_check(:idx_p2, false, :yb_index_check_log_num_errors) b
    ON a.ybctid = b.ybctid
ORDER BY a.ybbasectid, b.ybbasectid;

-- Cross join: p1 errors (5) CROSS JOIN p2 errors (2) = 10 rows.
-- Every p1 error is paired with every p2 error.
SELECT
    a.tablerelid  AS a_tablerelid,  a.indexrelid AS a_indexrelid,
    a.table_cols  AS a_table_cols,  a.index_cols AS a_index_cols,
    b.tablerelid  AS b_tablerelid,  b.indexrelid AS b_indexrelid,
    b.table_cols  AS b_table_cols,  b.index_cols AS b_index_cols
FROM yb_index_check(:idx_p1, false, :yb_index_check_log_num_errors) a
CROSS JOIN yb_index_check(:idx_p2, false, :yb_index_check_log_num_errors) b
ORDER BY a.ybbasectid, b.ybbasectid;

-- UNION ALL: all errors from p1 (5), p2 (2), and p3 (0) = 7 rows total.
SELECT tablerelid, indexrelid, table_cols, index_cols
FROM yb_index_check(:idx_p1, false, :yb_index_check_log_num_errors)
UNION ALL
SELECT tablerelid, indexrelid, table_cols, index_cols
FROM yb_index_check(:idx_p2, false, :yb_index_check_log_num_errors)
UNION ALL
SELECT tablerelid, indexrelid, table_cols, index_cols
FROM yb_index_check(:idx_p3, false, :yb_index_check_log_num_errors)
ORDER BY tablerelid, indexrelid;

DROP TABLE t_abort;

--
-- Test to validate the reporting of all types of index inconsistencies.
-- Covers every error_category except YBBASECTID_NULL (no clean SQL repro).
--
\set yb_index_check_log_num_errors 100

-- Table A: non-unique index
CREATE TABLE t_all_errs (
    id        INT PRIMARY KEY,
    k         INT,
    incl_int  INT,
    incl_json JSON
);
CREATE INDEX t_all_errs_idx ON t_all_errs (k) INCLUDE (incl_int, incl_json);

INSERT INTO t_all_errs VALUES
    (1, 10, 100, '{"a": 1}'),  -- BINARY_KEY_MISMATCH
    (2, 20, 200, '{"a": 2}'),  -- NULL_MISMATCH
    (3, 30, 300, '{"a": 3}'),  -- SEMANTIC_NONKEY_MISMATCH
    (4, 40, 400, '{"a": 4}'),  -- BINARY_NONKEY_MISMATCH
    (5, 50, 500, '{"a": 5}');  -- SPURIOUS_ROW

UPDATE pg_index SET indisready=FALSE, indisvalid=FALSE, indislive=FALSE
 WHERE indexrelid='t_all_errs_idx'::regclass;
:force_cache_refresh
-- BINARY_KEY_MISMATCH: index retains k=10; base is now 99
UPDATE t_all_errs SET k = 99 WHERE id = 1;
-- NULL_MISMATCH: index retains k=20; base is now NULL
UPDATE t_all_errs SET k = NULL WHERE id = 2;
-- SEMANTIC_NONKEY_MISMATCH: INT type has an equality operator: binary + semantic differ
UPDATE t_all_errs SET incl_int = 999 WHERE id = 3;
-- BINARY_NONKEY_MISMATCH: JSON type has no equality operator: binary differs
UPDATE t_all_errs SET incl_json = '{"a": 44}' WHERE id = 4;
-- SPURIOUS_ROW: base row deleted; index still retains the entry
DELETE FROM t_all_errs WHERE id = 5;
-- MISSING_ROW: new base row; index has no entry
INSERT INTO t_all_errs VALUES (6, 60, 600, '{"a": 6}');
UPDATE pg_index SET indisready=TRUE, indisvalid=TRUE, indislive=TRUE
 WHERE indexrelid='t_all_errs_idx'::regclass;
:force_cache_refresh

\set idx '''t_all_errs_idx''::regclass::oid'
:check_index

DROP TABLE t_all_errs;

-- Table B: unique-suffix categories
CREATE TABLE t_uq_suffix_nn (id INT PRIMARY KEY, a INT);
CREATE UNIQUE INDEX t_uq_suffix_nn_idx ON t_uq_suffix_nn (a);
INSERT INTO t_uq_suffix_nn VALUES (1, NULL);

-- UNIQUE_SUFFIX_NOT_NULL: NULLS DISTINCT row stores a non-null suffix; flip
-- the catalog flag to NULLS NOT DISTINCT so the checker expects a null suffix.
UPDATE pg_index SET indnullsnotdistinct = true
 WHERE indexrelid = 't_uq_suffix_nn_idx'::regclass;
:force_cache_refresh
\set idx '''t_uq_suffix_nn_idx''::regclass::oid'
:check_index

DROP TABLE t_uq_suffix_nn;

-- UNIQUE_SUFFIX_MISMATCH: NULLS NOT DISTINCT row stores a null suffix; flip
-- the catalog flag to NULLS DISTINCT so the checker expects suffix = ybbasectid.
CREATE TABLE t_uq_suffix_mm (id INT PRIMARY KEY, a INT);
CREATE UNIQUE INDEX t_uq_suffix_mm_idx ON t_uq_suffix_mm (a) NULLS NOT DISTINCT;
INSERT INTO t_uq_suffix_mm VALUES (1, NULL);

UPDATE pg_index SET indnullsnotdistinct = false
 WHERE indexrelid = 't_uq_suffix_mm_idx'::regclass;
:force_cache_refresh
\set idx '''t_uq_suffix_mm_idx''::regclass::oid'
:check_index

DROP TABLE t_uq_suffix_mm;

--
-- Test to validate that yb_index_check correctly reports true missing rows with
-- duplicate suppression enabled.
--
CREATE TABLE t_srf_select (id INT PRIMARY KEY, k INT);
CREATE INDEX t_srf_select_idx ON t_srf_select (k);
INSERT INTO t_srf_select VALUES (1, 10), (2, 20);

UPDATE pg_index SET indisready=FALSE, indisvalid=FALSE, indislive=FALSE WHERE indexrelid = 't_srf_select_idx'::regclass;
:force_cache_refresh
-- BINARY_KEY_MISMATCH: triggers missing-row dedup for the same ybctid
UPDATE t_srf_select SET k = 99 WHERE id = 1;
-- MISSING_ROW: true missing index entry
INSERT INTO t_srf_select VALUES (3, 30);
UPDATE pg_index SET indisready=TRUE, indisvalid=TRUE, indislive=TRUE WHERE indexrelid = 't_srf_select_idx'::regclass;
:force_cache_refresh

-- Use ProjectSet (rather than FunctionScan).
EXPLAIN (COSTS OFF) SELECT yb_index_check('t_srf_select_idx'::regclass::oid, false, :yb_index_check_log_num_errors);
SELECT yb_index_check('t_srf_select_idx'::regclass::oid, false, :yb_index_check_log_num_errors);

DROP TABLE t_srf_select;

