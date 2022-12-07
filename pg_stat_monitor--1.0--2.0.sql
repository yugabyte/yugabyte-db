/* contrib/pg_stat_monitor/pg_stat_monitor--1.0--2.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "ALTER EXTENSION pg_stat_monitor" to load this file. \quit

DROP FUNCTION pg_stat_monitor_internal CASCADE;
DROP FUNCTION pgsm_create_11_view CASCADE;
DROP FUNCTION pgsm_create_13_view CASCADE;
DROP FUNCTION pgsm_create_14_view CASCADE;
DROP FUNCTION pgsm_create_view CASCADE;

-- pg_stat_monitor internal function, must not call outside from this file.
CREATE FUNCTION pg_stat_monitor_internal(
    IN showtext             boolean,
    OUT bucket              int8,   -- 0
    OUT userid              oid,
    OUT dbid                oid,
    OUT client_ip           int8,

    OUT queryid             text,  -- 4
    OUT planid              text,
    OUT query               text,
    OUT query_plan          text,
    OUT top_queryid         text,
    OUT top_query           text,
	OUT application_name	text,

	OUT relations			text, -- 11
	OUT cmd_type			int,
	OUT elevel              int,
    OUT sqlcode             TEXT,
    OUT message             text,
    OUT bucket_start_time   timestamp,

	OUT calls         		int8,  -- 16

    OUT total_exec_time     float8,
    OUT min_exec_time       float8,
    OUT max_exec_time       float8,
    OUT mean_exec_time      float8,
    OUT stddev_exec_time    float8,

    OUT rows_retrieved      int8,

	OUT plans_calls    	 	int8,  -- 23
   
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

    OUT toplevel                BOOLEAN,
    OUT bucket_done             BOOLEAN
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
    userid::regrole,
    datname,
	'0.0.0.0'::inet + client_ip AS client_ip,
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
	rows_retrieved,
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
FROM pg_stat_monitor_internal(TRUE) p, pg_database d  WHERE dbid = oid
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
    userid::regrole,
    datname,
	'0.0.0.0'::inet + client_ip AS client_ip,
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
	rows_retrieved,
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
    plans_calls,
	total_plan_time,
	min_plan_time,
	max_plan_time,
	mean_plan_time,
    stddev_plan_time
FROM pg_stat_monitor_internal(TRUE) p, pg_database d  WHERE dbid = oid
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
    userid::regrole,
    datname,
	'0.0.0.0'::inet + client_ip AS client_ip,
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
	rows_retrieved,
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

    plans_calls,
	total_plan_time,
	min_plan_time,
	max_plan_time,
	mean_plan_time,
    stddev_plan_time
FROM pg_stat_monitor_internal(TRUE) p, pg_database d  WHERE dbid = oid
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
    userid::regrole,
    datname,
	'0.0.0.0'::inet + client_ip AS client_ip,
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
	rows_retrieved,
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

    plans_calls,
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

FROM pg_stat_monitor_internal(TRUE) p, pg_database d  WHERE dbid = oid
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
REVOKE ALL ON FUNCTION range FROM PUBLIC;
REVOKE ALL ON FUNCTION get_cmd_type FROM PUBLIC;
REVOKE ALL ON FUNCTION pg_stat_monitor_settings FROM PUBLIC;
REVOKE ALL ON FUNCTION decode_error_level FROM PUBLIC;
REVOKE ALL ON FUNCTION pg_stat_monitor_internal FROM PUBLIC;

GRANT SELECT ON pg_stat_monitor TO PUBLIC;

