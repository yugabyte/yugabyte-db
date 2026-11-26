CALL TEST_reset();

CREATE TABLE analyze_foo(i int PRIMARY KEY, j text);
INSERT INTO analyze_foo SELECT g, 'value' || (g % 3) FROM generate_series(1, 20) g;
CREATE TEMP TABLE analyze_temp_foo(i int PRIMARY KEY);

ANALYZE analyze_foo;
ANALYZE analyze_temp_foo;  -- temp relations are not replicated

-- Only the permanent relation should have been captured. The entry names the
-- relation so that the target can mark it as needing an ANALYZE of its own; the
-- statistics themselves are not shipped.
SELECT yb_data->>'command_tag' AS command_tag,
       yb_data->>'query' AS query,
       yb_data->'analyze_rels' AS analyze_rels
  FROM yb_xcluster_ddl_replication.ddl_queue
  WHERE yb_data->>'command_tag' = 'ANALYZE'
  ORDER BY ddl_end_time;

SELECT TEST_verify_replicated_ddls();

-- Analyzing a single column, or several relations at once, is captured the same
-- way: one entry per relation whose statistics were refreshed.
CALL TEST_reset();
CREATE TABLE analyze_bar(a int, b int);
ANALYZE analyze_foo(j), analyze_bar;

SELECT yb_data->>'query' AS query
  FROM yb_xcluster_ddl_replication.ddl_queue
  WHERE yb_data->>'command_tag' = 'ANALYZE'
  ORDER BY yb_data->>'query';

-- Quoting is preserved for names that need it.
CALL TEST_reset();
CREATE TABLE "Analyze Quoted"(i int);
ANALYZE "Analyze Quoted";

SELECT yb_data->>'query' AS query,
       yb_data->'analyze_rels' AS analyze_rels
  FROM yb_xcluster_ddl_replication.ddl_queue
  WHERE yb_data->>'command_tag' = 'ANALYZE';

-- In manual replication mode the user is responsible for running ANALYZE on
-- the target themselves, so nothing is captured.
CALL TEST_reset();
SET yb_xcluster_ddl_replication.enable_manual_ddl_replication = 1;
ANALYZE analyze_foo;
SET yb_xcluster_ddl_replication.enable_manual_ddl_replication = 0;
SELECT count(*) FROM yb_xcluster_ddl_replication.ddl_queue;

-- ANALYZE is only captured on the source of the replication.
CALL TEST_reset();
SET yb_xcluster_ddl_replication.TEST_replication_role_override = 'target';
ANALYZE analyze_foo;
SET yb_xcluster_ddl_replication.TEST_replication_role_override = 'source';
SELECT count(*) FROM yb_xcluster_ddl_replication.ddl_queue;

-- Analyzing a partitioned table captures the parent along with every leaf.
CALL TEST_reset();
CREATE TABLE analyze_part(k int, v int) PARTITION BY RANGE (k);
CREATE TABLE analyze_part_1 PARTITION OF analyze_part FOR VALUES FROM (0) TO (10);
CREATE TABLE analyze_part_2 PARTITION OF analyze_part FOR VALUES FROM (10) TO (20);
CALL TEST_reset();
ANALYZE analyze_part;
SELECT yb_data->>'query' AS query
  FROM yb_xcluster_ddl_replication.ddl_queue
  WHERE yb_data->>'command_tag' = 'ANALYZE'
  ORDER BY yb_data->>'query';
SELECT TEST_verify_replicated_ddls();

CALL TEST_reset();
ANALYZE pg_class, information_schema.sql_features;
SELECT count(*) FROM yb_xcluster_ddl_replication.ddl_queue;

-- Unlike a DDL, ANALYZE is permitted in a read-only transaction, but capture
-- has to write to ddl_queue, so it warns and skips instead.
-- TODO(#34537): capture it by writing to ddl_queue even in a read-only
-- transaction.
CALL TEST_reset();
SET default_transaction_read_only = on;
ANALYZE analyze_foo;
SET default_transaction_read_only = off;
SELECT count(*) FROM yb_xcluster_ddl_replication.ddl_queue;

-- Materialized views are sampled like tables and are captured.
CALL TEST_reset();
CREATE MATERIALIZED VIEW analyze_mv AS SELECT i FROM analyze_foo;
CALL TEST_reset();
ANALYZE analyze_mv;
SELECT yb_data->>'query' AS query
  FROM yb_xcluster_ddl_replication.ddl_queue
  WHERE yb_data->>'command_tag' = 'ANALYZE';
