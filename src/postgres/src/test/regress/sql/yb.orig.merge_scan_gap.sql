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
-- than in yb.orig.merge_scan_setup.  Unless a section says otherwise, ord1 =
-- gap makes gap redundant for ORDER BY ord1, ord2 on the index (bkt, ord1, gap,
-- ord2), and the first row is a decoy that fails ord1 = gap, sorts first within
-- its stream, and sorts last by (ord1, ord2), so that a merge that does not
-- sort on gap misorders the survivors.

-- Local filter: non-C collation
CREATE TABLE gap_coll_tbl (bkt int, ord1 text COLLATE "en_US", gap text COLLATE "en_US",
    ord2 int, PRIMARY KEY (bkt ASC, ord1 ASC, gap ASC, ord2 ASC));
INSERT INTO gap_coll_tbl VALUES
    (1, 'b', 'a', 9), (1, 'b', 'b', 0), (2, 'b', 'b', 4), (1, 'e', 'e', 1),
    (2, 'e', 'e', 2);
ANALYZE gap_coll_tbl;

\set query ':P :Q SELECT ord1, ord2, bkt, gap FROM gap_coll_tbl WHERE bkt IN (1, 2) AND ord1 = gap ORDER BY ord1, ord2;'
\i :run_query

-- Secondary index: filter pushed to the table, read after the index merge
--
-- d is not in the index.  The planner does not evaluate equality clauses as
-- written: it chains the members of the class of ord1, gap, and d in the order
-- the clauses bring them in, generates an equality for each link, and pushes an
-- equality to the index only when it references index columns alone (see
-- #37785).
-- - ord1 = d AND gap = d generates ord1 = d and d = gap, both pushed to the
--   table, so a filter ties gap only at the table read.
-- - ord1 = d AND ord1 = gap generates the same equalities, so the ord1 = gap it
--   writes does not run on the index.
-- - ord1 = gap AND ord1 = d generates ord1 = gap, which runs on the index,
--   within each stream, before the merge, and gap = d.
-- TODO(#37785): the last query should not merge on gap.
CREATE TABLE gap_idx_tbl (k int, bkt int, ord1 int, gap int, ord2 int, d int,
    PRIMARY KEY (k ASC));
CREATE INDEX NONCONCURRENTLY gap_idx_tbl_idx ON gap_idx_tbl (bkt ASC, ord1 ASC, gap ASC, ord2 ASC);
INSERT INTO gap_idx_tbl VALUES
    (1, 1, 1, 0, 9, 1), (2, 1, 1, 1, 0, 1), (3, 2, 1, 1, 4, 1), (4, 1, 5, 5, 1, 5),
    (5, 2, 5, 5, 2, 5);
ANALYZE gap_idx_tbl;

\set R1 'ord1 = d AND gap = d'
\set R2 'ord1 = d AND ord1 = gap'
\set R3 'ord1 = gap AND ord1 = d'
\set query ':P :Q SELECT ord1, ord2, bkt, gap, d FROM gap_idx_tbl WHERE bkt IN (1, 2) AND :R ORDER BY ord1, ord2;'
\i :run_query
\unset R1
\unset R2
\unset R3

-- DISTINCT over a local filter gap
--
-- The merge sorts on gap as well, so the scan returns the rows in (ord1, ord2)
-- order, and the Unique node sits directly on the scan.  Each (bkt, ord1, gap,
-- ord2) has three rows, one for each k.
CREATE TABLE gap_dist_tbl (bkt int, ord1 int, gap int, ord2 int, k int,
    PRIMARY KEY (bkt ASC, ord1 ASC, gap ASC, ord2 ASC, k ASC));
INSERT INTO gap_dist_tbl SELECT bkt, ord1, gap, ord2, k
    FROM (VALUES (1, 1, 0, 9), (1, 1, 1, 0), (2, 1, 1, 4), (1, 5, 5, 1),
                 (2, 5, 5, 2)) v(bkt, ord1, gap, ord2),
        generate_series(1, 3) k;
ANALYZE gap_dist_tbl;

SET yb_enable_expression_pushdown = off;
\set Q1 '/*+IndexScan(gap_dist_tbl) Set(yb_max_merge_scan_streams 0)*/'
\set Q2 '/*+IndexScan(gap_dist_tbl) Set(yb_max_merge_scan_streams 64)*/'
\set query ':P :Q SELECT DISTINCT ord1, ord2 FROM gap_dist_tbl WHERE bkt IN (1, 2) AND ord1 = gap ORDER BY ord1, ord2;'
\i :run_query
RESET yb_enable_expression_pushdown;

-- Descending gap column, forward and backward scans
--
-- The index sorts gap in descending order.  ord2 numbers the rows that pass
-- ord1 = gap in their order: stream bkt = 1 has 10 and 30, and stream bkt = 2
-- has 20, which belongs between them.  Each decoy fails ord1 = gap and comes
-- first in stream bkt = 1 for its scan direction.  Only a merge that compares
-- gap in the order of the scan lets the decoy out first.  Otherwise stream bkt
-- = 1 waits behind it, and 20 comes out too early.
CREATE TABLE gap_desc_tbl (bkt int, ord1 int, gap int, ord2 int,
    PRIMARY KEY (bkt ASC, ord1 ASC, gap DESC, ord2 ASC));
INSERT INTO gap_desc_tbl VALUES
    (1, 1, 1, 10), (1, 1, 1, 30),  -- stream bkt = 1
    (2, 1, 1, 20),                 -- stream bkt = 2
    (1, 1, 2, 99),                 -- forward decoy, first under gap DESC
    (1, 1, 0, 0);                  -- backward decoy, first backward
ANALYZE gap_desc_tbl;

SET yb_enable_expression_pushdown = off;
\set Q1 ':off'
\set Q2 ':on'
\set query ':P :Q SELECT ord1, ord2, bkt, gap FROM gap_desc_tbl WHERE bkt IN (1, 2) AND ord1 = gap ORDER BY ord1, ord2;'
\i :run_query
\set query ':P :Q SELECT ord1, ord2, bkt, gap FROM gap_desc_tbl WHERE bkt IN (1, 2) AND ord1 = gap ORDER BY ord1 DESC, ord2 DESC;'
\i :run_query
RESET yb_enable_expression_pushdown;

-- Gap column with NULLs first
--
-- The index sorts gap with NULLs first, so a backward scan returns gap with
-- NULLs last.  The rows are as in the descending gap column section, with a
-- forward decoy whose gap is NULL, which comes first under NULLS FIRST.  The
-- NULL row comes last in its stream on the backward scan, so the rows there
-- cannot check the NULLs order of the merge, and the Merge Sort Key of the
-- backward plan does.
CREATE TABLE gap_null_tbl (k int, bkt int, ord1 int, gap int, ord2 int,
    PRIMARY KEY (k ASC));
CREATE INDEX NONCONCURRENTLY gap_null_tbl_idx ON gap_null_tbl (bkt ASC, ord1 ASC, gap ASC NULLS FIRST, ord2 ASC);
INSERT INTO gap_null_tbl VALUES
    (1, 1, 1, 1, 10), (2, 1, 1, 1, 30),  -- stream bkt = 1
    (3, 2, 1, 1, 20),                    -- stream bkt = 2
    (4, 1, 1, NULL, 99),                 -- forward decoy, NULL is first
    (5, 1, 1, 2, 0);                     -- backward decoy, first backward
ANALYZE gap_null_tbl;

SET yb_enable_expression_pushdown = off;
\set Q1 '/*+IndexOnlyScan(gap_null_tbl gap_null_tbl_idx) Set(yb_max_merge_scan_streams 0)*/'
\set Q2 '/*+IndexOnlyScan(gap_null_tbl gap_null_tbl_idx) Set(yb_max_merge_scan_streams 64)*/'
\set query ':P :Q SELECT ord1, ord2, bkt, gap FROM gap_null_tbl WHERE bkt IN (1, 2) AND ord1 = gap ORDER BY ord1, ord2;'
\i :run_query
\set query ':P :Q SELECT ord1, ord2, bkt, gap FROM gap_null_tbl WHERE bkt IN (1, 2) AND ord1 = gap ORDER BY ord1 DESC, ord2 DESC;'
\i :run_query
RESET yb_enable_expression_pushdown;

RESET enable_sort;

DROP TABLE gap_coll_tbl, gap_idx_tbl, gap_dist_tbl, gap_desc_tbl, gap_null_tbl;

-- Cells run in both databases (yb_commands/merge_scan_gap_cells.sql)
--
-- In this database, the expression gap filters run after the merge: with
-- expression pushdown on, each is a storage table filter, since the index does
-- not hold gap itself, and with it off, a local filter.  The merge sorts on
-- every gap column.
-- TODO(#37784): the predicate gap merge should leave out (gap % 2), whose
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
-- and the filter gap scans merge on ord1 alone.  A pushed down filter runs
-- within each stream, before the merge, since the scan reads the index and
-- table rows of a stream in one request, but the filter on the decoded k1 still
-- runs after the merge.
-- TODO(#37784): the predicate gap merge should leave out (gap % 2), which the
-- predicate implies, and merge on ord2.
-- TODO(#37785): with expression pushdown on, the filter gap and relabeled
-- filter gap filters run within each stream, before the merge, so those merges
-- should sort on ord1 and ord2.
SET enable_sort = off;
SET yb_enable_primary_key_decode_from_index = on;
SET yb_enable_base_scans_cost_model = on;
\i :gap_cells
