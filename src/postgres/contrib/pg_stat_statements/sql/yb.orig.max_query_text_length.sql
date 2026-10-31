CREATE EXTENSION pg_stat_statements;

-- Keep SET, SHOW and other utility statements out of the results.
SET pg_stat_statements.track_utility = FALSE;
SELECT pg_stat_statements_reset() IS NOT NULL AS t;

-- Without a limit, a long statement is stored in full.
SELECT 'first' AS first_long_column_name, 'second' AS second_long_column_name;

SET pg_stat_statements.yb_max_query_text_length = 20;
SHOW pg_stat_statements.yb_max_query_text_length;

-- A statement recorded before the limit was set keeps its full text.
SELECT 'first' AS first_long_column_name, 'second' AS second_long_column_name;

-- Shorter than or exactly at the limit: stored as is.
SELECT 1 AS abc;
SELECT 1, 2 AS abc;

-- One byte over the limit: the first 20 bytes are kept, followed by "...".
SELECT 1, 2, 3, 4;

-- A statement without constants is truncated the same way.
SELECT count(*) FROM pg_am WHERE amname IS NULL;

-- A two-byte character that crosses the limit is dropped whole, so only 19
-- bytes are kept.
SELECT 1.5 AS "aéééé";

-- A limit of 0 keeps only the "..." marker.
SET pg_stat_statements.yb_max_query_text_length = 0;
SELECT 1, 2, 3;

-- A prepared statement with bind parameters, like a batched insert from a
-- driver, is truncated the same way.
CREATE TABLE yb_trunc_t (a int, b int);
SET pg_stat_statements.yb_max_query_text_length = 90;
PREPARE yb_trunc_ins(int, int, int, int, int, int) AS INSERT INTO yb_trunc_t VALUES ($1, $2), ($3, $4), ($5, $6);
EXECUTE yb_trunc_ins(1, 2, 3, 4, 5, 6);
DEALLOCATE yb_trunc_ins;

-- The password is redacted before the text is truncated, so a cut where the
-- password was never stores part of it.
SET pg_stat_statements.yb_max_query_text_length = 40;
SET pg_stat_statements.track_utility = TRUE;
CREATE ROLE yb_trunc_pw PASSWORD 'secret_value';
SET pg_stat_statements.track_utility = FALSE;

RESET pg_stat_statements.yb_max_query_text_length;
SELECT query, octet_length(query) AS bytes, calls
  FROM pg_stat_statements
  WHERE query NOT LIKE '%pg_stat_statements%'
  ORDER BY query COLLATE "C";

DROP ROLE yb_trunc_pw;
DROP TABLE yb_trunc_t;
DROP EXTENSION pg_stat_statements;
