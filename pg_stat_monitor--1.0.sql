/* contrib/pg_stat_monitor/pg_stat_monitor--1.4.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pg_stat_monitor" to load this file. \quit

-- Register functions.
CREATE FUNCTION pg_stat_monitor_reset()
RETURNS void
AS 'MODULE_PATHNAME'
LANGUAGE C PARALLEL SAFE;

CREATE FUNCTION pg_stat_monitor(IN showtext boolean,
    OUT userid oid,
    OUT dbid oid,
    OUT queryid bigint,
    OUT query text,
    OUT calls int8,
    OUT total_time float8,
    OUT min_time float8,
    OUT max_time float8,
    OUT mean_time float8,
    OUT stddev_time float8,
    OUT rows int8,
    OUT shared_blks_hit int8,
    OUT shared_blks_read int8,
    OUT shared_blks_dirtied int8,
    OUT shared_blks_written int8,
    OUT local_blks_hit int8,
    OUT local_blks_read int8,
    OUT local_blks_dirtied int8,
    OUT local_blks_written int8,
    OUT temp_blks_read int8,
    OUT temp_blks_written int8,
    OUT blk_read_time float8,
    OUT blk_write_time float8,
    OUT host int,
    OUT hist_calls text,
    OUT hist_min_time text,
    OUT hist_max_time text,
    OUT hist_mean_time text,
    OUT slow_query text,
    OUT cpu_user_time float8,
    OUT cpu_sys_time  float8
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pg_stat_monitor_1_3'
LANGUAGE C STRICT VOLATILE PARALLEL SAFE;

CREATE FUNCTION pg_stat_agg(
  OUT queryid bigint, 
  OUT id bigint, 
  OUT type bigint, 
  OUT total_calls int,
  OUT first_call_time timestamptz,
  OUT last_call_time timestamptz)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pg_stat_agg'
LANGUAGE C STRICT VOLATILE PARALLEL SAFE;

-- Register a view on the function for ease of use.
CREATE VIEW pg_stat_monitor AS
  SELECT * FROM pg_stat_monitor(true);

GRANT SELECT ON pg_stat_monitor TO PUBLIC;

CREATE VIEW pg_stat_agg_database AS
SELECT
  agg.queryid,
  agg.id AS dbid,
  ss.userid,
  '0.0.0.0'::inet + ss.host AS host,
  agg.total_calls,
  ss.min_time,
  ss.max_time,
  ss.mean_time,
  (string_to_array(hist_calls, ',')) hist_calls,
  (string_to_array(hist_min_time, ',')) hist_min_time,
  (string_to_array(hist_max_time, ',')) hist_max_time,
  (string_to_array(hist_mean_time, ',')) hist_mean_time,
  agg.first_call_time AS first_log_time,
  agg.last_call_time AS last_log_time,
  ss.cpu_user_time,
  ss.cpu_sys_time,
  ss.query,
  ss.slow_query
FROM pg_stat_agg() agg 
INNER JOIN (SELECT DISTINCT queryid, dbid, userid, query, host, min_time, max_time, mean_time, hist_calls, hist_min_time, hist_max_time,hist_mean_time,slow_query,cpu_user_time,cpu_sys_time
FROM pg_stat_monitor) ss 
ON agg.queryid = ss.queryid AND agg.type = 0 AND id = dbid;

CREATE VIEW pg_stat_agg_user AS
SELECT
  agg.queryid,
  agg.id AS dbid,
  ss.userid,
  '0.0.0.0'::inet + ss.host AS host,
  agg.total_calls,
  ss.min_time,
  ss.max_time,
  ss.mean_time,
  (string_to_array(hist_calls, ',')) hist_calls,
  (string_to_array(hist_min_time, ',')) hist_min_time,
  (string_to_array(hist_max_time, ',')) hist_max_time,
  (string_to_array(hist_mean_time, ',')) hist_mean_time,
  agg.first_call_time AS first_log_time,
  agg.last_call_time AS last_log_time,
  ss.cpu_user_time,
  ss.cpu_sys_time,
  ss.query,
  ss.slow_query
FROM pg_stat_agg() agg 
INNER JOIN (SELECT DISTINCT queryid, userid, query, host, min_time, max_time, mean_time, hist_calls, hist_min_time, hist_max_time,hist_mean_time,slow_query,cpu_user_time,cpu_sys_time FROM pg_stat_monitor) ss 
ON agg.queryid = ss.queryid AND agg.type = 1 AND id = userid;

CREATE VIEW pg_stat_agg_host AS
SELECT
  agg.queryid,
  agg.id AS dbid,
  ss.userid,
  '0.0.0.0'::inet + ss.host AS host,
  agg.total_calls,
  ss.min_time,
  ss.max_time,
  ss.mean_time,
  (string_to_array(hist_calls, ',')) hist_calls,
  (string_to_array(hist_min_time, ',')) hist_min_time,
  (string_to_array(hist_max_time, ',')) hist_max_time,
  (string_to_array(hist_mean_time, ',')) hist_mean_time,
  agg.first_call_time AS first_log_time,
  agg.last_call_time AS last_log_time,
  ss.cpu_user_time,
  ss.cpu_sys_time,
  ss.query,
  ss.slow_query 
FROM pg_stat_agg() agg 
INNER JOIN (SELECT DISTINCT queryid, userid, query, host, min_time, max_time, mean_time, hist_calls, hist_min_time, hist_max_time,hist_mean_time,slow_query,cpu_user_time,cpu_sys_time FROM pg_stat_monitor) ss 
ON agg.queryid = ss.queryid AND agg.type = 2 AND id = host;

GRANT SELECT ON pg_stat_agg_database TO PUBLIC;

-- Don't want this to be available to non-superusers.
REVOKE ALL ON FUNCTION pg_stat_monitor_reset() FROM PUBLIC;
