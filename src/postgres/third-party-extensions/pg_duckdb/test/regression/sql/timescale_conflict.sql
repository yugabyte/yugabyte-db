DROP EXTENSION pg_duckdb CASCADE;
CREATE FUNCTION public.time_bucket(bucket_width interval, ts timestamp)
RETURNS timestamp
SET search_path = pg_catalog, pg_temp
LANGUAGE sql AS $$
SELECT '1924-01-01 00:00:00'::timestamp;
$$;

SELECT time_bucket('1 hour', '2014-01-01 00:00:00'::timestamp);
CREATE EXTENSION pg_duckdb;
SELECT count(*) from pg_proc where proname = 'time_bucket' and pronamespace = 'public'::regnamespace;
SELECT count(*) from pg_proc where proname = 'time_bucket' and pronamespace = 'duckdb'::regnamespace;

DROP function public.time_bucket(bucket_width interval, ts timestamp);
DROP EXTENSION pg_duckdb CASCADE;
CREATE EXTENSION pg_duckdb;
SELECT count(*) from pg_proc where proname = 'time_bucket' and pronamespace = 'public'::regnamespace;
SELECT count(*) from pg_proc where proname = 'time_bucket' and pronamespace = 'duckdb'::regnamespace;
