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

CREATE FUNCTION get_histogram_timings()
RETURNS text
AS 'MODULE_PATHNAME'
LANGUAGE C PARALLEL SAFE;

CREATE FUNCTION range()
RETURNS text[] AS $$
SELECT string_to_array(get_histogram_timings(), ',');
$$ LANGUAGE SQL;

CREATE FUNCTION pg_stat_monitor_internal(IN showtext boolean,
    OUT bucket              int8,   -- 0
    OUT userid              oid,
    OUT dbid                oid,
    OUT client_ip           int8,

    OUT queryid             text,  -- 4
    OUT planid              text,
    OUT query               text,
    OUT query_plan          text,
    OUT state_code 			int8,
    OUT top_queryid         text,
	OUT application_name	text,

	OUT relations			text, -- 11
	OUT cmd_type			int,
	OUT elevel              int,
    OUT sqlcode             TEXT,
    OUT message             text,
    OUT bucket_start_time   text,

	OUT calls         		int8,  -- 16
    OUT total_time          float8,
    OUT min_time            float8,
    OUT max_time            float8,
    OUT mean_time           float8,
    OUT stddev_time         float8,
    OUT rows_retrieved       int8,

	OUT plans_calls    	 	int8,  -- 23
    OUT plan_total_time     float8,
    OUT plan_min_time       float8,
    OUT plan_max_time       float8,
    OUT plan_mean_time      float8,
    OUT plan_stddev_time    float8,

	OUT shared_blks_hit     int8, -- 29
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
    OUT resp_calls          text, -- 41
    OUT cpu_user_time       float8,
    OUT cpu_sys_time        float8,
    OUT wal_records 		int8,
    OUT wal_fpi 			int8,
    OUT wal_bytes 			numeric
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pg_stat_monitor'
LANGUAGE C STRICT VOLATILE PARALLEL SAFE;

CREATE OR REPLACE FUNCTION get_state(state_code int8) RETURNS TEXT AS
$$
SELECT
	CASE
		WHEN state_code = 0 THEN 'PARSING'
		WHEN state_code = 1 THEN 'PLANNING'
		WHEN state_code = 2 THEN 'ACTIVE'
		WHEN state_code = 3 THEN 'FINISHED'
		WHEN state_code = 4 THEN 'FINISHED WITH ERROR'
	END
$$
LANGUAGE SQL PARALLEL SAFE;

CREATE or REPLACE FUNCTION get_cmd_type (cmd_type INTEGER) RETURNS TEXT AS
$$
SELECT
	CASE
		WHEN cmd_type = 0 THEN ''
		WHEN cmd_type = 1 THEN 'SELECT'
		WHEN cmd_type = 2 THEN 'UPDATE'
		WHEN cmd_type = 3 THEN 'INSERT'
		WHEN cmd_type = 4 THEN 'DELETE'
		WHEN cmd_type = 5 THEN 'UTILITY'
		WHEN cmd_type = 6 THEN 'NOTHING'
	END
$$
LANGUAGE SQL PARALLEL SAFE;

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
    top_queryid,
    query,
	planid,
	query_plan,
	(SELECT query from pg_stat_monitor_internal(true) s where s.queryid = p.top_queryid) AS top_query,
	application_name,
	string_to_array(relations, ',') AS relations,
	cmd_type,
	get_cmd_type(cmd_type) AS cmd_type_text,
	elevel,
	sqlcode,
	message,
    calls,
	round( CAST(total_time as numeric), 4)::float8 as total_time,
	round( CAST(min_time as numeric), 4)::float8 as min_time,
	round( CAST(max_time as numeric), 4)::float8 as max_time,
	round( CAST(mean_time as numeric), 4)::float8 as mean_time,
	round( CAST(stddev_time as numeric), 4)::float8 as stddev_time,
	rows_retrieved,
	plans_calls,
	round( CAST(plan_total_time as numeric), 4)::float8 as plan_total_time,
	round( CAST(plan_min_time as numeric), 4)::float8 as plan_min_time,
	round( CAST(plan_max_time as numeric), 4)::float8 as plan_max_time,
	round( CAST(plan_mean_time as numeric), 4)::float8 as plan_mean_time,

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
	round(cpu_sys_time::numeric, 4) as cpu_sys_time,
    wal_records,
    wal_fpi,
    wal_bytes,
	state_code,
	get_state(state_code) as state
FROM pg_stat_monitor_internal(TRUE) p, pg_database d  WHERE dbid = oid
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

CREATE OR REPLACE FUNCTION histogram(_bucket int, _quryid text)
RETURNS SETOF RECORD AS $$
DECLARE
 rec record;
BEGIN
for rec in
        with stat as (select queryid, bucket, unnest(range()) as range, unnest(resp_calls)::int freq from pg_stat_monitor) select range, freq, repeat('■', (freq::float / max(freq) over() * 30)::int) as bar from stat where queryid = _quryid and bucket = _bucket
loop
return next rec;
end loop;
END
$$ language plpgsql;

GRANT SELECT ON pg_stat_monitor TO PUBLIC;
GRANT SELECT ON pg_stat_monitor_settings TO PUBLIC;
-- Don't want this to be available to non-superusers.
REVOKE ALL ON FUNCTION pg_stat_monitor_reset() FROM PUBLIC;
