CALL TEST_reset();

SET yb_xcluster_ddl_replication.replication_role = DISABLED;
CREATE SCHEMA create_index;
SET search_path TO create_index;

-- Test temp table and index.
SET yb_xcluster_ddl_replication.replication_role = SOURCE;
CREATE TEMP TABLE temp_foo(i int PRIMARY KEY, a int);
CREATE INDEX foo_idx_temp on temp_foo(a);

SELECT yb_data FROM yb_xcluster_ddl_replication.ddl_queue ORDER BY start_time;
SET yb_xcluster_ddl_replication.replication_role = BIDIRECTIONAL;

-- Create base table.
CREATE TABLE foo(i int PRIMARY KEY, a int, b text, c int);

-- Create indexes.
CREATE INDEX foo_idx_simple ON foo(a);

CREATE UNIQUE INDEX foo_idx_unique ON foo(b);

CREATE INDEX foo_idx_filtered ON foo(c ASC, a) WHERE a > c;

CREATE INDEX foo_idx_include ON foo(lower(b)) INCLUDE (a) SPLIT INTO 2 TABLETS;

SELECT yb_data FROM yb_xcluster_ddl_replication.ddl_queue ORDER BY start_time;
SELECT * FROM yb_xcluster_ddl_replication.replicated_ddls ORDER BY start_time;
