--
-- Tests for global views using file_fdw as the underlying data source.
-- Each tserver has a different CSV file placed in its data directory
-- by the test framework, simulating per-node data like pg_stat_statements.
--

-- Install required extensions
CREATE EXTENSION file_fdw;
CREATE EXTENSION postgres_fdw;

-- Create enum type for the status column
CREATE TYPE metric_status AS ENUM ('active', 'warning', 'critical');

-- Create file_fdw server for reading local CSV files
CREATE SERVER file_server FOREIGN DATA WRAPPER file_fdw;

-- Create file_fdw foreign table reading the tserver-local CSV.
-- The relative path resolves against each tserver's data directory,
-- so each tserver reads its own gv_test_data.csv with unique data.
CREATE FOREIGN TABLE local_node_metrics (
    node_id        int,
    metric_name    varchar(30),
    metric_value   real,
    event_count    int,
    total_bytes    bigint,
    ratio          double precision,
    is_healthy     boolean,
    tag            char(5),
    status         metric_status,
    created_at     timestamp,
    updated_at     timestamptz
) SERVER file_server
OPTIONS (format 'csv', filename 'gv_test_data.csv');

-- Verify local data is readable (should show only this node's data)
SELECT count(*) > 0 AS has_local_data FROM local_node_metrics;

-- Create global views server
CREATE SERVER IF NOT EXISTS gv_server FOREIGN DATA WRAPPER postgres_fdw
    OPTIONS (server_type 'federatedYugabyteDB');

-- Create global view foreign table
CREATE FOREIGN TABLE "gv$node_metrics" (
    node_id        int,
    metric_name    varchar(30),
    metric_value   real,
    event_count    int,
    total_bytes    bigint,
    ratio          double precision,
    is_healthy     boolean,
    tag            char(5),
    status         metric_status,
    created_at     timestamp,
    updated_at     timestamptz
) SERVER gv_server
OPTIONS (schema_name 'public', table_name 'local_node_metrics');

--
-- Basic SELECT: verify data from all 3 nodes (11 rows total)
--
EXPLAIN (VERBOSE, COSTS OFF)
SELECT * FROM "gv$node_metrics" ORDER BY node_id, metric_name;
SELECT * FROM "gv$node_metrics" ORDER BY node_id, metric_name;

--
-- WHERE clause tests: one per type
--

-- int4
EXPLAIN (VERBOSE, COSTS OFF)
SELECT node_id, metric_name FROM "gv$node_metrics"
WHERE node_id IN (1, 3)
ORDER BY node_id, metric_name;
SELECT node_id, metric_name FROM "gv$node_metrics"
WHERE node_id IN (1, 3)
ORDER BY node_id, metric_name;

-- float4 (real)
EXPLAIN (VERBOSE, COSTS OFF)
SELECT node_id, metric_name, metric_value FROM "gv$node_metrics"
WHERE metric_value > 70
ORDER BY node_id, metric_name;
SELECT node_id, metric_name, metric_value FROM "gv$node_metrics"
WHERE metric_value > 70
ORDER BY node_id, metric_name;

-- varchar (variable-length string)
EXPLAIN (VERBOSE, COSTS OFF)
SELECT node_id, metric_name, status FROM "gv$node_metrics"
WHERE status = 'active'
ORDER BY node_id, metric_name;
SELECT node_id, metric_name, status FROM "gv$node_metrics"
WHERE status = 'active'
ORDER BY node_id, metric_name;

-- boolean
EXPLAIN (VERBOSE, COSTS OFF)
SELECT node_id, metric_name, is_healthy FROM "gv$node_metrics"
WHERE is_healthy = false
ORDER BY node_id, metric_name;
SELECT node_id, metric_name, is_healthy FROM "gv$node_metrics"
WHERE is_healthy = false
ORDER BY node_id, metric_name;

-- int8 (bigint)
EXPLAIN (VERBOSE, COSTS OFF)
SELECT node_id, metric_name, total_bytes FROM "gv$node_metrics"
WHERE total_bytes > 8000000000
ORDER BY node_id, metric_name;
SELECT node_id, metric_name, total_bytes FROM "gv$node_metrics"
WHERE total_bytes > 8000000000
ORDER BY node_id, metric_name;

-- float8 (double precision)
EXPLAIN (VERBOSE, COSTS OFF)
SELECT node_id, metric_name, ratio FROM "gv$node_metrics"
WHERE ratio > 0.7
ORDER BY node_id, metric_name;
SELECT node_id, metric_name, ratio FROM "gv$node_metrics"
WHERE ratio > 0.7
ORDER BY node_id, metric_name;

-- char(5) (fixed-length string)
EXPLAIN (VERBOSE, COSTS OFF)
SELECT node_id, metric_name, tag FROM "gv$node_metrics"
WHERE tag LIKE 'CPU%'
ORDER BY node_id;
SELECT node_id, metric_name, tag FROM "gv$node_metrics"
WHERE tag LIKE 'CPU%'
ORDER BY node_id;

-- enum
EXPLAIN (VERBOSE, COSTS OFF)
SELECT node_id, metric_name, status FROM "gv$node_metrics"
WHERE status = 'critical'::metric_status
ORDER BY node_id, metric_name;
SELECT node_id, metric_name, status FROM "gv$node_metrics"
WHERE status = 'critical'::metric_status
ORDER BY node_id, metric_name;

-- timestamp (without TZ)
EXPLAIN (VERBOSE, COSTS OFF)
SELECT node_id, metric_name, created_at FROM "gv$node_metrics"
WHERE created_at >= '2025-02-01'::timestamp
ORDER BY node_id, metric_name;
SELECT node_id, metric_name, created_at FROM "gv$node_metrics"
WHERE created_at >= '2025-02-01'::timestamp
ORDER BY node_id, metric_name;

-- timestamptz (with TZ)
EXPLAIN (VERBOSE, COSTS OFF)
SELECT node_id, metric_name, updated_at FROM "gv$node_metrics"
WHERE updated_at < '2025-02-01 00:00:00+00'::timestamptz
ORDER BY node_id, updated_at;
SELECT node_id, metric_name, updated_at FROM "gv$node_metrics"
WHERE updated_at < '2025-02-01 00:00:00+00'::timestamptz
ORDER BY node_id, updated_at;

--
-- Aggregate queries
--
EXPLAIN (VERBOSE, COSTS OFF)
SELECT COUNT(*) FROM "gv$node_metrics";
SELECT COUNT(*) FROM "gv$node_metrics";

EXPLAIN (VERBOSE, COSTS OFF)
SELECT metric_name,
       COUNT(*) AS node_count,
       SUM(event_count) AS total_events,
       SUM(total_bytes) AS total_bytes_sum
FROM "gv$node_metrics"
GROUP BY metric_name
ORDER BY metric_name;
SELECT metric_name,
       COUNT(*) AS node_count,
       SUM(event_count) AS total_events,
       SUM(total_bytes) AS total_bytes_sum
FROM "gv$node_metrics"
GROUP BY metric_name
ORDER BY metric_name;

-- Aggregate on boolean
EXPLAIN (VERBOSE, COSTS OFF)
SELECT is_healthy, COUNT(*) AS cnt
FROM "gv$node_metrics"
GROUP BY is_healthy
ORDER BY is_healthy;
SELECT is_healthy, COUNT(*) AS cnt
FROM "gv$node_metrics"
GROUP BY is_healthy
ORDER BY is_healthy;

-- Aggregate on timestamp
EXPLAIN (VERBOSE, COSTS OFF)
SELECT MIN(created_at) AS earliest, MAX(created_at) AS latest
FROM "gv$node_metrics";
SELECT MIN(created_at) AS earliest, MAX(created_at) AS latest
FROM "gv$node_metrics";

--
-- HAVING clause
--
EXPLAIN (VERBOSE, COSTS OFF)
SELECT status, COUNT(*) AS cnt
FROM "gv$node_metrics"
GROUP BY status
HAVING COUNT(*) >= 2
ORDER BY status;
SELECT status, COUNT(*) AS cnt
FROM "gv$node_metrics"
GROUP BY status
HAVING COUNT(*) >= 2
ORDER BY status;

--
-- DISTINCT
--
EXPLAIN (VERBOSE, COSTS OFF)
SELECT DISTINCT metric_name FROM "gv$node_metrics" ORDER BY metric_name;
SELECT DISTINCT metric_name FROM "gv$node_metrics" ORDER BY metric_name;

EXPLAIN (VERBOSE, COSTS OFF)
SELECT DISTINCT status FROM "gv$node_metrics" ORDER BY status;
SELECT DISTINCT status FROM "gv$node_metrics" ORDER BY status;

EXPLAIN (VERBOSE, COSTS OFF)
SELECT DISTINCT is_healthy FROM "gv$node_metrics" ORDER BY is_healthy;
SELECT DISTINCT is_healthy FROM "gv$node_metrics" ORDER BY is_healthy;

--
-- LIMIT
--
EXPLAIN (VERBOSE, COSTS OFF)
SELECT node_id, metric_name, metric_value FROM "gv$node_metrics"
ORDER BY metric_value DESC LIMIT 3;
SELECT node_id, metric_name, metric_value FROM "gv$node_metrics"
ORDER BY metric_value DESC LIMIT 3;

EXPLAIN (VERBOSE, COSTS OFF)
SELECT node_id, metric_name, event_count FROM "gv$node_metrics"
ORDER BY event_count ASC LIMIT 5;
SELECT node_id, metric_name, event_count FROM "gv$node_metrics"
ORDER BY event_count ASC LIMIT 5;

--
-- Subqueries
--
-- Single parameter: scalar subquery result bound as $1
EXPLAIN (VERBOSE, COSTS OFF)
SELECT node_id, metric_name, metric_value
FROM "gv$node_metrics"
WHERE metric_value > (SELECT AVG(metric_value) FROM "gv$node_metrics")
ORDER BY node_id, metric_name;
SELECT node_id, metric_name, metric_value
FROM "gv$node_metrics"
WHERE metric_value > (SELECT AVG(metric_value) FROM "gv$node_metrics")
ORDER BY node_id, metric_name;

-- Two parameters: two scalar subquery results bound as $1 and $2
EXPLAIN (VERBOSE, COSTS OFF)
SELECT node_id, metric_name, metric_value
FROM "gv$node_metrics"
WHERE metric_value > (SELECT AVG(metric_value) FROM "gv$node_metrics")
  AND event_count > (SELECT MIN(event_count) FROM "gv$node_metrics")
ORDER BY node_id, metric_name;
SELECT node_id, metric_name, metric_value
FROM "gv$node_metrics"
WHERE metric_value > (SELECT AVG(metric_value) FROM "gv$node_metrics")
  AND event_count > (SELECT MIN(event_count) FROM "gv$node_metrics")
ORDER BY node_id, metric_name;

-- NULL parameter: subquery over empty result set returns NULL
EXPLAIN (VERBOSE, COSTS OFF)
SELECT node_id, metric_name, metric_value
FROM "gv$node_metrics"
WHERE metric_value > (SELECT AVG(metric_value) FROM "gv$node_metrics"
                      WHERE node_id = 999)
ORDER BY node_id, metric_name;
SELECT node_id, metric_name, metric_value
FROM "gv$node_metrics"
WHERE metric_value > (SELECT AVG(metric_value) FROM "gv$node_metrics"
                      WHERE node_id = 999)
ORDER BY node_id, metric_name;

-- Mixed NULL and non-NULL parameters: $1 is a real value, $2 is NULL
EXPLAIN (VERBOSE, COSTS OFF)
SELECT node_id, metric_name, metric_value
FROM "gv$node_metrics"
WHERE metric_value > (SELECT AVG(metric_value) FROM "gv$node_metrics")
  AND event_count > (SELECT MIN(event_count) FROM "gv$node_metrics"
                     WHERE node_id = 999)
ORDER BY node_id, metric_name;
SELECT node_id, metric_name, metric_value
FROM "gv$node_metrics"
WHERE metric_value > (SELECT AVG(metric_value) FROM "gv$node_metrics")
  AND event_count > (SELECT MIN(event_count) FROM "gv$node_metrics"
                     WHERE node_id = 999)
ORDER BY node_id, metric_name;

EXPLAIN (VERBOSE, COSTS OFF)
SELECT node_id, metric_name
FROM "gv$node_metrics"
WHERE status IN (SELECT DISTINCT status FROM "gv$node_metrics" WHERE status != 'active')
ORDER BY node_id, metric_name;
SELECT node_id, metric_name
FROM "gv$node_metrics"
WHERE status IN (SELECT DISTINCT status FROM "gv$node_metrics" WHERE status != 'active')
ORDER BY node_id, metric_name;

-- Empty string subquery parameter
EXPLAIN (VERBOSE, COSTS OFF)
SELECT node_id, metric_name, tag
FROM "gv$node_metrics"
WHERE metric_name = (SELECT ''::varchar)
ORDER BY node_id, metric_name;
SELECT node_id, metric_name, tag
FROM "gv$node_metrics"
WHERE metric_name = (SELECT ''::varchar)
ORDER BY node_id, metric_name;

-- NULL subquery parameter
EXPLAIN (VERBOSE, COSTS OFF)
SELECT node_id, metric_name, tag
FROM "gv$node_metrics"
WHERE tag IS NOT DISTINCT FROM (SELECT NULL::char(5))
ORDER BY node_id, metric_name;
SELECT node_id, metric_name, tag
FROM "gv$node_metrics"
WHERE tag IS NOT DISTINCT FROM (SELECT NULL::char(5))
ORDER BY node_id, metric_name;

--
-- JOIN with an inline VALUES list
--
EXPLAIN (VERBOSE, COSTS OFF)
SELECT ni.node_name, gv.metric_name, gv.event_count
FROM "gv$node_metrics" gv
JOIN (VALUES (1, 'alpha'), (2, 'beta'), (3, 'gamma'))
     AS ni(node_id, node_name) ON gv.node_id = ni.node_id
ORDER BY ni.node_name, gv.metric_name;
SELECT ni.node_name, gv.metric_name, gv.event_count
FROM "gv$node_metrics" gv
JOIN (VALUES (1, 'alpha'), (2, 'beta'), (3, 'gamma'))
     AS ni(node_id, node_name) ON gv.node_id = ni.node_id
ORDER BY ni.node_name, gv.metric_name;

--
-- UNION of filtered results
--
EXPLAIN (VERBOSE, COSTS OFF)
SELECT node_id, metric_name, status FROM "gv$node_metrics" WHERE status = 'critical'
UNION ALL
SELECT node_id, metric_name, status FROM "gv$node_metrics" WHERE status = 'warning'
ORDER BY node_id, metric_name;
SELECT node_id, metric_name, status FROM "gv$node_metrics" WHERE status = 'critical'
UNION ALL
SELECT node_id, metric_name, status FROM "gv$node_metrics" WHERE status = 'warning'
ORDER BY node_id, metric_name;

--
-- ORDER BY with expressions
--
EXPLAIN (VERBOSE, COSTS OFF)
SELECT node_id, metric_name,
       (metric_value * event_count)::int AS weighted_value
FROM "gv$node_metrics"
ORDER BY weighted_value DESC
LIMIT 5;
SELECT node_id, metric_name,
       (metric_value * event_count)::int AS weighted_value
FROM "gv$node_metrics"
ORDER BY weighted_value DESC
LIMIT 5;

--
-- Type-specific queries
--

-- Bigint arithmetic
EXPLAIN (VERBOSE, COSTS OFF)
SELECT node_id, metric_name,
       total_bytes,
       (total_bytes / 1000000000) AS total_gb
FROM "gv$node_metrics"
ORDER BY total_bytes DESC
LIMIT 3;
SELECT node_id, metric_name,
       total_bytes,
       (total_bytes / 1000000000) AS total_gb
FROM "gv$node_metrics"
ORDER BY total_bytes DESC
LIMIT 3;

-- Float8 rounding
EXPLAIN (VERBOSE, COSTS OFF)
SELECT node_id, metric_name,
       ratio,
       ROUND(ratio::numeric, 2) AS ratio_rounded
FROM "gv$node_metrics"
ORDER BY node_id, metric_name;
SELECT node_id, metric_name,
       ratio,
       ROUND(ratio::numeric, 2) AS ratio_rounded
FROM "gv$node_metrics"
ORDER BY node_id, metric_name;

-- Boolean logic
EXPLAIN (VERBOSE, COSTS OFF)
SELECT node_id, metric_name,
       is_healthy,
       NOT is_healthy AS is_unhealthy
FROM "gv$node_metrics"
ORDER BY node_id, metric_name;
SELECT node_id, metric_name,
       is_healthy,
       NOT is_healthy AS is_unhealthy
FROM "gv$node_metrics"
ORDER BY node_id, metric_name;

-- Char padding
EXPLAIN (VERBOSE, COSTS OFF)
SELECT node_id, tag,
       LENGTH(tag) AS tag_len
FROM "gv$node_metrics"
ORDER BY tag;
SELECT node_id, tag,
       LENGTH(tag) AS tag_len
FROM "gv$node_metrics"
ORDER BY tag;

-- Timestamp extract
EXPLAIN (VERBOSE, COSTS OFF)
SELECT node_id, metric_name,
       EXTRACT(MONTH FROM created_at) AS month,
       EXTRACT(YEAR FROM created_at) AS year
FROM "gv$node_metrics"
ORDER BY created_at
LIMIT 3;
SELECT node_id, metric_name,
       EXTRACT(MONTH FROM created_at) AS month,
       EXTRACT(YEAR FROM created_at) AS year
FROM "gv$node_metrics"
ORDER BY created_at
LIMIT 3;

-- Timestamptz arithmetic
EXPLAIN (VERBOSE, COSTS OFF)
SELECT node_id, metric_name,
       updated_at,
       updated_at + interval '1 hour' AS updated_at_plus_1h
FROM "gv$node_metrics"
ORDER BY updated_at
LIMIT 3;
SELECT node_id, metric_name,
       updated_at,
       updated_at + interval '1 hour' AS updated_at_plus_1h
FROM "gv$node_metrics"
ORDER BY updated_at
LIMIT 3;

--
-- Function pushdown behavior
--

-- Stable function (NOW()) in predicate: not pushed down because
-- contain_mutable_functions() rejects non-immutable expressions.
EXPLAIN (VERBOSE, COSTS OFF)
SELECT node_id, metric_name, updated_at FROM "gv$node_metrics"
WHERE updated_at < NOW()
ORDER BY node_id, metric_name;
SELECT node_id, metric_name, updated_at FROM "gv$node_metrics"
WHERE updated_at < NOW()
ORDER BY node_id, metric_name;

-- Immutable built-in function (ABS) in predicate: pushed down because
-- the function is built-in and immutable.
EXPLAIN (VERBOSE, COSTS OFF)
SELECT node_id, metric_name, event_count FROM "gv$node_metrics"
WHERE ABS(event_count) > 100
ORDER BY node_id, metric_name;
SELECT node_id, metric_name, event_count FROM "gv$node_metrics"
WHERE ABS(event_count) > 100
ORDER BY node_id, metric_name;

-- User-defined function in predicate: not pushed down because
-- postgres_fdw only considers built-in functions shippable.
CREATE FUNCTION is_high_value(val real) RETURNS boolean
    LANGUAGE plpgsql IMMUTABLE AS $$
BEGIN
    RETURN val > 70::real;
END;
$$;

EXPLAIN (VERBOSE, COSTS OFF)
SELECT node_id, metric_name, metric_value FROM "gv$node_metrics"
WHERE is_high_value(metric_value)
ORDER BY node_id, metric_name;
SELECT node_id, metric_name, metric_value FROM "gv$node_metrics"
WHERE is_high_value(metric_value)
ORDER BY node_id, metric_name;

--
-- Global views for yb_active_session_history and pg_stat_statements.
-- These tests only verify EXPLAIN plans (no data verification).
--

-- TODO(#30591): Remove the setup phase once the default views are created.
CREATE VIEW yb_active_session_history_with_tserver_uuid AS
    SELECT yb_get_local_tserver_uuid() AS tserver_uuid, *
    FROM yb_active_session_history;

CREATE VIEW pg_stat_statements_with_tserver_uuid AS
    SELECT yb_get_local_tserver_uuid() AS tserver_uuid, *
    FROM pg_stat_statements(true);

CREATE FOREIGN TABLE IF NOT EXISTS "gv$yb_active_session_history" (
    tserver_uuid UUID,
    sample_time TIMESTAMPTZ,
    root_request_id UUID,
    rpc_request_id BIGINT,
    wait_event_component TEXT,
    wait_event_class TEXT,
    wait_event TEXT,
    top_level_node_id UUID,
    query_id BIGINT,
    pid INT,
    client_node_ip TEXT,
    wait_event_aux TEXT,
    sample_weight REAL,
    wait_event_type TEXT,
    ysql_dbid OID,
    wait_event_code BIGINT,
    pss_mem_bytes BIGINT,
    ysql_userid OID
)
SERVER gv_server
OPTIONS (schema_name 'public', table_name 'yb_active_session_history_with_tserver_uuid');

CREATE FOREIGN TABLE IF NOT EXISTS "gv$pg_stat_statements" (
    tserver_uuid UUID,
    userid OID,
    dbid OID,
    toplevel BOOL,
    queryid BIGINT,
    query TEXT,
    plans INT8,
    total_plan_time FLOAT8,
    min_plan_time FLOAT8,
    max_plan_time FLOAT8,
    mean_plan_time FLOAT8,
    stddev_plan_time FLOAT8,
    calls INT8,
    total_exec_time FLOAT8,
    min_exec_time FLOAT8,
    max_exec_time FLOAT8,
    mean_exec_time FLOAT8,
    stddev_exec_time FLOAT8,
    rows INT8,
    shared_blks_hit INT8,
    shared_blks_read INT8,
    shared_blks_dirtied INT8,
    shared_blks_written INT8,
    local_blks_hit INT8,
    local_blks_read INT8,
    local_blks_dirtied INT8,
    local_blks_written INT8,
    temp_blks_read INT8,
    temp_blks_written INT8,
    blk_read_time FLOAT8,
    blk_write_time FLOAT8,
    temp_blk_read_time FLOAT8,
    temp_blk_write_time FLOAT8,
    wal_records INT8,
    wal_fpi INT8,
    wal_bytes NUMERIC,
    jit_functions INT8,
    jit_generation_time FLOAT8,
    jit_inlining_count INT8,
    jit_inlining_time FLOAT8,
    jit_optimization_count INT8,
    jit_optimization_time FLOAT8,
    jit_emission_count INT8,
    jit_emission_time FLOAT8,
    yb_latency_histogram JSONB,
    docdb_read_rpcs INT8,
    docdb_write_rpcs INT8,
    catalog_wait_time FLOAT8,
    docdb_read_operations INT8,
    docdb_write_operations INT8,
    docdb_rows_scanned INT8,
    docdb_rows_returned INT8,
    docdb_wait_time FLOAT8,
    conflict_retries INT8,
    read_restart_retries INT8,
    total_retries INT8,
    docdb_obsolete_rows_scanned INT8,
    docdb_seeks INT8,
    docdb_nexts INT8,
    docdb_prevs INT8,
    docdb_read_time FLOAT8,
    docdb_write_time FLOAT8
)
SERVER gv_server
OPTIONS (schema_name 'public', table_name 'pg_stat_statements_with_tserver_uuid');

-- ASH with current_timestamp (stable, not pushed down)
EXPLAIN (VERBOSE, COSTS OFF)
SELECT
    query_id,
    wait_event_component,
    wait_event,
    wait_event_type,
    COUNT(*)
FROM
    "gv$yb_active_session_history"
WHERE
    sample_time >= current_timestamp - interval '20 minutes'
GROUP BY
    query_id,
    wait_event_component,
    wait_event,
    wait_event_type
ORDER BY
    query_id,
    wait_event_component,
    wait_event_type
LIMIT 10;

-- ASH with literal timestamp (immutable, pushed down)
EXPLAIN (VERBOSE, COSTS OFF)
SELECT
    query_id,
    wait_event_component,
    wait_event,
    wait_event_type,
    COUNT(*)
FROM
    "gv$yb_active_session_history"
WHERE
    sample_time >= '2026-04-03 13:28:25.640388+05:30'
GROUP BY
    query_id,
    wait_event_component,
    wait_event,
    wait_event_type
ORDER BY
    query_id,
    wait_event_component,
    wait_event_type
LIMIT 10;

-- ASH JOIN pg_stat_statements with current_timestamp
EXPLAIN (VERBOSE, COSTS OFF)
SELECT
    SUBSTRING(query, 1, 50) AS query,
    wait_event_component,
    wait_event,
    wait_event_type,
    COUNT(*)
FROM
    "gv$yb_active_session_history"
JOIN
    "gv$pg_stat_statements"
ON
    query_id = queryid
WHERE
    sample_time >= current_timestamp - interval '20 minutes'
GROUP BY
    query,
    wait_event_component,
    wait_event,
    wait_event_type
ORDER BY
    query,
    wait_event_component,
    wait_event_type
LIMIT 10;

-- ASH JOIN pg_stat_statements with literal timestamp
EXPLAIN (VERBOSE, COSTS OFF)
SELECT
    SUBSTRING(query, 1, 50) AS query,
    wait_event_component,
    wait_event,
    wait_event_type,
    COUNT(*)
FROM
    "gv$yb_active_session_history"
JOIN
    "gv$pg_stat_statements"
ON
    query_id = queryid
WHERE
    sample_time >= '2026-04-03 13:28:25.640388+05:30'
GROUP BY
    query,
    wait_event_component,
    wait_event,
    wait_event_type
ORDER BY
    query,
    wait_event_component,
    wait_event_type
LIMIT 10;

-- ASH JOIN pg_stat_statements with ORDER BY only on ASH
EXPLAIN (VERBOSE, COSTS OFF)
SELECT
    SUBSTRING(query, 1, 50) AS query,
    wait_event_component,
    wait_event,
    wait_event_type,
    COUNT(*)
FROM
    "gv$yb_active_session_history"
JOIN
    "gv$pg_stat_statements"
ON
    query_id = queryid
WHERE
    sample_time >= '2026-04-03 13:28:25.640388+05:30'
GROUP BY
    query,
    wait_event_component,
    wait_event,
    wait_event_type
ORDER BY
    wait_event_component,
    wait_event_type
LIMIT 10;

-- ASH JOIN pg_stat_statements with ORDER BY only on pg_stat_statements
EXPLAIN (VERBOSE, COSTS OFF)
SELECT
    SUBSTRING(query, 1, 50) AS query,
    wait_event_component,
    wait_event,
    wait_event_type,
    COUNT(*)
FROM
    "gv$yb_active_session_history"
JOIN
    "gv$pg_stat_statements"
ON
    query_id = queryid
WHERE
    sample_time >= '2026-04-03 13:28:25.640388+05:30'
GROUP BY
    query,
    wait_event_component,
    wait_event,
    wait_event_type
ORDER BY
    query
LIMIT 10;

EXPLAIN (VERBOSE, COSTS OFF)
SELECT query_id, queryid
FROM "gv$yb_active_session_history"
JOIN "gv$pg_stat_statements" ON query_id = queryid;

--
-- Rescan correctness: Nested Loop is forced by disabling competing
-- join strategies, so postgresReScanForeignScan is exercised.
--
CREATE TABLE gv_driver_nodes (node_id int, label text);
INSERT INTO gv_driver_nodes VALUES (1, 'first'), (3, 'third');

SET enable_hashjoin = off;
SET enable_mergejoin = off;
SET enable_material = off;

EXPLAIN (VERBOSE, COSTS OFF)
SELECT d.label, gv.metric_name, gv.event_count
FROM gv_driver_nodes d, "gv$node_metrics" gv
WHERE gv.node_id = d.node_id
ORDER BY d.label, gv.metric_name;

SELECT d.label, gv.metric_name, gv.event_count
FROM gv_driver_nodes d, "gv$node_metrics" gv
WHERE gv.node_id = d.node_id
ORDER BY d.label, gv.metric_name;

RESET enable_material;
RESET enable_hashjoin;
RESET enable_mergejoin;
DROP TABLE gv_driver_nodes;

--
-- Non-decomposable aggregates must NOT be pushed down as partial
-- aggregates to individual tservers. Verify the aggregate runs locally.
--
EXPLAIN (VERBOSE, COSTS OFF)
SELECT string_agg(metric_name, ', ' ORDER BY metric_name)
FROM "gv$node_metrics"
WHERE node_id = 1;

EXPLAIN (VERBOSE, COSTS OFF)
SELECT array_agg(DISTINCT metric_name ORDER BY metric_name)
FROM "gv$node_metrics"
WHERE node_id = 1;

EXPLAIN (VERBOSE, COSTS OFF)
SELECT COUNT(DISTINCT metric_name)
FROM "gv$node_metrics";

--
-- Cleanup
--
DROP FOREIGN TABLE "gv$pg_stat_statements";
DROP FOREIGN TABLE "gv$yb_active_session_history";
DROP VIEW pg_stat_statements_with_tserver_uuid;
DROP VIEW yb_active_session_history_with_tserver_uuid;
DROP FOREIGN TABLE "gv$node_metrics";
DROP FOREIGN TABLE local_node_metrics;
DROP SERVER gv_server CASCADE;
DROP SERVER file_server CASCADE;
DROP EXTENSION postgres_fdw;
DROP EXTENSION file_fdw;
DROP TYPE metric_status;
DROP FUNCTION is_high_value(real);
