CREATE EXTENSION pg_stat_monitor;

CREATE DATABASE db1;
CREATE DATABASE db2;

\c db1
CREATE TABLE t1 (a int);
CREATE TABLE t2 (b int);

\c db2
CREATE TABLE t3 (c int);
CREATE TABLE t4 (d int);

\c contrib_regression
SELECT pg_stat_monitor_reset();
\c db1
SELECT * FROM t1,t2 WHERE t1.a = t2.b;

\c db2
SELECT * FROM t3,t4 WHERE t3.c = t4.d;

\c contrib_regression
DROP DATABASE db2;

SELECT datname, query FROM pg_stat_monitor ORDER BY query COLLATE "C";
SELECT pg_stat_monitor_reset();

\c db1
DROP TABLE t1;
DROP TABLE t2;

\c contrib_regression
DROP DATABASE db1;
DROP EXTENSION pg_stat_monitor;
