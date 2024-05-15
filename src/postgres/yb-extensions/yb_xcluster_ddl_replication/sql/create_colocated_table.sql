\c colocation_test
CALL TEST_reset();

-- Verify that temporary objects are not captured.
SET yb_xcluster_ddl_replication.replication_role = SOURCE;
CREATE TEMP TABLE temp_foo(i int PRIMARY KEY);

-- Verify that colocated tables are blocked.
CREATE TABLE coloc_foo(i int PRIMARY KEY);

SELECT yb_data FROM yb_xcluster_ddl_replication.ddl_queue ORDER BY start_time;

SET yb_xcluster_ddl_replication.replication_role = BIDIRECTIONAL;

-- Verify that non-colocated table is captured.
CREATE TABLE non_coloc_foo(i int PRIMARY KEY) WITH (COLOCATION = false);

SELECT yb_data FROM yb_xcluster_ddl_replication.ddl_queue ORDER BY start_time;
SELECT * FROM yb_xcluster_ddl_replication.replicated_ddls ORDER BY start_time;
