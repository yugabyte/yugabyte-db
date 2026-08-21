-- All functions should automatically use duckdb
set duckdb.execution = false;

SELECT time_bucket('1 day'::interval, '2023-01-15'::date);

SELECT time_bucket('1 month'::interval, '2023-01-15'::date, '2022-12-25'::date);

SELECT time_bucket('1 week'::interval, '2023-01-15'::date, '2 days'::interval);

SELECT time_bucket('1 hour'::interval, '2023-01-15 14:30:00'::timestamp);

SELECT time_bucket('4 hours'::interval, '2023-01-15 14:30:00'::timestamp, '1 hour'::interval);

SELECT time_bucket('1 day'::interval, '2023-01-15 14:30:00'::timestamp, '2022-12-25 06:00:00'::timestamp);

SELECT time_bucket('12 hours'::interval, '2023-01-15 14:30:00+00'::timestamp with time zone);

SELECT time_bucket('1 day'::interval, '2023-01-15 14:30:00+00'::timestamp with time zone, '6 hours'::interval);

SELECT time_bucket('1 week'::interval, '2023-01-15 14:30:00+00'::timestamp with time zone, '2022-12-25 12:00:00+00'::timestamp with time zone);

SELECT time_bucket('1 day'::interval, '2023-01-15 14:30:00+00'::timestamp with time zone, 'America/New_York');

SELECT time_bucket('1 day'::interval, r['date_col'])
FROM duckdb.query($$ SELECT DATE '2023-01-15' AS date_col $$) r;

SELECT time_bucket('1 week'::interval, r['date_col'], '2 days'::interval)
FROM duckdb.query($$ SELECT DATE '2023-01-15' AS date_col $$) r;

SELECT time_bucket('1 month'::interval, r['date_col'], '2022-12-25'::date)
FROM duckdb.query($$ SELECT DATE '2023-01-15' AS date_col $$) r;

SELECT time_bucket('1 hour'::interval, r['ts_col'])
FROM duckdb.query($$ SELECT TIMESTAMP '2023-01-15 14:30:00' AS ts_col $$) r;

SELECT time_bucket('4 hours'::interval, r['ts_col'], '1 hour'::interval)
FROM duckdb.query($$ SELECT TIMESTAMP '2023-01-15 14:30:00' AS ts_col $$) r;

SELECT time_bucket('1 day'::interval, r['ts_col'], '2022-12-25 06:00:00'::timestamp)
FROM duckdb.query($$ SELECT TIMESTAMP '2023-01-15 14:30:00' AS ts_col $$) r;

SELECT time_bucket('12 hours'::interval, r['tstz_col'])
FROM duckdb.query($$ SELECT TIMESTAMP WITH TIME ZONE '2023-01-15 14:30:00+00' AS tstz_col $$) r;

SELECT time_bucket('1 day'::interval, r['tstz_col'], '6 hours'::interval)
FROM duckdb.query($$ SELECT TIMESTAMP WITH TIME ZONE '2023-01-15 14:30:00+00' AS tstz_col $$) r;

SELECT time_bucket('1 week'::interval, r['tstz_col'], '2022-12-25 12:00:00+00'::timestamp with time zone)
FROM duckdb.query($$ SELECT TIMESTAMP WITH TIME ZONE '2023-01-15 14:30:00+00' AS tstz_col $$) r;

SELECT time_bucket('1 day'::interval, r['tstz_col'], 'America/New_York')
FROM duckdb.query($$ SELECT TIMESTAMP WITH TIME ZONE '2023-01-15 14:30:00+00' AS tstz_col $$) r;
