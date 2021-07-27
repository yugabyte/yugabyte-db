-- Create new tables
CREATE TABLE x(a int PRIMARY KEY, b text);
CREATE TABLE y(a int PRIMARY KEY, b text);

INSERT INTO x
  SELECT i, chr(ascii('A') + i) FROM generate_series(0, 24) as s(i);
INSERT INTO y
  SELECT i, CASE i % 4 WHEN 1 THEN 'odd' WHEN 2 THEN 'two' WHEN 3 THEN 'odd' END
  FROM generate_series(1, 25000) as s(i);

-- Check statistics is not there
SELECT
  relname,
  reltuples,
  staattnum,
  stanullfrac,
  stawidth,
  stadistinct
FROM
  pg_class LEFT JOIN pg_statistic ON pg_class.oid = starelid
WHERE
  relname IN ('x', 'y')
ORDER BY
  relname, staattnum;

-- Explain queries without statistics
EXPLAIN SELECT * FROM x;
EXPLAIN SELECT * FROM y;
EXPLAIN SELECT DISTINCT b FROM y;
EXPLAIN SELECT * FROM x, y WHERE x.a = y.a;
EXPLAIN SELECT * FROM y, x WHERE x.a = y.a;

ANALYZE x, y;

-- Check statistic is there
SELECT
  relname,
  reltuples,
  staattnum,
  stanullfrac,
  stawidth,
  stadistinct
FROM
  pg_class LEFT JOIN pg_statistic ON pg_class.oid = starelid
WHERE
  relname IN ('x', 'y')
ORDER BY
  relname, staattnum;

-- Explain queries with statistics
EXPLAIN SELECT * FROM x;
EXPLAIN SELECT * FROM y;
EXPLAIN SELECT DISTINCT b FROM y;
EXPLAIN SELECT * FROM x, y WHERE x.a = y.a;
EXPLAIN SELECT * FROM y, x WHERE x.a = y.a;

-- Modify tables
INSERT INTO x VALUES (25, 'Z');
DELETE FROM y WHERE a % 10 = 0;
ALTER TABLE y ADD COLUMN c int;
UPDATE y SET c = LENGTH(b) WHERE b IS NOT NULL;

ANALYZE x;
ANALYZE y;

-- Check updated statistics
SELECT
  relname,
  reltuples,
  staattnum,
  stanullfrac,
  stawidth,
  stadistinct
FROM
  pg_class LEFT JOIN pg_statistic ON pg_class.oid = starelid
WHERE
  relname IN ('x', 'y')
ORDER BY
  relname, staattnum;

-- Cleanup
DROP TABLE x;
DROP TABLE y;
