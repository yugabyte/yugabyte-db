--
-- See yb_merge_scan_schedule for details about the test.  Tests a merge scan
-- whose sort columns skip an index column that a filter holds equal to an
-- earlier column.  Such a gap column varies within each stream, so the merge
-- also sorts on it, and the merge order holds wherever the filter runs.
--

\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/merge_scan_setup.sql'
\i :filename

SET enable_sort = off;

-- These tables are of no use to other tests, so they are defined here rather
-- than in yb.orig.merge_scan_setup.  Unless a section says otherwise, c1 = c2
-- makes c2 redundant for ORDER BY c1, c3 on the index (s, c1, c2, c3), and the
-- first row is a decoy that fails c1 = c2, sorts first within its stream, and
-- sorts last by (c1, c3), so that a merge that does not sort on c2 misorders
-- the survivors.

--
-- Local filter: expression pushdown off
--
CREATE TABLE gap_tbl (s int, c1 int, c2 int, c3 int,
    PRIMARY KEY (s ASC, c1 ASC, c2 ASC, c3 ASC));
INSERT INTO gap_tbl VALUES
    (1, 1, 0, 9), (1, 1, 1, 0), (2, 1, 1, 4), (1, 5, 5, 1), (2, 5, 5, 2);
ANALYZE gap_tbl;

SET yb_enable_expression_pushdown = off;
\set query ':P :Q SELECT c1, c3, s, c2 FROM gap_tbl WHERE s IN (1, 2) AND c1 = c2 ORDER BY c1, c3;'
\i :run_query
RESET yb_enable_expression_pushdown;

--
-- Local filter: non-C collation
--
CREATE TABLE gap_coll_tbl (s int, c1 text COLLATE "en_US", c2 text COLLATE "en_US",
    c3 int, PRIMARY KEY (s ASC, c1 ASC, c2 ASC, c3 ASC));
INSERT INTO gap_coll_tbl VALUES
    (1, 'b', 'a', 9), (1, 'b', 'b', 0), (2, 'b', 'b', 4), (1, 'e', 'e', 1),
    (2, 'e', 'e', 2);
ANALYZE gap_coll_tbl;

\set query ':P :Q SELECT c1, c3, s, c2 FROM gap_coll_tbl WHERE s IN (1, 2) AND c1 = c2 ORDER BY c1, c3;'
\i :run_query

--
-- Local filter: stable cross type operator
--
SET TimeZone = 'UTC';
CREATE TABLE gap_ts_tbl (s int, c1 timestamp, c2 timestamptz, c3 int,
    PRIMARY KEY (s ASC, c1 ASC, c2 ASC, c3 ASC));
INSERT INTO gap_ts_tbl VALUES
    (1, '2020-01-02', '2020-01-01+00', 9), (1, '2020-01-02', '2020-01-02+00', 0),
    (2, '2020-01-02', '2020-01-02+00', 4), (1, '2020-01-05', '2020-01-05+00', 1),
    (2, '2020-01-05', '2020-01-05+00', 2);
ANALYZE gap_ts_tbl;

\set query ':P :Q SELECT c1, c3, s, c2 FROM gap_ts_tbl WHERE s IN (1, 2) AND c1 = c2 ORDER BY c1, c3;'
\i :run_query
RESET TimeZone;

--
-- Secondary index: filter pushed to the table, read after the index merge
--
-- d is not in the index, and the equalities the planner generates from c2 = d
-- and c1 = d reference it, so they are pushed to the table.  With c1 = c2
-- instead, the filter runs on the index.
CREATE TABLE gap_idx_tbl (k int, s int, c1 int, c2 int, c3 int, d int,
    PRIMARY KEY (k ASC));
CREATE INDEX NONCONCURRENTLY gap_idx_tbl_idx ON gap_idx_tbl (s ASC, c1 ASC, c2 ASC, c3 ASC);
INSERT INTO gap_idx_tbl VALUES
    (1, 1, 1, 0, 9, 1), (2, 1, 1, 1, 0, 1), (3, 2, 1, 1, 4, 1), (4, 1, 5, 5, 1, 5),
    (5, 2, 5, 5, 2, 5);
ANALYZE gap_idx_tbl;

\set query ':P :Q SELECT c1, c3, s, c2, d FROM gap_idx_tbl WHERE s IN (1, 2) AND c2 = d AND c1 = d ORDER BY c1, c3;'
\i :run_query
\set query ':P :Q SELECT c1, c3, s, c2 FROM gap_idx_tbl WHERE s IN (1, 2) AND c1 = c2 ORDER BY c1, c3;'
\i :run_query

