CREATE EXTENSION IF NOT EXISTS pg_stat_monitor;

CREATE DATABASE db1;
CREATE DATABASE db2;

\c db1
CREATE EXTENSION IF NOT EXISTS pg_stat_monitor;
CREATE TABLE t1 (a int);
CREATE TABLE t2 (b int);

\c db2
CREATE EXTENSION IF NOT EXISTS pg_stat_monitor;
CREATE TABLE t3 (c int);
CREATE TABLE t4 (d int);

SELECT pg_stat_monitor_reset();
\c db1
SELECT * FROM t1,t2 WHERE t1.a = t2.b;

\c db2
SELECT * FROM t3,t4 WHERE t3.c = t4.d;

SELECT datname, query FROM pg_stat_monitor ORDER BY query COLLATE "C";
SELECT pg_stat_monitor_reset();

\c db1
DROP TABLE t1;
DROP TABLE t2;

\c db2
DROP TABLE t3;
DROP TABLE t4;

\c yugabyte
DROP DATABASE db1;
DROP DATABASE db2;
DROP EXTENSION IF EXISTS pg_stat_monitor;
