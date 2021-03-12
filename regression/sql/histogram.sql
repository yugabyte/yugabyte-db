CREATE EXTENSION pg_stat_monitor;

CREATE TABLE t1(a int);

SELECT pg_stat_monitor_reset();

INSERT INTO t1 VALUES(generate_series(1,10));
ANALYZE t1;
SELECT count(*) FROM t1;

INSERT INTO t1 VALUES(generate_series(1,10000));
ANALYZE t1;
SELECT count(*) FROM t1;;

INSERT INTO t1 VALUES(generate_series(1,1000000));
ANALYZE t1;
SELECT count(*) FROM t1;

INSERT INTO t1 VALUES(generate_series(1,10000000));
ANALYZE t1;
SELECT count(*) FROM t1;

SELECT query, calls, min_time, max_time, resp_calls FROM pg_stat_monitor ORDER BY query COLLATE "C";
SELECT * FROM histogram(0, 'F44CD1B4B33A47AF') AS a(range TEXT, freq INT, bar TEXT);

DROP TABLE t1;
SELECT pg_stat_monitor_reset();
DROP EXTENSION pg_stat_monitor;
