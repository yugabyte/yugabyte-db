--
-- Cells that yb.orig.merge_scan_gap runs in the default database and again in
-- the colocated database, where the indexes are embedded.  The comments there
-- say how the plans differ.
--

-- Expression gap columns
--
-- Each gap column here is an expression column of the index: the operator
-- expression (gap || ''), tied by the partial index predicate or by a filter,
-- or the relabel (gap::text), tied by a filter.  The rows are as in the
-- descending gap column section of yb.orig.merge_scan_gap, with a forward decoy
-- only.
CREATE TABLE gap_expr_tbl (k int, bkt int, ord1 text, gap varchar, ord2 int,
    PRIMARY KEY (k ASC));
CREATE INDEX NONCONCURRENTLY gap_expr_tbl_pred_idx ON gap_expr_tbl (bkt ASC, (gap || '') ASC, ord2 ASC)
    WHERE gap || '' = 'b';
CREATE INDEX NONCONCURRENTLY gap_expr_tbl_op_idx ON gap_expr_tbl (bkt ASC, ord1 ASC, (gap || '') ASC, ord2 ASC);
CREATE INDEX NONCONCURRENTLY gap_expr_tbl_relabel_idx ON gap_expr_tbl (bkt ASC, ord1 ASC, (gap::text) ASC, ord2 ASC);
INSERT INTO gap_expr_tbl VALUES
    (1, 1, 'b', 'b', 10), (2, 1, 'b', 'b', 30),  -- stream bkt = 1
    (3, 2, 'b', 'b', 20),                        -- stream bkt = 2
    (4, 1, 'b', 'a', 99);                        -- decoy, fails ord1 = gap
ANALYZE gap_expr_tbl;

-- Predicate gap: the predicate implies (gap || ''), which comes before the only
-- merge sort column ord2.
\set hint1 '/*+IndexScan(gap_expr_tbl gap_expr_tbl_pred_idx) Set(yb_max_merge_scan_streams 0)*/'
\set hint2 '/*+IndexScan(gap_expr_tbl gap_expr_tbl_pred_idx) Set(yb_max_merge_scan_streams 64)*/'
\set query 'SELECT gap, ord2, bkt FROM gap_expr_tbl WHERE bkt IN (1, 2) AND gap || '''' = ''b'' ORDER BY ord2'
:explain2run2

-- Operator filter gap: ord1 = gap || '' ties (gap || '') to ord1.
\set hint1 '/*+IndexScan(gap_expr_tbl gap_expr_tbl_op_idx) Set(yb_max_merge_scan_streams 0)*/'
\set hint2 '/*+IndexScan(gap_expr_tbl gap_expr_tbl_op_idx) Set(yb_max_merge_scan_streams 64)*/'
\set query 'SELECT gap, ord1, ord2, bkt FROM gap_expr_tbl WHERE bkt IN (1, 2) AND ord1 = gap || '''' ORDER BY ord1, ord2'
:explain2run2
SET yb_enable_expression_pushdown = off;
:explain2run2
RESET yb_enable_expression_pushdown;

-- Relabeled filter gap: gap::text, a relabeled varchar column, is still an
-- expression column of the index.
\set hint1 '/*+IndexScan(gap_expr_tbl gap_expr_tbl_relabel_idx) Set(yb_max_merge_scan_streams 0)*/'
\set hint2 '/*+IndexScan(gap_expr_tbl gap_expr_tbl_relabel_idx) Set(yb_max_merge_scan_streams 64)*/'
\set query 'SELECT gap, ord1, ord2, bkt FROM gap_expr_tbl WHERE bkt IN (1, 2) AND ord1 = gap ORDER BY ord1, ord2'
:explain2run2
SET yb_enable_expression_pushdown = off;
:explain2run2
RESET yb_enable_expression_pushdown;
DROP TABLE gap_expr_tbl;
