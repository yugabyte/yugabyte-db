/* contrib/pg_stat_monitor/pg_stat_monitor--2.0.sql */

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

-- Some generic utility function used internally.

CREATE FUNCTION get_cmd_type (cmd_type INTEGER) RETURNS TEXT AS
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

CREATE FUNCTION histogram(_bucket int, _quryid int8)
RETURNS SETOF RECORD AS $$
DECLARE
 rec record;
BEGIN
    FOR rec IN
        WITH stat AS (select queryid, bucket, unnest(range()) AS range, 
            unnest(resp_calls)::int freq FROM pg_stat_monitor) select range, 
            freq, repeat('■', (freq::float / max(freq) over() * 30)::int) AS bar 
            FROM stat WHERE queryid = _quryid and bucket = _bucket
    LOOP
        RETURN next rec;
    END loop;
END
$$ language plpgsql;

-- pg_stat_monitor internal function, must not call outside from this file.
CREATE FUNCTION pg_stat_monitor_internal(
    IN showtext             boolean,
    OUT bucket              int8,   -- 0
    OUT userid              oid,
    OUT username            text,
    OUT dbid                oid,
    OUT datname             text,
    OUT client_ip           int8,

    OUT queryid             int8,  -- 4
    OUT planid              int8,
    OUT query               text,
    OUT query_plan          text,
    OUT pgsm_query_id       int8,
    OUT top_queryid         int8,
	OUT top_query           text,
	OUT application_name	text,

	OUT relations			text, -- 11
	OUT cmd_type			int,
	OUT elevel              int,
    OUT sqlcode             TEXT,
    OUT message             text,
    OUT bucket_start_time   timestamptz,

	OUT calls         		int8,  -- 16

    OUT total_exec_time     float8,
    OUT min_exec_time       float8,
    OUT max_exec_time       float8,
    OUT mean_exec_time      float8,
    OUT stddev_exec_time    float8,

    OUT rows                int8,

	OUT plans          	 	int8,  -- 23
   
    OUT total_plan_time     float8,
    OUT min_plan_time       float8,
    OUT max_plan_time       float8,
    OUT mean_plan_time      float8,
    OUT stddev_plan_time    float8,

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

    OUT temp_blk_read_time  float8,
    OUT temp_blk_write_time float8,

    OUT resp_calls          text, -- 41
    OUT cpu_user_time       float8,
    OUT cpu_sys_time        float8,
    OUT wal_records         int8,
    OUT wal_fpi             int8,
    OUT wal_bytes           numeric,
    OUT comments            TEXT,

    OUT jit_functions           int8,
    OUT jit_generation_time     float8,
    OUT jit_inlining_count      int8,
    OUT jit_inlining_time       float8,
    OUT jit_optimization_count  int8,
    OUT jit_optimization_time   float8,
    OUT jit_emission_count      int8,
    OUT jit_emission_time       float8,

    OUT toplevel            BOOLEAN,
    OUT bucket_done         BOOLEAN
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pg_stat_monitor_2_0'
LANGUAGE C STRICT VOLATILE PARALLEL SAFE;

-- Register a view on the function for ease of use.
CREATE FUNCTION pgsm_create_11_view() RETURNS INT AS
$$
BEGIN
CREATE VIEW pg_stat_monitor AS SELECT
    bucket,
	bucket_start_time AS bucket_start_time,
    userid,
    username,
    dbid,
    datname,
	'0.0.0.0'::inet + client_ip AS client_ip,
	pgsm_query_id,
    queryid,
    top_queryid,
    query,
	comments,
	planid,
	query_plan,
    top_query,
	application_name,
	string_to_array(relations, ',') AS relations,
	cmd_type,
	get_cmd_type(cmd_type) AS cmd_type_text,
	elevel,
	sqlcode,
	message,
    calls,
	total_exec_time AS total_time,
	min_exec_time AS min_time,
	max_exec_time AS max_time,
	mean_exec_time AS mean_time,
	stddev_exec_time AS stddev_time,
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
	bucket_done
FROM pg_stat_monitor_internal(TRUE)
ORDER BY bucket_start_time;
RETURN 0;
END;
$$ LANGUAGE plpgsql;


CREATE FUNCTION pgsm_create_13_view() RETURNS INT AS
$$
BEGIN
CREATE VIEW pg_stat_monitor AS SELECT
    bucket,
	bucket_start_time AS bucket_start_time,
    userid,
    username,
    dbid,
    datname,
	'0.0.0.0'::inet + client_ip AS client_ip,
	pgsm_query_id,
    queryid,
    toplevel,
    top_queryid,
    query,
	comments,
	planid,
	query_plan,
    top_query,
	application_name,
	string_to_array(relations, ',') AS relations,
	cmd_type,
	get_cmd_type(cmd_type) AS cmd_type_text,
	elevel,
	sqlcode,
	message,
    calls,
	total_exec_time,
	min_exec_time,
	max_exec_time,
	mean_exec_time,
	stddev_exec_time,
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
    wal_records,
    wal_fpi,
    wal_bytes,
	bucket_done,
    -- PostgreSQL-13 Specific Coulumns
	plans,
	total_plan_time,
	min_plan_time,
	max_plan_time,
	mean_plan_time,
    stddev_plan_time
FROM pg_stat_monitor_internal(TRUE)
ORDER BY bucket_start_time;
RETURN 0;
END;
$$ LANGUAGE plpgsql;

CREATE FUNCTION pgsm_create_14_view() RETURNS INT AS
$$
BEGIN
CREATE VIEW pg_stat_monitor AS SELECT
    bucket,
	bucket_start_time AS bucket_start_time,
    userid,
    username,
    dbid,
    datname,
	'0.0.0.0'::inet + client_ip AS client_ip,
	pgsm_query_id,
    queryid,
    toplevel,
    top_queryid,
    query,
	comments,
	planid,
	query_plan,
    top_query,
	application_name,
	string_to_array(relations, ',') AS relations,
	cmd_type,
	get_cmd_type(cmd_type) AS cmd_type_text,
	elevel,
	sqlcode,
	message,
    calls,
	total_exec_time,
	min_exec_time,
	max_exec_time,
	mean_exec_time,
	stddev_exec_time,
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
    wal_records,
    wal_fpi,
    wal_bytes,
	bucket_done,

    plans,
	total_plan_time,
	min_plan_time,
	max_plan_time,
	mean_plan_time,
    stddev_plan_time
FROM pg_stat_monitor_internal(TRUE)
ORDER BY bucket_start_time;
RETURN 0;
END;
$$ LANGUAGE plpgsql;

CREATE FUNCTION pgsm_create_15_view() RETURNS INT AS
$$
BEGIN
CREATE VIEW pg_stat_monitor AS SELECT
    bucket,
	bucket_start_time AS bucket_start_time,
    userid,
    username,
    dbid,
    datname,
	'0.0.0.0'::inet + client_ip AS client_ip,
	pgsm_query_id,
    queryid,
    toplevel,
    top_queryid,
    query,
	comments,
	planid,
	query_plan,
    top_query,
	application_name,
	string_to_array(relations, ',') AS relations,
	cmd_type,
	get_cmd_type(cmd_type) AS cmd_type_text,
	elevel,
	sqlcode,
	message,
    calls,
	total_exec_time,
	min_exec_time,
	max_exec_time,
	mean_exec_time,
	stddev_exec_time,
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
    temp_blk_read_time,
    temp_blk_write_time,

	(string_to_array(resp_calls, ',')) resp_calls,
	cpu_user_time,
	cpu_sys_time,
    wal_records,
    wal_fpi,
    wal_bytes,
	bucket_done,

    plans,
	total_plan_time,
	min_plan_time,
	max_plan_time,
	mean_plan_time,
    stddev_plan_time,

    jit_functions,
    jit_generation_time,
    jit_inlining_count,
    jit_inlining_time,
    jit_optimization_count,
    jit_optimization_time,
    jit_emission_count,
    jit_emission_time

FROM pg_stat_monitor_internal(TRUE)
ORDER BY bucket_start_time;
RETURN 0;
END;
$$ LANGUAGE plpgsql;

CREATE FUNCTION pgsm_create_view() RETURNS INT AS
$$
    DECLARE ver integer;
    BEGIN
        SELECT current_setting('server_version_num') INTO ver;
    IF (ver >= 150000) THEN
        return pgsm_create_15_view();
    END IF;
    IF (ver >= 140000) THEN
        return pgsm_create_14_view();
    END IF;
    IF (ver >= 130000) THEN
        return pgsm_create_13_view();
    END IF;
    IF (ver >= 110000) THEN
        return pgsm_create_11_view();
    END IF;
    RETURN 0;
    END;
$$ LANGUAGE plpgsql;

SELECT pgsm_create_view();
REVOKE ALL ON FUNCTION pgsm_create_view FROM PUBLIC;
REVOKE ALL ON FUNCTION pgsm_create_11_view FROM PUBLIC;
REVOKE ALL ON FUNCTION pgsm_create_13_view FROM PUBLIC;
REVOKE ALL ON FUNCTION pgsm_create_14_view FROM PUBLIC;
REVOKE ALL ON FUNCTION pgsm_create_15_view FROM PUBLIC;

GRANT EXECUTE ON FUNCTION range TO PUBLIC;
GRANT EXECUTE ON FUNCTION decode_error_level TO PUBLIC;
GRANT EXECUTE ON FUNCTION get_histogram_timings TO PUBLIC;
GRANT EXECUTE ON FUNCTION get_cmd_type TO PUBLIC;
GRANT EXECUTE ON FUNCTION pg_stat_monitor_internal TO PUBLIC;

GRANT SELECT ON pg_stat_monitor TO PUBLIC;

-- Reset is only available to super user
REVOKE ALL ON FUNCTION pg_stat_monitor_reset FROM PUBLIC;
