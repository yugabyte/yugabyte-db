CREATE EXTENSION pg_stat_monitor;

CREATE TABLE IF NOT EXISTS t1(a int);
CREATE TABLE IF NOT EXISTS t2(b int);
INSERT INTO t1 VALUES(generate_series(1,1000));
INSERT INTO t2 VALUES(generate_series(1,5000));

SELECT pg_stat_monitor_reset();
SELECT * FROM t1 ORDER BY a ASC;
SELECT * FROM t2 ORDER BY b ASC;

SELECT * FROM t1 ORDER BY a ASC LIMIT 10;
SELECt * FROM t2  WHERE b % 2 = 0 ORDER BY b ASC;

SELECT query, rows_retrieved FROM pg_stat_monitor ORDER BY query COLLATE "C";
SELECT pg_stat_monitor_reset();

DROP TABLE t1;
DROP EXTENSION pg_stat_monitor;
