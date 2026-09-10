CALL TEST_reset();
SELECT yb_xcluster_ddl_replication.get_replication_role();

-- Verify that temporary objects are not captured.
CREATE TEMP TABLE temp_foo(i int PRIMARY KEY);
DROP TABLE temp_foo;

SELECT yb_data FROM TEST_filtered_ddl_queue() ORDER BY ddl_end_time;

-- Verify that regular tables are captured.
CREATE TABLE foo(i int PRIMARY KEY);
INSERT INTO foo(i) VALUES (1), (2), (3);

-- Check with manual replication flags enabled, ddl string is captured with flag.
SET yb_xcluster_ddl_replication.enable_manual_ddl_replication = 1;
CREATE TABLE manual_foo(i int PRIMARY KEY);
SET yb_xcluster_ddl_replication.enable_manual_ddl_replication = 0;

-- Verify that extra info is captured.
CREATE TABLE extra_foo(i int PRIMARY KEY) WITH (COLOCATION = false) SPLIT INTO 1 TABLETS;

-- Verify that info for unique constraint indexes are also captured.
CREATE TABLE unique_foo(i int PRIMARY KEY, u text UNIQUE);

SELECT yb_data FROM TEST_filtered_ddl_queue() ORDER BY ddl_end_time;
SELECT yb_data FROM yb_xcluster_ddl_replication.replicated_ddls ORDER BY ddl_end_time;

-- Test tables partitioned by their primary key or a column.
CREATE TABLE foo_partitioned_by_pkey(id int, PRIMARY KEY (id)) PARTITION BY RANGE (id);
CREATE TABLE foo_partitioned_by_col(id int) PARTITION BY RANGE (id);
CREATE TABLE partition1 PARTITION OF foo_partitioned_by_col FOR VALUES FROM (1) TO (10);
CREATE TABLE partition2 PARTITION OF foo_partitioned_by_col FOR VALUES FROM (10) TO (20);
INSERT INTO foo_partitioned_by_col(id) VALUES (1), (2), (3), (10), (11), (12);

-- Test for relations that trigger nonconcurrent backfills.
CREATE INDEX NONCONCURRENTLY nonconcurrent_foo on foo(i);
CREATE INDEX NONCONCURRENTLY on foo(i);  -- test without a name
ALTER TABLE foo ADD CONSTRAINT constraint_foo UNIQUE (i);
CREATE UNIQUE INDEX partitioned_index ON foo_partitioned_by_col(id);

-- Tests for ALTER TABLE ... RENAME CONSTRAINT.
CREATE TABLE rc_parent(id int PRIMARY KEY);
CREATE TABLE rc_child(id int PRIMARY KEY, parent_id int REFERENCES rc_parent(id));
ALTER TABLE rc_child RENAME CONSTRAINT rc_child_parent_id_fkey TO rc_fkey_renamed;
-- Constraint renames on temporary tables are not replicated.
CREATE TEMP TABLE rc_temp(val int CONSTRAINT rc_temp_check CHECK (val > 0));
ALTER TABLE rc_temp RENAME CONSTRAINT rc_temp_check TO rc_temp_check_renamed;
DROP TABLE rc_temp;

-- Now test dropping these tables.
DROP TABLE foo;

-- Check with manual replication flags enabled, ddl string is captured with flag.
SET yb_xcluster_ddl_replication.enable_manual_ddl_replication = 1;
DROP TABLE manual_foo;
SET yb_xcluster_ddl_replication.enable_manual_ddl_replication = 0;

DROP TABLE extra_foo;
DROP TABLE unique_foo;
DROP TABLE foo_partitioned_by_pkey;
DROP TABLE foo_partitioned_by_col;
DROP TABLE rc_child;
DROP TABLE rc_parent;

SELECT yb_data FROM TEST_filtered_ddl_queue() ORDER BY ddl_end_time;
SELECT yb_data FROM yb_xcluster_ddl_replication.replicated_ddls ORDER BY ddl_end_time;
