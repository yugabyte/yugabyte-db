CREATE TABLE t(a INT);

INSERT INTO t SELECT g % 10 from generate_series(1,1000) g;

SELECT COUNT(*) FROM t;
SELECT a, COUNT(*) FROM t WHERE a > 5 GROUP BY a ORDER BY a;

CREATE TABLE empty(a INT);
SELECT COUNT(*) FROM empty;

SET client_min_messages TO default;

DROP TABLE t;
DROP TABLE empty;

-- Check that DROP / CREATE extension works

DROP EXTENSION pg_duckdb;
CREATE EXTENSION pg_duckdb;


-- Verify that all pages that are fetched are closed before execution ends.
-- Table with smaller number of tuples (if nothing is matched) will terminate execution
-- before second table is read to the end.

CREATE TABLE rt(a INT);
CREATE TABLE lt(a INT);
INSERT INTO rt SELECT g FROM generate_series(1,100) g;
INSERT INTO lt SELECT g % 10 FROM generate_series(1,100000) g;
SELECT lt.a * rt.a FROM lt, rt WHERE lt.a % 2 = 0 AND rt.a = 0;
DROP TABLE lt;
DROP TABLE rt;


---- Regression for gh#347

CREATE TABLE t(a INT, b VARCHAR);
INSERT INTO t SELECT g from generate_series(1,10) g;
INSERT INTO t SELECT g % 10 from generate_series(1,1000) g;
INSERT INTO t SELECT g % 100, MD5(g::VARCHAR) FROM generate_series(1,1000) g;
INSERT INTO t SELECT g % 100, MD5(g::VARCHAR) FROM generate_series(1,1000) g;
SELECT COUNT(b) FROM t WHERE a > 3;
DROP TABLE t;

CREATE TABLE t (a INT) PARTITION BY LIST (a);
CREATE TABLE s (b INT);
SELECT * FROM t JOIN s ON a = b;
DROP TABLE t;
DROP TABLE s;

-- Check that we are counting detoasted value
CREATE TABLE t(a INT, b VARCHAR);
INSERT INTO t SELECT g, repeat('ABCDE', 10000) FROM generate_series(1, 10) g;
SELECT LENGTH(b) FROM t WHERE a = 5;
DROP TABLE t;
