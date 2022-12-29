CREATE EXTENSION pg_stat_monitor;

CREATE DATABASE db1;
CREATE DATABASE db2;

\c db1
CREATE TABLE t1 (a int);
CREATE TABLE t2 (b int);

CREATE FUNCTION add(integer, integer) RETURNS integer
    AS 'select $1 + $2;'
    LANGUAGE SQL
    IMMUTABLE
    RETURNS NULL ON NULL INPUT;

\c db2
CREATE TABLE t1 (a int);
CREATE TABLE t3 (c int);

CREATE FUNCTION add(integer, integer) RETURNS integer
    AS 'select $1 + $2;'
    LANGUAGE SQL
    IMMUTABLE
    RETURNS NULL ON NULL INPUT;

\c contrib_regression
SELECT pg_stat_monitor_reset();
\c db1
SELECT * FROM t1;
SELECT *, ADD(1, 2) FROM t1;
SELECT * FROM t2;

\c db2
SELECT * FROM t1;
SELECT *, ADD(1, 2) FROM t1;
SELECT * FROM t3;

\c contrib_regression
SELECT datname, pgsm_query_id, query FROM pg_stat_monitor ORDER BY pgsm_query_id, query, datname;
SELECT pg_stat_monitor_reset();

\c db1
DROP TABLE t1;
DROP TABLE t2;
DROP FUNCTION ADD;

\c db2
DROP TABLE t1;
DROP TABLE t3;
DROP FUNCTION ADD;

\c contrib_regression
DROP DATABASE db1;
DROP DATABASE db2;
DROP EXTENSION pg_stat_monitor;
