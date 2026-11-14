--
-- Cells that yb.orig.merge_scan_gap runs in the default database and again in
-- the colocated database, where the indexes are embedded.  The comments there
-- say how the plans differ.
--

-- Decoded primary key column: filter kept local on an index-only scan
--
-- With yb_enable_primary_key_decode_from_index on, an index-only scan returns
-- k1 by decoding it from the base table row reference, and a filter on it runs
-- locally, after the merge.  This runs on a secondary index that is not
-- embedded, and again in the colocated database, where the index is embedded.
-- There the scan pushes a table filter within each stream, but not a filter on
-- k1.
-- Here gap = k1 and ord1 = k1 make gap redundant, and the first row is the
-- decoy.
\set Q1 '/*+IndexOnlyScan(gap_pk_tbl gap_pk_tbl_idx) Set(yb_max_merge_scan_streams 0)*/'
\set Q2 '/*+IndexOnlyScan(gap_pk_tbl gap_pk_tbl_idx) Set(yb_max_merge_scan_streams 64)*/'
\set query ':P :Q SELECT ord1, ord2, bkt, gap, k1 FROM gap_pk_tbl WHERE bkt IN (1, 2) AND gap = k1 AND ord1 = k1 ORDER BY ord1, ord2;'
CREATE TABLE gap_pk_tbl (k1 int, k2 int, bkt int, ord1 int, gap int, ord2 int,
    PRIMARY KEY (k1 ASC, k2 ASC));
CREATE INDEX NONCONCURRENTLY gap_pk_tbl_idx ON gap_pk_tbl (bkt ASC, ord1 ASC, gap ASC, ord2 ASC);
INSERT INTO gap_pk_tbl VALUES
    (1, 100, 1, 1, 0, 9), (1, 1, 1, 1, 1, 0), (1, 2, 2, 1, 1, 4), (5, 1, 1, 5, 5, 1),
    (5, 2, 2, 5, 5, 2);
ANALYZE gap_pk_tbl;
\i :run_query
DROP TABLE gap_pk_tbl;

-- Expression gap columns
--
-- Each gap column here is an expression column of the index, tied by a partial
-- index predicate, by a filter, or by a filter through a relabel.
CREATE TABLE gap_expr_tbl (k int, bkt int, ord1 int, gap int, ord2 int,
    PRIMARY KEY (k ASC));
CREATE INDEX NONCONCURRENTLY gap_expr_tbl_pred_idx ON gap_expr_tbl (bkt ASC, (gap % 2) ASC, ord2 ASC)
    WHERE gap % 2 = 1;
CREATE INDEX NONCONCURRENTLY gap_expr_tbl_idx ON gap_expr_tbl (bkt ASC, ord1 ASC, (gap + 0) ASC, ord2 ASC);
INSERT INTO gap_expr_tbl VALUES
    (1, 1, 1, 0, 9), (2, 1, 1, 1, 0), (3, 2, 1, 1, 4), (4, 1, 5, 5, 1),
    (5, 2, 5, 5, 2);
ANALYZE gap_expr_tbl;

-- Predicate gap: the predicate implies (gap % 2), which comes before the only
-- merge sort column ord2.
\set Q1 '/*+IndexScan(gap_expr_tbl gap_expr_tbl_pred_idx) Set(yb_max_merge_scan_streams 0)*/'
\set Q2 '/*+IndexScan(gap_expr_tbl gap_expr_tbl_pred_idx) Set(yb_max_merge_scan_streams 64)*/'
\set query ':P :Q SELECT ord2, bkt, gap FROM gap_expr_tbl WHERE bkt IN (1, 2) AND gap % 2 = 1 ORDER BY ord2;'
\i :run_query

-- Filter gap: ord1 = gap + 0 ties (gap + 0) to ord1.
\set Q1 '/*+IndexScan(gap_expr_tbl gap_expr_tbl_idx) Set(yb_max_merge_scan_streams 0)*/'
\set Q2 '/*+IndexScan(gap_expr_tbl gap_expr_tbl_idx) Set(yb_max_merge_scan_streams 64)*/'
\set query ':P :Q SELECT ord1, ord2, bkt, gap FROM gap_expr_tbl WHERE bkt IN (1, 2) AND ord1 = gap + 0 ORDER BY ord1, ord2;'
\i :run_query
SET yb_enable_expression_pushdown = off;
\i :run_query
RESET yb_enable_expression_pushdown;

-- Relabeled filter gap: gap::text, a relabeled varchar column, is still an
-- expression column of the index.
\set Q1 '/*+IndexScan(gap_relabel_tbl gap_relabel_tbl_idx) Set(yb_max_merge_scan_streams 0)*/'
\set Q2 '/*+IndexScan(gap_relabel_tbl gap_relabel_tbl_idx) Set(yb_max_merge_scan_streams 64)*/'
\set query ':P :Q SELECT ord1, ord2, bkt, gap FROM gap_relabel_tbl WHERE bkt IN (1, 2) AND ord1 = gap ORDER BY ord1, ord2;'
CREATE TABLE gap_relabel_tbl (k int, bkt int, ord1 text, gap varchar, ord2 int,
    PRIMARY KEY (k ASC));
CREATE INDEX NONCONCURRENTLY gap_relabel_tbl_idx ON gap_relabel_tbl (bkt ASC, ord1 ASC, (gap::text) ASC, ord2 ASC);
INSERT INTO gap_relabel_tbl VALUES
    (1, 1, 'b', 'a', 9), (2, 1, 'b', 'b', 0), (3, 2, 'b', 'b', 4), (4, 1, 'e', 'e', 1),
    (5, 2, 'e', 'e', 2);
ANALYZE gap_relabel_tbl;
\i :run_query
SET yb_enable_expression_pushdown = off;
\i :run_query
RESET yb_enable_expression_pushdown;
DROP TABLE gap_expr_tbl, gap_relabel_tbl;

