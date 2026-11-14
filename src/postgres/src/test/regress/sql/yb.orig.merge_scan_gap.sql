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

-- Secondary index: filter pushed to the table, read after the index merge
--
-- d is not in the index.  The planner does not evaluate equality clauses as
-- written: it chains the members of the class of c1, c2, and d in the order the
-- clauses bring them in, generates an equality for each link, and pushes an
-- equality to the index only when it references index columns alone (see
-- #37785).
-- - c1 = d AND c2 = d generates c1 = d and d = c2, both pushed to the table, so
--   a filter ties c2 only at the table read.
-- - c1 = d AND c1 = c2 generates the same equalities, so the c1 = c2 it writes
--   does not run on the index.
-- - c1 = c2 AND c1 = d generates c1 = c2, which runs on the index, within each
--   stream, before the merge, and c2 = d.
-- TODO(#37785): the last query should not merge on c2.
CREATE TABLE gap_idx_tbl (k int, s int, c1 int, c2 int, c3 int, d int,
    PRIMARY KEY (k ASC));
CREATE INDEX NONCONCURRENTLY gap_idx_tbl_idx ON gap_idx_tbl (s ASC, c1 ASC, c2 ASC, c3 ASC);
INSERT INTO gap_idx_tbl VALUES
    (1, 1, 1, 0, 9, 1), (2, 1, 1, 1, 0, 1), (3, 2, 1, 1, 4, 1), (4, 1, 5, 5, 1, 5),
    (5, 2, 5, 5, 2, 5);
ANALYZE gap_idx_tbl;

\set R1 'c1 = d AND c2 = d'
\set R2 'c1 = d AND c1 = c2'
\set R3 'c1 = c2 AND c1 = d'
\set query ':P :Q SELECT c1, c3, s, c2, d FROM gap_idx_tbl WHERE s IN (1, 2) AND :R ORDER BY c1, c3;'
\i :run_query
\unset R1
\unset R2
\unset R3

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

-- Gap column with NULLs first
--
-- The index sorts c2 with NULLs first, so a backward scan returns c2 with NULLs
-- last.  Each scan direction has its own decoy, (1, 1, 1, NULL, 9) for the
-- forward scan and (6, 1, 1, 2, -1) for the backward scan.  The NULL row comes
-- last in its stream on the backward scan, so the rows there cannot check the
-- NULLs order of the merge, and the Merge Sort Key of the backward plan does.
CREATE TABLE gap_null_tbl (k int, s int, c1 int, c2 int, c3 int,
    PRIMARY KEY (k ASC));
CREATE INDEX NONCONCURRENTLY gap_null_tbl_idx ON gap_null_tbl (s ASC, c1 ASC, c2 ASC NULLS FIRST, c3 ASC);
INSERT INTO gap_null_tbl VALUES
    (1, 1, 1, NULL, 9), (2, 1, 1, 1, 0), (3, 2, 1, 1, 4), (4, 1, 5, 5, 1),
    (5, 2, 5, 5, 2), (6, 1, 1, 2, -1), (7, 1, 1, 1, 6);
ANALYZE gap_null_tbl;

SET yb_enable_expression_pushdown = off;
\set Q1 '/*+IndexOnlyScan(gap_null_tbl gap_null_tbl_idx) Set(yb_max_merge_scan_streams 0)*/'
\set Q2 '/*+IndexOnlyScan(gap_null_tbl gap_null_tbl_idx) Set(yb_max_merge_scan_streams 64)*/'
\set query ':P :Q SELECT c1, c3, s, c2 FROM gap_null_tbl WHERE s IN (1, 2) AND c1 = c2 ORDER BY c1, c3;'
\i :run_query
\set query ':P :Q SELECT c1, c3, s, c2 FROM gap_null_tbl WHERE s IN (1, 2) AND c1 = c2 ORDER BY c1 DESC, c3 DESC;'
\i :run_query
RESET yb_enable_expression_pushdown;

RESET enable_sort;

DROP TABLE gap_coll_tbl, gap_idx_tbl, gap_dist_tbl, gap_desc_tbl, gap_null_tbl;

-- Cells run in both databases (yb_commands/merge_scan_gap_cells.sql)
--
-- In this database, the expression gap filters run after the merge: with
-- expression pushdown on, each is a storage table filter, since the index does
-- not hold c2 itself, and with it off, a local filter.  The merge sorts on
-- every gap column.
-- TODO(#37784): the predicate gap merge should leave out (c2 % 2), whose
-- comparisons always tie.
\set gap_cells :abs_srcdir '/yb_commands/merge_scan_gap_cells.sql'
SET enable_sort = off;
SET yb_enable_primary_key_decode_from_index = on;
SET yb_enable_base_scans_cost_model = on;
\i :gap_cells

\c co
-- The same cells on embedded indexes.  For an embedded index scan, an
-- expression column is not available for sort (see also #36017), so the merge
-- stops before an expression gap column: the predicate gap scan has no merge,
-- and the filter gap scans merge on c1 alone.  A pushed down filter runs within
-- each stream, before the merge, since the scan reads the index and table rows
-- of a stream in one request, but the filter on the decoded k1 still runs after
-- the merge.
-- TODO(#37784): the predicate gap merge should leave out (c2 % 2), which the
-- predicate implies, and merge on c3.
-- TODO(#37785): with expression pushdown on, the filter gap and relabeled
-- filter gap filters run within each stream, before the merge, so those merges
-- should sort on c1 and c3.
SET enable_sort = off;
SET yb_enable_primary_key_decode_from_index = on;
SET yb_enable_base_scans_cost_model = on;
\i :gap_cells
