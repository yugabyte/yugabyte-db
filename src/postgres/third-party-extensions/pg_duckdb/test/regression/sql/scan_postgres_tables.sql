CREATE TABLE t1(a INT, b INT, c TEXT);
INSERT INTO t1 SELECT g, g % 100, 'scan_potgres_table_'||g from generate_series(1,100000) g;

SET duckdb.log_pg_explain = true;

-- COUNT(*)
SELECT COUNT(*) FROM t1;

-- SEQ SCAN
SELECT COUNT(a) FROM t1 WHERE a < 10;

-- CREATE INDEX on t1
CREATE INDEX ON t1(a);

-- BITMAP INDEX
SELECT COUNT(a) FROM t1 WHERE a < 10;

-- INDEXONLYSCAN
SET enable_bitmapscan TO false;
SELECT COUNT(a) FROM t1 WHERE a = 1;

-- INDEXSCAN
SELECT COUNT(c) FROM t1 WHERE a = 1;

-- TEMPORARY TABLES JOIN WITH HEAP TABLES
CREATE TEMP TABLE t2(a int);
INSERT INTO t2 VALUES (1), (2), (3);

SELECT t1.a, t2.a FROM t1, t2 WHERE t1.a = t2.a;

-- JOIN WITH SAME TABLE (on WORKERS)
SELECT COUNT(*) FROM t1 AS t1_1, t1 AS t1_2 WHERE t1_1.a < 2 AND t1_2.a > 8;

-- JOIN WITH SAME TABLE (in BACKEND PROCESS)
SET max_parallel_workers TO 0;
SELECT COUNT(*) FROM t1 AS t1_1, t1 AS t1_2 WHERE t1_1.a < 2 AND t1_2.a > 8;
SET max_parallel_workers TO DEFAULT;


-- PARTITIONED TABLE

CREATE TABLE partitioned_table(a int, b INT, c text) PARTITION BY RANGE (a);
CREATE TABLE partition_1 PARTITION OF partitioned_table FOR VALUES FROM (0) TO (50);
CREATE TABLE partition_2 PARTITION OF partitioned_table FOR VALUES FROM (50) TO (100);
INSERT INTO partitioned_table SELECT g % 100, g, 'abcde_'||g from generate_series(1,100000) g;
CREATE INDEX ON partitioned_table(b);

SELECT COUNT(*) FROM partitioned_table WHERE a < 25;
SELECT COUNT(*) FROM partitioned_table WHERE a < 75;
SELECT COUNT(*) FROM partitioned_table WHERE a < 25 OR a > 75;
SELECT COUNT(*) FROM partitioned_table WHERE a < 25 AND b = 1;
SELECT COUNT(*) FROM partitioned_table, t2 WHERE partitioned_table.a = t2.a AND partitioned_table.a < 2;


SET enable_bitmapscan TO DEFAULT;
DROP TABLE t1, t2, partitioned_table;