--
-- DISTINCT over a local filter gap
--
-- The merge sorts on c2 as well, so the scan returns the rows in (c1, c3)
-- order, and the Unique node sits directly on the scan.  Each (s, c1, c2, c3)
-- has three rows, one for each k.
CREATE TABLE gap_dist_tbl (s int, c1 int, c2 int, c3 int, k int,
    PRIMARY KEY (s ASC, c1 ASC, c2 ASC, c3 ASC, k ASC));
INSERT INTO gap_dist_tbl SELECT s, c1, c2, c3, k
    FROM (VALUES (1, 1, 0, 9), (1, 1, 1, 0), (2, 1, 1, 4), (1, 5, 5, 1),
                 (2, 5, 5, 2)) v(s, c1, c2, c3),
        generate_series(1, 3) k;
ANALYZE gap_dist_tbl;

SET yb_enable_expression_pushdown = off;
\set Q1 '/*+IndexScan(gap_dist_tbl) Set(yb_max_merge_scan_streams 0)*/'
\set Q2 '/*+IndexScan(gap_dist_tbl) Set(yb_max_merge_scan_streams 64)*/'
\set query ':P :Q SELECT DISTINCT c1, c3 FROM gap_dist_tbl WHERE s IN (1, 2) AND c1 = c2 ORDER BY c1, c3;'
\i :run_query
RESET yb_enable_expression_pushdown;

--
-- Descending gap column, forward and backward scans
--
-- The index sorts c2 in descending order.  Each scan direction has its own
-- decoy, (1, 1, 2, 9) for the forward scan and (1, 1, 0, -1) for the backward
-- scan.
CREATE TABLE gap_desc_tbl (s int, c1 int, c2 int, c3 int,
    PRIMARY KEY (s ASC, c1 ASC, c2 DESC, c3 ASC));
INSERT INTO gap_desc_tbl VALUES
    (1, 1, 2, 9), (1, 1, 0, -1), (1, 1, 1, 0), (1, 1, 1, 6), (2, 1, 1, 4),
    (1, 5, 5, 1), (2, 5, 5, 2);
ANALYZE gap_desc_tbl;

SET yb_enable_expression_pushdown = off;
\set Q1 ':off'
\set Q2 ':on'
\set query ':P :Q SELECT c1, c3, s, c2 FROM gap_desc_tbl WHERE s IN (1, 2) AND c1 = c2 ORDER BY c1, c3;'
\i :run_query
\set query ':P :Q SELECT c1, c3, s, c2 FROM gap_desc_tbl WHERE s IN (1, 2) AND c1 = c2 ORDER BY c1 DESC, c3 DESC;'
\i :run_query
RESET yb_enable_expression_pushdown;

--
-- Gap column with NULLs first
--
-- The index sorts c2 with NULLs first, and the decoy's c2 is NULL.
CREATE TABLE gap_null_tbl (k int, s int, c1 int, c2 int, c3 int,
    PRIMARY KEY (k ASC));
CREATE INDEX NONCONCURRENTLY gap_null_tbl_idx ON gap_null_tbl (s ASC, c1 ASC, c2 ASC NULLS FIRST, c3 ASC);
INSERT INTO gap_null_tbl VALUES
    (1, 1, 1, NULL, 9), (2, 1, 1, 1, 0), (3, 2, 1, 1, 4), (4, 1, 5, 5, 1),
    (5, 2, 5, 5, 2);
ANALYZE gap_null_tbl;

SET yb_enable_expression_pushdown = off;
\set Q1 '/*+IndexOnlyScan(gap_null_tbl gap_null_tbl_idx) Set(yb_max_merge_scan_streams 0)*/'
\set Q2 '/*+IndexOnlyScan(gap_null_tbl gap_null_tbl_idx) Set(yb_max_merge_scan_streams 64)*/'
\set query ':P :Q SELECT c1, c3, s, c2 FROM gap_null_tbl WHERE s IN (1, 2) AND c1 = c2 ORDER BY c1, c3;'
\i :run_query
RESET yb_enable_expression_pushdown;

--
-- Expression gap column
--
-- c1 = c2 + 0 makes the index expression c2 + 0 redundant, and the merge sorts
-- on the expression's values in the index rows.
\set Q1 '/*+IndexScan(gap_expr_tbl gap_expr_tbl_idx) Set(yb_max_merge_scan_streams 0)*/'
\set Q2 '/*+IndexScan(gap_expr_tbl gap_expr_tbl_idx) Set(yb_max_merge_scan_streams 64)*/'
\set query ':P :Q SELECT c1, c3, s, c2 FROM gap_expr_tbl WHERE s IN (1, 2) AND c1 = c2 + 0 ORDER BY c1, c3;'
CREATE TABLE gap_expr_tbl (k int, s int, c1 int, c2 int, c3 int,
    PRIMARY KEY (k ASC));
