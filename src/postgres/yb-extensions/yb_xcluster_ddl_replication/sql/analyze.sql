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
