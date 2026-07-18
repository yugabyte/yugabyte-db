-- avoid bit-exact output here because operations may not be bit-exact.
SET extra_float_digits = 0;

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

create temp table prtx1 (a integer, b integer, c integer)
  partition by range (a);
create temp table prtx1_1 partition of prtx1 for values from (1) to (11);
create temp table prtx1_2 partition of prtx1 for values from (11) to (21);
create temp table prtx1_3 partition of prtx1 for values from (21) to (31);
insert into prtx1 select 1 + i%30, i, i
  from generate_series(1,1000) i;
analyze prtx1;

-- Verify analyze works after dropping a table column.
ALTER TABLE x DROP COLUMN b;
ANALYZE x;
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
  relname = 'x'
ORDER BY
  relname, staattnum;

-- Cleanup
DROP TABLE x;
DROP TABLE y;

-- Analyze all tables
ANALYZE;


-- Analyze partitioned table
CREATE TABLE t_part (i int, a int, b int) PARTITION BY RANGE (i);
CREATE TABLE t_part1 PARTITION OF t_part FOR VALUES FROM (1) TO (501);
CREATE TABLE t_part2 PARTITION OF t_part FOR VALUES FROM (501) TO (601);
CREATE TABLE t_part3 PARTITION OF t_part FOR VALUES FROM (601) TO (MAXVALUE)
    PARTITION BY RANGE (a);
CREATE TABLE t_part3s1 PARTITION OF t_part3 FOR VALUES FROM (10) TO (51);
CREATE TABLE t_part3s2 PARTITION OF t_part3 FOR VALUES FROM (51) TO (401);
CREATE TABLE t_part3s3 PARTITION OF t_part3 DEFAULT;

INSERT INTO t_part
    SELECT i, a, b
    FROM (
        SELECT
            i, a,
            CASE
                WHEN i BETWEEN 1 AND 500 THEN
                    i % 20 + 1
                WHEN i BETWEEN 501 AND 600 THEN
                    i % 100 + 1
                WHEN i >= 601 THEN
                    CASE
                        WHEN a BETWEEN 10 AND 50 THEN
                            i
                        WHEN a BETWEEN 51 AND 400 THEN
                            NULL
                        ELSE
                            CASE WHEN i % 20 = 0 THEN i END
                    END
                ELSE
                    i % 20 + 1
            END AS b
        FROM (
            SELECT i, i % 500 + 1 AS a
            FROM generate_series(1, 10000) i
        ) vv
    ) v;

-- Same size of sample from each (sub)partition
ANALYZE t_part;

SELECT relname, attname, reltuples, stadistinct, stanullfrac
    FROM pg_statistic s, pg_class c, pg_attribute a
    WHERE c.oid = starelid
      AND c.oid = attrelid
      AND attnum = staattnum
      AND relname LIKE 't_part%'
    ORDER BY starelid, attnum;

-- Varying size of sample based on previously collected reltuples
ANALYZE t_part;

SELECT relname, attname, reltuples, stadistinct, stanullfrac
    FROM pg_statistic s, pg_class c, pg_attribute a
    WHERE c.oid = starelid
      AND c.oid = attrelid
      AND attnum = staattnum
      AND relname LIKE 't_part%'
    ORDER BY starelid, attnum;

DROP TABLE t_part;
