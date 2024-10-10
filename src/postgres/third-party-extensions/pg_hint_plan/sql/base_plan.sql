SET search_path TO public;

-- query type 1
EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id;
-- query type 2
EXPLAIN (COSTS false) SELECT * FROM t1, t4 WHERE t1.val < 10;
-- YB_COMMENT
-- CTID based scans and searches not supported in Yugabyte
-- query type 3
-- EXPLAIN (COSTS false) SELECT * FROM t3, t4 WHERE t3.id = t4.id AND t4.ctid = '(1,1)';
-- query type 4
-- EXPLAIN (COSTS false) SELECT * FROM t1, t2 WHERE t1.id = t2.id AND t1.ctid = '(1,1)';
-- query type 5
EXPLAIN (COSTS false) SELECT * FROM t1, t3 WHERE t1.val = t3.val;
-- query type 6
EXPLAIN (COSTS false) SELECT * FROM t1, t2, t3, t4 WHERE t1.id = t2.id AND t1.id = t3.id AND t1.id = t4.id;
