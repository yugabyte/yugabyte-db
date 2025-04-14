CREATE EXTENSION pg_stat_monitor;

CREATE TABLE t1(a int);
CREATE TABLE t2(b int);
INSERT INTO t1 VALUES(generate_series(1,1000));
INSERT INTO t2 VALUES(generate_series(1,5000));

SELECT pg_stat_monitor_reset();
SELECT * FROM t1 ORDER BY a ASC; -- YB: add ordering
SELECT * FROM t2 ORDER BY b ASC; -- YB: add ordering

SELECT * FROM t1 ORDER BY a ASC LIMIT 10; -- YB: add ordering
SELECt * FROM t2  WHERE b % 2 = 0 ORDER BY b ASC; -- YB: add ordering

SELECT query, rows FROM pg_stat_monitor ORDER BY query COLLATE "C"; -- YB: output differs because of YB ordering
SELECT pg_stat_monitor_reset();

DROP TABLE t1;
DROP TABLE t2;
DROP EXTENSION pg_stat_monitor;
