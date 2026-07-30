----------------------------------------
-- Timestamp tests
----------------------------------------
-- Test +/- inf values
CREATE TABLE t(a TIMESTAMP, b TEXT);
INSERT INTO t VALUES('Infinity','Positive INF'), ('-Infinity','Negative INF');

-- PG Execution
SELECT * from t;
SELECT isfinite(a),b FROM t;

set duckdb.force_execution = true;
-- DuckDB execution
SELECT * from t;
SELECT isfinite(a),b FROM t;

-- Cleanup
set duckdb.force_execution = false;
DROP TABLE t;

SELECT * FROM duckdb.query($$ SELECT '4714-11-24 (BC) 00:00:00'::timestamp_s as timestamp_s $$);
SELECT * FROM duckdb.query($$ SELECT '4714-11-23 (BC) 23:59:59'::timestamp_s as timestamp_s $$);  -- out of range
SELECT * FROM duckdb.query($$ SELECT '294246-12-31 23:59:59'::timestamp_s as timestamp_s $$);
SELECT * FROM duckdb.query($$ SELECT '294247-01-01 00:00:00'::timestamp_s as timestamp_s $$);  -- out of range

SELECT * FROM duckdb.query($$ SELECT '4714-11-24 (BC) 00:00:00.000000'::timestamp as timestamp $$);
SELECT * FROM duckdb.query($$ SELECT '4714-11-23 (BC) 23:59:59.999999'::timestamp as timestamp $$);  -- out of range
SELECT * FROM duckdb.query($$ SELECT '294246-12-31 23:59:59.999999'::timestamp as timestamp $$);
SELECT * FROM duckdb.query($$ SELECT '294247-01-01 00:00:00.000000'::timestamp as timestamp $$);  -- out of range

SELECT * FROM duckdb.query($$ SELECT '4714-11-24 (BC) 00:00:00.000'::timestamp_ms as timestamp_ms $$);
SELECT * FROM duckdb.query($$ SELECT '4714-11-23 (BC) 23:59:59.999'::timestamp_ms as timestamp_ms $$);  -- out of range
SELECT * FROM duckdb.query($$ SELECT '294246-12-31 23:59:59.999'::timestamp_ms as timestamp_ms $$);
SELECT * FROM duckdb.query($$ SELECT '294247-01-01 00:00:00.000'::timestamp_ms as timestamp_ms $$);  -- out of range
----------------------------------------
-- TimestampTz tests
----------------------------------------
-- Test +/- inf valuestz
CREATE TABLE t(a TIMESTAMPTZ, b TEXT);
INSERT INTO t VALUES('Infinity','Positive INF'), ('-Infinity','Negative INF');

-- PG Execution
SELECT * from t;
SELECT isfinite(a), b FROM t;

set duckdb.force_execution = true;
-- DuckDB execution
SELECT * from t;
SELECT isfinite(a), b FROM t;

-- Cleanup
set duckdb.force_execution = false;
DROP TABLE t;

SELECT * FROM duckdb.query($$ SELECT '4714-11-24 (BC) 00:00:00+00'::timestamptz as timestamptz $$);
SELECT * FROM duckdb.query($$ SELECT '4714-11-23 (BC) 23:59:59+00'::timestamptz as timestamptz $$);  -- out of range
SELECT * FROM duckdb.query($$ SELECT '294246-12-31 23:59:59+00'::timestamptz as timestamptz $$);
SELECT * FROM duckdb.query($$ SELECT '294247-01-01 00:00:00+00'::timestamptz as timestamptz $$);  -- out of range

set time zone 'Africa/Abidjan';
select * from duckdb.query($$ SELECT * FROM duckdb_settings() WHERE name = 'TimeZone' $$) r;
SELECT * FROM duckdb.query($$ SELECT '4714-11-24 (BC) 00:00:00+00'::timestamptz as timestamptz $$);
SELECT * FROM duckdb.query($$ SELECT '294246-12-31 23:59:59+00'::timestamptz as timestamptz $$);
select * from duckdb.query($$ select strptime('2025-07-03 21:35:03.871 utc', '%Y-%m-%d %H:%M:%S.%g %Z') $$) r;

SET TIME ZONE 'UTC';
select * from duckdb.query($$ SELECT * FROM duckdb_settings() WHERE name = 'TimeZone' $$) r;
SELECT * FROM duckdb.query($$ SELECT '4714-11-24 (BC) 00:00:00+00'::timestamptz as timestamptz $$);
SELECT * FROM duckdb.query($$ SELECT '294246-12-31 23:59:59+00'::timestamptz as timestamptz $$);
select * from duckdb.query($$ select strptime('2025-07-03 21:35:03.871 utc', '%Y-%m-%d %H:%M:%S.%g %Z') $$) r;

CREATE TABLE t (
    id int PRIMARY KEY,
    d timestamp without time zone,
    tz timestamp with time zone
);

SET TIME ZONE 'Asia/Shanghai';
select * from duckdb.query($$ SELECT * FROM duckdb_settings() WHERE name = 'TimeZone' $$) r;

insert into t values (1,'2025-07-03 20:35:03.871', '2025-07-03 20:35:03.871');
insert into t values (2,'2025-07-03 21:35:03.871', '2025-07-03 21:35:03.871');
insert into t values (3,'2025-07-03 22:35:03.871', '2025-07-03 22:35:03.871');


SET TIME ZONE 'UTC';
select * from duckdb.query($$ SELECT * FROM duckdb_settings() WHERE name = 'TimeZone' $$) r;

insert into t values (4,'2025-07-03 23:35:03.871', '2025-07-03 23:35:03.871');
SELECT * FROM t WHERE d IN (SELECT tz FROM t);

set duckdb.force_execution = on;

SELECT * FROM t WHERE d IN (SELECT tz FROM t);

SET TIME ZONE 'Asia/Shanghai';
select * from duckdb.query($$ SELECT * FROM duckdb_settings() WHERE name = 'TimeZone' $$) r;

set duckdb.force_execution = off;
SELECT * FROM t WHERE d IN (SELECT tz FROM t);

set duckdb.force_execution = on;

SELECT * FROM t WHERE d IN (SELECT tz FROM t);


drop extension pg_duckdb;

SET TIME ZONE 'UTC';
SHOW TIME ZONE;

SELECT * FROM t WHERE d IN (SELECT tz FROM t);

SET TIME ZONE 'Asia/Shanghai';
SHOW TIME ZONE;

SELECT * FROM t WHERE d IN (SELECT tz FROM t);

drop table t;
SET TIME ZONE DEFAULT;

create extension pg_duckdb;
