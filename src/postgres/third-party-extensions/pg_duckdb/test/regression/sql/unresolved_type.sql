-- All functions should automatically use duckdb
set duckdb.execution = false;
select r['a'] ~ 'a.*' from duckdb.query($$ SELECT 'abc' a $$) r;
select r['a'] !~ 'x.*' from duckdb.query($$ SELECT 'abc' a $$) r;
select r['a'] LIKE '%b%' from duckdb.query($$ SELECT 'abc' a $$) r;
select r['a'] NOT LIKE '%x%' from duckdb.query($$ SELECT 'abc' a $$) r;
select r['a'] ILIKE '%B%' from duckdb.query($$ SELECT 'abc' a $$) r;
select r['a'] NOT ILIKE '%X%' from duckdb.query($$ SELECT 'abc' a $$) r;
-- Currently not working, we need to add the similar_to_escape function to
-- DuckDB to make this work.
select r['a'] SIMILAR TO '%b%' from duckdb.query($$ SELECT 'abc' a $$) r;
select r['a'] NOT SIMILAR TO '%b%' from duckdb.query($$ SELECT 'abc' a $$) r;
select length(r['a']) from duckdb.query($$ SELECT 'abc' a $$) r;
select regexp_replace(r['a'], 'a', 'x') from duckdb.query($$ SELECT 'abc' a $$) r;
select regexp_replace(r['a'], 'A', 'x', 'i') from duckdb.query($$ SELECT 'abc' a $$) r;
select date_trunc('year', r['ts']) from duckdb.query($$ SELECT '2024-08-04 12:34:56'::timestamp ts $$) r;

select strptime('Wed, 1 January 1992 - 08:38:40 PM', '%a, %-d %B %Y - %I:%M:%S %p');
select strptime('4/15/2023 10:56:00', ARRAY['%d/%m/%Y %H:%M:%S', '%m/%d/%Y %H:%M:%S']);
select strftime(date '1992-01-01', '%a, %-d %B %Y - %I:%M:%S %p');
select strftime(timestamp '1992-01-01 20:38:40', '%a, %-d %B %Y - %I:%M:%S %p');
select strftime(timestamptz '1992-01-01 20:38:40', '%a, %-d %B %Y - %I:%M:%S %p');
select strftime(r['ts'], '%a, %-d %B %Y - %I:%M:%S %p') from duckdb.query($$ SELECT timestamp '1992-01-01 20:38:40' ts $$) r;

select epoch_ms(701222400000);
select epoch_ms(TIMESTAMP '2021-08-03 11:59:44.123456');
select epoch_ns(TIMESTAMP '2021-08-03 11:59:44.123456');
select epoch_us(TIMESTAMP '2021-08-03 11:59:44.123456');
select epoch('2022-11-07 08:43:04'::TIMESTAMP);

select epoch_ms(r['a']) from duckdb.query($$ SELECT 701222400000 a $$) r;
select epoch_ms(r['ts']) from duckdb.query($$ SELECT TIMESTAMP '2021-08-03 11:59:44.123456' ts $$) r;
select epoch_ns(r['ts']) from duckdb.query($$ SELECT TIMESTAMP '2021-08-03 11:59:44.123456' ts $$) r;
select epoch_us(r['ts']) from duckdb.query($$ SELECT TIMESTAMP '2021-08-03 11:59:44.123456' ts $$) r;
select epoch(r['ts']) from duckdb.query($$ SELECT '2022-11-07 08:43:04'::TIMESTAMP ts $$) r;

-- Tests for make_timestamp[tz]. The ones with many arguments also exist in
-- postgres, so we force duckdb execution by using duckdb.query.
select make_timestamp(2023, 6, 12, 10, 30, 12.42) FROM duckdb.query($$ SELECT 1
$$);
select make_timestamptz(2023, 6, 12, 10, 30, 12.42) FROM duckdb.query($$ SELECT 1
$$);
select make_timestamptz(2023, 6, 12, 10, 30, 12.42, 'CET') FROM duckdb.query($$ SELECT 1
$$);

select make_timestamp(1686570000000000);
select make_timestamp(r['microseconds']) from duckdb.query($$ SELECT 1686570000000000 AS microseconds $$) r;
select make_timestamptz(1686570000000000);
select make_timestamptz(r['microseconds']) from duckdb.query($$ SELECT 1686570000000000 AS microseconds $$) r;
