CREATE TABLE t(a TEXT);
INSERT INTO t VALUES ('a\');

set duckdb.force_execution = true;

-- Test with PG default escape character
SELECT a LIKE 'a\%' FROM t;
SELECT a ILIKE 'a\%' FROM t;
SELECT a NOT LIKE 'a\%' FROM t;
SELECT a NOT ILIKE 'a\%' FROM t;

INSERT INTO t VALUES ('a$'), ('a%'), ('A\'), ('A$'), ('A%');

-- With a different escape character, present in the pattern
SELECT a, a LIKE 'a$%' ESCAPE '$' FROM t;
SELECT a, a ILIKE 'a$%' ESCAPE '$' FROM t;
SELECT a, a NOT LIKE 'a$%' ESCAPE '$' FROM t;
SELECT a, a NOT ILIKE 'a$%' ESCAPE '$' FROM t;

-- With a different escape character, not present in the pattern
SELECT a, a LIKE 'a%' ESCAPE '$' FROM t;
SELECT a, a ILIKE 'a%' ESCAPE '$' FROM t;
SELECT a, a NOT LIKE 'a%' ESCAPE '$' FROM t;
SELECT a, a NOT ILIKE 'a%' ESCAPE '$' FROM t;

DROP TABLE t;
