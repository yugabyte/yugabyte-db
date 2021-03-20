CREATE EXTENSION pg_stat_monitor;

CREATE TABLE t1(a int);
CREATE TABLE t2(b int);
INSERT INTO t1 VALUES(generate_series(1,1000));
INSERT INTO t2 VALUES(generate_series(1,5000));

SELECT pg_stat_monitor_reset();
SELECT * FROM t1;
SELECT * FROM t2;

SELECT * FROM t1 LIMIT 10;
SELECt * FROM t2  WHERE b % 2 = 0;

SELECT query, rows_retrieved FROM pg_stat_monitor ORDER BY query COLLATE "C";
SELECT pg_stat_monitor_reset();

DROP TABLE t1;
DROP EXTENSION pg_stat_monitor;