CREATE INDEX NONCONCURRENTLY gap_expr_tbl_idx ON gap_expr_tbl (s ASC, c1 ASC, (c2 + 0) ASC, c3 ASC);
INSERT INTO gap_expr_tbl VALUES
    (1, 1, 1, 0, 9), (2, 1, 1, 1, 0), (3, 2, 1, 1, 4), (4, 1, 5, 5, 1),
    (5, 2, 5, 5, 2);
ANALYZE gap_expr_tbl;
\i :run_query
DROP TABLE gap_expr_tbl;

RESET enable_sort;

DROP TABLE gap_tbl, gap_coll_tbl, gap_ts_tbl, gap_idx_tbl, gap_dist_tbl,
    gap_desc_tbl, gap_null_tbl;

--
-- Decoded primary key column: filter kept local on an index-only scan
--
-- With yb_enable_primary_key_decode_from_index on, an index-only scan returns
-- k1 by decoding it from the base table row reference, and a filter on it runs
-- locally, after the merge.  This runs on a secondary index that is not
-- embedded, and again in the colocated database, where the index is embedded.
-- There the scan pushes a table filter within each stream, but not a filter on
-- k1.
-- Here c2 = k1 and c1 = k1 make c2 redundant, and the first row is the decoy.
\set Q1 '/*+IndexOnlyScan(gap_pk_tbl gap_pk_tbl_idx) Set(yb_max_merge_scan_streams 0)*/'
\set Q2 '/*+IndexOnlyScan(gap_pk_tbl gap_pk_tbl_idx) Set(yb_max_merge_scan_streams 64)*/'
\set query ':P :Q SELECT c1, c3, s, c2, k1 FROM gap_pk_tbl WHERE s IN (1, 2) AND c2 = k1 AND c1 = k1 ORDER BY c1, c3;'
SET enable_sort = off;
SET yb_enable_primary_key_decode_from_index = on;
SET yb_enable_base_scans_cost_model = on;
CREATE TABLE gap_pk_tbl (k1 int, k2 int, s int, c1 int, c2 int, c3 int,
    PRIMARY KEY (k1 ASC, k2 ASC));
CREATE INDEX NONCONCURRENTLY gap_pk_tbl_idx ON gap_pk_tbl (s ASC, c1 ASC, c2 ASC, c3 ASC);
INSERT INTO gap_pk_tbl VALUES
    (1, 100, 1, 1, 0, 9), (1, 1, 1, 1, 1, 0), (1, 2, 2, 1, 1, 4), (5, 1, 1, 5, 5, 1),
    (5, 2, 2, 5, 5, 2);
ANALYZE gap_pk_tbl;
\i :run_query
DROP TABLE gap_pk_tbl;

\c co
SET enable_sort = off;
SET yb_enable_primary_key_decode_from_index = on;
SET yb_enable_base_scans_cost_model = on;
CREATE TABLE gap_pk_tbl (k1 int, k2 int, s int, c1 int, c2 int, c3 int,
    PRIMARY KEY (k1 ASC, k2 ASC));
CREATE INDEX NONCONCURRENTLY gap_pk_tbl_idx ON gap_pk_tbl (s ASC, c1 ASC, c2 ASC, c3 ASC);
INSERT INTO gap_pk_tbl VALUES
    (1, 100, 1, 1, 0, 9), (1, 1, 1, 1, 1, 0), (1, 2, 2, 1, 1, 4), (5, 1, 1, 5, 5, 1),
    (5, 2, 2, 5, 5, 2);
ANALYZE gap_pk_tbl;
\i :run_query
DROP TABLE gap_pk_tbl;

-- The same expression gap column on a colocated index.  pggate cannot compare
-- an expression column of an embedded index, so the scan is not a merge scan.
\set Q1 '/*+IndexScan(gap_expr_tbl gap_expr_tbl_idx) Set(yb_max_merge_scan_streams 0)*/'
\set Q2 '/*+IndexScan(gap_expr_tbl gap_expr_tbl_idx) Set(yb_max_merge_scan_streams 64)*/'
\set query ':P :Q SELECT c1, c3, s, c2 FROM gap_expr_tbl WHERE s IN (1, 2) AND c1 = c2 + 0 ORDER BY c1, c3;'
CREATE TABLE gap_expr_tbl (k int, s int, c1 int, c2 int, c3 int,
    PRIMARY KEY (k ASC));
CREATE INDEX NONCONCURRENTLY gap_expr_tbl_idx ON gap_expr_tbl (s ASC, c1 ASC, (c2 + 0) ASC, c3 ASC);
INSERT INTO gap_expr_tbl VALUES
    (1, 1, 1, 0, 9), (2, 1, 1, 1, 0), (3, 2, 1, 1, 4), (4, 1, 5, 5, 1),
    (5, 2, 5, 5, 2);
ANALYZE gap_expr_tbl;
\i :run_query
DROP TABLE gap_expr_tbl;
