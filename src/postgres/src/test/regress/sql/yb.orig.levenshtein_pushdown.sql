-- Test levenshtein function pushdown to DocDB
-- Verifies that all levenshtein variants use Storage Filter and return correct results.

\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/explainrun.sql'
\i :filename
\set explain 'EXPLAIN (ANALYZE, COSTS OFF, TIMING OFF, SUMMARY OFF)'

CREATE TABLE test_levenshtein (id SERIAL PRIMARY KEY, name TEXT);
INSERT INTO test_levenshtein (name) VALUES ('GUMBO'), ('GAMBOL'), ('JUMBO'), ('BUMBO'), ('HELLO'), ('WORLD');

-- Test levenshtein(text, text) push down
\set query 'SELECT name FROM test_levenshtein WHERE levenshtein(name, ''GUMBO'') < 3 ORDER BY name'
:explain1run1

-- Test levenshtein(text, text, int, int, int) push down
\set query 'SELECT name FROM test_levenshtein WHERE levenshtein(name, ''GUMBO'', 1, 1, 2) < 3 ORDER BY name'
:explain1run1

-- Test levenshtein_less_equal(text, text, int) push down
\set query 'SELECT name FROM test_levenshtein WHERE levenshtein_less_equal(name, ''GUMBO'', 2) <= 2 ORDER BY name'
:explain1run1

-- Test levenshtein_less_equal(text, text, int, int, int, int) push down
\set query 'SELECT name FROM test_levenshtein WHERE levenshtein_less_equal(name, ''GUMBO'', 1, 1, 2, 2) <= 2 ORDER BY name'
:explain1run1

-- Test exact match (distance = 0)
\set query 'SELECT name FROM test_levenshtein WHERE levenshtein(name, ''GUMBO'') = 0'
:explain1run1

-- Test no matches case
\set query 'SELECT name FROM test_levenshtein WHERE levenshtein(name, ''XXXXX'') < 2'
:explain1run1

-- Test SELECT with levenshtein function call
\set query 'SELECT levenshtein(''GUMBO'', ''GAMBOL'')'
:explain1run1

-- Test levenshtein in complex expression
\set query 'SELECT name FROM test_levenshtein WHERE levenshtein(name, ''GUMBO'') + length(name) < 10 ORDER BY name'
:explain1run1

-- Create fuzzystrmatch extension with levenshtein functions in the public schema
-- This version of levenshtein will not be pushed down to DocDB
CREATE EXTENSION IF NOT EXISTS fuzzystrmatch SCHEMA public;

-- Test levenshtein with schema qualified
\set query 'SELECT name FROM test_levenshtein WHERE public.levenshtein(name, ''GUMBO'') < 3 ORDER BY name'
:explain1run1

-- Test levenshtein with setting search_path
-- There is a default prioritized scan of pg_catalog over other schemas,
-- so levenshtein functions will still be pushed down when search_path is set to public
SET search_path = public;
\set query 'SELECT name FROM test_levenshtein WHERE levenshtein(name, ''GUMBO'') < 3 ORDER BY name'
:explain1run1
-- If pg_catalog is explicitly put after other schemas in the search_path,
-- levenshtein calls will then refer to the non built-in version
SET search_path = public, pg_catalog;
\set query 'SELECT name FROM test_levenshtein WHERE levenshtein(name, ''GUMBO'') < 3 ORDER BY name'
:explain1run1

-- Cleanup
RESET search_path;
DROP TABLE test_levenshtein;

-- Test LIMIT optimization with levenshtein pushdown (from github #29299)
-- This tests early exit when LIMIT is present with pushed-down filter

-- Helper function to normalize text for fuzzy matching
CREATE OR REPLACE FUNCTION sort_words(t text)
RETURNS text
LANGUAGE sql
IMMUTABLE
RETURNS NULL ON NULL INPUT
AS $$
WITH tokens AS (
  SELECT lower(x) AS tok
  FROM regexp_split_to_table(t, E'\\s+') AS x
  WHERE x <> ''
  ORDER BY 1
)
SELECT string_agg(tok, ' ')
FROM tokens
$$;

DROP TABLE IF EXISTS t1 CASCADE;

CREATE TABLE t1 (
  id bigserial PRIMARY KEY,
  c1 int NOT NULL,
  c2 timestamptz NOT NULL,
  c3 text,
  c4 text
);

INSERT INTO t1 (c1, c2, c3, c4)
SELECT
  100,
  ts,
  n,
  sort_words(n)
FROM (
  SELECT
    (timestamp '2025-03-01' + (g*interval '1 minute')) AS ts,
    CASE (g % 12)
      WHEN 0 THEN 'body95855317'
      WHEN 1 THEN '95855317 body'
      WHEN 2 THEN 'b0dy95855317'
      WHEN 3 THEN 'body 95855317'
      WHEN 4 THEN 'bod y95855317'
      WHEN 5 THEN 'john doe'
      WHEN 6 THEN 'mary jane'
      WHEN 7 THEN 'body9585531'
      WHEN 8 THEN 'buddy95855317'
      WHEN 9 THEN '95855317body'
      WHEN 10 THEN 'alpha beta'
      ELSE 'totally different'
    END AS n
  FROM generate_series(0, 9999) AS g
) s
WHERE ts < '2025-09-01';

INSERT INTO t1 (c1, c2, c3, c4)
SELECT
  200,
  ts,
  n,
  sort_words(n)
FROM (
  SELECT
    (timestamp '2025-03-01' + (g*interval '2 minute')) AS ts,
    CASE (g % 6)
      WHEN 0 THEN 'other customer'
      WHEN 1 THEN 'alice'
      WHEN 2 THEN 'bob'
      WHEN 3 THEN 'charlie'
      WHEN 4 THEN 'david'
      ELSE 'eve'
    END AS n
  FROM generate_series(0, 3000) AS g
) s
WHERE ts < '2025-09-01';

-- Test expression index with levenshtein uses Index Cond without Sort
CREATE INDEX t1_levenshtein_idx ON t1 (levenshtein(c4, 'body95855317') asc);
\set query 'SELECT c3 FROM t1 WHERE levenshtein(c4, ''body95855317'') < 3 ORDER BY levenshtein(c4, ''body95855317'') LIMIT 5'
:explain1run1
DROP INDEX t1_levenshtein_idx;

CREATE INDEX ON t1 (c1 asc, c2);
CREATE INDEX ON t1 (c1 asc, c2, c3);

-- This query should use Index Scan with Storage Index Filter and exit early due to LIMIT
SELECT $$
SELECT count(x.agg_field) AS aggr
FROM (
  SELECT t.c3 AS agg_field
  FROM t1 AS t
  WHERE t.c1 = 100
    AND t.c3 IS NOT NULL
    AND t.c3 != ''
    AND t.c3 != 'body95855317'
    AND ((length('body95855317') + length(t.c3)
          - levenshtein(sort_words('body95855317'), t.c4))::float
         / (length('body95855317') + length(t.c3)))::float >= 0.85
    AND t.c2 >= '2025-03-01'
    AND t.c2 < '2025-09-01'
  LIMIT 3
) AS x
$$ AS query \gset
:explain1run1

-- Cleanup
DROP TABLE t1 CASCADE;
DROP FUNCTION sort_words(text);
