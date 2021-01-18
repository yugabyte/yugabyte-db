/* contrib/pg_stat_monitor/pg_stat_monitor--1.1.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pg_stat_monitor" to load this file. \quit

-- Register functions.
CREATE FUNCTION pg_stat_monitor_reset()
RETURNS void
AS 'MODULE_PATHNAME'
LANGUAGE C PARALLEL SAFE;

CREATE FUNCTION pg_stat_monitor_version()
RETURNS text
AS 'MODULE_PATHNAME'
LANGUAGE C PARALLEL SAFE;

CREATE FUNCTION pg_stat_monitor(IN showtext boolean,
    OUT bucket              int,
    OUT userid              oid,
    OUT dbid                oid,
    OUT client_ip           bigint,

    OUT queryid             text,
    OUT query               text,
	OUT application_name	text,
	OUT relations			text,
	OUT cmd_type			text,
	OUT elevel              int,
    OUT sqlcode             int,
    OUT message             text,
    OUT bucket_start_time   text,

	OUT plans          		int8,
    OUT plan_total_time     float8,
    OUT plan_min_time       float8,
    OUT plan_max_time       float8,
    OUT plan_mean_time      float8,
    OUT plan_stddev_time    float8,
    OUT plan_rows           int8,

	OUT calls         		int8,
    OUT total_time          float8,
    OUT min_time            float8,
    OUT max_time            float8,
    OUT mean_time           float8,
    OUT stddev_time         float8,
    OUT rows       			int8,

	OUT shared_blks_hit     int8,
    OUT shared_blks_read    int8,
    OUT shared_blks_dirtied int8,
    OUT shared_blks_written int8,
    OUT local_blks_hit      int8,
    OUT local_blks_read     int8,
    OUT local_blks_dirtied  int8,
    OUT local_blks_written  int8,
    OUT temp_blks_read      int8,
    OUT temp_blks_written   int8,
    OUT blk_read_time       float8,
    OUT blk_write_time      float8,
    OUT resp_calls          text,
    OUT cpu_user_time       float8,
    OUT cpu_sys_time        float8
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pg_stat_monitor'
LANGUAGE C STRICT VOLATILE PARALLEL SAFE;

CREATE FUNCTION pg_stat_monitor_settings(
    OUT name  text,
    OUT value INTEGER,
    OUT default_value INTEGER,
    OUT description  text,
    OUT minimum INTEGER,
    OUT maximum INTEGER,
    OUT restart INTEGER
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pg_stat_monitor_settings'
LANGUAGE C STRICT VOLATILE PARALLEL SAFE;

CREATE VIEW pg_stat_monitor_settings AS SELECT
    name,
    value,
    default_value,
    description,
    minimum,
    maximum,
    restart
FROM pg_stat_monitor_settings();

-- Register a view on the function for ease of use.
CREATE VIEW pg_stat_monitor AS SELECT
    bucket,
	bucket_start_time AS bucket_start_time,
    userid::regrole,
    datname,
	'0.0.0.0'::inet + client_ip AS client_ip,
    queryid,
    query,
	application_name,
	(string_to_array(relations, ','))::oid[]::regclass[] AS relations,
	CASE
			WHEN query like 'BEGIN' THEN  ''
            WHEN query like 'END' THEN ''
            ELSE (string_to_array(cmd_type, ','))[1]
    END AS cmd_type,
	elevel,
	sqlcode,
	message,
	plans,
	round( CAST(plan_total_time as numeric), 4)::float8 as plan_total_time,
	round( CAST(plan_min_time as numeric), 4)::float8 as plan_min_time,
	round( CAST(plan_max_time as numeric), 4)::float8 as plan_max_time,
	round( CAST(plan_mean_time as numeric), 4)::float8 as plan_mean_time,
	round( CAST(plan_stddev_time as numeric), 4)::float8 as plan_stddev_time,
    calls,
	round( CAST(total_time as numeric), 4)::float8 as total_time,
	round( CAST(min_time as numeric), 4)::float8 as min_time,
	round( CAST(max_time as numeric), 4)::float8 as max_time,
	round( CAST(mean_time as numeric), 4)::float8 as mean_time,
	round( CAST(stddev_time as numeric), 4)::float8 as stddev_time,
	rows,
    shared_blks_hit,
    shared_blks_read,
    shared_blks_dirtied,
    shared_blks_written,
    local_blks_hit,
    local_blks_read,
    local_blks_dirtied,
    local_blks_written,
    temp_blks_read,
    temp_blks_written,
    blk_read_time,
    blk_write_time,
	(string_to_array(resp_calls, ',')) resp_calls,
	round(cpu_user_time::numeric, 4) as cpu_user_time,
	round(cpu_sys_time::numeric, 4) as cpu_sys_time
FROM pg_stat_monitor(TRUE), pg_database WHERE dbid = oid
ORDER BY bucket_start_time;

CREATE FUNCTION decode_error_level(elevel int)
RETURNS  text
AS
$$
SELECT
        CASE
           WHEN elevel = 0 THEN  ''
           WHEN elevel = 10 THEN 'DEBUG5'
           WHEN elevel = 11 THEN 'DEBUG4'
           WHEN elevel = 12 THEN 'DEBUG3'
           WHEN elevel = 13 THEN 'DEBUG2'
           WHEN elevel = 14 THEN 'DEBUG1'
           WHEN elevel = 15 THEN 'LOG'
           WHEN elevel = 16 THEN 'LOG_SERVER_ONLY'
           WHEN elevel = 17 THEN 'INFO'
           WHEN elevel = 18 THEN 'NOTICE'
           WHEN elevel = 19 THEN 'WARNING'
           WHEN elevel = 20 THEN 'ERROR'
       END
$$
LANGUAGE SQL PARALLEL SAFE;

GRANT SELECT ON pg_stat_monitor_settings TO PUBLIC;
-- Don't want this to be available to non-superusers.
REVOKE ALL ON FUNCTION pg_stat_monitor_reset() FROM PUBLIC;
