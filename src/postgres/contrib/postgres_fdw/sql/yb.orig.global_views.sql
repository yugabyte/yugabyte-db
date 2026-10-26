--
-- Tests for global views using file_fdw as the underlying data source.
-- Each tserver has a different CSV file placed in its data directory
-- by the test framework, simulating per-node data like pg_stat_statements.
--

-- Most queries below run through the parameterized-query helper: :P runs
-- each query first under EXPLAIN and then for its result.
\getenv abs_srcdir PG_ABS_SRCDIR
\set yb_commands_dir :abs_srcdir '/../../src/test/regress/yb_commands'
\set filename :yb_commands_dir '/parameterized_query.sql'
\i :filename
\set explain 'EXPLAIN (VERBOSE, COSTS OFF)'
\set P1 ':explain'
\set P2

-- Install required extensions
CREATE EXTENSION file_fdw;

-- Create enum type for the status column
CREATE TYPE metric_status AS ENUM ('active', 'warning', 'critical');

-- Create file_fdw server for reading local CSV files
CREATE SERVER file_server FOREIGN DATA WRAPPER file_fdw;

-- Create file_fdw foreign table reading the tserver-local CSV.
-- The relative path resolves against each tserver's data directory,
-- so each tserver reads its own gv_test_data.csv with unique data.
CREATE FOREIGN TABLE local_node_metrics_csv (
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

-- Wrap the file_fdw foreign table in a view that exposes the local tserver
-- UUID, since federated foreign tables require a "server_uuid" UUID column.
CREATE VIEW local_node_metrics AS
    SELECT yb_get_local_tserver_uuid() AS server_uuid, *
    FROM local_node_metrics_csv;

-- Remote global-view queries execute as the non-superuser yb_global_views_user role
-- (a member of pg_read_all_stats), so grant it read access to the partial view.
GRANT SELECT ON local_node_metrics TO pg_read_all_stats;

-- Verify local data is readable (should show only this node's data)
SELECT count(*) > 0 AS has_local_data FROM local_node_metrics;

-- Create global views server
CREATE SERVER IF NOT EXISTS gv_server FOREIGN DATA WRAPPER postgres_fdw
    OPTIONS (server_type 'federatedYugabyteDB');

-- Create global view foreign table
CREATE FOREIGN TABLE "gv$node_metrics" (
    server_uuid   uuid,
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
-- Basic SELECT: verify data from all 3 nodes (11 rows total).
-- server_uuid is excluded because its value varies per tserver and run.
--
SELECT $$
:P
SELECT node_id, metric_name, metric_value, event_count, total_bytes,
       ratio, is_healthy, tag, status, created_at, updated_at
FROM "gv$node_metrics" ORDER BY node_id, metric_name;
$$ AS query \gset
\i :run_query

--
-- WHERE clause tests: one per type
--

-- int4
SELECT $$
:P
SELECT node_id, metric_name FROM "gv$node_metrics"
WHERE node_id IN (1, 3)
ORDER BY node_id, metric_name;
$$ AS query \gset
\i :run_query

-- float4 (real)
SELECT $$
:P
SELECT node_id, metric_name, metric_value FROM "gv$node_metrics"
WHERE metric_value > 70
ORDER BY node_id, metric_name;
$$ AS query \gset
\i :run_query

-- varchar (variable-length string)
SELECT $$
:P
SELECT node_id, metric_name, status FROM "gv$node_metrics"
WHERE status = 'active'
ORDER BY node_id, metric_name;
$$ AS query \gset
\i :run_query

-- boolean
SELECT $$
:P
SELECT node_id, metric_name, is_healthy FROM "gv$node_metrics"
WHERE is_healthy = false
ORDER BY node_id, metric_name;
$$ AS query \gset
\i :run_query

-- int8 (bigint)
SELECT $$
:P
SELECT node_id, metric_name, total_bytes FROM "gv$node_metrics"
WHERE total_bytes > 8000000000
ORDER BY node_id, metric_name;
$$ AS query \gset
\i :run_query

-- float8 (double precision)
SELECT $$
:P
SELECT node_id, metric_name, ratio FROM "gv$node_metrics"
WHERE ratio > 0.7
ORDER BY node_id, metric_name;
$$ AS query \gset
\i :run_query

-- char(5) (fixed-length string)
SELECT $$
:P
SELECT node_id, metric_name, tag FROM "gv$node_metrics"
WHERE tag LIKE 'CPU%'
ORDER BY node_id;
$$ AS query \gset
\i :run_query

-- enum
SELECT $$
:P
SELECT node_id, metric_name, status FROM "gv$node_metrics"
WHERE status = 'critical'::metric_status
ORDER BY node_id, metric_name;
$$ AS query \gset
\i :run_query

-- timestamp (without TZ)
SELECT $$
:P
SELECT node_id, metric_name, created_at FROM "gv$node_metrics"
WHERE created_at >= '2025-02-01'::timestamp
ORDER BY node_id, metric_name;
$$ AS query \gset
\i :run_query

-- timestamptz (with TZ)
SELECT $$
:P
SELECT node_id, metric_name, updated_at FROM "gv$node_metrics"
WHERE updated_at < '2025-02-01 00:00:00+00'::timestamptz
ORDER BY node_id, updated_at;
$$ AS query \gset
\i :run_query

--
-- Aggregate queries
--
SELECT $$
:P
SELECT COUNT(*) FROM "gv$node_metrics";
$$ AS query \gset
\i :run_query

SELECT $$
:P
SELECT metric_name,
       COUNT(*) AS node_count,
       SUM(event_count) AS total_events,
       SUM(total_bytes) AS total_bytes_sum
FROM "gv$node_metrics"
GROUP BY metric_name
ORDER BY metric_name;
$$ AS query \gset
\i :run_query

-- Aggregate on boolean
SELECT $$
:P
SELECT is_healthy, COUNT(*) AS cnt
FROM "gv$node_metrics"
GROUP BY is_healthy
ORDER BY is_healthy;
$$ AS query \gset
\i :run_query

-- Aggregate on timestamp
SELECT $$
:P
SELECT MIN(created_at) AS earliest, MAX(created_at) AS latest
FROM "gv$node_metrics";
$$ AS query \gset
\i :run_query

--
-- Aggregate pushdown by server_uuid
-- server_uuid is not selected (its value is random per run); the queries group
-- by server_uuid and order by the aggregates, whose per-tserver values are
-- fixed by the data.

-- GROUP BY server_uuid: count(*) pushed to each tserver.
SELECT $$
:P
SELECT COUNT(*) AS cnt
FROM "gv$node_metrics"
GROUP BY server_uuid
ORDER BY cnt;
$$ AS query \gset
\i :run_query

-- One group per tserver; counts sum to 11 rows.
SELECT COUNT(*) AS num_server_groups, SUM(cnt) AS total_rows
FROM (SELECT server_uuid, COUNT(*) AS cnt
      FROM "gv$node_metrics" GROUP BY server_uuid) s;

-- Without GROUP BY on server_uuid count(*) is not pushed down
SELECT $$
:P
SELECT COUNT(*) FROM "gv$node_metrics";
$$ AS query \gset
\i :run_query

-- GROUP BY a non-partition-key column: only a partial aggregate is pushed
-- below the Append. Each node_id lives on a single tserver, but the planner
-- cannot know that, so it must not pick a full per-tserver aggregate.
SELECT $$
:P
SELECT node_id, COUNT(*) FROM "gv$node_metrics"
GROUP BY node_id
ORDER BY node_id;
$$ AS query \gset
\i :run_query

-- Superset of the partition key: still fully pushed down.
SELECT $$
:P
SELECT metric_name, COUNT(*) AS cnt, SUM(event_count) AS events
FROM "gv$node_metrics"
GROUP BY server_uuid, metric_name
ORDER BY metric_name, cnt, events;
$$ AS query \gset
\i :run_query

-- Aggregates pushed to each tserver, by type:
-- - Q1: numeric (sum/avg/min/max/count(col))
-- - Q2: statistical (stddev/variance)
-- - Q3: boolean (bool_and/bool_or)
-- - Q4: temporal and text min/max
\set Q1 'SUM(event_count), AVG(metric_value), MIN(total_bytes), MAX(ratio), COUNT(node_id)'
\set Q2 'STDDEV(metric_value), VARIANCE(event_count)'
\set Q3 'BOOL_AND(is_healthy), BOOL_OR(is_healthy)'
\set Q4 'MIN(created_at), MAX(updated_at), MIN(metric_name), MAX(tag)'
SELECT $$
:P
SELECT :Q
FROM "gv$node_metrics"
GROUP BY server_uuid
ORDER BY :Q;
$$ AS query \gset
\i :run_query
\unset Q3
\unset Q4

-- enable_partitionwise_aggregate = off: the GROUP BY server_uuid aggregate is
-- no longer pushed down. Rows are pulled back from each tserver and aggregated
-- locally (plain HashAggregate over the Append); the result stays correct.
SET enable_partitionwise_aggregate = off;
SELECT $$
:P
SELECT COUNT(*) AS num_server_groups, SUM(cnt) AS total_rows
FROM (SELECT server_uuid, COUNT(*) AS cnt
      FROM "gv$node_metrics" GROUP BY server_uuid) s;
$$ AS query \gset
\i :run_query
RESET enable_partitionwise_aggregate;

--
-- HAVING clause
--
SELECT $$
:P
SELECT status, COUNT(*) AS cnt
FROM "gv$node_metrics"
GROUP BY status
HAVING COUNT(*) >= 2
ORDER BY status;
$$ AS query \gset
\i :run_query

--
-- DISTINCT
--
\set Q1 metric_name
\set Q2 status
\set Q3 is_healthy
SELECT $$
:P
SELECT DISTINCT :Q FROM "gv$node_metrics" ORDER BY :Q;
$$ AS query \gset
\i :run_query
\unset Q3

--
-- LIMIT
--
SELECT $$
:P
SELECT node_id, metric_name, metric_value FROM "gv$node_metrics"
ORDER BY metric_value DESC LIMIT 3;
$$ AS query \gset
\i :run_query

SELECT $$
:P
SELECT node_id, metric_name, event_count FROM "gv$node_metrics"
ORDER BY event_count ASC LIMIT 5;
$$ AS query \gset
\i :run_query

--
-- Subqueries
--
-- Scalar subquery results bound as parameters:
-- - Q1: one parameter ($1)
-- - Q2: two parameters ($1 and $2)
-- - Q3: a NULL parameter (subquery over an empty result set)
-- - Q4: mixed NULL and non-NULL parameters ($1 is a real value, $2 is NULL)
\set avg '(SELECT AVG(metric_value) FROM "gv$node_metrics")'
\set avg_empty '(SELECT AVG(metric_value) FROM "gv$node_metrics" WHERE node_id = 999)'
\set min '(SELECT MIN(event_count) FROM "gv$node_metrics")'
\set min_empty '(SELECT MIN(event_count) FROM "gv$node_metrics" WHERE node_id = 999)'
\set Q1 'metric_value > :avg'
\set Q2 'metric_value > :avg AND event_count > :min'
\set Q3 'metric_value > :avg_empty'
\set Q4 'metric_value > :avg AND event_count > :min_empty'
SELECT $$
:P
SELECT node_id, metric_name, metric_value
FROM "gv$node_metrics"
WHERE :Q
ORDER BY node_id, metric_name;
$$ AS query \gset
\i :run_query
\unset Q3
\unset Q4

SELECT $$
:P
SELECT node_id, metric_name
FROM "gv$node_metrics"
WHERE status IN (SELECT DISTINCT status FROM "gv$node_metrics" WHERE status != 'active')
ORDER BY node_id, metric_name;
$$ AS query \gset
\i :run_query

-- Empty-string and NULL subquery parameters
\set Q1 'metric_name = (SELECT ''''::varchar)'
\set Q2 'tag IS NOT DISTINCT FROM (SELECT NULL::char(5))'
SELECT $$
:P
SELECT node_id, metric_name, tag
FROM "gv$node_metrics"
WHERE :Q
ORDER BY node_id, metric_name;
$$ AS query \gset
\i :run_query

--
-- JOIN with an inline VALUES list
--
SELECT $$
:P
SELECT ni.node_name, gv.metric_name, gv.event_count
FROM "gv$node_metrics" gv
JOIN (VALUES (1, 'alpha'), (2, 'beta'), (3, 'gamma'))
     AS ni(node_id, node_name) ON gv.node_id = ni.node_id
ORDER BY ni.node_name, gv.metric_name;
$$ AS query \gset
\i :run_query

--
-- UNION of filtered results
--
SELECT $$
:P
SELECT node_id, metric_name, status FROM "gv$node_metrics" WHERE status = 'critical'
UNION ALL
SELECT node_id, metric_name, status FROM "gv$node_metrics" WHERE status = 'warning'
ORDER BY node_id, metric_name;
$$ AS query \gset
\i :run_query

--
-- ORDER BY with expressions
--
SELECT $$
:P
SELECT node_id, metric_name,
       (metric_value * event_count)::int AS weighted_value
FROM "gv$node_metrics"
ORDER BY weighted_value DESC
LIMIT 5;
$$ AS query \gset
\i :run_query

--
-- Type-specific queries
--

-- Bigint arithmetic
SELECT $$
:P
SELECT node_id, metric_name,
       total_bytes,
       (total_bytes / 1000000000) AS total_gb
FROM "gv$node_metrics"
ORDER BY total_bytes DESC
LIMIT 3;
$$ AS query \gset
\i :run_query

-- Float8 rounding
SELECT $$
:P
SELECT node_id, metric_name,
       ratio,
       ROUND(ratio::numeric, 2) AS ratio_rounded
FROM "gv$node_metrics"
ORDER BY node_id, metric_name;
$$ AS query \gset
\i :run_query

-- Boolean logic
SELECT $$
:P
SELECT node_id, metric_name,
       is_healthy,
       NOT is_healthy AS is_unhealthy
FROM "gv$node_metrics"
ORDER BY node_id, metric_name;
$$ AS query \gset
\i :run_query

-- Char padding
SELECT $$
:P
SELECT node_id, tag,
       LENGTH(tag) AS tag_len
FROM "gv$node_metrics"
ORDER BY tag;
$$ AS query \gset
\i :run_query

-- Timestamp extract
SELECT $$
:P
SELECT node_id, metric_name,
       EXTRACT(MONTH FROM created_at) AS month,
       EXTRACT(YEAR FROM created_at) AS year
FROM "gv$node_metrics"
ORDER BY created_at
LIMIT 3;
$$ AS query \gset
\i :run_query

-- Timestamptz arithmetic
SELECT $$
:P
SELECT node_id, metric_name,
       updated_at,
       updated_at + interval '1 hour' AS updated_at_plus_1h
FROM "gv$node_metrics"
ORDER BY updated_at
LIMIT 3;
$$ AS query \gset
\i :run_query

--
-- Function pushdown behavior
--

-- Stable function (NOW()) in predicate: not pushed down because
-- contain_mutable_functions() rejects non-immutable expressions.
SELECT $$
:P
SELECT node_id, metric_name, updated_at FROM "gv$node_metrics"
WHERE updated_at < NOW()
ORDER BY node_id, metric_name;
$$ AS query \gset
\i :run_query

-- Immutable built-in function (ABS) in predicate: pushed down because
-- the function is built-in and immutable.
SELECT $$
:P
SELECT node_id, metric_name, event_count FROM "gv$node_metrics"
WHERE ABS(event_count) > 100
ORDER BY node_id, metric_name;
$$ AS query \gset
\i :run_query

-- User-defined function in predicate: not pushed down because
-- postgres_fdw only considers built-in functions shippable.
CREATE FUNCTION is_high_value(val real) RETURNS boolean
    LANGUAGE plpgsql IMMUTABLE AS $$
BEGIN
    RETURN val > 70::real;
END;
$$;

SELECT $$
:P
SELECT node_id, metric_name, metric_value FROM "gv$node_metrics"
WHERE is_high_value(metric_value)
ORDER BY node_id, metric_name;
$$ AS query \gset
\i :run_query

--
-- Global views for yb_active_session_history and pg_stat_statements.
-- These tests only verify EXPLAIN plans (no data verification).
--

-- TODO(#30591): Remove the setup phase once the default views are created.
CREATE VIEW yb_active_session_history_with_server_uuid AS
    SELECT yb_get_local_tserver_uuid() AS server_uuid, *
    FROM yb_active_session_history;

CREATE VIEW pg_stat_statements_with_server_uuid AS
    SELECT yb_get_local_tserver_uuid() AS server_uuid, *
    FROM pg_stat_statements(true);

CREATE FOREIGN TABLE IF NOT EXISTS "gv$yb_active_session_history" (
    server_uuid UUID,
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
OPTIONS (schema_name 'public', table_name 'yb_active_session_history_with_server_uuid');

CREATE FOREIGN TABLE IF NOT EXISTS "gv$pg_stat_statements" (
    server_uuid UUID,
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
OPTIONS (schema_name 'public', table_name 'pg_stat_statements_with_server_uuid');

-- ASH filtered by current_timestamp (stable, not pushed down) and by a
-- literal timestamp (immutable, pushed down).
\set Q1 'current_timestamp - interval ''20 minutes'''
\set Q2 '''2026-04-03 13:28:25.640388+05:30'''
SELECT $$
:explain
SELECT
    query_id,
    wait_event_component,
    wait_event,
    wait_event_type,
    COUNT(*)
FROM
    "gv$yb_active_session_history"
WHERE
    sample_time >= :Q
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
$$ AS query \gset
\i :run_query

-- ASH JOIN pg_stat_statements ordered by columns of both tables, of only
-- ASH, and of only pg_stat_statements, each with the same two filters
\set Q1 'query, wait_event_component, wait_event_type'
\set Q2 'wait_event_component, wait_event_type'
\set Q3 query
\set R1 'current_timestamp - interval ''20 minutes'''
\set R2 '''2026-04-03 13:28:25.640388+05:30'''
SELECT $$
:explain
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
    sample_time >= :R
GROUP BY
    query,
    wait_event_component,
    wait_event,
    wait_event_type
ORDER BY
    :Q
LIMIT 10;
$$ AS query \gset
\i :run_query
\unset Q3

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

SELECT $$
:P
SELECT d.label, gv.metric_name, gv.event_count
FROM gv_driver_nodes d, "gv$node_metrics" gv
WHERE gv.node_id = d.node_id
ORDER BY d.label, gv.metric_name;
$$ AS query \gset
\i :run_query

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
DROP VIEW pg_stat_statements_with_server_uuid;
DROP VIEW yb_active_session_history_with_server_uuid;
DROP FOREIGN TABLE "gv$node_metrics";
DROP VIEW local_node_metrics;
DROP FOREIGN TABLE local_node_metrics_csv;
DROP SERVER gv_server CASCADE;
DROP SERVER file_server CASCADE;
DROP EXTENSION file_fdw;
DROP TYPE metric_status;
DROP FUNCTION is_high_value(real);
