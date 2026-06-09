CREATE TABLE foo AS SELECT 'bar'::text AS t;
BEGIN; SET duckdb.force_execution = true; SELECT t::integer AS t1 FROM foo; ROLLBACK;
SET duckdb.force_execution = true;
SELECT 1 FROM foo;

-- Make sure we query the cache during a valid TX
DROP EXTENSION IF EXISTS pg_duckdb CASCADE;

BEGIN;
CREATE EXTENSION pg_duckdb;
SELECT * FROM do_not_exist;
END;

CREATE EXTENSION pg_duckdb;
