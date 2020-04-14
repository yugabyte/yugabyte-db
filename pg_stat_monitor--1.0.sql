/* contrib/pg_stat_monitor/pg_stat_monitor--1.1.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pg_stat_monitor" to load this file. \quit

-- Register functions.
CREATE FUNCTION pg_stat_monitor_reset()
RETURNS void
AS 'MODULE_PATHNAME'
LANGUAGE C PARALLEL SAFE;

CREATE FUNCTION pg_stat_monitor(IN showtext boolean,
    OUT bucket oid,
    OUT userid oid,
    OUT dbid oid,

    OUT queryid text,
    OUT query text,
    OUT bucket_start_time timestamptz,
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
    OUT client_ip bigint,
    OUT resp_calls text,
    OUT cpu_user_time float8,
    OUT cpu_sys_time  float8,
    OUT tables_names text
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

CREATE FUNCTION pg_stat_agg(
  OUT queryid text, 
  OUT id bigint, 
  OUT type bigint, 
  OUT total_calls int)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pg_stat_agg'
LANGUAGE C STRICT VOLATILE PARALLEL SAFE;

-- Register a view on the function for ease of use.
CREATE VIEW pg_stat_monitor AS SELECT
    bucket,
	bucket_start_time,
    userid,
    dbid,
    m.queryid,
    query,
    calls,
    total_time,
    min_time,
    max_time,
    mean_time,
    stddev_time,
    rows int8,
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
	client_ip as host,
	'0.0.0.0'::inet + client_ip AS client_ip,
	(string_to_array(resp_calls, ',')) resp_calls,
    cpu_user_time,
    cpu_sys_time,
	(string_to_array(tables_names, ',')) tables_names,
	wait_event,
	wait_event_type 
from  pg_stat_monitor(true) m LEFT OUTER JOIN pg_stat_wait_events() w ON w.queryid = m.queryid;


-- Register a view on the function for ease of use.
CREATE VIEW pg_stat_wait_events AS SELECT
    m.queryid,
    query,
	wait_event,
	wait_event_type 
FROM  pg_stat_monitor(true) m, pg_stat_wait_events() w WHERE w.queryid = m.queryid;

GRANT SELECT ON pg_stat_wait_events TO PUBLIC;
GRANT SELECT ON pg_stat_monitor TO PUBLIC;

CREATE VIEW pg_stat_agg_database AS
SELECT
  ss.bucket,
  agg.queryid,
  agg.id AS dbid,
  ss.userid,
  client_ip,
  agg.total_calls,
  ss.min_time,
  ss.max_time,
  ss.mean_time,
  ss.resp_calls,
  ss.cpu_user_time,
  ss.cpu_sys_time,
  ss.query,
  ss.tables_names
FROM pg_stat_agg() agg 
INNER JOIN (SELECT DISTINCT bucket, queryid, dbid, userid, query, client_ip, min_time, max_time, mean_time, resp_calls, tables_names, cpu_user_time,cpu_sys_time
FROM pg_stat_monitor) ss 
ON agg.queryid = ss.queryid AND agg.type = 0 AND id = dbid;

CREATE VIEW pg_stat_agg_user AS
SELECT
  ss.bucket,
  agg.queryid,
  agg.id AS dbid,
  ss.userid,
  client_ip,
  agg.total_calls,
  ss.min_time,
  ss.max_time,
  ss.mean_time,
  ss.resp_calls,
  ss.cpu_user_time,
  ss.cpu_sys_time,
  ss.query,
  ss.tables_names
FROM pg_stat_agg() agg 
INNER JOIN (SELECT DISTINCT bucket, queryid, userid, query, client_ip, min_time, max_time, mean_time, resp_calls, tables_names, cpu_user_time,cpu_sys_time FROM pg_stat_monitor) ss 
ON agg.queryid = ss.queryid AND agg.type = 1 AND id = userid;

CREATE VIEW pg_stat_agg_ip AS
SELECT
  ss.bucket,
  agg.queryid,
  agg.id AS dbid,
  ss.userid,
  ss.client_ip,
  ss.host,
  agg.total_calls,
  ss.min_time,
  ss.max_time,
  ss.mean_time,
  ss.resp_calls,
  ss.cpu_user_time,
  ss.cpu_sys_time,
  ss.query,
  ss.tables_names
FROM pg_stat_agg() agg 
INNER JOIN (SELECT DISTINCT bucket, queryid, userid, query, client_ip, host, min_time, max_time, mean_time, resp_calls, tables_names, cpu_user_time,cpu_sys_time FROM pg_stat_monitor) ss 
ON agg.queryid = ss.queryid AND agg.type = 2 AND id = host;



GRANT SELECT ON pg_stat_agg_user TO PUBLIC;
GRANT SELECT ON pg_stat_agg_ip TO PUBLIC;
GRANT SELECT ON pg_stat_agg_database TO PUBLIC;

-- Don't want this to be available to non-superusers.
REVOKE ALL ON FUNCTION pg_stat_monitor_reset() FROM PUBLIC;
