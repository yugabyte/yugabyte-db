CREATE EXTENSION pg_stat_monitor;

SELECT pg_stat_monitor_reset();
CREATE TABLE t1 (a INTEGER);
CREATE TABLE t2 (b INTEGER);
INSERT INTO t1 VALUES(1);
SELECT a FROM t1;
UPDATE t1 SET a = 2;
DELETE FROM t1;
SELECT b FROM t2 FOR UPDATE;
TRUNCATE t1;
DROP TABLE t1;
DROP TABLE t2;
SELECT query, cmd_type,  cmd_type_text FROM pg_stat_monitor ORDER BY query COLLATE "C";
SELECT pg_stat_monitor_reset();

DROP EXTENSION pg_stat_monitor;
