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
\set Q1 '/*+IndexScan(gap_expr_tbl gap_expr_tbl_pred_idx) Set(yb_max_merge_scan_streams 0)*/'
\set Q2 '/*+IndexScan(gap_expr_tbl gap_expr_tbl_pred_idx) Set(yb_max_merge_scan_streams 64)*/'
\set query ':P :Q SELECT gap, ord2, bkt FROM gap_expr_tbl WHERE bkt IN (1, 2) AND gap || '''' = ''b'' ORDER BY ord2;'
\i :run_query

-- Operator filter gap: ord1 = gap || '' ties (gap || '') to ord1.
\set Q1 '/*+IndexScan(gap_expr_tbl gap_expr_tbl_op_idx) Set(yb_max_merge_scan_streams 0)*/'
\set Q2 '/*+IndexScan(gap_expr_tbl gap_expr_tbl_op_idx) Set(yb_max_merge_scan_streams 64)*/'
\set query ':P :Q SELECT gap, ord1, ord2, bkt FROM gap_expr_tbl WHERE bkt IN (1, 2) AND ord1 = gap || '''' ORDER BY ord1, ord2;'
\i :run_query
SET yb_enable_expression_pushdown = off;
\i :run_query
RESET yb_enable_expression_pushdown;

-- Relabeled filter gap: gap::text, a relabeled varchar column, is still an
-- expression column of the index.
\set Q1 '/*+IndexScan(gap_expr_tbl gap_expr_tbl_relabel_idx) Set(yb_max_merge_scan_streams 0)*/'
\set Q2 '/*+IndexScan(gap_expr_tbl gap_expr_tbl_relabel_idx) Set(yb_max_merge_scan_streams 64)*/'
\set query ':P :Q SELECT gap, ord1, ord2, bkt FROM gap_expr_tbl WHERE bkt IN (1, 2) AND ord1 = gap ORDER BY ord1, ord2;'
\i :run_query
SET yb_enable_expression_pushdown = off;
\i :run_query
RESET yb_enable_expression_pushdown;
DROP TABLE gap_expr_tbl;

-- Gap column used only by a pushed down filter
--
-- ord1 = x compares timestamptz with timestamp, which is stable, so that filter
-- runs locally, after the merge, while x = gap runs as a storage filter within
-- each stream.  The query does not select gap, and no local filter uses it, so
-- the scan reads it only because the merge sorts on it.  The decoy passes x =
-- gap, fails ord1 = x, and comes first in stream bkt = 1, so a merge that
-- compared gap without reading it would return the rows out of order.  The
-- primary key scan reads gap from the table row, the index-only scan reads gap
-- from the index row, and the plain index scan reads gap from the index row in
-- its separate index request or from the table row when the index is embedded.
SET TimeZone = 'UTC';
CREATE TABLE gap_unread_tbl (gap timestamp, ord2 int, x timestamp, bkt int,
    ord1 timestamptz, PRIMARY KEY (bkt ASC, ord1 ASC, gap ASC, ord2 ASC));
CREATE INDEX NONCONCURRENTLY gap_unread_tbl_idx ON gap_unread_tbl
    (bkt ASC, ord1 ASC, gap ASC, ord2 ASC) INCLUDE (x);
INSERT INTO gap_unread_tbl (bkt, ord1, gap, ord2, x) VALUES
    (1, '2020-01-02', '2020-01-02', 10, '2020-01-02'),  -- stream bkt = 1
    (1, '2020-01-02', '2020-01-02', 30, '2020-01-02'),
    (2, '2020-01-02', '2020-01-02', 20, '2020-01-02'),  -- stream bkt = 2
    (1, '2020-01-02', '2020-01-01', 99, '2020-01-01');  -- decoy, fails ord1 = x
ANALYZE gap_unread_tbl;

\set query ':P :Q SELECT ord1, ord2, bkt FROM gap_unread_tbl WHERE bkt IN (1, 2) AND ord1 = x AND x = gap ORDER BY ord1, ord2;'
\set Q1 '/*+IndexScan(gap_unread_tbl gap_unread_tbl_pkey) Set(yb_max_merge_scan_streams 64)*/'
\set Q2 '/*+IndexOnlyScan(gap_unread_tbl gap_unread_tbl_idx) Set(yb_max_merge_scan_streams 64)*/'
\set Q3 '/*+IndexScan(gap_unread_tbl gap_unread_tbl_idx) Set(yb_max_merge_scan_streams 64)*/'
\i :run_query
\unset Q1
\unset Q2
\unset Q3
DROP TABLE gap_unread_tbl;
RESET TimeZone;
