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
    OUT bucket_start_time   timestamptz,

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
    OUT cpu_sys_time        float8,
    OUT tables_names        text
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pg_stat_monitor'
LANGUAGE C STRICT VOLATILE PARALLEL SAFE;

CREATE FUNCTION pg_stat_wait_events(
  OUT queryid text, 
  OUT pid bigint, 
  OUT wait_event text, 
  OUT wait_event_type text 
  )
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pg_stat_wait_events'
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
	bucket_start_time,
    userid,
    dbid,
	'0.0.0.0'::inet + client_ip AS client_ip,
    queryid,
    query,
	plans,
	round( CAST(plan_total_time as numeric), 2)::float8 as plan_total_time,
	round( CAST(plan_min_time as numeric), 2)::float8 as plan_min_timei,
	round( CAST(plan_max_time as numeric), 2)::float8 as plan_max_time,
	round( CAST(plan_mean_time as numeric), 2)::float8 as plan_mean_time,
	round( CAST(plan_stddev_time as numeric), 2)::float8 as plan_stddev_time,
    calls,
	round( CAST(total_time as numeric), 2)::float8 as total_time,
	round( CAST(min_time as numeric), 2)::float8 as min_time,
	round( CAST(max_time as numeric), 2)::float8 as max_time,
	round( CAST(mean_time as numeric), 2)::float8 as mean_time,
	round( CAST(stddev_time as numeric), 2)::float8 as stddev_time,
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
    cpu_user_time,
    cpu_sys_time,
	(string_to_array(tables_names, ',')) tables_names
FROM pg_stat_monitor(TRUE);


-- Register a view on the function for ease of use.
CREATE VIEW pg_stat_wait_events AS SELECT
    m.queryid,
    query,
	wait_event,
	wait_event_type 
FROM  pg_stat_monitor(true) m, pg_stat_wait_events() w WHERE w.queryid = m.queryid;

/*CREATE VIEW pg_stat_monitor_db AS
SELECT
  *
FROM pg_stat_monitor GROUP BY dbid;

CREATE VIEW pg_stat_monitor_user AS
SELECT
  *
FROM pg_stat_monitor GROUP BY userid;

CREATE VIEW pg_stat_monitor_ip AS
SELECT
  *
FROM pg_stat_monitor GROUP BY client_ip;

GRANT SELECT ON pg_stat_agg_user TO PUBLIC;
GRANT SELECT ON pg_stat_agg_ip TO PUBLIC;
GRANT SELECT ON pg_stat_agg_database TO PUBLIC;
GRANT SELECT ON pg_stat_monitor_settings TO PUBLIC;
*/
-- Don't want this to be available to non-superusers.
REVOKE ALL ON FUNCTION pg_stat_monitor_reset() FROM PUBLIC;
